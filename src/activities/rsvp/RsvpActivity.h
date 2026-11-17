#pragma once

// RSVP playback and its pause screen (docs/PLAN.md section 6; controls: docs/spikes/S4-control-map.md).
// Pushed over the EPUB reader, which keeps the book open meanwhile. See README.md in this directory
// for the task split and the log lines.

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <IndexWordSource.h>
#include <Scheduler.h>

#include <atomic>
#include <memory>
#include <optional>

#include "RsvpLaunch.h"
#include "WordBand.h"
#include "activities/Activity.h"
#include "controls/ControlInput.h"
#include "display/DisplayProfile.h"
#include "reading/ChapterWordIndex.h"
#include "reading/RsvpPosition.h"

class Epub;

// The RSVP defaults until R4b moves them into the settings (PRD: 240 WPM, one word per update).
struct RsvpDefaults {
  static constexpr uint16_t WPM = rsvp::DEFAULT_WPM;
  static constexpr uint8_t WORDS_PER_UPDATE = 1;
};

class RsvpActivity final : public Activity {
 public:
  RsvpActivity(GfxRenderer& renderer, MappedInputManager& input, std::shared_ptr<Epub> epub, int spineIndex,
               uint32_t pageStartOffset, std::shared_ptr<rsvpentry::HandOff> handOff);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

  bool preventAutoSleep() override { return playing.load(std::memory_order_relaxed); }
  bool handleHomeGesture() override;
  bool handleForcedRefresh() override;
  void prepareForSleep() override;

 private:
  enum class Phase : uint8_t { Loading, Paused, Playing };
  // The one-line note on the pause screen.
  enum class Note : uint8_t { None, ItemSkipped, EndOfBook, ChapterFailed };

  // The scheduler's word source: an IndexWordSource over the open chapter, made anew per chapter
  // (it caches marker lookups of the chapter it was made for).
  class ChapterSource final : public rsvp::WordSource {
   public:
    void reset(wordindex::WordIndexReader& reader) { source.emplace(reader); }
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
  void startPlayback();
  void stepOnce();
  void enterPause(Note note, const char* reason);
  void advanceChapter();
  bool openChapter(int spineIndex);
  void drawPauseScreen(bool clearing);
  void paintPauseScreen();
  void paintPlaybackScreen();
  void paintBand(const rsvp::Frame* frame);
  void paintButton(int index, const char* label);
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

  std::shared_ptr<Epub> epub;
  std::shared_ptr<rsvpentry::HandOff> handOff;
  int spine;
  const uint32_t pageStartOffset;

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

  // Pause-screen state: written by the main task under the RenderLock, read by the render task.
  int focus = 0;
  Note note = Note::None;
  bool pendingPlay = false;
  bool needClear = true;
  bool playbackScreenShown = false;  // render task only: the guides are on screen
  char statusLine[160] = {};         // render task only: the pause screen's first line, also logged
};

#endif  // CROSSPOINT_RSVP_ENABLED
