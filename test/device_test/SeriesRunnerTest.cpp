// The series runner through every step of every profile, on the fake display backend (modelled
// times, the real allow rule and exposure limits) with a fake clock: the counts the results file
// records come out exactly as the plan says, clearing refreshes never count as updates, no request
// is promoted, and a stop ends a series before its next display call.

#include <FakeDisplayBackend.h>
#include <gtest/gtest.h>

#include <vector>

#include "activities/devicetest/SeriesRunner.h"

using namespace devicetest;
using displayprofile::FakeClock;
using displayprofile::FakeDisplayBackend;
using displayprofile::FakeEvent;
using displayprofile::PanelId;
using displayprofile::ProfileId;

namespace {

class CountingPainter final : public SeriesPainter {
 public:
  void paintStepScreen(const StepSpec&) override { ++stepScreens; }
  void paintBand(const rsvp::Frame* frame, const uint32_t shown) override {
    if (frame != nullptr) {
      ++bands;
      lastShown = shown;
    } else {
      ++emptyBands;
    }
  }
  void paintPage(const uint32_t n) override { pages.push_back(n); }

  int stepScreens = 0;
  int bands = 0;
  int emptyBands = 0;
  uint32_t lastShown = 0;
  std::vector<uint32_t> pages;
};

struct Rig {
  explicit Rig(const ProfileId profile) : backend(clock, displayprofile::TimingModel(), PanelId::SIMULATED) {
    backend.setUnlockedTrials(displayprofile::ALL_TRIALS);
    EXPECT_TRUE(backend.select(profile));
  }

  // Runs the series to its end; returns the number of tick() calls.
  int run(const StepSpec& step, const int stopAfterOps = -1) {
    runner.begin(step);
    int ticks = 0;
    while (!runner.done() && ticks < 100000) {
      if (stopAfterOps >= 0 && runner.progress() >= static_cast<uint32_t>(stopAfterOps)) runner.requestStop();
      const SeriesRunner::Tick t = runner.tick();
      ++ticks;
      if (t.wake) clock.advanceTo(t.wakeAtMs);
    }
    return ticks;
  }

  FakeClock clock{1000};
  FakeDisplayBackend backend;
  CountingPainter painter;
  SeriesRunner runner{backend, clock, painter};
};

const StepSpec& stepOf(const StepKind kind, const uint16_t wpm = 0, const uint8_t words = 0) {
  for (const StepSpec& s : STEPS) {
    if (s.kind == kind && (wpm == 0 || (s.wpm == wpm && s.wordsPerUpdate == words))) return s;
  }
  return STEPS[0];
}

class EveryProfile : public ::testing::TestWithParam<ProfileId> {};

}  // namespace

TEST_P(EveryProfile, EveryStepRecordsExactlyThePlannedCounts) {
  Rig rig(GetParam());
  const rsvp::ClearingLimits limits = displayprofile::profileInfo(GetParam()).clearing;
  for (const StepSpec& step : STEPS) {
    SCOPED_TRACE(step.key);
    const size_t eventsBefore = rig.backend.events.size();
    rig.run(step);
    ASSERT_TRUE(rig.runner.done());
    EXPECT_FALSE(rig.runner.stopped());
    const StepFigures f = rig.runner.figures();
    EXPECT_EQ(f.promotions, 0u) << "the series respects every exposure rule by itself";
    EXPECT_EQ(f.updates.timeouts, 0u);
    EXPECT_GT(f.durationMs, 0u);

    // The step starts with one clearing refresh, before the statistics are reset.
    ASSERT_GT(rig.backend.events.size(), eventsBefore);
    EXPECT_EQ(rig.backend.events[eventsBefore].kind, FakeEvent::CLEAR);

    switch (step.kind) {
      case StepKind::FULL_SCREEN:
        EXPECT_EQ(f.updates.count, 10u);
        EXPECT_EQ(f.clears.count, 0u);
        EXPECT_FALSE(f.hasPlayback);
        break;
      case StepKind::CLEARING:
        EXPECT_EQ(f.updates.count, 0u);
        EXPECT_EQ(f.clears.count, 5u);
        EXPECT_EQ(f.clears.medianMs, displayprofile::profileInfo(GetParam()).simulatorDefault.clearMs);
        break;
      case StepKind::WORD_BAND:
      case StepKind::PLAYBACK:
      case StepKind::ENDURANCE: {
        ASSERT_TRUE(f.hasPlayback);
        const PlaybackFigures& p = f.playback;
        const uint32_t words = step.count;
        EXPECT_EQ(p.wordsShown, words);
        EXPECT_EQ(p.wordsDrawn, words);
        EXPECT_TRUE(p.inOrder);
        EXPECT_EQ(p.segments, 1u);
        EXPECT_EQ(p.violations, 0u);
        EXPECT_EQ(f.updates.count, p.frames) << "every word update is a frame; clears are counted apart";
        EXPECT_EQ(f.clears.count, p.clearingRefreshes);
        if (step.wordsPerUpdate == 1) {
          EXPECT_EQ(p.frames, words);
        } else {
          EXPECT_LT(p.frames, words);
          EXPECT_GE(p.frames, words / step.wordsPerUpdate);
        }
        // Clearing follows the profile's limits: never more than `hard` updates in a row.
        EXPECT_GE(f.clears.count, (p.frames - 1) / limits.hardLimit);
        EXPECT_GT(p.deliveredWpmTenths, 0u);
        break;
      }
    }
    // Word updates take the profile's modelled time; the median and p95 cover the series only.
    if (f.updates.count > 0) {
      EXPECT_EQ(f.updates.medianMs, displayprofile::profileInfo(GetParam()).simulatorDefault.updateMs);
      EXPECT_EQ(f.updates.p95Ms, f.updates.medianMs);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(Profiles, EveryProfile,
                         ::testing::Values(ProfileId::STOCK, ProfileId::TRIAL_A, ProfileId::TRIAL_B, ProfileId::TRIAL_C,
                                           ProfileId::TRIAL_D));

TEST(SeriesRunner, TheWordBandRunsAtTheDwellNotThePacing) {
  Rig rig(ProfileId::STOCK);
  rig.run(STEPS[0]);
  const StepFigures f = rig.runner.figures();
  ASSERT_EQ(f.updates.count, 100u);
  // 100 updates of 540 ms each and 60 ms of dwell between them, nothing else (no clear: 100 < 120).
  EXPECT_EQ(f.clears.count, 0u);
  EXPECT_EQ(f.durationMs, 100u * 540u + 99u * 60u);
}

TEST(SeriesRunner, PlaybackDeliversTheTargetOnAnInstantDisplay) {
  displayprofile::TimingModel instant;
  displayprofile::TimingOverrides zero;
  zero.hasUpdate = true;
  zero.hasClear = true;
  ASSERT_TRUE(instant.resolve(displayprofile::TimingFile{}, zero));
  FakeClock clock(5);
  FakeDisplayBackend backend(clock, instant);
  CountingPainter painter;
  SeriesRunner runner(backend, clock, painter);
  for (const uint16_t wpm : {120, 180, 240, 300}) {
    runner.begin(stepOf(StepKind::PLAYBACK, wpm, 1));
    while (!runner.done()) {
      const auto t = runner.tick();
      if (t.wake) clock.advanceTo(t.wakeAtMs);
    }
    const uint32_t delivered = runner.figures().playback.deliveredWpmTenths;
    // The passage's punctuation is denser than the prose sample the weights are normalised on, so it
    // plays somewhat slower than the target; within 15 %.
    EXPECT_GT(delivered, wpm * 10u * 85u / 100u) << wpm;
    EXPECT_LE(delivered, wpm * 10u * 105u / 100u) << wpm;
  }
}

TEST(SeriesRunner, AStopEndsTheSeriesBeforeItsNextDisplayCall) {
  Rig rig(ProfileId::TRIAL_C);
  rig.run(stepOf(StepKind::PLAYBACK, 240, 1), 20);
  EXPECT_TRUE(rig.runner.done());
  EXPECT_TRUE(rig.runner.stopped());
  const StepFigures f = rig.runner.figures();
  EXPECT_EQ(f.playback.frames, 20u);
  EXPECT_EQ(f.updates.count, 20u);

  rig.run(stepOf(StepKind::FULL_SCREEN), 4);
  EXPECT_TRUE(rig.runner.stopped());
  EXPECT_EQ(rig.runner.figures().updates.count, 4u);
  rig.run(stepOf(StepKind::CLEARING), 0);
  EXPECT_TRUE(rig.runner.stopped());
  EXPECT_EQ(rig.runner.figures().clears.count, 0u);
}

TEST(SeriesRunner, FiguresAreFrozenWhenTheSeriesEnds) {
  Rig rig(ProfileId::TRIAL_D);
  rig.run(stepOf(StepKind::FULL_SCREEN));
  const StepFigures before = rig.runner.figures();
  // The prompt drawn after a trial series: a screen update, which the exposure rules run as a clear.
  rig.backend.update();
  EXPECT_EQ(rig.backend.lastDecision().promotion, displayprofile::Promotion::RESIDUE);
  const StepFigures after = rig.runner.figures();
  EXPECT_EQ(after.updates.count, before.updates.count);
  EXPECT_EQ(after.clears.count, before.clears.count);
  EXPECT_EQ(after.promotions, 0u);
}

TEST(SeriesRunner, PagesAlternateAndBandsAreCounted) {
  Rig rig(ProfileId::STOCK);
  rig.run(stepOf(StepKind::CLEARING));
  EXPECT_EQ(rig.painter.pages, (std::vector<uint32_t>{0, 1, 2, 3, 4}));
  rig.run(stepOf(StepKind::PLAYBACK, 240, 3));
  EXPECT_EQ(static_cast<uint32_t>(rig.painter.bands), rig.runner.figures().playback.frames);
  EXPECT_EQ(rig.painter.lastShown, rig.runner.figures().playback.frames);
  EXPECT_EQ(rig.painter.stepScreens, 2);
}
