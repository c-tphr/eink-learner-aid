#pragma once

// Fake clock, fake display and a run loop for scheduler tests. The fake display advances the fake
// clock by a configured refresh time inside each blocking call and keeps its own log of what was
// drawn, independent of the scheduler's audit and meter.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "lib/RsvpCore/Clock.h"
#include "lib/RsvpCore/DisplaySink.h"
#include "lib/RsvpCore/Scheduler.h"

namespace rsvptest {

class FakeClock : public rsvp::Clock {
 public:
  explicit FakeClock(uint32_t start = 0) : now(start) {}
  uint32_t nowMs() override { return now; }
  void advance(uint32_t ms) { now += ms; }
  // Moves forward to `t` if it is later (wrap-safe); never moves back.
  void advanceTo(uint32_t t) {
    if (rsvp::timeBefore(now, t)) now = t;
  }
  uint32_t now;
};

struct DisplayCall {
  enum Kind { FRAME, CLEAR } kind = FRAME;
  uint32_t startMs = 0;
  uint32_t endMs = 0;
  // Frames only.
  uint32_t firstWordIndex = 0;
  uint32_t lastWordIndex = 0;
  uint8_t wordCount = 0;
  uint32_t holdMs = 0;
  uint8_t stops = 0;
  std::string text;
};

class FakeDisplay : public rsvp::DisplaySink {
 public:
  explicit FakeDisplay(FakeClock& clock, uint32_t refreshMs = 0, uint32_t clearMs = 0)
      : clock(clock), refreshMs(refreshMs), clearMs(clearMs) {}

  void showFrame(const rsvp::Frame& frame) override {
    DisplayCall call;
    call.kind = DisplayCall::FRAME;
    call.startMs = clock.now;
    call.firstWordIndex = frame.firstWordIndex;
    call.lastWordIndex = frame.lastWordIndex;
    call.wordCount = frame.wordCount;
    call.holdMs = frame.holdMs;
    call.stops = frame.stops;
    call.text = std::string(frame.view());
    const size_t index = frames;
    ++frames;
    clock.advance(index == stallAtFrame ? stallMs : refreshMs);
    if (duringShow) duringShow(index);
    call.endMs = clock.now;
    log.push_back(call);
  }

  void clearingRefresh() override {
    DisplayCall call;
    call.kind = DisplayCall::CLEAR;
    call.startMs = clock.now;
    ++clears;
    clock.advance(clearMs);
    call.endMs = clock.now;
    log.push_back(call);
  }

  std::vector<DisplayCall> frameCalls() const {
    std::vector<DisplayCall> out;
    for (const auto& call : log) {
      if (call.kind == DisplayCall::FRAME) out.push_back(call);
    }
    return out;
  }

  FakeClock& clock;
  uint32_t refreshMs;
  uint32_t clearMs;
  size_t stallAtFrame = SIZE_MAX;  // 0-based frame that takes stallMs instead of refreshMs
  uint32_t stallMs = 0;
  std::function<void(size_t frameIndex)> duringShow;  // runs inside the blocking call
  std::vector<DisplayCall> log;
  size_t frames = 0;
  size_t clears = 0;
};

// Steps the scheduler, advancing the fake clock to each wake time, until it reports a pause or a
// stop or stops asking to be woken. Returns the last result.
inline rsvp::StepResult runUntilHalt(rsvp::Scheduler& scheduler, FakeClock& clock, size_t maxSteps = 1000000) {
  rsvp::StepResult result;
  for (size_t i = 0; i < maxSteps; ++i) {
    result = scheduler.step();
    if (result.event == rsvp::StepEvent::PAUSED || result.event == rsvp::StepEvent::STOPPED || !result.wake) {
      return result;
    }
    clock.advanceTo(result.wakeAtMs);
  }
  ADD_FAILURE() << "scheduler did not halt";
  return result;
}

// Steps until `frames` more frames have been drawn (or playback halts).
inline void runFrames(rsvp::Scheduler& scheduler, FakeClock& clock, FakeDisplay& display, size_t frames) {
  const size_t target = display.frames + frames;
  while (display.frames < target) {
    const rsvp::StepResult result = scheduler.step();
    if (!result.wake) return;
    if (display.frames < target) clock.advanceTo(result.wakeAtMs);
  }
}

}  // namespace rsvptest
