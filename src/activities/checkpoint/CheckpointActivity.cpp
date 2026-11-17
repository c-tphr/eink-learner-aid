#include "CheckpointActivity.h"

#if CROSSPOINT_RSVP_ENABLED

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "CheckpointSave.h"
#include "CrossPointSettings.h"
#include "activities/reader/ReaderUtils.h"
#include "fontIds.h"
#include "reading/BookStateFile.h"
#include "reading/CheckpointBook.h"

namespace {

using checkpoint::FlowEvent;
using checkpoint::FlowState;
using checkpoint::Grade;
using checkpointui::Screen;
using controls::Item;

// Layout (portrait 480x800, S4 section 2); the touch zones are in controls::CHECKPOINT_*.
constexpr int MARGIN = 16;
constexpr int CONTENT_WIDTH = 448;
constexpr int HEADER_Y = 12;
constexpr int RECORDED_Y = 56;        // Prompt: "Recorded earlier: ..."
constexpr int PROMPT_TOP = 92;        // Prompt: the prompt text, down to 8 px above Reveal
constexpr int PROMPT_BOTTOM = 408;    //
constexpr int SHORT_PROMPT_Y = 52;    // Answer: the prompt, two lines
constexpr int REREAD_NOTE_Y = 56;     // After Incorrect: the note, then the prompt in two lines
constexpr int INCORRECT_PROMPT_Y = 100;
constexpr int BOX_PADDING = 12;
constexpr int HINT_Y = 752;
constexpr int HINT_H = 48;
constexpr int BORDER = 2;
constexpr int FOCUS_BORDER = 6;  // the answer box keeps its text readable when highlighted
constexpr int SHORT_PROMPT_LINES = 2;

// Prompt and answer text: the reader's body font; a prompt too long for it falls back to smaller ones.
constexpr int TEXT_FONT = NOTOSANS_16_FONT_ID;
constexpr int PROMPT_FONTS[] = {NOTOSANS_16_FONT_ID, NOTOSANS_14_FONT_ID, UI_12_FONT_ID};
constexpr int LABEL_FONT = UI_12_FONT_ID;
constexpr int SMALL_FONT = UI_10_FONT_ID;

// Wrapping never cuts a valid answer: every line holds at least one code point.
constexpr int ALL_LINES = checkpoint::MAX_ANSWER_CHARS + 1;

const char* modeName(const checkpoint::ReadingMode mode) {
  return mode == checkpoint::ReadingMode::Rsvp ? "rsvp" : "page";
}

const char* gradeLabel(const Grade grade) { return grade == Grade::Correct ? tr(STR_CP_CORRECT) : tr(STR_CP_INCORRECT); }

const char* gradeLog(const Grade grade) { return grade == Grade::Ungraded ? "none" : checkpoint::gradeName(grade); }

const char* itemLabel(const Item item) {
  switch (item) {
    case Item::Reveal:
      return tr(STR_CP_REVEAL);
    case Item::SkipPrompt:
      return tr(STR_CP_SKIP_PROMPT);
    case Item::SkipCheckpoint:
      return tr(STR_CP_SKIP_CHECKPOINT);
    case Item::Correct:
      return tr(STR_CP_CORRECT);
    case Item::Incorrect:
      return tr(STR_CP_INCORRECT);
    case Item::RereadRsvp:
      return tr(STR_CP_REREAD_RSVP);
    case Item::RereadPage:
      return tr(STR_CP_REREAD_PAGE);
    case Item::ContinueOn:
      return tr(STR_CP_CONTINUE);
    default:
      return "";
  }
}

// Wraps `text` at its line feeds and at `width`, into at most `maxLines` lines (the last one ends in
// an ellipsis when the text needs more). One vector per paint, freed when the paint ends.
std::vector<std::string> wrap(const GfxRenderer& renderer, const int fontId, const char* text, const int width,
                              const int maxLines) {
  std::vector<std::string> lines;
  const char* start = text;
  while (static_cast<int>(lines.size()) < maxLines) {
    const char* end = std::strchr(start, '\n');
    const std::string paragraph = end ? std::string(start, static_cast<size_t>(end - start)) : std::string(start);
    if (paragraph.empty()) {
      lines.emplace_back();
    } else {
      std::vector<std::string> part =
          renderer.wrappedText(fontId, paragraph.c_str(), width, maxLines - static_cast<int>(lines.size()));
      for (auto& line : part) lines.push_back(std::move(line));
    }
    if (!end) break;
    start = end + 1;
  }
  return lines;
}

}  // namespace

namespace checkpointentry {

std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& input, std::string bookPath,
                                 std::shared_ptr<reading::CheckpointBook> book, const Request& request,
                                 std::shared_ptr<Outcome> outcome) {
  // One screen-lifetime object (about 4 KB, mostly the prompt and answer text buffers).
  auto activity = makeUniqueNoThrow<CheckpointActivity>(renderer, input, std::move(bookPath), std::move(book), request,
                                                        std::move(outcome));
  if (!activity) {
    LOG_ERR("CPT", "OOM: checkpoint activity (%u bytes)", static_cast<unsigned>(sizeof(CheckpointActivity)));
  }
  return activity;
}

}  // namespace checkpointentry

CheckpointActivity::CheckpointActivity(GfxRenderer& renderer, MappedInputManager& input, std::string bookPath,
                                       std::shared_ptr<reading::CheckpointBook> book,
                                       const checkpointentry::Request& request,
                                       std::shared_ptr<checkpointentry::Outcome> outcome)
    : Activity("Checkpoint", renderer, input),
      bookPath(std::move(bookPath)),
      book(std::move(book)),
      request(request),
      outcome(outcome ? std::move(outcome) : std::make_shared<checkpointentry::Outcome>()),
      backend(displayprofile::firmwareBackend(renderer)),
      controlInput(input) {}

// ---------------------------------------------------------------------------------------------
// Lifecycle

void CheckpointActivity::onEnter() {
  Activity::onEnter();
  // The layout and the touch zones are portrait; the reader restores its own orientation on return.
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  {
    RenderLock lock;
    startFailed = !start();
  }
  controlInput.restartGuard();
  requestUpdate();
}

void CheckpointActivity::onExit() {
  Activity::onExit();
  // Left without an ending (Home gesture, sleep): every transition is already in the state file.
  if (!ended && session) {
    LOG_DBG("CPT", "left checkpoint=%u prompt=%u state=%s (saved)", session->flow.checkpoint(),
            session->flow.promptIndex(), checkpoint::flowStateName(session->flow.state()));
  }
  session.reset();
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
}

bool CheckpointActivity::handleForcedRefresh() {
  {
    RenderLock lock;
    needClear = true;
  }
  requestUpdate();
  return true;  // every refresh here goes through the display profile
}

bool CheckpointActivity::start() {
  outcome->checkpoint = request.fire.checkpoint;
  if (!book) {
    LOG_ERR("CPT", "open failed: no checkpoint data");
    return false;
  }
  // The flow and the state it changes, for the activity's life (78 KB; PSRAM on the Picco).
  session = makeUniqueNoThrow<Session>(book->index());
  if (!session) {
    LOG_ERR("CPT", "OOM: checkpoint session (%u bytes)", static_cast<unsigned>(sizeof(Session)));
    return false;
  }
  const reading::StateLoad load = reading::loadBookState(bookPath, book->sourceSha256(), session->state);
  checkpoint::applyBookChangeRule(session->state, &book->index());

  const checkpoint::Transition t = session->flow.start(request.fire, request.mode);
  const checkpoint::CheckpointIndex& index = book->index();
  LOG_DBG("CPT",
          "open checkpoint=%s prompts=%u section=%s mode=%s resume_ch=%u resume_word=%lu state=%s grades=%u "
          "activity_bytes=%u session_bytes=%u accepted=%d",
          index.checkpointId(request.fire.checkpoint), index.promptCount(request.fire.checkpoint),
          index.sectionId(request.fire.section.section), modeName(request.mode), request.fire.resume.chapter,
          static_cast<unsigned long>(request.fire.resume.word), reading::stateSourceName(load.source),
          session->state.grades.count(), static_cast<unsigned>(sizeof(CheckpointActivity)),
          static_cast<unsigned>(sizeof(Session)), t.accepted ? 1 : 0);
  if (!t.accepted) {
    LOG_ERR("CPT", "open refused: refusal=%u", static_cast<unsigned>(t.refusal));
    return false;
  }
  persist(t, nullptr, "start");  // the checkpoint in progress
  enterScreen();
  return true;
}

// ---------------------------------------------------------------------------------------------
// Main task

void CheckpointActivity::loop() {
  if (ended) return;
  if (startFailed) {
    ended = true;
    outcome->end = checkpointui::End::Failed;
    LOG_DBG("CPT", "end result=failed");
    finish();
    return;
  }
  if (guardRestartPending.exchange(false)) controlInput.restartGuard();
  const bool wasArmed = controlInput.armed();
  const controls::Event e = controlInput.pollFocus();
  const char* name = checkpointui::screenName(screen);  // written by this task only
  if (!wasArmed && controlInput.armed()) LOG_DBG("CTL", "%s armed", name);
  const controls::Screen& layout = checkpointui::layoutOf(screen);

  switch (controls::resolve(controls::Ctx::FocusScreen, e.ev)) {
    case controls::Act::FocusPrev:
    case controls::Act::FocusNext: {
      int moved = 0;
      {
        RenderLock lock;
        focus = e.ev == controls::Ev::PrevShort ? controls::focusPrev(focus) : controls::focusNext(layout, focus);
        moved = focus;
      }
      LOG_DBG("CTL", "%s focus=%s", name, controls::itemName(layout.zones[moved].item));
      requestUpdate();
      return;
    }
    case controls::Act::Select:
      activate(layout.zones[focus].item);
      return;
    case controls::Act::TapItem:
      if (const controls::Zone* z = controls::hit(layout, e.x, e.y)) {
        {
          RenderLock lock;
          focus = controls::indexOf(layout, z->item);
        }
        activate(z->item);
      }
      return;
    case controls::Act::SwipeNext:
    case controls::Act::SwipePrev: {
      const controls::Zone* z = controls::hit(layout, e.x, e.y);
      if (screen == Screen::Answer && z != nullptr && z->item == Item::AnswerBox) {
        page(e.ev == controls::Ev::SwipeUp ? checkpointui::PageMove::Next : checkpointui::PageMove::Previous,
             "swipe");
      }
      return;
    }
    default:
      // Back is ignored here (S4 section 6): a swipe meant as "previous page" must not skip.
      return;
  }
}

void CheckpointActivity::activate(const Item item) {
  LOG_DBG("CTL", "%s select=%s", checkpointui::screenName(screen), controls::itemName(item));
  if (item == Item::AnswerBox) {
    page(checkpointui::PageMove::Cycle, "next");
    return;
  }
  FlowEvent event = FlowEvent::Reveal;
  if (!checkpointui::flowEventFor(item, event) || !checkpointui::offered(item, session->flow.state())) return;
  apply(event);
}

void CheckpointActivity::page(const checkpointui::PageMove move, const char* how) {
  bool changed = false;
  {
    RenderLock lock;
    const uint16_t to = checkpointui::movePart(answerPart, answerParts, move);
    changed = to != answerPart;
    answerPart = to;
    LOG_DBG("CPT", "answer part=%u/%u move=%s changed=%d", answerPart + 1, answerParts, how, changed ? 1 : 0);
  }
  if (changed) requestUpdate();
}

const char* CheckpointActivity::gradeTime(char* out) {
  struct tm local = {};
  if (!halClock.localTime(local)) return nullptr;
  return checkpointui::formatGradeTime(local.tm_year, local.tm_mon, local.tm_mday, local.tm_hour, local.tm_min,
                                       local.tm_sec, out)
             ? out
             : nullptr;
}

void CheckpointActivity::apply(const FlowEvent event) {
  checkpointui::End how = checkpointui::End::None;
  {
    RenderLock lock;  // the flow, the index and the screen belong to the render task too
    char gradedId[checkpoint::ID_BUFFER_BYTES];
    std::memcpy(gradedId, promptId, sizeof(gradedId));
    char at[checkpoint::TIMESTAMP_CHARS + 1] = {};
    const bool grading = event == FlowEvent::Correct || event == FlowEvent::Incorrect;
    const checkpoint::Transition t = session->flow.handle(event, grading ? gradeTime(at) : nullptr);
    LOG_DBG("CPT", "event=%s prompt=%s accepted=%d refusal=%u state=%s grade_recorded=%d save=%s",
            checkpoint::flowEventName(event), gradedId, t.accepted ? 1 : 0, static_cast<unsigned>(t.refusal),
            checkpoint::flowStateName(t.state), t.gradeRecorded ? 1 : 0,
            t.save == checkpoint::Save::Now ? "now" : (t.save == checkpoint::Save::Position ? "position" : "no"));
    if (!t.accepted) return;
    // "Every transition saves state first": the file is written before anything else is shown.
    if (t.save == checkpoint::Save::Now) persist(t, gradedId, checkpoint::flowEventName(event));
    how = checkpointui::endFor(event, t);
    if (how != checkpointui::End::None) {
      end(how, t);
    } else {
      enterScreen();
    }
  }
  if (how != checkpointui::End::None) {
    finish();
  } else {
    requestUpdate();
  }
}

void CheckpointActivity::persist(const checkpoint::Transition& t, const char* gradedPrompt, const char* reason) {
  const checkpointui::StateChange change = checkpointui::changeFor(session->state, t, gradedPrompt);
  struct Edit {
    const checkpointui::StateChange* change;
    const char* sha;
  } edit{&change, book->sourceSha256()};
  const bool ok = reading::editBookState(
      bookPath, book->sourceSha256(),
      [](checkpoint::BookState& state, const void* context) {
        const auto* e = static_cast<const Edit*>(context);
        checkpointui::applyChange(state, *e->change, e->sha);
      },
      &edit);
  if (!ok) outcome->saved = false;
  char position[40] = "none";
  if (change.positionMoved) {
    snprintf(position, sizeof(position), "%u:%lu:%s", change.position.position.chapter,
             static_cast<unsigned long>(change.position.position.word), modeName(change.position.mode));
  }
  LOG_DBG("CPT", "saved after=%s prompt=%s grade=%s active=%s prompt_index=%u position=%s ok=%d", reason,
          change.gradeRecorded ? change.promptId : "-", gradeLog(change.grade),
          change.hasActive ? change.active.id : "none", change.active.promptIndex, position, ok ? 1 : 0);
}

void CheckpointActivity::end(const checkpointui::End how, const checkpoint::Transition& t) {
  ended = true;
  outcome->end = how;
  outcome->promptIndex = session->flow.promptIndex();
  if (t.finished) {
    outcome->resume = t.resume;
  } else {
    outcome->reread = session->state.active.reread;
  }
  const checkpoint::Place& place = t.finished ? outcome->resume : outcome->reread;
  LOG_DBG("CPT", "end result=%s prompt=%u ch=%u word=%lu mode=%s saved=%d", checkpointui::endName(how),
          outcome->promptIndex, place.position.chapter, static_cast<unsigned long>(place.position.word),
          modeName(place.mode), outcome->saved ? 1 : 0);
}

void CheckpointActivity::enterScreen() {
  const checkpoint::CheckpointFlow& flow = session->flow;
  screen = checkpointui::screenFor(flow.state());
  focus = checkpointui::defaultFocus(screen, request.mode);
  answerPart = 0;
  answerParts = 1;
  needClear = true;
  guardRestartPending.store(true);
  const uint16_t prompt = flow.promptIndex();
  if (prompt != loadedPrompt) {
    // Read once per prompt, so that painting needs no file access. A read error shows a note
    // instead of the text (the file changed or the card failed since the book opened).
    const checkpoint::CheckpointIndex& index = book->index();
    const uint16_t c = flow.checkpoint();
    if (!index.loadPromptId(c, prompt, promptId, sizeof(promptId))) promptId[0] = '\0';
    if (!index.loadPromptText(c, prompt, promptText, sizeof(promptText))) {
      snprintf(promptText, sizeof(promptText), "%s", tr(STR_CP_TEXT_UNREADABLE));
      LOG_ERR("CPT", "prompt %u of checkpoint %u unreadable", prompt, c);
    }
    if (!index.loadAnswerText(c, prompt, answerText, sizeof(answerText))) {
      snprintf(answerText, sizeof(answerText), "%s", tr(STR_CP_TEXT_UNREADABLE));
      LOG_ERR("CPT", "answer %u of checkpoint %u unreadable", prompt, c);
    }
    loadedPrompt = prompt;
  }
  // The stored first grade: shown as already recorded on a later pass, and after this pass's grade.
  recorded = flow.storedGrade();
}

// ---------------------------------------------------------------------------------------------
// Render task

void CheckpointActivity::render(RenderLock&&) {
  if (ended || startFailed || !session || screen == Screen::None) return;
  paint();
  const bool clearing = needClear;
  if (clearing) {
    backend.clear();
  } else {
    backend.update();
  }
  needClear = false;
  LOG_DBG("CPT", "screen=%s question=%u/%u prompt=%s recorded=%s focus=%s part=%u/%u lines=%lu refresh=%s",
          checkpointui::screenName(screen), session->flow.promptIndex() + 1, session->flow.promptCount(), promptId,
          gradeLog(recorded), controls::itemName(checkpointui::layoutOf(screen).zones[focus].item), answerPart + 1,
          answerParts, static_cast<unsigned long>(answerLines), clearing ? "clear" : "update");
}

void CheckpointActivity::paint() {
  renderer.clearScreen();
  switch (screen) {
    case Screen::Prompt:
      paintPrompt();
      break;
    case Screen::Answer:
      paintAnswer();
      break;
    case Screen::AfterIncorrect:
      paintAfterIncorrect();
      break;
    case Screen::None:
      return;
  }
  const controls::Screen& layout = checkpointui::layoutOf(screen);
  for (int i = 0; i < layout.count; ++i) {
    if (layout.zones[i].item != Item::AnswerBox) paintButton(i);
  }
  const int small = renderer.getLineHeight(SMALL_FONT);
  renderer.drawCenteredText(SMALL_FONT, HINT_Y + (HINT_H - small) / 2, tr(STR_RSVP_KEY_HINT));
}

void CheckpointActivity::paintHeader(const bool capital, const char* suffix) {
  char question[48];
  snprintf(question, sizeof(question), capital ? tr(STR_CP_QUESTION_OF_CAPITAL) : tr(STR_CP_QUESTION_OF),
           static_cast<unsigned>(session->flow.promptIndex() + 1), static_cast<unsigned>(session->flow.promptCount()));
  char line[160];
  if (capital) {
    snprintf(line, sizeof(line), "%s%s%s", question, suffix ? " · " : "", suffix ? suffix : "");
  } else {
    snprintf(line, sizeof(line), "%s · %s%s%s", tr(STR_CP_CHECKPOINT), question, suffix ? " · " : "",
             suffix ? suffix : "");
  }
  renderer.drawText(LABEL_FONT, MARGIN, HEADER_Y, line, true, EpdFontFamily::BOLD);
}

int CheckpointActivity::paintWrapped(const int fontId, const char* text, const int y, const int maxLines) {
  const std::vector<std::string> lines = wrap(renderer, fontId, text, CONTENT_WIDTH, maxLines);
  const int height = renderer.getLineHeight(fontId);
  for (size_t i = 0; i < lines.size(); ++i) {
    renderer.drawText(fontId, MARGIN, y + static_cast<int>(i) * height, lines[i].c_str());
  }
  return static_cast<int>(lines.size());
}

void CheckpointActivity::paintPrompt() {
  paintHeader(false, nullptr);
  if (recorded != Grade::Ungraded) {
    char line[96];
    snprintf(line, sizeof(line), tr(STR_CP_RECORDED_EARLIER), gradeLabel(recorded));
    renderer.drawText(SMALL_FONT, MARGIN, RECORDED_Y, line);
  }
  // The whole prompt, in the largest font it fits in (a valid prompt fits the last one).
  for (const int fontId : PROMPT_FONTS) {
    const int fit = (PROMPT_BOTTOM - PROMPT_TOP) / renderer.getLineHeight(fontId);
    const std::vector<std::string> lines = wrap(renderer, fontId, promptText, CONTENT_WIDTH, fit + 1);
    if (static_cast<int>(lines.size()) <= fit || fontId == PROMPT_FONTS[sizeof(PROMPT_FONTS) / sizeof(int) - 1]) {
      if (static_cast<int>(lines.size()) > fit) LOG_ERR("CPT", "prompt %s does not fit: cut at %d lines", promptId, fit);
      paintWrapped(fontId, promptText, PROMPT_TOP, fit);
      return;
    }
  }
}

void CheckpointActivity::paintAnswer() {
  paintHeader(false, recorded == Grade::Ungraded ? nullptr : gradeLabel(recorded));
  // The prompt, shortened to two lines (an ellipsis marks the cut): it was shown in full before.
  paintWrapped(LABEL_FONT, promptText, SHORT_PROMPT_Y, SHORT_PROMPT_LINES);

  const controls::Zone& box = controls::CHECKPOINT_ANSWER.zones[controls::indexOf(controls::CHECKPOINT_ANSWER,
                                                                                    Item::AnswerBox)];
  const bool focused = checkpointui::layoutOf(screen).zones[focus].item == Item::AnswerBox;
  renderer.drawRect(box.x, box.y, box.w, box.h, focused ? FOCUS_BORDER : BORDER, true);

  // The answer in parts of whole lines; a counter row at the top of the box says which part.
  const int counterHeight = renderer.getLineHeight(SMALL_FONT);
  const int lineHeight = renderer.getLineHeight(TEXT_FONT);
  const int textTop = box.y + FOCUS_BORDER + BOX_PADDING / 2 + counterHeight;
  const int textHeight = box.y + box.h - FOCUS_BORDER - BOX_PADDING / 2 - textTop;
  const uint16_t perPart = static_cast<uint16_t>(textHeight / lineHeight);
  const int textWidth = box.w - 2 * (FOCUS_BORDER + BOX_PADDING);
  const std::vector<std::string> lines = wrap(renderer, TEXT_FONT, answerText, textWidth, ALL_LINES);
  answerLines = static_cast<uint32_t>(lines.size());
  answerParts = checkpointui::partCount(answerLines, perPart);
  if (answerPart >= answerParts) answerPart = static_cast<uint16_t>(answerParts - 1);
  if (answerParts > 1) {
    char counter[16];
    snprintf(counter, sizeof(counter), "%u/%u", answerPart + 1, answerParts);
    const int w = renderer.getTextWidth(SMALL_FONT, counter);
    renderer.drawText(SMALL_FONT, box.x + box.w - FOCUS_BORDER - BOX_PADDING - w, box.y + FOCUS_BORDER + 2, counter);
  }
  const uint32_t first = checkpointui::partFirstLine(answerPart, perPart);
  const uint32_t count = checkpointui::partLines(answerLines, answerPart, perPart);
  for (uint32_t i = 0; i < count; ++i) {
    renderer.drawText(TEXT_FONT, box.x + FOCUS_BORDER + BOX_PADDING, textTop + static_cast<int>(i) * lineHeight,
                      lines[first + i].c_str());
  }
}

void CheckpointActivity::paintAfterIncorrect() {
  paintHeader(true, tr(STR_CP_MARKED_INCORRECT));
  renderer.drawText(SMALL_FONT, MARGIN, REREAD_NOTE_Y, tr(STR_CP_REREAD_NOTE));
  paintWrapped(LABEL_FONT, promptText, INCORRECT_PROMPT_Y, SHORT_PROMPT_LINES);
}

void CheckpointActivity::paintButton(const int index) {
  const controls::Zone& z = checkpointui::layoutOf(screen).zones[index];
  const bool focused = index == focus;
  if (focused) {
    renderer.fillRect(z.x, z.y, z.w, z.h, true);
  } else {
    renderer.drawRect(z.x, z.y, z.w, z.h, BORDER, true);
  }
  const char* label = itemLabel(z.item);
  const int width = renderer.getTextWidth(LABEL_FONT, label);
  const int x = z.x + (z.w - width) / 2;
  const int y = z.y + (z.h - renderer.getLineHeight(LABEL_FONT)) / 2;
  renderer.drawText(LABEL_FONT, x, y, label, !focused);
}

#endif  // CROSSPOINT_RSVP_ENABLED
