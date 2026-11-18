#include "RsvpActivity.h"

#if CROSSPOINT_RSVP_ENABLED

#include <Arduino.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "activities/reader/ReaderUtils.h"
#include "controls/ControlMap.h"
#include "fontIds.h"
#include "reading/BookStateFile.h"
#include "reading/ReadingSession.h"

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

// Stepper glyphs (drawn, not text, so the screen text of a row stays its value).
constexpr int16_t GLYPH_LENGTH = 24;
constexpr int16_t GLYPH_WIDTH = 4;

static_assert(reading::RSVP_WPM_TENS_DEFAULT == 24, "CrossPointSettings::rsvpWpmTens default");
static_assert(reading::RSVP_DEFAULTS.wordsPerUpdate == 1, "CrossPointSettings::rsvpWordsPerUpdate default");

// The render task has not seen the main loop for this long: another screen covers RSVP (a push does
// not call onExit()). Playback pauses rather than run unseen.
constexpr uint32_t COVERED_MS = 1500;

bool reached(const uint32_t now, const uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

const char* itemLabel(const Item item) {
  switch (item) {
    case Item::Continue:
      return tr(STR_RSVP_CONTINUE);
    case Item::Stay:
      return tr(STR_RSVP_STAY);
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
    case Item::SaveDefault:
      return tr(STR_RSVP_SAVE_DEFAULT);
    case Item::SaveBook:
      return tr(STR_RSVP_SAVE_BOOK);
    case Item::WpmDown:
    case Item::WpmUp:
    case Item::WordsDown:
    case Item::WordsUp:
      return nullptr;  // a drawn minus or plus
  }
  return "";
}

const char* modeName(const checkpoint::ReadingMode mode) {
  return mode == checkpoint::ReadingMode::Rsvp ? "rsvp" : "page";
}

reading::RsvpValues globalDefaults() {
  return {reading::wpmFromTens(SETTINGS.rsvpWpmTens), reading::clampWordsPerUpdate(SETTINGS.rsvpWordsPerUpdate)};
}

}  // namespace

namespace rsvpentry {

std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& input, std::shared_ptr<Epub> epub,
                                 const Entry& entry, std::shared_ptr<HandOff> handOff) {
  // One screen-lifetime object (about 5.5 KB: scheduler and frame, index reader, sentence window).
  auto activity = makeUniqueNoThrow<RsvpActivity>(renderer, input, std::move(epub), entry, std::move(handOff));
  if (!activity) LOG_ERR("RSVP", "OOM: RSVP activity (%u bytes)", static_cast<unsigned>(sizeof(RsvpActivity)));
  return activity;
}

bool openedInRsvp(const Epub& epub, const reading::ReadingSession* session) {
  // Most books have no state file: answer without the allocation below.
  if (!Storage.exists(reading::bookStatePath(epub.getPath()).c_str())) return false;
  // 78 KB while the file is read (BookStateFile.h), freed on return.
  auto state = makeUniqueNoThrow<checkpoint::BookState>();
  if (!state) return false;
  const reading::StateLoad load =
      reading::loadBookState(epub.getPath(), session ? session->identity() : reading::BookIdentity{}, *state);
  const bool rsvp = state->hasPosition && state->position.mode == checkpoint::ReadingMode::Rsvp;
  if (load.source != reading::StateSource::Missing) {
    LOG_DBG("RSVP", "book state %s mode=%s ch=%u word=%lu", reading::stateSourceName(load.source),
            state->hasPosition ? modeName(state->position.mode) : "none", state->position.position.chapter,
            static_cast<unsigned long>(state->position.position.word));
  }
  return rsvp;
}

}  // namespace rsvpentry

RsvpActivity::RsvpActivity(GfxRenderer& renderer, MappedInputManager& input, std::shared_ptr<Epub> epub,
                           const rsvpentry::Entry& entry, std::shared_ptr<rsvpentry::HandOff> handOff)
    : Activity("Rsvp", renderer, input),
      epub(std::move(epub)),
      handOff(std::move(handOff)),
      spine(entry.spineIndex),
      entry(entry),
      backend(displayprofile::firmwareBackend(renderer)),
      sink(backend, painter),
      scheduler(source, clock, sink, &RsvpActivity::fits, this),
      controlInput(input) {
  this->handOff->session = entry.session;
}

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

  scheduler.setClearingLimits(backend.info().clearing);
  if (entry.session) entry.session->setMode(checkpoint::ReadingMode::Rsvp);
  lastLoopMs.store(static_cast<uint32_t>(millis()));
  controlInput.restartGuard();
  requestUpdate();
}

void RsvpActivity::onExit() {
  Activity::onExit();
  // The caller holds the RenderLock. Leaving by an item saved in leave(); the Home gesture (and sleep,
  // already saved in prepareForSleep(), which savePosition() then skips) does not return to the
  // reader, so the reader's progress is saved here too.
  // While a checkpoint shows, the state file already holds the position before the break (written by
  // CheckpointActivity when it started), which savePosition() would overwrite.
  if (phase.load() != Phase::Loading && handOff->then == rsvpentry::HandOff::Then::None) {
    if (phase.load() != Phase::Checkpoint) savePosition(checkpoint::ReadingMode::Rsvp, "home");
    saveReaderProgress();
  }
  if (entry.session) entry.session->setMode(checkpoint::ReadingMode::Page);
  playing.store(false);
  index.close();
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
}

void RsvpActivity::prepareForSleep() {
  // Called under the RenderLock: no step is running. The device may not wake into this screen (a
  // power-off from sleep), so the position and the reader's page are saved now.
  if (phase.load() == Phase::Loading) return;
  if (phase.load() == Phase::Playing) scheduler.requestPause();
  if (phase.load() != Phase::Checkpoint) savePosition(checkpoint::ReadingMode::Rsvp, "sleep");
  saveReaderProgress();
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
    case Phase::Leaving:
    case Phase::Checkpoint:
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
  resetSource();
  return true;
}

void RsvpActivity::resetSource() {
  // A prepared book's section ends (task C2): playback stops after the last word of a checkpoint's
  // section.
  const bool sections = entry.session && entry.session->loadSections(spine, index.reader());
  source.reset(index.reader(), sections ? &reading::ReadingSession::sectionEnds : nullptr,
               sections ? entry.session.get() : nullptr);
}

reading::BookIdentity RsvpActivity::identity() const {
  return entry.session ? entry.session->identity() : reading::BookIdentity{};
}

reading::SavedWord RsvpActivity::loadState() {
  reading::SavedWord saved;
  const reading::RsvpValues global = globalDefaults();
  // 78 KB while the file is read (BookStateFile.h), freed on return. Without it RSVP still opens, with
  // the global defaults at the page's first word.
  auto state = makeUniqueNoThrow<checkpoint::BookState>();
  if (!state) {
    LOG_ERR("RSVP", "OOM: book state (%u bytes)", static_cast<unsigned>(sizeof(checkpoint::BookState)));
    values = global;
  } else {
    const reading::StateLoad load = reading::loadBookState(epub->getPath(), identity(), *state);
    values = reading::resolveValues(&state->overrides, global);
    if (state->hasPosition) {
      saved.valid = true;
      saved.chapter = state->position.position.chapter;
      saved.word = state->position.position.word;
    }
    LOG_DBG("RSVP",
            "state %s book_wpm=%u book_words=%u default_wpm=%u default_words=%u wpm=%u words_per_update=%u "
            "position=%s ch=%d word=%lu",
            reading::stateSourceName(load.source), state->overrides.hasWpm ? state->overrides.wpm : 0,
            state->overrides.hasWordsPerUpdate ? state->overrides.wordsPerUpdate : 0, global.wpm, global.wordsPerUpdate,
            values.wpm, values.wordsPerUpdate, state->hasPosition ? modeName(state->position.mode) : "none",
            saved.chapter, static_cast<unsigned long>(saved.word));
  }
  scheduler.setWpm(values.wpm);
  scheduler.setWordsPerFrame(values.wordsPerUpdate);
  return saved;
}

void RsvpActivity::load() {
  scheduler.resetSession();
  focus = controls::PAUSE.defaultIndex;
  if (entry.after) {
    loadContinued();
    drawPauseScreen(true);
    phase.store(Phase::Paused);
    guardRestartPending.store(true);
    return;
  }
  const reading::SavedWord saved = loadState();
  if (entry.resume && saved.valid && saved.chapter < epub->getSpineItemsCount()) spine = saved.chapter;
  if (openChapter(spine)) {
    const reading::EntryChoice choice =
        reading::chooseEntry(index.reader(), spine, entry.pageStartOffset, entry.pageEndOffset, saved, entry.resume);
    source.seek(choice.word);
    scheduler.reposition();
    LOG_DBG("RSVP", "enter ch=%d page_offset=%lu word=%lu chapter_words=%lu activity_bytes=%u from=%s", spine,
            static_cast<unsigned long>(entry.pageStartOffset), static_cast<unsigned long>(source.position()),
            static_cast<unsigned long>(index.reader().wordCount()), static_cast<unsigned>(sizeof(RsvpActivity)),
            reading::entryFromName(choice.from));
  } else {
    note = Note::ChapterFailed;
    LOG_ERR("RSVP", "enter ch=%d failed: no word index", spine);
  }
  drawPauseScreen(true);
  phase.store(Phase::Paused);
  guardRestartPending.store(true);
}

void RsvpActivity::loadContinued() {
  // Continue on the banner over a non-prose item: the same session, at the first prose word after the
  // item, with the pace that was in use (README.md, "Non-prose items"). The state file already holds
  // this position.
  const rsvpentry::ItemStop& item = entry.after->item;
  values = item.values;
  scheduler.setWpm(values.wpm);
  scheduler.setWordsPerFrame(values.wordsPerUpdate);
  spine = entry.after->spineIndex;
  if (!openChapter(spine)) {
    note = Note::ChapterFailed;
    LOG_ERR("RSVP", "enter ch=%d failed: no word index", spine);
    return;
  }
  source.seek(item.nextWord);
  scheduler.reposition();
  scheduler.continueSession(item.meter, item.audit, item.afterNonProseStop);
  LOG_DBG("RSVP", "enter ch=%d page_offset=%lu word=%lu chapter_words=%lu activity_bytes=%u from=%s", spine,
          static_cast<unsigned long>(entry.pageStartOffset), static_cast<unsigned long>(source.position()),
          static_cast<unsigned long>(index.reader().wordCount()), static_cast<unsigned>(sizeof(RsvpActivity)),
          reading::entryFromName(reading::EntryFrom::Continued));
}

void RsvpActivity::startPlayback() {
  note = Note::None;
  status = Status::Paused;
  timeoutWatch.arm(backend.stats().updates.timeouts);
  timeoutPause = false;
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
  rsvp::StepResult r = scheduler.step();
  switch (r.event) {
    case rsvp::StepEvent::SHOWED_FRAME:
      logFrame();
      if (timeoutWatch.rose(backend.stats().updates.timeouts)) {
        // The panel took 5 s or more: pause before the next word rather than risk that per word.
        LOG_ERR("RSVP", "display timeout ch=%d word=%lu: pausing", spine,
                static_cast<unsigned long>(source.position()));
        scheduler.requestPause();
        timeoutPause = true;
        r.wakeAtMs = static_cast<uint32_t>(millis());
      }
      break;
    case rsvp::StepEvent::CLEARED:
      LOG_DBG("RSVP", "clear ch=%d ms=%lu", spine, static_cast<unsigned long>(backend.stats().clears.lastMs));
      break;
    case rsvp::StepEvent::PAUSED:
      if (timeoutPause) {
        enterPause(Note::DisplayTimeout, "display-timeout");
      } else {
        enterPause(Note::None, "pause");
      }
      return;
    case rsvp::StepEvent::STOPPED:
      if (r.stops & rsvp::STOP_CHECKPOINT) {
        stopAtCheckpoint();
      } else if (r.stops & rsvp::STOP_NON_PROSE) {
        showItem(scheduler.lastFrame().lastWordIndex + 1, true);
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
  timeoutPause = false;
  note = pauseNote;
  focus = controls::PAUSE.defaultIndex;
  logPause(reason);
  drawPauseScreen(true);  // the clearing refresh that ends playback carries the pause screen
  savePosition(checkpoint::ReadingMode::Rsvp, reason);
  phase.store(Phase::Paused);
  guardRestartPending.store(true);
}

void RsvpActivity::showItem(const uint32_t from, const bool afterStop) {
  const uint32_t next = source.position();
  wordindex::Marker marker;
  if (!reading::itemBefore(index.reader(), from, next, marker)) {
    LOG_ERR("RSVP", "non-prose stop ch=%d word=%lu: no item found", spine, static_cast<unsigned long>(next));
    enterPause(Note::None, "non-prose");
    return;
  }
  playing.store(false);
  wakeValid.store(false);
  pauseRequested.store(false);
  timeoutPause = false;
  // The meter and audit of the session so far; the item's viewing time is outside it (README.md).
  logPause("non-prose");
  // Reopening the book while page view shows the item continues after it, in RSVP.
  savePosition(checkpoint::ReadingMode::Rsvp, "non-prose");

  rsvpentry::ItemStop& item = handOff->item;
  item.kind = marker.kind;
  item.nextWord = next;
  item.values = values;
  item.meter = scheduler.meter();
  item.audit = scheduler.audit();
  item.afterNonProseStop = afterStop;
  handOff->then = rsvpentry::HandOff::Then::ShowItem;
  handOff->spineIndex = spine;
  if (entry.session) entry.session->setPageReading(static_cast<uint16_t>(spine), next);
  handOff->visibleTextOffset = marker.offsetStart;
  LOG_DBG("RSVP", "leave to=item kind=%s ch=%d word=%lu offset=%lu", reading::itemKindName(marker.kind), spine,
          static_cast<unsigned long>(next), static_cast<unsigned long>(marker.offsetStart));
  if (backend.info().experimental) {
    // A trial waveform is always followed by a clearing refresh when playback ends (plan section 6).
    renderer.clearScreen();
    backend.clear();
  }
  phase.store(Phase::Leaving);
  leavePending.store(true);
}

void RsvpActivity::stopAtCheckpoint() {
  // The section's last word is on screen and nothing beyond it was drawn (plan section 7).
  const rsvp::Frame& frame = scheduler.lastFrame();
  const checkpoint::Step step =
      entry.session ? entry.session->rsvpStop(spine, frame.lastWordIndex, source.position()) : checkpoint::Step{};
  if (step.kind != checkpoint::StepKind::Fire) {
    enterPause(Note::None, "checkpoint");
    return;
  }
  playing.store(false);
  wakeValid.store(false);
  pauseRequested.store(false);
  timeoutPause = false;
  // The meter and audit of the session so far; time in the checkpoint is outside playback.
  logPause("checkpoint");
  pendingCheckpoint = step;
  checkpointBefore = frame.firstWordIndex;
  LOG_DBG("RSVP", "leave to=checkpoint ch=%d first=%lu last=%lu", spine, static_cast<unsigned long>(frame.firstWordIndex),
          static_cast<unsigned long>(frame.lastWordIndex));
  phase.store(Phase::Checkpoint);
  checkpointPending.store(true);
}

void RsvpActivity::openCheckpoint() {
  std::unique_ptr<Activity> activity;
  std::shared_ptr<readingsession::CheckpointRun> run;
  {
    RenderLock lock;
    checkpoint::Place before;
    before.position.chapter = static_cast<uint16_t>(spine);
    before.position.word = checkpointBefore;
    before.mode = checkpoint::ReadingMode::Rsvp;
    activity = entry.session->startCheckpoint(renderer, mappedInput, pendingCheckpoint, checkpoint::ReadingMode::Rsvp,
                                              before, run);
    // CheckpointActivity writes the position before the break: what this session wrote last is stale.
    positionSaved = false;
  }
  if (!activity) {
    // No memory for the screens: reading goes on after the break.
    readingsession::CheckpointRun failed;
    failed.request.fire = pendingCheckpoint;
    failed.request.mode = checkpoint::ReadingMode::Rsvp;
    failed.outcome = std::make_shared<checkpointentry::Outcome>();
    failed.outcome->end = checkpointui::End::Failed;
    returnFromCheckpoint(failed);
    return;
  }
  startActivityForResult(std::move(activity), [this, run](const ActivityResult&) { returnFromCheckpoint(*run); });
}

void RsvpActivity::returnFromCheckpoint(const readingsession::CheckpointRun& run) {
  {
    RenderLock lock;
    // CheckpointActivity restored the reader's orientation when it closed.
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    const checkpoint::Place place = entry.session->endCheckpoint(run);
    moveAfterCheckpoint(place.position);
    const checkpointui::End end = run.outcome->end;
    const bool done = end == checkpointui::End::Finished || end == checkpointui::End::Skipped ||
                      end == checkpointui::End::RereadRsvp || end == checkpointui::End::RereadPage;
    // Plan section 7, point 6, and S4 row 33: the pause screen at the first word after the break, Play
    // highlighted, "Checkpoint done"; nothing plays until Play.
    status = done ? Status::CheckpointDone : Status::Paused;
    focus = controls::PAUSE.defaultIndex;
    needClear = true;
    // The activity already saved this place when it finished; savePosition() skips a repeat.
    if (done && run.outcome->saved && place.position.chapter == spine && place.position.word == source.position()) {
      positionSaved = true;
      savedPlace.position = place.position;
      savedPlace.mode = checkpoint::ReadingMode::Rsvp;
    }
    savePosition(checkpoint::ReadingMode::Rsvp, "checkpoint");
    phase.store(Phase::Paused);
    guardRestartPending.store(true);
  }
  requestUpdate();
}

void RsvpActivity::moveAfterCheckpoint(const checkpoint::Position& resume) {
  note = Note::None;
  const uint32_t words = index.isOpen() ? index.reader().wordCount() : 0;
  if (resume.chapter == spine && resume.word < words) {
    // The section ended mid-chapter: the source already stands after the stop, normally at the resume
    // word, so the audit's segment carries on.
    if (source.position() == resume.word) {
      scheduler.resolveStop();
    } else {
      source.seek(resume.word);
      scheduler.reposition();
    }
    return;
  }
  if (resume.chapter == spine) {
    // The section ended the book.
    source.seek(words);
    scheduler.reposition();
    note = Note::EndOfBook;
    return;
  }
  // The section ended its chapter: the next chapter with prose, as playback would have moved.
  const int from = spine;
  bool found = false;
  for (int next = resume.chapter; next < epub->getSpineItemsCount() && !found; ++next) {
    if (!openChapter(next)) {
      LOG_ERR("RSVP", "chapter ch=%d skipped: no word index", next);
      continue;
    }
    rsvp::Token first;
    found = source.peek(first);
  }
  if (!found) {
    if (openChapter(from)) source.seek(index.reader().wordCount());
    note = Note::EndOfBook;
  }
  scheduler.reposition();
  LOG_DBG("RSVP", "chapter ch=%d word=%lu chapter_words=%lu", spine, static_cast<unsigned long>(source.position()),
          static_cast<unsigned long>(index.isOpen() ? index.reader().wordCount() : 0));
}

void RsvpActivity::advanceChapter() {
  const int from = spine;
  const uint32_t at = source.position();
  if (source.failed()) {
    // A damaged block: rebuild the index and carry on from the same word.
    if (index.rebuild(*epub, &RsvpActivity::showPreparing, this)) {
      resetSource();
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
  // Once per chapter during playback, so a power loss costs at most the chapter read so far.
  savePosition(checkpoint::ReadingMode::Rsvp, "chapter");
  // An image, table or code block before the first prose word cannot be flagged by the source.
  wordindex::Marker marker;
  if (pauseRequested.load()) {
    enterPause(Note::None, "pause");
  } else if (reading::itemBefore(index.reader(), 0, source.position(), marker)) {
    showItem(0, false);
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

void RsvpActivity::paintButton(const int i) {
  const controls::Zone& z = controls::PAUSE.zones[i];
  const bool focused = i == focus;
  if (focused) {
    renderer.fillRect(z.x, z.y, z.w, z.h, true);
  } else {
    renderer.drawRect(z.x, z.y, z.w, z.h, BORDER, true);
  }
  const char* label = itemLabel(z.item);
  if (label == nullptr) {
    // A stepper: a minus, or a plus.
    const int cx = z.x + z.w / 2;
    const int cy = z.y + z.h / 2;
    renderer.fillRect(cx - GLYPH_LENGTH / 2, cy - GLYPH_WIDTH / 2, GLYPH_LENGTH, GLYPH_WIDTH, !focused);
    if (z.item == Item::WpmUp || z.item == Item::WordsUp) {
      renderer.fillRect(cx - GLYPH_WIDTH / 2, cy - GLYPH_LENGTH / 2, GLYPH_WIDTH, GLYPH_LENGTH, !focused);
    }
    return;
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

  // Status: "Paused" or a save's outcome, the target and delivered WPM, then the position.
  const char* lead = tr(STR_RSVP_PAUSED);
  switch (status) {
    case Status::SavedDefault:
      lead = tr(STR_RSVP_SAVED_DEFAULT);
      break;
    case Status::SavedBook:
      lead = tr(STR_RSVP_SAVED_BOOK);
      break;
    case Status::SaveFailed:
      lead = tr(STR_RSVP_SAVE_FAILED);
      break;
    case Status::CheckpointDone:
      lead = tr(STR_RSVP_CHECKPOINT_DONE);
      break;
    case Status::Paused:
      break;
  }
  const uint32_t tenths = scheduler.meter().deliveredWpmTenths();
  if (tenths == 0) {
    snprintf(statusLine, sizeof(statusLine), "%s · %u %s · -- %s", lead, scheduler.wpm(), tr(STR_RSVP_WPM_TARGET),
             tr(STR_RSVP_WPM_ACTUAL));
  } else {
    snprintf(statusLine, sizeof(statusLine), "%s · %u %s · %lu %s", lead, scheduler.wpm(), tr(STR_RSVP_WPM_TARGET),
             static_cast<unsigned long>((tenths + 5) / 10), tr(STR_RSVP_WPM_ACTUAL));
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

  for (int i = 0; i < controls::PAUSE.count; ++i) paintButton(i);

  // The values between their steppers.
  const int rowText = (SETTING_ROW_H - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  snprintf(line, sizeof(line), "%u %s", scheduler.wpm(), tr(STR_RSVP_WPM));
  renderer.drawCenteredText(UI_12_FONT_ID, WPM_ROW_Y + rowText, line);
  snprintf(line, sizeof(line), "%u %s", scheduler.wordsPerFrame(),
           scheduler.wordsPerFrame() == 1 ? tr(STR_RSVP_WORD_PER_UPDATE) : tr(STR_RSVP_WORDS_PER_UPDATE));
  renderer.drawCenteredText(UI_12_FONT_ID, WORDS_ROW_Y + rowText, line);

  const char* noteText = nullptr;
  switch (note) {
    case Note::DisplayTimeout:
      noteText = tr(STR_RSVP_DISPLAY_TIMEOUT);
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
    case Phase::Leaving:
      if (leavePending.exchange(false)) finish();
      return;
    case Phase::Checkpoint:
      if (checkpointPending.exchange(false)) openCheckpoint();
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
        focus =
            e.ev == controls::Ev::PrevShort ? controls::focusPrev(focus) : controls::focusNext(controls::PAUSE, focus);
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
    case Item::WpmDown:
    case Item::WpmUp:
    case Item::WordsDown:
    case Item::WordsUp:
      stepValue(item);
      return;
    case Item::SaveDefault:
      saveAsDefault();
      return;
    case Item::SaveBook:
      saveForBook();
      return;
    case Item::Continue:
    case Item::Stay:
      return;  // banner items (ItemBanner.cpp)
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
    status = Status::Paused;
    LOG_DBG("RSVP", "sentence %s ch=%d from=%lu to=%lu", forward ? "forward" : "back", spine,
            static_cast<unsigned long>(from), static_cast<unsigned long>(source.position()));
  }
  requestUpdate();
}

void RsvpActivity::stepValue(const Item item) {
  {
    RenderLock lock;  // the scheduler belongs to the render task; the values take effect at the next Play
    const reading::RsvpValues before = values;
    if (item == Item::WpmDown || item == Item::WpmUp) {
      values.wpm = reading::stepWpm(values.wpm, item == Item::WpmUp);
      scheduler.setWpm(values.wpm);
    } else {
      values.wordsPerUpdate = reading::stepWordsPerUpdate(values.wordsPerUpdate, item == Item::WordsUp);
      scheduler.setWordsPerFrame(values.wordsPerUpdate);
    }
    status = Status::Paused;
    LOG_DBG("RSVP", "values wpm=%u words_per_update=%u changed=%d", values.wpm, values.wordsPerUpdate,
            values == before ? 0 : 1);
  }
  requestUpdate();
}

void RsvpActivity::saveAsDefault() {
  // The values are written only by this task, so reading them needs no lock.
  SETTINGS.rsvpWpmTens = reading::wpmToTens(values.wpm);
  SETTINGS.rsvpWordsPerUpdate = values.wordsPerUpdate;
  const bool ok = SETTINGS.saveToFile();
  LOG_DBG("RSVP", "saved default wpm=%u words_per_update=%u ok=%d", reading::wpmFromTens(SETTINGS.rsvpWpmTens),
          SETTINGS.rsvpWordsPerUpdate, ok ? 1 : 0);
  {
    RenderLock lock;
    status = ok ? Status::SavedDefault : Status::SaveFailed;
  }
  requestUpdate();
}

void RsvpActivity::saveForBook() {
  const checkpoint::Overrides overrides = reading::overridesFor(values);
  const bool ok = reading::editBookState(
      epub->getPath(), identity(),
      [](checkpoint::BookState& state, const void* context) {
        state.overrides = *static_cast<const checkpoint::Overrides*>(context);
      },
      &overrides);
  LOG_DBG("RSVP", "saved book wpm=%u words_per_update=%u ok=%d", overrides.wpm, overrides.wordsPerUpdate, ok ? 1 : 0);
  {
    RenderLock lock;
    status = ok ? Status::SavedBook : Status::SaveFailed;
  }
  requestUpdate();
}

void RsvpActivity::savePosition(const checkpoint::ReadingMode mode, const char* reason) {
  if (!index.isOpen()) return;
  checkpoint::Place place;
  place.position.chapter = static_cast<uint16_t>(spine);
  place.position.word = source.position();
  place.mode = mode;
  if (positionSaved && place == savedPlace) return;
  const bool ok = reading::editBookState(
      epub->getPath(), identity(),
      [](checkpoint::BookState& state, const void* context) {
        state.hasPosition = true;
        state.position = *static_cast<const checkpoint::Place*>(context);
      },
      &place);
  if (ok) {
    positionSaved = true;
    savedPlace = place;
  }
  LOG_DBG("RSVP", "position saved reason=%s ch=%d word=%lu mode=%s ok=%d", reason, spine,
          static_cast<unsigned long>(place.position.word), modeName(mode), ok ? 1 : 0);
}

void RsvpActivity::saveReaderProgress() {
  if (readerProgressSaved || !handOff->saveReaderProgress || !index.isOpen()) return;
  uint32_t offset = 0;
  const uint32_t word = phase.load() == Phase::Checkpoint ? checkpointBefore : source.position();
  if (!reading::pageViewOffset(index.reader(), word, offset)) return;
  handOff->saveReaderProgress(spine, offset);
  readerProgressSaved = true;
}

void RsvpActivity::leave(const rsvpentry::HandOff::Then then) {
  {
    RenderLock lock;
    uint32_t offset = 0;
    if (index.isOpen() && !reading::pageViewOffset(index.reader(), source.position(), offset)) offset = 0;
    handOff->then = then;
    handOff->spineIndex = spine;
    handOff->visibleTextOffset = offset;
    const char* to = then == rsvpentry::HandOff::Then::PageView     ? "page-view"
                     : then == rsvpentry::HandOff::Then::ReaderMenu ? "menu"
                                                                    : "exit";
    LOG_DBG("RSVP", "leave to=%s ch=%d word=%lu offset=%lu", to, spine, static_cast<unsigned long>(source.position()),
            static_cast<unsigned long>(offset));
    // Page view counts its next next-page action from this word, not from the page's first (task C2).
    if (entry.session && then != rsvpentry::HandOff::Then::ExitBook) {
      entry.session->setPageReading(static_cast<uint16_t>(spine), source.position());
    }
    // Exit keeps the book in RSVP for its next opening (S4 row 14); page view and the menu do not.
    savePosition(
        then == rsvpentry::HandOff::Then::ExitBook ? checkpoint::ReadingMode::Rsvp : checkpoint::ReadingMode::Page, to);
  }
  finish();
}

#endif  // CROSSPOINT_RSVP_ENABLED
