#include "RsvpActivity.h"

#if CROSSPOINT_RSVP_ENABLED

#include <Arduino.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "activities/reader/ReaderUtils.h"
#include "controls/ControlMap.h"
#include "fontIds.h"

namespace {

using controls::Item;

// Band fonts, largest first (WordBand.h): the built-in reader fonts, bold for weight on e-ink.
constexpr int BAND_FONTS[rsvpui::BAND_FONT_COUNT] = {NOTOSANS_18_FONT_ID, NOTOSANS_16_FONT_ID, NOTOSANS_14_FONT_ID,
                                                     NOTOSANS_12_FONT_ID};
constexpr EpdFontFamily::Style BAND_STYLE = EpdFontFamily::BOLD;

// Playback screen (portrait): the band between two pairs of ticks on the focus column, at 40% of the
// width so that the longer part of a word, after its focus letter, has the wider side.
constexpr int16_t MARGIN = 16;
constexpr int16_t BAND_TOP = 352;
constexpr int16_t BAND_HEIGHT = 96;
constexpr int16_t TICK_LENGTH = 20;
constexpr int16_t TICK_GAP = 4;
constexpr int16_t TICK_WIDTH = 3;

// Pause screen rows outside the zones of controls::PAUSE (S4 section 2).
constexpr int16_t STATUS_Y = 4;
constexpr int16_t POSITION_Y = 26;
constexpr int16_t NEXT_WORDS_Y = 128;
constexpr int16_t NEXT_WORDS_H = 64;
constexpr int16_t WPM_ROW_Y = 488;
constexpr int16_t WORDS_ROW_Y = 564;
constexpr int16_t SETTING_ROW_H = 64;
constexpr int16_t NOTE_Y = 712;
constexpr int16_t HINT_Y = 752;
constexpr int16_t HINT_H = 48;
constexpr int16_t BORDER = 2;
constexpr int NEXT_WORDS_MAX = 12;

// The render task has not seen the main loop for this long: another screen covers RSVP (a push does
// not call onExit()). Playback pauses rather than run unseen.
constexpr uint32_t COVERED_MS = 1500;

bool reached(const uint32_t now, const uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

const char* itemLabel(const Item item) {
  switch (item) {
    case Item::Exit:
      return tr(STR_RSVP_EXIT);
    case Item::Menu:
      return tr(STR_RSVP_MENU);
    case Item::PageView:
      return tr(STR_RSVP_PAGE_VIEW);
    case Item::SentenceBack:
      return tr(STR_RSVP_SENTENCE_BACK);
    case Item::Play:
      return tr(STR_RSVP_PLAY);
    case Item::SentenceForward:
      return tr(STR_RSVP_SENTENCE_FORWARD);
  }
  return "";
}

}  // namespace

namespace rsvpentry {

std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& input, std::shared_ptr<Epub> epub,
                                 const int spineIndex, const uint32_t pageStartOffset,
                                 std::shared_ptr<HandOff> handOff) {
  // One screen-lifetime object (about 6 KB: scheduler and frame, index reader, sentence window).
  auto activity = makeUniqueNoThrow<RsvpActivity>(renderer, input, std::move(epub), spineIndex, pageStartOffset,
                                                  std::move(handOff));
  if (!activity) LOG_ERR("RSVP", "OOM: RSVP activity (%u bytes)", static_cast<unsigned>(sizeof(RsvpActivity)));
  return activity;
}

}  // namespace rsvpentry

RsvpActivity::RsvpActivity(GfxRenderer& renderer, MappedInputManager& input, std::shared_ptr<Epub> epub,
                           const int spineIndex, const uint32_t pageStartOffset,
                           std::shared_ptr<rsvpentry::HandOff> handOff)
    : Activity("Rsvp", renderer, input),
      epub(std::move(epub)),
      handOff(std::move(handOff)),
      spine(spineIndex),
      pageStartOffset(pageStartOffset),
      backend(displayprofile::firmwareBackend(renderer)),
      sink(backend, painter),
      scheduler(source, clock, sink, &RsvpActivity::fits, this),
      controlInput(input) {}

// ---------------------------------------------------------------------------------------------
// Lifecycle

void RsvpActivity::onEnter() {
  Activity::onEnter();
  // The layout and the touch zones are portrait; the reader restores its own orientation on return.
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  const int width = renderer.getScreenWidth();
  band.left = MARGIN;
  band.right = static_cast<int16_t>(width - MARGIN);
  band.focusX = static_cast<int16_t>(width * 2 / 5);
  band.top = BAND_TOP;
  band.height = BAND_HEIGHT;
  for (uint8_t i = 0; i < rsvpui::BAND_FONT_COUNT; ++i) {
    band.lineHeight[i] = static_cast<int16_t>(renderer.getLineHeight(BAND_FONTS[i]));
  }

  scheduler.setWpm(RsvpDefaults::WPM);
  scheduler.setWordsPerFrame(RsvpDefaults::WORDS_PER_UPDATE);
  scheduler.setClearingLimits(backend.info().clearing);
  lastLoopMs.store(static_cast<uint32_t>(millis()));
  controlInput.restartGuard();
  requestUpdate();
}

void RsvpActivity::onExit() {
  Activity::onExit();
  playing.store(false);
  index.close();
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
}

void RsvpActivity::prepareForSleep() {
  // Called under the RenderLock: no step is running. R4b saves the position here.
  if (phase.load() == Phase::Playing) scheduler.requestPause();
}

bool RsvpActivity::handleHomeGesture() {
  if (phase.load() != Phase::Playing) return false;
  requestPauseNow("home");
  return true;
}

bool RsvpActivity::handleForcedRefresh() {
  if (phase.load() == Phase::Paused) {
    RenderLock lock;
    needClear = true;
  }
  requestUpdate();
  return true;  // every refresh here goes through the display profile
}

// ---------------------------------------------------------------------------------------------
// Render task

void RsvpActivity::render(RenderLock&&) {
  switch (phase.load()) {
    case Phase::Loading:
      load();
      return;
    case Phase::Paused:
      if (pendingPlay) {
        pendingPlay = false;
        pauseRequested.store(false);
        startPlayback();
        stepOnce();
      } else {
        drawPauseScreen(needClear);
      }
      return;
    case Phase::Playing:
      stepOnce();
      return;
  }
}

void RsvpActivity::showPreparing(void* context) {
  auto* self = static_cast<RsvpActivity*>(context);
  self->playbackScreenShown = false;
  self->renderer.clearScreen();
  self->renderer.drawCenteredText(UI_12_FONT_ID, self->renderer.getScreenHeight() / 2, tr(STR_RSVP_PREPARING));
  self->backend.update();
}

bool RsvpActivity::openChapter(const int spineIndex) {
  if (!index.open(*epub, spineIndex, &RsvpActivity::showPreparing, this)) return false;
  spine = spineIndex;
  source.reset(index.reader());
  return true;
}

void RsvpActivity::load() {
  scheduler.resetSession();
  focus = controls::PAUSE.defaultIndex;
  if (openChapter(spine)) {
    const uint32_t word = reading::entryWord(index.reader(), pageStartOffset);
    source.seek(word);
    scheduler.reposition();
    LOG_DBG("RSVP", "enter ch=%d page_offset=%lu word=%lu chapter_words=%lu", spine,
            static_cast<unsigned long>(pageStartOffset), static_cast<unsigned long>(source.position()),
            static_cast<unsigned long>(index.reader().wordCount()));
  } else {
    note = Note::ChapterFailed;
    LOG_ERR("RSVP", "enter ch=%d failed: no word index", spine);
  }
  drawPauseScreen(true);
  phase.store(Phase::Paused);
  guardRestartPending.store(true);
}

void RsvpActivity::startPlayback() {
  note = Note::None;
  if (!playbackScreenShown) {
    paintPlaybackScreen();
    backend.clear();
    scheduler.noteClearingRefresh();
  }
  scheduler.play();
  if (pauseRequested.load()) scheduler.requestPause();  // asked for while this ran
  phase.store(Phase::Playing);
  playing.store(true);
  LOG_DBG("RSVP", "play ch=%d word=%lu wpm=%u words_per_update=%u", spine,
          static_cast<unsigned long>(source.position()), scheduler.wpm(), scheduler.wordsPerFrame());
}

void RsvpActivity::stepOnce() {
  if (reached(static_cast<uint32_t>(millis()), lastLoopMs.load() + COVERED_MS)) {
    scheduler.requestPause();  // another screen covered this one
  }
  const rsvp::StepResult r = scheduler.step();
  switch (r.event) {
    case rsvp::StepEvent::SHOWED_FRAME:
      logFrame();
      break;
    case rsvp::StepEvent::CLEARED:
      LOG_DBG("RSVP", "clear ch=%d ms=%lu", spine, static_cast<unsigned long>(backend.stats().clears.lastMs));
      break;
    case rsvp::StepEvent::PAUSED:
      enterPause(Note::None, "pause");
      return;
    case rsvp::StepEvent::STOPPED:
      if (r.stops & (rsvp::STOP_NON_PROSE | rsvp::STOP_CHECKPOINT)) {
        enterPause(Note::ItemSkipped, "non-prose");
      } else {
        advanceChapter();
      }
      return;
    case rsvp::StepEvent::NONE:
      break;
  }
  wakeAtMs.store(r.wakeAtMs);
  wakeValid.store(r.wake);
}

void RsvpActivity::enterPause(const Note pauseNote, const char* reason) {
  playing.store(false);
  wakeValid.store(false);
  pauseRequested.store(false);
  note = pauseNote;
  focus = controls::PAUSE.defaultIndex;
  logPause(reason);
  drawPauseScreen(true);  // the clearing refresh that ends playback carries the pause screen
  phase.store(Phase::Paused);
  guardRestartPending.store(true);
}

void RsvpActivity::advanceChapter() {
  const int from = spine;
  const uint32_t at = source.position();
  if (source.failed()) {
    // A damaged block: rebuild the index and carry on from the same word.
    if (index.rebuild(*epub, &RsvpActivity::showPreparing, this)) {
      source.reset(index.reader());
      source.seek(at);
      scheduler.reposition();
      enterPause(Note::None, "index-rebuilt");
      return;
    }
  }

  bool found = false;
  for (int next = from + 1; next < epub->getSpineItemsCount() && !found; ++next) {
    if (!openChapter(next)) {
      LOG_ERR("RSVP", "chapter ch=%d skipped: no word index", next);
      continue;
    }
    rsvp::Token first;
    found = source.peek(first);
  }
  if (!found) {
    // Stay at the end of the last chapter read.
    if (openChapter(from)) source.seek(index.reader().wordCount());
    scheduler.reposition();
    enterPause(Note::EndOfBook, "end-of-book");
    return;
  }

  scheduler.reposition();
  LOG_DBG("RSVP", "chapter ch=%d word=%lu chapter_words=%lu", spine, static_cast<unsigned long>(source.position()),
          static_cast<unsigned long>(index.reader().wordCount()));
  // An image, table or code block before the first prose word cannot be flagged by the source.
  wordindex::Marker marker;
  const uint8_t nonProse = wordindex::kindBit(wordindex::MARKER_IMAGE) | wordindex::kindBit(wordindex::MARKER_TABLE) |
                           wordindex::kindBit(wordindex::MARKER_CODE);
  const bool leadingItem = index.reader().nextMarker(0, nonProse, marker) &&
                           (marker.flags & wordindex::MARKER_IN_NOTE) == 0 && marker.wordStart <= source.position();
  if (pauseRequested.load()) {
    enterPause(Note::None, "pause");
  } else if (leadingItem) {
    enterPause(Note::ItemSkipped, "non-prose");
  } else {
    startPlayback();
    wakeAtMs.store(static_cast<uint32_t>(millis()));
    wakeValid.store(true);
  }
}

void RsvpActivity::drawPauseScreen(const bool clearing) {
  paintPauseScreen();
  if (clearing) {
    backend.clear();
    scheduler.noteClearingRefresh();
  } else {
    backend.update();
  }
  needClear = false;
  LOG_DBG("RSVP", "pause screen focus=%s refresh=%s status=\"%s\"",
          controls::itemName(controls::PAUSE.zones[focus].item), clearing ? "clear" : "update", statusLine);
}

void RsvpActivity::paintButton(const int i, const char* label) {
  const controls::Zone& z = controls::PAUSE.zones[i];
  const bool focused = i == focus;
  if (focused) {
    renderer.fillRect(z.x, z.y, z.w, z.h, true);
  } else {
    renderer.drawRect(z.x, z.y, z.w, z.h, BORDER, true);
  }
  const int width = renderer.getTextWidth(UI_12_FONT_ID, label);
  const int x = z.x + (z.w - width) / 2;
  const int y = z.y + (z.h - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  renderer.drawText(UI_12_FONT_ID, x, y, label, !focused);
}

void RsvpActivity::paintPauseScreen() {
  playbackScreenShown = false;
  renderer.clearScreen();
  const int width = renderer.getScreenWidth();
  const int smallHeight = renderer.getLineHeight(UI_10_FONT_ID);
  char line[160];

  // Status: target and delivered WPM, then the position.
  const uint32_t tenths = scheduler.meter().deliveredWpmTenths();
  if (tenths == 0) {
    snprintf(statusLine, sizeof(statusLine), "%s · %u %s · -- %s", tr(STR_RSVP_PAUSED), scheduler.wpm(),
             tr(STR_RSVP_WPM_TARGET), tr(STR_RSVP_WPM_ACTUAL));
  } else {
    snprintf(statusLine, sizeof(statusLine), "%s · %u %s · %lu %s", tr(STR_RSVP_PAUSED), scheduler.wpm(),
             tr(STR_RSVP_WPM_TARGET), static_cast<unsigned long>((tenths + 5) / 10), tr(STR_RSVP_WPM_ACTUAL));
  }
  renderer.drawText(UI_10_FONT_ID, MARGIN, STATUS_Y, statusLine);

  const uint32_t words = index.isOpen() ? index.reader().wordCount() : 0;
  const uint32_t position = source.position();
  const float chapterRead = words == 0 ? 1.0f : static_cast<float>(position < words ? position : words) / words;
  const int bookPercent = static_cast<int>(epub->calculateProgress(spine, chapterRead) * 100.0f + 0.5f);
  const int tocIndex = epub->getTocIndexForSpineIndex(spine);
  const std::string title = tocIndex >= 0 ? epub->getTocItem(tocIndex).title : std::string();
  snprintf(line, sizeof(line), " · %s %u%% · %s %d%%", tr(STR_RSVP_CHAPTER), reading::chapterPercent(position, words),
           tr(STR_RSVP_BOOK), bookPercent);
  const int suffixWidth = renderer.getTextWidth(UI_10_FONT_ID, line);
  const std::string shown = renderer.truncatedText(UI_10_FONT_ID, title.c_str(), width - 2 * MARGIN - suffixWidth);
  char positionLine[200];
  snprintf(positionLine, sizeof(positionLine), "%s%s", shown.c_str(), line);
  renderer.drawText(UI_10_FONT_ID, MARGIN, POSITION_Y, positionLine);

  // The words Play starts with.
  char next[rsvp::FRAME_TEXT_CAPACITY + 1] = {};
  if (index.isOpen()) {
    wordindex::IndexWordSource peekSource(index.reader());
    peekSource.seek(position);
    size_t length = 0;
    rsvp::Token token;
    for (int n = 0; n < NEXT_WORDS_MAX && peekSource.peek(token); ++n, peekSource.advance()) {
      const size_t add = token.text.size() + (length > 0 ? 1 : 0);
      if (length + add >= sizeof(next)) break;
      const size_t before = length;
      if (length > 0) next[length++] = ' ';
      std::memcpy(next + length, token.text.data(), token.text.size());
      length += token.text.size();
      next[length] = '\0';
      if (renderer.getTextWidth(UI_12_FONT_ID, next) > width - 2 * MARGIN) {
        next[before] = '\0';
        break;
      }
    }
  }
  renderer.drawCenteredText(UI_12_FONT_ID, NEXT_WORDS_Y + (NEXT_WORDS_H - renderer.getLineHeight(UI_12_FONT_ID)) / 2,
                            next);

  for (int i = 0; i < controls::PAUSE.count; ++i) paintButton(i, itemLabel(controls::PAUSE.zones[i].item));

  // Settings shown without steppers (R4b adds them in these rows).
  const int rowText = (SETTING_ROW_H - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  snprintf(line, sizeof(line), "%u %s", scheduler.wpm(), tr(STR_RSVP_WPM));
  renderer.drawCenteredText(UI_12_FONT_ID, WPM_ROW_Y + rowText, line);
  snprintf(line, sizeof(line), "%u %s", scheduler.wordsPerFrame(),
           scheduler.wordsPerFrame() == 1 ? tr(STR_RSVP_WORD_PER_UPDATE) : tr(STR_RSVP_WORDS_PER_UPDATE));
  renderer.drawCenteredText(UI_12_FONT_ID, WORDS_ROW_Y + rowText, line);

  const char* noteText = nullptr;
  switch (note) {
    case Note::ItemSkipped:
      noteText = tr(STR_RSVP_ITEM_SKIPPED);
      break;
    case Note::EndOfBook:
      noteText = tr(STR_RSVP_END_OF_BOOK);
      break;
    case Note::ChapterFailed:
      noteText = tr(STR_RSVP_CHAPTER_FAILED);
      break;
    case Note::None:
      break;
  }
  if (noteText) renderer.drawCenteredText(UI_10_FONT_ID, NOTE_Y + (HINT_Y - NOTE_Y - smallHeight) / 2, noteText);
  renderer.drawCenteredText(UI_10_FONT_ID, HINT_Y + (HINT_H - smallHeight) / 2, tr(STR_RSVP_KEY_HINT));
}

void RsvpActivity::paintPlaybackScreen() {
  playbackScreenShown = true;
  renderer.clearScreen();
  const int x = band.focusX - TICK_WIDTH / 2;
  renderer.fillRect(x, band.top - TICK_GAP - TICK_LENGTH, TICK_WIDTH, TICK_LENGTH, true);
  renderer.fillRect(x, band.top + band.height + TICK_GAP, TICK_WIDTH, TICK_LENGTH, true);
}

void RsvpActivity::paintBand(const rsvp::Frame* frame) {
  renderer.fillRect(0, band.top, renderer.getScreenWidth(), band.height, false);
  if (!frame || frame->textLength == 0) return;
  const std::string_view text = frame->view();
  const rsvpui::BandLayout layout =
      rsvpui::layoutBand(band, &RsvpActivity::measure, this, text, frame->focusByteOffset, frame->focusByteLength);
  char line[rsvp::FRAME_TEXT_CAPACITY + 1];
  for (uint8_t i = 0; i < layout.lineCount; ++i) {
    const rsvpui::BandLine& l = layout.lines[i];
    std::memcpy(line, text.data() + l.offset, l.length);
    line[l.length] = '\0';
    renderer.drawText(BAND_FONTS[layout.font], l.x, l.y, line, true, BAND_STYLE);
  }
}

int RsvpActivity::measure(void* context, const uint8_t font, const std::string_view text) {
  auto* self = static_cast<RsvpActivity*>(context);
  if (text.empty()) return 0;
  char buffer[rsvp::FRAME_TEXT_CAPACITY + 1];
  const size_t length = text.size() < rsvp::FRAME_TEXT_CAPACITY ? text.size() : rsvp::FRAME_TEXT_CAPACITY;
  std::memcpy(buffer, text.data(), length);
  buffer[length] = '\0';
  return self->renderer.getTextWidth(BAND_FONTS[font], buffer, BAND_STYLE);
}

bool RsvpActivity::fits(void* context, const std::string_view text, const uint16_t focusOffset) {
  auto* self = static_cast<RsvpActivity*>(context);
  return rsvpui::fitsOnColumn(self->band, &RsvpActivity::measure, context, 0, text, focusOffset,
                              rsvpui::codePointBytes(text, focusOffset));
}

void RsvpActivity::logFrame() {
  const rsvp::Frame& f = scheduler.lastFrame();
  LOG_DBG("RSVP", "frame ch=%d first=%lu last=%lu words=%u hold=%lu ms=%lu text=\"%s\"", spine,
          static_cast<unsigned long>(f.firstWordIndex), static_cast<unsigned long>(f.lastWordIndex), f.wordCount,
          static_cast<unsigned long>(f.holdMs), static_cast<unsigned long>(backend.stats().updates.lastMs), f.text);
}

void RsvpActivity::logPause(const char* reason) {
  const rsvp::MeterStats& m = scheduler.meter().stats();
  const rsvp::WordOrderAudit& a = scheduler.audit();
  const uint32_t tenths = scheduler.meter().deliveredWpmTenths();
  LOG_DBG("RSVP",
          "pause reason=%s ch=%d word=%lu target=%u delivered=%lu.%lu words=%lu active_ms=%lu updates=%lu clears=%lu "
          "mean_update_ms=%lu max_update_ms=%lu audit=%s drawn=%lu segments=%lu violations=%lu gaps=%lu repeats=%lu "
          "skipped=%lu declared=%lu",
          reason, spine, static_cast<unsigned long>(source.position()), scheduler.wpm(),
          static_cast<unsigned long>(tenths / 10), static_cast<unsigned long>(tenths % 10),
          static_cast<unsigned long>(m.wordsShown), static_cast<unsigned long>(m.activeMs),
          static_cast<unsigned long>(m.updates), static_cast<unsigned long>(m.clearingRefreshes),
          static_cast<unsigned long>(scheduler.meter().meanUpdateMs()), static_cast<unsigned long>(m.updateMsMax),
          a.inOrder() ? "ok" : "violated", static_cast<unsigned long>(a.wordsDrawn()),
          static_cast<unsigned long>(a.segments()), static_cast<unsigned long>(a.violations()),
          static_cast<unsigned long>(a.gaps()), static_cast<unsigned long>(a.repeats()),
          static_cast<unsigned long>(a.skippedWords()), static_cast<unsigned long>(a.declaredSkips()));
  displayprofile::logStats(backend);
}

// ---------------------------------------------------------------------------------------------
// Main task

void RsvpActivity::loop() {
  lastLoopMs.store(static_cast<uint32_t>(millis()));
  switch (phase.load()) {
    case Phase::Playing:
      loopPlaying();
      return;
    case Phase::Paused:
      loopPaused();
      return;
    case Phase::Loading:
      return;
  }
}

void RsvpActivity::requestPauseNow(const char* cause) {
  pauseRequested.store(true);
  scheduler.requestPause();
  LOG_DBG("CTL", "Playing pause=%s", cause);
  requestUpdate();
}

void RsvpActivity::loopPlaying() {
  const controls::Event e = controlInput.pollPlaying();
  if (controls::resolve(controls::Ctx::RsvpPlaying, e.ev) == controls::Act::Pause) {
    requestPauseNow(e.ev == controls::Ev::TouchDown ? "touch" : e.ev == controls::Ev::KeyDown ? "key" : "gesture");
    return;
  }
  // The render task is idle between frames; wake it when the next frame is due.
  if (wakeValid.load() && reached(static_cast<uint32_t>(millis()), wakeAtMs.load()) && !RenderLock::peek()) {
    requestUpdate();
  }
}

void RsvpActivity::loopPaused() {
  if (guardRestartPending.exchange(false)) controlInput.restartGuard();
  const bool wasArmed = controlInput.armed();
  const controls::Event e = controlInput.pollFocus();
  if (!wasArmed && controlInput.armed()) LOG_DBG("CTL", "Pause armed");
  switch (controls::resolve(controls::Ctx::FocusScreen, e.ev)) {
    case controls::Act::FocusPrev:
    case controls::Act::FocusNext: {
      int moved = 0;
      {
        RenderLock lock;
        focus = e.ev == controls::Ev::PrevShort ? controls::focusPrev(focus)
                                                : controls::focusNext(controls::PAUSE, focus);
        moved = focus;
      }
      LOG_DBG("CTL", "Pause focus=%s", controls::itemName(controls::PAUSE.zones[moved].item));
      requestUpdate();
      return;
    }
    case controls::Act::Select:
      activate(controls::PAUSE.zones[focus].item);
      return;
    case controls::Act::SelectRepeat:
      if (controls::PAUSE.zones[focus].repeats) activate(controls::PAUSE.zones[focus].item);
      return;
    case controls::Act::TapItem:
      if (const controls::Zone* z = controls::hit(controls::PAUSE, e.x, e.y)) {
        {
          RenderLock lock;
          focus = controls::indexOf(controls::PAUSE, z->item);
        }
        activate(z->item);
      }
      return;
    case controls::Act::Back:
      activate(Item::PageView);
      return;
    default:
      return;
  }
}

void RsvpActivity::activate(const Item item) {
  LOG_DBG("CTL", "Pause select=%s", controls::itemName(item));
  switch (item) {
    case Item::Play: {
      {
        RenderLock lock;
        if (note == Note::EndOfBook || !index.isOpen()) return;
        pendingPlay = true;
      }
      requestUpdate();
      return;
    }
    case Item::SentenceBack:
    case Item::SentenceForward:
      stepSentence(item == Item::SentenceForward);
      return;
    case Item::PageView:
      leave(rsvpentry::HandOff::Then::PageView);
      return;
    case Item::Menu:
      leave(rsvpentry::HandOff::Then::ReaderMenu);
      return;
    case Item::Exit:
      leave(rsvpentry::HandOff::Then::ExitBook);
      return;
  }
}

void RsvpActivity::stepSentence(const bool forward) {
  {
    RenderLock lock;  // the source, the scheduler and the reader belong to the render task
    if (!index.isOpen()) return;
    const uint32_t from = source.position();
    uint32_t to = from;
    const bool moved = forward ? reading::sentenceForward(index.reader(), from, sentenceWindow, to)
                               : reading::sentenceBack(index.reader(), from, sentenceWindow, to);
    if (moved && to != from) {
      source.seek(to);
      scheduler.reposition();
      note = Note::None;
    }
    LOG_DBG("RSVP", "sentence %s ch=%d from=%lu to=%lu", forward ? "forward" : "back", spine,
            static_cast<unsigned long>(from), static_cast<unsigned long>(source.position()));
  }
  requestUpdate();
}

void RsvpActivity::leave(const rsvpentry::HandOff::Then then) {
  {
    RenderLock lock;
    uint32_t offset = 0;
    if (index.isOpen() && !reading::pageViewOffset(index.reader(), source.position(), offset)) offset = 0;
    handOff->then = then;
    handOff->spineIndex = spine;
    handOff->visibleTextOffset = offset;
    LOG_DBG("RSVP", "leave to=%s ch=%d word=%lu offset=%lu",
            then == rsvpentry::HandOff::Then::PageView     ? "page-view"
            : then == rsvpentry::HandOff::Then::ReaderMenu ? "menu"
                                                           : "exit",
            spine, static_cast<unsigned long>(source.position()), static_cast<unsigned long>(offset));
  }
  finish();
}

#endif  // CROSSPOINT_RSVP_ENABLED
