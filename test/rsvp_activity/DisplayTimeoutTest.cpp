// The RSVP activity's pause on a display timeout (task R5, carried over from D1): the real scheduler
// drawn through ProfileDisplaySink and the device backend's timing logic (TimedBackend) with a fake
// refresh port, and reading::UpdateTimeoutWatch deciding as RsvpActivity::stepOnce() does. The
// simulator's modelled backend never classes a call as a timeout, so this is where the rule is proven.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "FakeDisplayBackend.h"
#include "PanelRecords.h"
#include "ProfileDisplaySink.h"
#include "TestSupport.h"
#include "lib/RsvpCore/Scheduler.h"
#include "reading/UpdateTimeoutWatch.h"

using namespace displayprofile;

namespace {

class NullPainter final : public FramePainter {
 public:
  void paintFrame(const rsvp::Frame&) override {}
  void paintClearedBand() override {}
};

std::vector<rsvptest::OwnedToken> words(const uint32_t count) {
  std::vector<rsvptest::OwnedToken> tokens;
  for (uint32_t i = 0; i < count; ++i) tokens.push_back({"word" + std::to_string(i), i, 0});
  return tokens;
}

// BUSY never reports the end: the SDK's wait gives up after 30 s (TimedBackendTest).
constexpr uint32_t STUCK_BUSY_MS = 30020;
constexpr uint32_t NORMAL_BUSY_MS = 380;

struct Rig {
  Rig() : clock(1000), port(clock), backend(port, clock, PanelId::DEPG0397), sink(backend, painter) {}

  // Plays until a pause takes effect or `maxFrames` frames were shown, pausing after a frame whose
  // update timed out as the activity does. From frame `stuckFrom` (0-based; -1: never) every update
  // lasts the SDK's 30 s.
  int play(const int maxFrames, const int stuckFrom = -1) {
    scheduler.play();
    watch.arm(backend.stats().updates.timeouts);
    int frames = 0;
    for (int i = 0; i < 10000 && frames < maxFrames; ++i) {
      port.updateBusyMs = stuckFrom >= 0 && frames >= stuckFrom ? STUCK_BUSY_MS : NORMAL_BUSY_MS;
      const rsvp::StepResult r = scheduler.step();
      if (r.event == rsvp::StepEvent::SHOWED_FRAME) {
        ++frames;
        if (watch.rose(backend.stats().updates.timeouts)) scheduler.requestPause();
      } else if (r.event == rsvp::StepEvent::PAUSED) {
        paused = true;
        break;
      }
      if (!r.wake) break;
      clock.advanceTo(r.wakeAtMs);
    }
    return frames;
  }

  displayprofile::FakeClock clock;
  FakeRefreshPort port;
  TimedBackend backend;
  NullPainter painter;
  ProfileDisplaySink sink;
  rsvptest::VectorSource source{words(40)};
  rsvp::Scheduler scheduler{source, clock, sink};
  reading::UpdateTimeoutWatch watch;
  bool paused = false;
};

}  // namespace

TEST(DisplayTimeout, AStuckPanelPausesAfterOneTimedOutUpdate) {
  Rig rig;
  const uint32_t start = rig.clock.now;
  EXPECT_EQ(rig.play(40, 3), 4) << "frames 0-2 normal, frame 3 times out, then the pause";
  EXPECT_TRUE(rig.paused);
  EXPECT_EQ(rig.backend.stats().updates.timeouts, 1u);
  // One 30 s update, not one per remaining word.
  EXPECT_LT(rig.clock.now - start, 32000u);
  EXPECT_EQ(rig.scheduler.audit().wordsDrawn(), 4u);
  EXPECT_TRUE(rig.scheduler.audit().inOrder());
}

TEST(DisplayTimeout, AnEarlierTimeoutDoesNotPauseTheNextPlay) {
  Rig rig;
  rig.play(40, 0);
  ASSERT_TRUE(rig.paused);
  ASSERT_EQ(rig.backend.stats().updates.timeouts, 1u);
  rig.paused = false;
  EXPECT_EQ(rig.play(10), 10) << "the panel answers again: playback carries on";
  EXPECT_FALSE(rig.paused);
}

TEST(DisplayTimeout, ASlowPanelThatAnswersDoesNotPause) {
  // Just under the limit: a slow update, not a timeout. Playback carries on, slowly.
  Rig rig;
  rig.scheduler.play();
  rig.watch.arm(rig.backend.stats().updates.timeouts);
  rig.port.updateBusyMs = UPDATE_TIMEOUT_MS - 1;
  int frames = 0;
  for (int i = 0; i < 100 && frames < 5; ++i) {
    const rsvp::StepResult r = rig.scheduler.step();
    ASSERT_NE(r.event, rsvp::StepEvent::PAUSED);
    if (r.event == rsvp::StepEvent::SHOWED_FRAME) {
      ++frames;
      EXPECT_FALSE(rig.watch.rose(rig.backend.stats().updates.timeouts));
    }
    rig.clock.advanceTo(r.wakeAtMs);
  }
  EXPECT_EQ(frames, 5);
  EXPECT_EQ(rig.backend.stats().updates.timeouts, 0u);
}

TEST(DisplayTimeout, WatchCountsOnlyNewTimeouts) {
  reading::UpdateTimeoutWatch watch;
  watch.arm(2);
  EXPECT_FALSE(watch.rose(2));
  EXPECT_TRUE(watch.rose(3));
  EXPECT_FALSE(watch.rose(3));
  EXPECT_TRUE(watch.rose(5));
}
