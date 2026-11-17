#pragma once

// RSVP playback and its pause screen (docs/PLAN.md section 6; controls: docs/spikes/S4-control-map.md).
// Pushed over the EPUB reader, which keeps the book open meanwhile. See README.md in this directory
// for the task split and the log lines.

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <CheckpointTrigger.h>
#include <CheckpointTypes.h>
#include <IndexWordSource.h>
#include <Scheduler.h>

#include <atomic>
#include <memory>
#include <optional>

#include "RsvpLaunch.h"
#include "WordBand.h"
#include "activities/Activity.h"
#include "controls/ControlInput.h"
#include "controls/ControlMap.h"
#include "display/DisplayProfile.h"
#include "reading/BookStateFile.h"
#include "reading/ChapterWordIndex.h"
#include "reading/RsvpPosition.h"
#include "reading/RsvpValues.h"
#include "reading/UpdateTimeoutWatch.h"

class Epub;

namespace readingsession {
struct CheckpointRun;
}

class RsvpActivity final : public Activity {
 public:
  RsvpActivity(GfxRenderer& renderer, MappedInputManager& input, std::shared_ptr<Epub> epub,
               const rsvpentry::Entry& entry, std::shared_ptr<rsvpentry::HandOff> handOff);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

  bool preventAutoSleep() override { return playing.load(std::memory_order_relaxed); }
  bool handleHomeGesture() override;
  bool handleForcedRefresh() override;
  void prepareForSleep() override;

 private:
  // Leaving: handed over to page view at a non-prose item; the main task finishes the activity.
  // Checkpoint: stopped at a checkpoint (task C2); the main task shows CheckpointActivity over RSVP.
  enum class Phase : uint8_t { Loading, Paused, Playing, Leaving, Checkpoint };
  // The one-line note on the pause screen.
  enum class Note : uint8_t { None, EndOfBook, ChapterFailed, DisplayTimeout };
  // What the status line starts with: "Paused", the outcome of a save button, or "Checkpoint done".
  enum class Status : uint8_t { Paused, SavedDefault, SavedBook, SaveFailed, CheckpointDone };

  // The scheduler's word source: an IndexWordSource over the open chapter, made anew per chapter
  // (it caches marker lookups of the chapter it was made for), with the chapter's section ends.
  class ChapterSource final : public rsvp::WordSource {
   public:
    void reset(wordindex::WordIndexReader& reader, wordindex::SectionEndFn ends, void* context) {
      source.emplace(reader, ends, context);
    }
    bool peek(rsvp::Token& out) override { return source && source->peek(out); }
    void advance() override {
      if (source) source->advance();
    }
    bool seek(uint32_t index) { return source && source->seek(index); }
    uint32_t position() const { return source ? source->position() : 0; }
    bool failed() const { return source && source->failed(); }

   private:
    std::optional<wordindex::IndexWordSource> source;
  };

  // Draws the word band for ProfileDisplaySink (the render task, inside Scheduler::step()).
  class BandPainter final : public displayprofile::FramePainter {
   public:
    explicit BandPainter(RsvpActivity& owner) : owner(owner) {}
    void paintFrame(const rsvp::Frame& frame) override { owner.paintBand(&frame); }
    void paintClearedBand() override { owner.paintBand(nullptr); }

   private:
    RsvpActivity& owner;
  };

  // --- Render task (holding the RenderLock) ---
  void load();
  // The usual entry (README.md, "Where RSVP opens"): the reader's page, or the saved word.
  void openAtPage(const reading::SavedWord& saved);
  void loadContinued();
  reading::SavedWord loadState();
  void startPlayback();
  void stepOnce();
  void enterPause(Note note, const char* reason);
  // Hands over to page view at the image, table or code block that starts at or after word `from`
  // (README.md, "Non-prose items"). `afterStop`: reached by a STOP_NON_PROSE stop.
  void showItem(uint32_t from, bool afterStop);
  void advanceChapter();
  bool openChapter(int spineIndex);
  // The word source over the open chapter, with its section ends when the book has checkpoints.
  void resetSource();
  // A STOP_CHECKPOINT stop: the session's step; the main task then shows the checkpoint.
  void stopAtCheckpoint();
  // After a checkpoint: to the first word after the break (another chapter when the section ended
  // its chapter), paused.
  void moveAfterCheckpoint(const checkpoint::Position& resume);
  void drawPauseScreen(bool clearing);
  void paintPauseScreen();
  void paintPlaybackScreen();
  void paintBand(const rsvp::Frame* frame);
  void paintButton(int index);
  void logFrame();
  void logPause(const char* reason);
  static void showPreparing(void* context);
  static bool fits(void* context, std::string_view text, uint16_t focusOffset);
  static int measure(void* context, uint8_t font, std::string_view text);

  // --- Main task ---
  void loopPlaying();
  void loopPaused();
  void activate(controls::Item item);
  void stepSentence(bool forward);
  void leave(rsvpentry::HandOff::Then then);
  void requestPauseNow(const char* cause);
  void stepValue(controls::Item item);
  void saveAsDefault();
  void saveForBook();
  void openCheckpoint();
  void returnFromCheckpoint(const readingsession::CheckpointRun& run);
  // The checkpoint's screens closed and reading goes on in page view (task C4): a reread in page view,
  // or a checkpoint that fired in page view ended after a reread in RSVP. RSVP closes.
  void leaveAfterCheckpoint(const readingsession::CheckpointRun& run);
  // "Back to the question" on the pause screen during a reread (S4 row 30).
  void backToQuestion();

  // --- Either task, holding the RenderLock (the render task holds it inside render()) ---
  // Writes the position (the next word) and `mode` to the book's state file, unless it is what was
  // written last. During a reread it records the reread place instead (task C4): `position` keeps the
  // place before the break. `rereadSource`: why the reread moved.
  void savePosition(checkpoint::ReadingMode mode, const char* reason,
                    checkpoint::NavSource rereadSource = checkpoint::NavSource::RsvpPlayback);
  // The pause screen's items: PAUSE, or PAUSE_REREAD with "Back to the question" during a reread.
  const controls::Screen& pauseLayout() const { return rereadScreen.load() ? controls::PAUSE_REREAD : controls::PAUSE; }
  // Updates rereadScreen from the session (RenderLock held).
  void noteRereading();
  // Home gesture and sleep: the reader's own progress, through HandOff (once), at the next word or,
  // while a checkpoint shows, at the word before the break.
  void saveReaderProgress();
  // The state file as the session sees it (book_sha256 and checkpoint index of a prepared book).
  reading::BookIdentity identity() const;

  std::shared_ptr<Epub> epub;
  std::shared_ptr<rsvpentry::HandOff> handOff;
  int spine;
  const rsvpentry::Entry entry;

  displayprofile::DisplayBackend& backend;
  BandPainter painter{*this};
  displayprofile::ProfileDisplaySink sink;
  displayprofile::MillisClock clock;
  reading::ChapterWordIndex index;
  ChapterSource source;
  rsvp::Scheduler scheduler;
  reading::SentenceWindow sentenceWindow;
  rsvpui::BandGeometry band;
  controls::ControlInput controlInput;

  // Shared between the main task and the render task.
  std::atomic<Phase> phase{Phase::Loading};
  std::atomic<bool> playing{false};
  std::atomic<bool> pauseRequested{false};
  std::atomic<bool> wakeValid{false};
  std::atomic<uint32_t> wakeAtMs{0};
  std::atomic<uint32_t> lastLoopMs{0};
  std::atomic<bool> guardRestartPending{false};
  std::atomic<bool> leavePending{false};       // Phase::Leaving: finish() on the next main-loop pass
  std::atomic<bool> checkpointPending{false};  // Phase::Checkpoint: show it on the next main-loop pass
  std::atomic<bool> rereadScreen{false};       // the session is rereading: the pause screen has its row

  // Phase::Checkpoint (RenderLock): the step that fired, and the first word of the frame on screen,
  // which is the reading position before the break.
  checkpoint::Step pendingCheckpoint;
  uint32_t checkpointBefore = 0;

  // Pause-screen state: written by the main task under the RenderLock, read by the render task.
  int focus = 0;
  Note note = Note::None;
  Status status = Status::Paused;
  reading::RsvpValues values = reading::RSVP_DEFAULTS;  // the steppers; applied at the next Play
  bool pendingPlay = false;
  // The last position written to the state file (RenderLock).
  bool positionSaved = false;
  checkpoint::Place savedPlace;
  bool readerProgressSaved = false;
  bool needClear = true;
  bool playbackScreenShown = false;          // render task only: the guides are on screen
  reading::UpdateTimeoutWatch timeoutWatch;  // render task only
  bool timeoutPause = false;                 // render task only: the pause asked for is a timeout's
  char statusLine[160] = {};                 // render task only: the pause screen's first line, also logged
};

#endif  // CROSSPOINT_RSVP_ENABLED
