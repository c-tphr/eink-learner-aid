#include "ItemBanner.h"

#if CROSSPOINT_RSVP_ENABLED

#include <Epub.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WordIndexFormat.h>

#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "activities/reader/ReaderUtils.h"
#include "fontIds.h"
#include "reading/BookStateFile.h"
#include "reading/RsvpPosition.h"

namespace {

using controls::Item;

// The banner strip (portrait, S4 section 2): a rule, the line naming the item, the two buttons
// (zones in controls::BANNER), and the key hint.
constexpr int16_t BANNER_TOP = 632;
constexpr int16_t LABEL_H = 56;
constexpr int16_t HINT_Y = 760;
constexpr int16_t BORDER = 2;
constexpr int16_t TEXT_PAD = 8;

const char* itemLine(const uint8_t kind) {
  switch (kind) {
    case wordindex::MARKER_TABLE:
      return tr(STR_RSVP_ITEM_TABLE);
    case wordindex::MARKER_CODE:
      return tr(STR_RSVP_ITEM_CODE);
    default:
      return tr(STR_RSVP_ITEM_IMAGE);
  }
}

}  // namespace

namespace rsvpentry {

std::unique_ptr<Activity> createItemBanner(GfxRenderer& renderer, MappedInputManager& input, std::shared_ptr<Epub> epub,
                                           std::shared_ptr<HandOff> handOff) {
  // One small screen-lifetime object per hand-over.
  auto banner = makeUniqueNoThrow<ItemBanner>(renderer, input, std::move(epub), std::move(handOff));
  if (!banner) LOG_ERR("RSVP", "OOM: item banner (%u bytes)", static_cast<unsigned>(sizeof(ItemBanner)));
  return banner;
}

}  // namespace rsvpentry

ItemBanner::ItemBanner(GfxRenderer& renderer, MappedInputManager& input, std::shared_ptr<Epub> epub,
                       std::shared_ptr<rsvpentry::HandOff> handOff)
    : Activity("RsvpBanner", renderer, input),
      epub(std::move(epub)),
      handOff(std::move(handOff)),
      controlInput(input) {}

void ItemBanner::onEnter() {
  Activity::onEnter();
  // The zones are portrait, like RSVP's; the reader restores its own orientation on return.
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  handOff->then = rsvpentry::HandOff::Then::None;
  handOff->pageTurn = 0;
  LOG_DBG("RSVP", "banner kind=%s ch=%d word=%lu item_offset=%lu activity_bytes=%u hand_off_bytes=%u",
          reading::itemKindName(handOff->item.kind), handOff->spineIndex,
          static_cast<unsigned long>(handOff->item.nextWord), static_cast<unsigned long>(handOff->visibleTextOffset),
          static_cast<unsigned>(sizeof(ItemBanner)), static_cast<unsigned>(sizeof(rsvpentry::HandOff)));
  controlInput.restartGuard();
  drawRequest();
}

void ItemBanner::onExit() {
  Activity::onExit();
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
}

void ItemBanner::drawRequest() {
  drawAsked.store(true);
  requestUpdate();
}

void ItemBanner::render(RenderLock&&) {
  if (!drawAsked.exchange(false)) {
    // Another screen (the light panel) covered this one and has closed: the frame buffer no longer
    // holds the page. The reader draws the page again and opens the banner anew.
    pageLost.store(true);
    return;
  }
  paint();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  LOG_DBG("RSVP", "banner screen focus=%s", controls::itemName(controls::BANNER.zones[focus].item));
}

void ItemBanner::paint() {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int smallHeight = renderer.getLineHeight(UI_10_FONT_ID);
  renderer.fillRect(0, BANNER_TOP, width, height - BANNER_TOP, false);
  renderer.fillRect(0, BANNER_TOP, width, BORDER, true);
  renderer.drawCenteredText(UI_12_FONT_ID, BANNER_TOP + (LABEL_H - renderer.getLineHeight(UI_12_FONT_ID)) / 2,
                            itemLine(handOff->item.kind));
  for (int i = 0; i < controls::BANNER.count; ++i) paintButton(i);
  renderer.drawCenteredText(UI_10_FONT_ID, HINT_Y + (height - HINT_Y - smallHeight) / 2, tr(STR_RSVP_KEY_HINT));
}

void ItemBanner::paintButton(const int i) {
  const controls::Zone& z = controls::BANNER.zones[i];
  const bool focused = i == focus;
  if (focused) {
    renderer.fillRect(z.x, z.y, z.w, z.h, true);
  } else {
    renderer.drawRect(z.x, z.y, z.w, z.h, BORDER, true);
  }
  const char* label = z.item == Item::Continue ? tr(STR_RSVP_CONTINUE) : tr(STR_RSVP_STAY);
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int room = z.w - 2 * TEXT_PAD;
  if (renderer.getTextWidth(UI_12_FONT_ID, label) <= room) {
    const int x = z.x + (z.w - renderer.getTextWidth(UI_12_FONT_ID, label)) / 2;
    renderer.drawText(UI_12_FONT_ID, x, z.y + (z.h - lineHeight) / 2, label, !focused);
    return;
  }
  // Two lines, split at the last space that lets the first line fit (S4's "Stay in / page view").
  char first[64];
  std::strncpy(first, label, sizeof(first) - 1);
  first[sizeof(first) - 1] = '\0';
  const char* second = "";
  for (char* space = std::strrchr(first, ' '); space != nullptr; space = std::strrchr(first, ' ')) {
    *space = '\0';
    second = label + (space - first) + 1;
    if (renderer.getTextWidth(UI_12_FONT_ID, first) <= room) break;
  }
  const int top = z.y + (z.h - 2 * lineHeight) / 2;
  renderer.drawText(UI_12_FONT_ID, z.x + (z.w - renderer.getTextWidth(UI_12_FONT_ID, first)) / 2, top, first,
                    !focused);
  renderer.drawText(UI_12_FONT_ID, z.x + (z.w - renderer.getTextWidth(UI_12_FONT_ID, second)) / 2, top + lineHeight,
                    second, !focused);
}

void ItemBanner::loop() {
  if (pageLost.exchange(false)) {
    LOG_DBG("RSVP", "banner page redraw");
    handOff->then = rsvpentry::HandOff::Then::ShowItem;
    finish();
    return;
  }
  const bool wasArmed = controlInput.armed();
  const controls::Event e = controlInput.pollFocus();
  if (!wasArmed && controlInput.armed()) LOG_DBG("CTL", "Banner armed");
  switch (controls::resolve(controls::Ctx::FocusScreen, e.ev)) {
    case controls::Act::FocusPrev:
    case controls::Act::FocusNext: {
      int moved = 0;
      {
        RenderLock lock;
        focus = e.ev == controls::Ev::PrevShort ? controls::focusPrev(focus)
                                                : controls::focusNext(controls::BANNER, focus);
        moved = focus;
      }
      LOG_DBG("CTL", "Banner focus=%s", controls::itemName(controls::BANNER.zones[moved].item));
      drawRequest();
      return;
    }
    case controls::Act::Select:
      activate(controls::BANNER.zones[focus].item);
      return;
    case controls::Act::TapItem:
      if (const controls::Zone* z = controls::hit(controls::BANNER, e.x, e.y)) activate(z->item);
      return;  // a tap on the page itself does nothing while the banner shows
    case controls::Act::Back:
      activate(Item::Stay);
      return;
    default:
      break;
  }
  if (e.ev != controls::Ev::None || !controlInput.armed()) return;
  // Reading on with a page-turn swipe is Stay, and the page turns as it would have.
  const MappedInputManager::SwipeDir dir = mappedInput.wasSwipe();
  if (dir == MappedInputManager::SwipeDir::Left || dir == MappedInputManager::SwipeDir::Right) {
    LOG_DBG("CTL", "Banner swipe=%s", dir == MappedInputManager::SwipeDir::Left ? "next" : "previous");
    stay(dir == MappedInputManager::SwipeDir::Left ? 1 : -1);
  }
}

void ItemBanner::activate(const Item item) {
  LOG_DBG("CTL", "Banner select=%s", controls::itemName(item));
  if (item == Item::Continue) {
    handOff->then = rsvpentry::HandOff::Then::Continue;
    finish();
    return;
  }
  stay(0);
}

void ItemBanner::stay(const int8_t pageTurn) {
  // Normal page view from here on: the state file's mode becomes page, at the word after the item, so
  // reopening the book shows the reader's page and speed reading from it starts after the item.
  checkpoint::Place place;
  place.position.chapter = static_cast<uint16_t>(handOff->spineIndex);
  place.position.word = handOff->item.nextWord;
  place.mode = checkpoint::ReadingMode::Page;
  const bool ok = reading::editBookState(
      epub->getPath(), reading::NO_BOOK_SHA256,
      [](checkpoint::BookState& state, const void* context) {
        state.hasPosition = true;
        state.position = *static_cast<const checkpoint::Place*>(context);
      },
      &place);
  LOG_DBG("RSVP", "position saved reason=stay ch=%d word=%lu mode=page ok=%d", handOff->spineIndex,
          static_cast<unsigned long>(place.position.word), ok ? 1 : 0);
  handOff->then = rsvpentry::HandOff::Then::None;
  handOff->pageTurn = pageTurn;
  finish();
}

#endif  // CROSSPOINT_RSVP_ENABLED
