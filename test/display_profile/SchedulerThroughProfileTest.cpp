// The real rsvp::Scheduler driven through ProfileDisplaySink and a modelled backend (the fake),
// over the committed English prose sample (test/rsvp_core/prose_sample). This is the simulator's
// arrangement with a fake clock: the delivered WPM must match the arithmetic of the RsvpCore sweep
// (test/rsvp_core/PlaybackSweepTest.cpp):
//
//   expected = 60,000 * W / (sum over frames of max(holdMs, U + D) + K * C)
//
// with U the profile's modelled update time, C its clearing time, D the 60 ms dwell and K the
// clearing refreshes the scheduler ran.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "FakeDisplayBackend.h"
#include "ProfileDisplaySink.h"
#include "TestSupport.h"
#include "lib/RsvpCore/FrameBuilder.h"
#include "lib/RsvpCore/Scheduler.h"

using namespace displayprofile;

namespace {

constexpr uint32_t DWELL = rsvp::DEFAULT_MIN_DWELL_MS;

// Records what was painted, and checks that each paint comes right before its refresh.
class RecordingPainter final : public FramePainter {
 public:
  explicit RecordingPainter(const DisplayBackend& backend) : backend(backend) {}

  void paintFrame(const rsvp::Frame& frame) override {
    // Painted before the backend presents it: no update has run for this frame yet.
    EXPECT_EQ(backend.stats().updates.count, frames.size());
    frames.push_back(std::string(frame.view()));
  }
  void paintClearedBand() override {
    EXPECT_EQ(backend.stats().clears.count, clearedBands);
    ++clearedBands;
  }

  const DisplayBackend& backend;
  std::vector<std::string> frames;
  uint32_t clearedBands = 0;
};

const std::vector<rsvptest::OwnedToken>& prose() {
  static const std::vector<rsvptest::OwnedToken> tokens =
      rsvptest::proseTokens(rsvptest::readFile(RSVP_TEST_DIR "/prose_sample/prose_sample.txt"));
  return tokens;
}

struct Result {
  rsvp::MeterStats stats;
  uint32_t deliveredTenths = 0;
  uint64_t expectedActiveMs = 0;
  std::vector<std::string> painted;
  uint32_t maxUpdatesBetweenClears = 0;
  bool inOrder = false;
};

Result play(const ProfileId profile, const uint16_t wpm, const uint8_t wordsPerFrame) {
  FakeClock clock(5000);
  FakeDisplayBackend backend(clock);  // simulator defaults: stock 540/1960, trial C 198/1960
  EXPECT_TRUE(backend.select(profile));
  backend.setRealMs(4, 9);  // a little real refresh time, as in the simulator; the model absorbs it
  RecordingPainter painter(backend);
  ProfileDisplaySink sink(backend, painter);
  rsvptest::VectorSource source(prose());
  rsvp::Scheduler scheduler(source, clock, sink);
  scheduler.setWpm(wpm);
  scheduler.setWordsPerFrame(wordsPerFrame);
  scheduler.setClearingLimits(backend.info().clearing);  // the activity passes the profile's limits

  scheduler.play();
  while (true) {
    const rsvp::StepResult r = scheduler.step();
    if (r.event == rsvp::StepEvent::STOPPED || !r.wake) break;
    clock.advanceTo(r.wakeAtMs);
  }
  EXPECT_EQ(scheduler.state(), rsvp::PlaybackState::STOPPED);
  EXPECT_TRUE(scheduler.stopReasons() & rsvp::STOP_CHAPTER_END);

  Result result;
  result.stats = scheduler.meter().stats();
  result.deliveredTenths = scheduler.meter().deliveredWpmTenths();
  result.painted = painter.frames;
  result.inOrder = scheduler.audit().inOrder();

  // The arithmetic expectation from an independent FrameBuilder run with the same settings.
  const ProfileTiming timing = backend.model().timing(profile);
  rsvptest::VectorSource reference(prose());
  rsvp::FrameBuilder builder(reference);
  builder.setWpm(wpm);
  builder.setWordsPerFrame(wordsPerFrame);
  for (const rsvp::Frame& f : rsvptest::allFrames(builder)) result.expectedActiveMs += std::max(f.holdMs, timing.updateMs + DWELL);
  result.expectedActiveMs += uint64_t{backend.stats().clears.count} * timing.clearMs;

  uint32_t run = 0;
  for (const FakeEvent& e : backend.events) {
    if (e.kind == FakeEvent::UPDATE) result.maxUpdatesBetweenClears = std::max(result.maxUpdatesBetweenClears, ++run);
    if (e.kind == FakeEvent::CLEAR) run = 0;
  }
  // The backend saw every display operation the meter counted, and each lasted the model.
  EXPECT_EQ(backend.stats().updates.count, result.stats.updates);
  EXPECT_EQ(backend.stats().clears.count, result.stats.clearingRefreshes);
  EXPECT_EQ(backend.stats().updates.minMs, timing.updateMs);
  EXPECT_EQ(backend.stats().updates.maxMs, timing.updateMs);
  EXPECT_EQ(result.stats.updateMsMax, timing.updateMs);  // the scheduler's own clock agrees
  if (result.stats.clearingRefreshes > 0) {
    EXPECT_EQ(result.stats.clearMsMax, timing.clearMs);
  }
  return result;
}

}  // namespace

TEST(SchedulerThroughProfile, StockAt540DeliversTheArithmeticWpm) {
  const Result r = play(ProfileId::STOCK, 240, 1);
  const size_t words = prose().size();
  EXPECT_TRUE(r.inOrder);
  EXPECT_EQ(r.stats.wordsShown, words);
  ASSERT_EQ(r.painted.size(), words);  // one word per update, every word painted once, in order
  for (size_t i = 0; i < words; ++i) ASSERT_EQ(r.painted[i], prose()[i].text) << i;

  EXPECT_EQ(r.stats.activeMs, r.expectedActiveMs);
  const double expected = 60000.0 * static_cast<double>(words) / static_cast<double>(r.expectedActiveMs);
  const double delivered = r.deliveredTenths / 10.0;
  EXPECT_NEAR(delivered, expected, 0.05);
  // 60,000 / (540 + 60) = 100 WPM before clears; long holds and 1,960 ms clears take it lower.
  EXPECT_LT(delivered, 100.0);
  EXPECT_GT(delivered, 90.0);
  EXPECT_LE(r.maxUpdatesBetweenClears, 120u);
  std::printf(
      "stock, 540/1960 ms, 240 WPM, 1 word/update: %zu words, %u updates, %u clears, active %llu ms, "
      "delivered %.1f WPM, arithmetic %.2f WPM\n",
      words, r.stats.updates, r.stats.clearingRefreshes, static_cast<unsigned long long>(r.stats.activeMs), delivered,
      expected);
}

TEST(SchedulerThroughProfile, GroupedWordsAndATrialProfileWithItsLowerLimits) {
  const Result r = play(ProfileId::TRIAL_C, 240, 2);
  EXPECT_TRUE(r.inOrder);
  EXPECT_EQ(r.stats.wordsShown, prose().size());
  EXPECT_EQ(r.stats.activeMs, r.expectedActiveMs);
  const double expected = 60000.0 * static_cast<double>(prose().size()) / static_cast<double>(r.expectedActiveMs);
  EXPECT_NEAR(r.deliveredTenths / 10.0, expected, 0.05);
  EXPECT_LE(r.maxUpdatesBetweenClears, 60u);  // trial C's hard limit
  EXPECT_GT(r.stats.clearingRefreshes, 0u);
  std::printf("trial-c, 198/1960 ms, 240 WPM, 2 words/update: delivered %.1f WPM, arithmetic %.2f WPM\n",
              r.deliveredTenths / 10.0, expected);
}
