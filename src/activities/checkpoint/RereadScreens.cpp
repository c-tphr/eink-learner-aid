// The page-view reread's own screens (task C4; docs/spikes/S4-control-map.md row 31 and section 2): the
// bar drawn over the page, and the list a side-key hold opens. README.md, "Rereading".

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "CheckpointLaunch.h"
#include "CrossPointSettings.h"
#include "activities/Activity.h"
#include "activities/reader/ReaderUtils.h"
#include "controls/ControlInput.h"
#include "controls/ControlMap.h"
#include "fontIds.h"
#include "reading/ReadingSession.h"

namespace {

using controls::Item;
using readingsession::RereadChoice;

// The bar: a white strip over the reader's status-bar row with a rule along its top, the label at the
// left and the button at the right (controls::rereadBarLayout()).
constexpr int MARGIN = 16;
constexpr int BORDER = 2;
constexpr int TEXT_PAD = 8;
// The list (S4's generic list): a header row above the rows, the key hint at the bottom.
constexpr int HEADER_Y = 16;
constexpr int HINT_Y = 752;
constexpr int HINT_H = 48;

const char* choiceLabel(const Item item) {
  switch (item) {
    case Item::BackToQuestion:
      return tr(STR_CP_BACK_TO_QUESTION);
    case Item::SpeedReadHere:
      return tr(STR_CP_SPEED_READ_HERE);
    default:
      return tr(STR_CP_KEEP_READING);
  }
}

RereadChoice choiceFor(const Item item) {
  switch (item) {
    case Item::BackToQuestion:
      return RereadChoice::BackToQuestion;
    case Item::SpeedReadHere:
      return RereadChoice::SpeedRead;
    default:
      return RereadChoice::KeepReading;
  }
}

// Draws `label` centred in zone `z`, on one line or, when too wide, on two split at a space.
void drawLabel(const GfxRenderer& renderer, const controls::Zone& z, const char* label, const bool inverted,
               const int font = UI_12_FONT_ID, const bool twoLines = true) {
  const int lineHeight = renderer.getLineHeight(font);
  const int room = z.w - 2 * TEXT_PAD;
  if (renderer.getTextWidth(font, label) <= room || !twoLines) {
    const std::string line = renderer.truncatedText(font, label, room);
    renderer.drawText(font, z.x + (z.w - renderer.getTextWidth(font, line.c_str())) / 2, z.y + (z.h - lineHeight) / 2,
                      line.c_str(), !inverted);
    return;
  }
  char first[64];
  snprintf(first, sizeof(first), "%s", label);
  const char* second = "";
  for (char* space = strrchr(first, ' '); space != nullptr; space = strrchr(first, ' ')) {
    *space = '\0';
    second = label + (space - first) + 1;
    if (renderer.getTextWidth(font, first) <= room) break;
  }
  const int top = z.y + (z.h - 2 * lineHeight) / 2;
  renderer.drawText(font, z.x + (z.w - renderer.getTextWidth(font, first)) / 2, top, first, !inverted);
  renderer.drawText(font, z.x + (z.w - renderer.getTextWidth(font, second)) / 2, top + lineHeight, second, !inverted);
}

class RereadChoices final : public Activity {
 public:
  RereadChoices(GfxRenderer& renderer, MappedInputManager& input, std::shared_ptr<RereadChoice> chosen)
      : Activity("RereadList", renderer, input), chosen(std::move(chosen)), controlInput(input) {}

  void onEnter() override {
    Activity::onEnter();
    // Portrait like every RSVP and checkpoint screen; the reader restores its orientation on return.
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    *chosen = RereadChoice::KeepReading;
    controlInput.restartGuard();
    requestUpdate();
  }

  void onExit() override {
    Activity::onExit();
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  }

  void loop() override {
    const bool wasArmed = controlInput.armed();
    const controls::Event e = controlInput.pollFocus();
    if (!wasArmed && controlInput.armed()) LOG_DBG("CTL", "RereadList armed");
    const controls::Screen& list = controls::REREAD_CHOICES;
    switch (controls::resolve(controls::Ctx::FocusScreen, e.ev)) {
      case controls::Act::FocusPrev:
      case controls::Act::FocusNext: {
        int moved = 0;
        {
          RenderLock lock;
          focus = e.ev == controls::Ev::PrevShort ? controls::focusPrev(focus) : controls::focusNext(list, focus);
          moved = focus;
        }
        LOG_DBG("CTL", "RereadList focus=%s", controls::itemName(list.zones[moved].item));
        requestUpdate();
        return;
      }
      case controls::Act::Select:
        choose(list.zones[focus].item);
        return;
      case controls::Act::TapItem:
        if (const controls::Zone* z = controls::hit(list, e.x, e.y)) choose(z->item);
        return;
      case controls::Act::Back:
        choose(Item::KeepReading);
        return;
      default:
        return;
    }
  }

  void render(RenderLock&&) override {
    const controls::Screen& list = controls::REREAD_CHOICES;
    renderer.clearScreen();
    renderer.drawText(UI_12_FONT_ID, MARGIN, HEADER_Y, tr(STR_CP_REREAD_BAR), true, EpdFontFamily::BOLD);
    for (int i = 0; i < list.count; ++i) {
      const controls::Zone& z = list.zones[i];
      const bool focused = i == focus;
      if (focused) {
        renderer.fillRect(z.x, z.y, z.w, z.h, true);
      } else {
        renderer.drawRect(z.x, z.y, z.w, z.h, BORDER, true);
      }
      drawLabel(renderer, z, choiceLabel(z.item), focused);
    }
    const int small = renderer.getLineHeight(UI_10_FONT_ID);
    renderer.drawCenteredText(UI_10_FONT_ID, HINT_Y + (HINT_H - small) / 2, tr(STR_RSVP_KEY_HINT));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    LOG_DBG("CPT", "reread list focus=%s", controls::itemName(list.zones[focus].item));
  }

 private:
  void choose(const Item item) {
    LOG_DBG("CTL", "RereadList select=%s", controls::itemName(item));
    *chosen = choiceFor(item);
    finish();
  }

  std::shared_ptr<RereadChoice> chosen;
  controls::ControlInput controlInput;
  int focus = controls::REREAD_CHOICES.defaultIndex;  // written by the main task under the RenderLock
};

}  // namespace

namespace checkpointentry {

void drawRereadBar(GfxRenderer& renderer, const controls::RereadBarLayout& bar) {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  renderer.fillRect(0, bar.top, width, height - bar.top, false);
  renderer.fillRect(0, bar.top, width, controls::REREAD_BAR_RULE, true);
  const controls::Zone& button = bar.button;
  // The texts in the largest UI font whose line fits the strip; two lines when the strip holds two.
  // (with room for the border and the descenders: a 23 px strip takes the small font).
  const int font = button.h >= renderer.getLineHeight(UI_12_FONT_ID) + 2 * BORDER + 4 ? UI_12_FONT_ID : UI_10_FONT_ID;
  const bool twoLines = button.h >= 2 * renderer.getLineHeight(font) + 2 * BORDER;
  // The label left of the button; "Rereading" when the whole label does not fit.
  const controls::Zone label{Item::BackToQuestion, controls::REREAD_BAR_MARGIN, button.y, bar.labelW, button.h, false};
  const char* labelText = tr(STR_CP_REREAD_BAR);
  if (!twoLines && renderer.getTextWidth(font, labelText) > label.w - 2 * TEXT_PAD) labelText = tr(STR_CP_REREADING);
  const controls::Zone labelArea{label.item, label.x, label.y, label.w, static_cast<int16_t>(label.h - 2 * BORDER),
                                 false};
  drawLabel(renderer, labelArea, labelText, false, font, twoLines);
  renderer.drawRect(button.x, button.y, button.w, button.h, BORDER, true);
  // Centred above the bottom border, which sits on the screen's edge, so that descenders stay inside.
  const controls::Zone text{button.item, button.x, button.y, button.w, static_cast<int16_t>(button.h - 2 * BORDER),
                            false};
  drawLabel(renderer, text, tr(STR_CP_BACK_TO_QUESTION), false, font, twoLines);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  LOG_DBG("CPT", "reread bar shown top=%d button=%d,%d,%d,%d clear_of_text=%d label=\"%s\"", bar.top, button.x,
          button.y, button.w, button.h, bar.clearOfText ? 1 : 0, labelText);
}

std::unique_ptr<Activity> createRereadChoices(GfxRenderer& renderer, MappedInputManager& input,
                                              std::shared_ptr<readingsession::RereadChoice> chosen) {
  // One small screen-lifetime object per hold.
  auto list = makeUniqueNoThrow<RereadChoices>(renderer, input, std::move(chosen));
  if (!list) LOG_ERR("CPT", "OOM: reread list (%u bytes)", static_cast<unsigned>(sizeof(RereadChoices)));
  return list;
}

}  // namespace checkpointentry

#endif  // CROSSPOINT_RSVP_ENABLED
