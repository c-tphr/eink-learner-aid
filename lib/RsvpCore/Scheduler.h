#pragma once

// Playback scheduler (plan section 6, "Scheduler"): draws frames through a DisplaySink against a
// Clock, holds each frame for its paced interval or the minimum dwell after the refresh, whichever
// ends later, stops before anything past a checkpoint, non-prose item or chapter end, runs the
// clearing policy, and feeds the delivered-WPM meter and the word-order audit.
//
// It never sleeps. The caller calls step() when it is due and waits for input in between; see
// README.md, "Calling contract", for the loop.

#include <atomic>
#include <cstdint>

#include "ClearingPolicy.h"
#include "Clock.h"
#include "DeliveredWpmMeter.h"
#include "DisplaySink.h"
#include "FrameBuilder.h"
#include "WordOrderAudit.h"
#include "WordSource.h"

namespace rsvp {

// Minimum time a frame stays on screen after its refresh finishes, so it can become readable.
inline constexpr uint16_t DEFAULT_MIN_DWELL_MS = 60;

enum class PlaybackState : uint8_t {
  PAUSED,   // waiting for play(): initially, after a pause, after resolveStop() or reposition()
  PLAYING,  // call step() when due
  STOPPED,  // at a stop; stopReasons() says which. play() or resolveStop() continues
};

enum class StepEvent : uint8_t {
  NONE,          // nothing was due yet (or not playing)
  SHOWED_FRAME,  // drew lastFrame()
  CLEARED,       // ran a clearing refresh
  PAUSED,        // a requested pause took effect; state() is PAUSED
  STOPPED,       // reached a stop; `stops` holds the StopReason bits and state() is STOPPED
};

struct StepResult {
  StepEvent event = StepEvent::NONE;
  uint8_t stops = 0;  // StopReason bits, when event is STOPPED
  // True while playing: call step() again at wakeAtMs, or earlier after input. False when
  // playback is paused or stopped: wait for input only.
  bool wake = false;
  uint32_t wakeAtMs = 0;  // clock time, when `wake` is true
};

class Scheduler {
 public:
  // The source is the caller's single, repositionable word supply (it may move to another chapter;
  // call reposition() after moving it). All references must outlive the scheduler.
  Scheduler(WordSource& source, Clock& clock, DisplaySink& display, FitsFn fits = nullptr, void* fitsContext = nullptr);

  // --- Called from the task that calls step() ---

  // Starts or continues playback from PAUSED or STOPPED with the next unshown word. Applies
  // settings changed since the last play. Returns false (and does nothing) when already playing.
  bool play();

  // Does the next due action, if any: at most one blocking display call. See StepEvent.
  StepResult step();

  // STOPPED -> PAUSED, keeping the position after the stop. play() does this implicitly.
  void resolveStop();

  // Call after moving the source (sentence step, reread, mode switch, next chapter). Pauses if
  // playing, drops any pending stop, resets the pacing remainder and the paced deadline, and opens
  // a new word-order audit segment. The meter and the clearing count carry on.
  void reposition();

  // Target WPM and words per update. They take effect at the next play(), without moving.
  void setWpm(uint16_t wpm) { requestedWpm = wpm; }
  void setWordsPerFrame(uint8_t count) { requestedWordsPerFrame = count; }
  uint16_t wpm() const { return requestedWpm; }
  uint8_t wordsPerFrame() const { return requestedWordsPerFrame; }

  // Takes effect from the next frame.
  void setMinDwellMs(uint16_t ms) { dwellMs = ms; }
  uint16_t minDwellMs() const { return dwellMs; }
  void setClearingLimits(ClearingLimits limits) { clearing.setLimits(limits); }

  // The caller ran a clearing refresh itself (on pause, or around a checkpoint or page view).
  // Restarts the clearing count. Not counted by the meter: it happens outside active playback.
  void noteClearingRefresh() { clearing.noteClearingRefresh(); }

  // Starts a new meter session and a new audit. Call while not playing.
  void resetSession();

  // Carries on a session that another scheduler stopped (RSVP closed to show a non-prose item in
  // page view, then reopened after it): the meter and audit as they were at the stop. Call after
  // moving the source to the word after the stop and calling reposition(), while not playing.
  // `afterNonProse`: the stop was STOP_NON_PROSE, so the next play() allows the audit's one forward
  // skip, as play() at that stop would have; the audit's open segment carries on.
  void continueSession(const DeliveredWpmMeter& meter, const WordOrderAudit& audit, bool afterNonProse);

  // --- Safe from any task ---

  // Asks playback to pause. Takes effect at the next step(), before any further frame is drawn,
  // even when it arrives during a blocking display call. If the frame on screen ends at a stop,
  // the stop takes effect instead (the reader has reached it). Ignored when not playing.
  void requestPause() { pauseRequested.store(true, std::memory_order_relaxed); }

  // --- State ---

  PlaybackState state() const { return playState; }
  uint8_t stopReasons() const { return stops; }  // StopReason bits while STOPPED
  bool hasFrame() const { return frameShown; }   // false before the first frame and after reposition()
  const Frame& lastFrame() const { return frame; }
  const DeliveredWpmMeter& meter() const { return wpmMeter; }
  const WordOrderAudit& audit() const { return wordAudit; }
  WordOrderAudit& audit() { return wordAudit; }
  const ClearingPolicy& clearingPolicy() const { return clearing; }

 private:
  void extendActive(uint32_t operationStart);
  void closeActive(uint32_t now);
  StepResult enterStop();

  WordSource& source;
  Clock& clock;
  DisplaySink& display;
  FrameBuilder builder;
  ClearingPolicy clearing;
  DeliveredWpmMeter wpmMeter;
  WordOrderAudit wordAudit;
  Frame frame;

  uint16_t requestedWpm = DEFAULT_WPM;
  uint8_t requestedWordsPerFrame = 1;
  uint16_t dwellMs = DEFAULT_MIN_DWELL_MS;

  PlaybackState playState = PlaybackState::PAUSED;
  std::atomic<bool> pauseRequested{false};
  uint8_t pendingStops = 0;  // stops of the frame on screen, reported when its slot ends
  uint8_t stops = 0;         // reported stops, while STOPPED
  bool skipAfterNonProse = false;
  bool frameShown = false;

  bool haveDue = false;
  uint32_t dueMs = 0;  // when the next display operation may start
  bool haveLastRefreshEnd = false;
  uint32_t lastRefreshEndMs = 0;

  bool activeOpen = false;       // inside an active-time span (README.md, "Delivered-WPM meter")
  uint32_t lastOperationMs = 0;  // start of the last display operation, counted up to here
};

}  // namespace rsvp
