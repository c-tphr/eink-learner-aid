#include "Scheduler.h"

namespace rsvp {

Scheduler::Scheduler(WordSource& wordSource, Clock& timeSource, DisplaySink& sink, const FitsFn fits, void* fitsContext)
    : source(wordSource), clock(timeSource), display(sink), builder(wordSource, fits, fitsContext) {}

bool Scheduler::play() {
  if (playState == PlaybackState::PLAYING) return false;
  if (playState == PlaybackState::STOPPED) resolveStop();
  pauseRequested.store(false, std::memory_order_relaxed);

  if (requestedWpm != builder.wpm()) builder.setWpm(requestedWpm);
  builder.setWordsPerFrame(requestedWordsPerFrame);
  if (skipAfterNonProse) {
    wordAudit.allowForwardSkip();
    skipAfterNonProse = false;
  }

  // The paced deadline does not survive a pause: the next update waits only for the rest of the
  // dwell after the last refresh, if that refresh was very recent.
  const uint32_t now = clock.nowMs();
  haveDue = haveLastRefreshEnd && now - lastRefreshEndMs < dwellMs;
  dueMs = lastRefreshEndMs + dwellMs;
  activeOpen = false;
  playState = PlaybackState::PLAYING;
  return true;
}

void Scheduler::resolveStop() {
  if (playState != PlaybackState::STOPPED) return;
  stops = 0;
  playState = PlaybackState::PAUSED;
}

void Scheduler::reposition() {
  if (playState == PlaybackState::PLAYING) closeActive(clock.nowMs());
  playState = PlaybackState::PAUSED;
  pendingStops = 0;
  stops = 0;
  skipAfterNonProse = false;
  frameShown = false;
  haveDue = false;
  builder.reset();
  wordAudit.beginSegment();
}

void Scheduler::resetSession() {
  wpmMeter.reset();
  wordAudit.reset();
  if (activeOpen) lastOperationMs = clock.nowMs();
}

void Scheduler::extendActive(const uint32_t operationStart) {
  if (activeOpen) {
    wpmMeter.addActiveMs(operationStart - lastOperationMs);
  } else {
    activeOpen = true;
  }
  lastOperationMs = operationStart;
}

void Scheduler::closeActive(const uint32_t now) {
  if (!activeOpen) return;
  // The last operation counts until now, but never past the time the next one was due: time
  // after that, before a pause or stop took effect, is not playback.
  const uint32_t end = haveDue ? timeEarlier(now, dueMs) : now;
  if (timeBefore(lastOperationMs, end)) wpmMeter.addActiveMs(end - lastOperationMs);
  activeOpen = false;
}

StepResult Scheduler::enterStop() {
  stops = pendingStops;
  pendingStops = 0;
  if (stops & STOP_NON_PROSE) skipAfterNonProse = true;
  playState = PlaybackState::STOPPED;
  StepResult result;
  result.event = StepEvent::STOPPED;
  result.stops = stops;
  return result;
}

StepResult Scheduler::step() {
  StepResult result;
  if (playState != PlaybackState::PLAYING) return result;

  const uint32_t now = clock.nowMs();
  if (pauseRequested.exchange(false, std::memory_order_relaxed)) {
    closeActive(now);
    if (pendingStops != 0) return enterStop();
    playState = PlaybackState::PAUSED;
    result.event = StepEvent::PAUSED;
    return result;
  }

  if (haveDue && timeBefore(now, dueMs)) {
    result.wake = true;
    result.wakeAtMs = dueMs;
    return result;
  }

  // The frame on screen ended at a stop and has had its full slot: report the stop. Nothing past
  // it has been consumed from the source.
  if (pendingStops != 0) {
    closeActive(now);
    return enterStop();
  }

  Token next;
  if (!source.peek(next)) {
    closeActive(now);
    pendingStops = STOP_CHAPTER_END;
    return enterStop();
  }

  result.wake = true;
  if (clearing.clearDue()) {
    extendActive(now);
    display.clearingRefresh();
    const uint32_t end = clock.nowMs();
    wpmMeter.addClearingRefresh(end - now);
    clearing.noteClearingRefresh();
    // The next frame follows at once: nothing on screen needs a dwell.
    haveDue = true;
    dueMs = end;
    result.event = StepEvent::CLEARED;
    result.wakeAtMs = end;
    return result;
  }

  if (!builder.next(frame)) {
    closeActive(now);
    pendingStops = STOP_CHAPTER_END;
    return enterStop();
  }

  const uint32_t start = clock.nowMs();
  extendActive(start);
  display.showFrame(frame);
  const uint32_t end = clock.nowMs();

  wpmMeter.addWordUpdate(frame.wordCount, end - start);
  wordAudit.recordFrame(frame.firstWordIndex, frame.lastWordIndex, frame.wordCount);
  clearing.noteWordUpdate(frame.lastWord.endsBlock());
  frameShown = true;
  haveLastRefreshEnd = true;
  lastRefreshEndMs = end;
  pendingStops = frame.stops;

  // Next update: the later of the paced deadline and the end of the dwell. The deadline counts
  // from this update's actual start, so lateness is never repaid by shortening later frames.
  haveDue = true;
  dueMs = timeLater(start + frame.holdMs, end + dwellMs);

  result.event = StepEvent::SHOWED_FRAME;
  result.wakeAtMs = pauseRequested.load(std::memory_order_relaxed) ? end : dueMs;
  return result;
}

}  // namespace rsvp
