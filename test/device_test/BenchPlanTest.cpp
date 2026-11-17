#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "activities/devicetest/BenchPlan.h"

using namespace devicetest;
using displayprofile::PanelId;
using displayprofile::ProfileId;

namespace {

StepFigures someFigures(const uint32_t updates) {
  StepFigures f;
  f.updates.count = updates;
  f.updates.medianMs = 500;
  return f;
}

// Runs the current profile's steps up to (not including) `untilStep`, rating every rated step Clean
// and answering the residue prompt No.
void runSteps(Sweep& s, const size_t untilStep) {
  while (s.stepIndex() < untilStep) {
    const Next n = s.seriesDone(someFigures(s.stepSpec().count));
    if (n == Next::ASK_RATING) {
      const Next r = s.rate(RatingChoice::CLEAN);
      if (r == Next::ASK_RESIDUE) s.residue(ResidueChoice::NO);
    }
    if (s.profile().status != ProfileStatus::RUNNING) return;
  }
}

}  // namespace

TEST(BenchPlan, StepsAreThePlansTableInOrderWithItsCounts) {
  // docs/PLAN.md section 10, "Benchmark sweep, per display profile".
  ASSERT_EQ(STEP_COUNT, 10u);
  struct Expected {
    StepKind kind;
    const char* key;
    uint16_t count;
    uint16_t wpm;
    uint8_t words;
    bool rated;
  };
  const Expected expected[] = {
      {StepKind::WORD_BAND, "word-band", 100, WORD_BAND_WPM, 1, false},
      {StepKind::FULL_SCREEN, "full-screen", 10, 0, 0, false},
      {StepKind::CLEARING, "clearing", 5, 0, 0, false},
      {StepKind::PLAYBACK, "playback-120-1", 150, 120, 1, true},
      {StepKind::PLAYBACK, "playback-180-1", 150, 180, 1, true},
      {StepKind::PLAYBACK, "playback-240-1", 150, 240, 1, true},
      {StepKind::PLAYBACK, "playback-300-1", 150, 300, 1, true},
      {StepKind::PLAYBACK, "playback-240-2", 150, 240, 2, true},
      {StepKind::PLAYBACK, "playback-240-3", 150, 240, 3, true},
      {StepKind::ENDURANCE, "endurance", 300, 240, 1, true},
  };
  for (size_t i = 0; i < STEP_COUNT; ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(STEPS[i].kind, expected[i].kind);
    EXPECT_STREQ(STEPS[i].key, expected[i].key);
    EXPECT_EQ(STEPS[i].count, expected[i].count);
    EXPECT_EQ(STEPS[i].wpm, expected[i].wpm);
    EXPECT_EQ(STEPS[i].wordsPerUpdate, expected[i].words);
    EXPECT_EQ(STEPS[i].rated, expected[i].rated);
    EXPECT_EQ(asksResidue(STEPS[i]), STEPS[i].kind == StepKind::ENDURANCE);
  }
  EXPECT_EQ(WORD_BAND_WPM, 1000) << "the scheduler's highest pace: the word band measures the refresh";
}

TEST(BenchPlan, RunnableTrialsFollowTheAllowRule) {
  using displayprofile::trialBit;
  const auto all = displayprofile::ALL_TRIALS;
  EXPECT_EQ(runnableTrials(PanelId::DEPG0397, false), all);
  EXPECT_EQ(runnableTrials(PanelId::YRD0426, false), all);
  EXPECT_EQ(runnableTrials(PanelId::OPM040B3, false), all);
  EXPECT_EQ(runnableTrials(PanelId::SIMULATED, false), all);
  EXPECT_EQ(runnableTrials(PanelId::SE0400NQW47, false), trialBit(ProfileId::TRIAL_D)) << "only Trial D";
  EXPECT_EQ(runnableTrials(PanelId::UNKNOWN, false), displayprofile::NO_TRIALS);
  EXPECT_EQ(runnableTrials(PanelId::DEPG0397, true), displayprofile::NO_TRIALS) << "inverted: none";

  EXPECT_EQ(notAllowedReason(ProfileId::STOCK, PanelId::UNKNOWN, true), NotRunReason::NONE);
  EXPECT_EQ(notAllowedReason(ProfileId::TRIAL_A, PanelId::SE0400NQW47, false), NotRunReason::PANEL_NO_TABLES);
  EXPECT_EQ(notAllowedReason(ProfileId::TRIAL_D, PanelId::SE0400NQW47, false), NotRunReason::NONE);
  EXPECT_EQ(notAllowedReason(ProfileId::TRIAL_D, PanelId::UNKNOWN, false), NotRunReason::PANEL_UNRECOGNISED);
  EXPECT_EQ(notAllowedReason(ProfileId::TRIAL_B, PanelId::DEPG0397, true), NotRunReason::SCREEN_INVERTED);
  EXPECT_EQ(notAllowedReason(ProfileId::TRIAL_A, PanelId::SE0400NQW47, true), NotRunReason::PANEL_NO_TABLES)
      << "inversion is not the reason when the panel refuses it anyway";
}

TEST(BenchPlan, PlanMarksRefusedProfilesWithTheirReason) {
  Sweep s;
  EXPECT_EQ(s.plan(PanelId::SE0400NQW47, false), displayprofile::trialBit(ProfileId::TRIAL_D));
  EXPECT_EQ(s.profiles[0].id, ProfileId::STOCK);
  EXPECT_EQ(s.profiles[0].status, ProfileStatus::PENDING);
  for (int i = 1; i <= 3; ++i) {
    EXPECT_EQ(s.profiles[i].status, ProfileStatus::NOT_ALLOWED);
    EXPECT_EQ(s.profiles[i].reason, NotRunReason::PANEL_NO_TABLES);
    for (const StepRecord& r : s.profiles[i].steps) EXPECT_EQ(r.status, StepStatus::SKIPPED);
  }
  EXPECT_EQ(s.profiles[4].status, ProfileStatus::PENDING);
  EXPECT_EQ(s.runnableCount(), 2);

  Sweep unknown;
  EXPECT_EQ(unknown.plan(PanelId::UNKNOWN, false), displayprofile::NO_TRIALS);
  EXPECT_EQ(unknown.runnableCount(), 1) << "Stock only";
  for (int i = 1; i < 5; ++i) EXPECT_EQ(unknown.profiles[i].reason, NotRunReason::PANEL_UNRECOGNISED);
}

TEST(BenchPlan, StoredStateAfterABenchmarkUnlocksTheTrialsRunAndKeepsTheChosenProfile) {
  using displayprofile::decodeTrialSetting;
  using displayprofile::encodeTrialSetting;
  using displayprofile::TrialSetting;

  // Nothing stored: the SE0400NQW47 benchmark unlocks Trial D only; Stock stays chosen.
  const TrialSetting none = decodeTrialSetting(displayprofile::TRIAL_SETTING_DEFAULT);
  const TrialSetting afterSe = unlockForBenchmark(none, runnableTrials(PanelId::SE0400NQW47, false));
  EXPECT_EQ(encodeTrialSetting(afterSe), 0x08);
  EXPECT_EQ(afterSe.chosen, ProfileId::STOCK);
  // RSVP still plays Stock with this byte (RsvpActivity::onEnter selects `chosen`).
  EXPECT_EQ(displayprofile::resolveProfile(decodeTrialSetting(0x08), PanelId::SE0400NQW47, false), ProfileId::STOCK);

  // A DEPG0397 run unlocks all four; an unrecognised panel changes nothing.
  EXPECT_EQ(encodeTrialSetting(unlockForBenchmark(none, runnableTrials(PanelId::DEPG0397, false))), 0x0F);
  EXPECT_EQ(encodeTrialSetting(unlockForBenchmark(none, runnableTrials(PanelId::UNKNOWN, false))), 0x00);

  // A profile D3b had chosen stays chosen, and earlier unlocks are kept.
  TrialSetting kept;
  kept.unlocked = displayprofile::trialBit(ProfileId::TRIAL_B);
  kept.chosen = ProfileId::TRIAL_B;
  const TrialSetting again = unlockForBenchmark(kept, displayprofile::trialBit(ProfileId::TRIAL_D));
  EXPECT_EQ(again.chosen, ProfileId::TRIAL_B);
  EXPECT_EQ(again.unlocked,
            displayprofile::trialBit(ProfileId::TRIAL_B) | displayprofile::trialBit(ProfileId::TRIAL_D));
}

TEST(BenchPlan, AFullProfileRunsEveryStepThenSummarises) {
  Sweep s;
  s.plan(PanelId::DEPG0397, false);
  EXPECT_EQ(s.start(), Next::RUN_STEP);
  EXPECT_EQ(s.profile().id, ProfileId::STOCK);
  EXPECT_EQ(s.profile().status, ProfileStatus::RUNNING);

  // The three timing series are not rated: each goes straight on.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(s.stepIndex(), i);
    EXPECT_EQ(s.seriesDone(someFigures(STEPS[i].count)), Next::RUN_STEP);
    EXPECT_EQ(s.profiles[0].steps[i].status, StepStatus::DONE);
    EXPECT_EQ(s.profiles[0].steps[i].attempts, 1);
  }
  // Rated steps ask for a rating, and wait for it.
  for (size_t i = 3; i < 9; ++i) {
    EXPECT_EQ(s.seriesDone(someFigures(150)), Next::ASK_RATING);
    EXPECT_EQ(s.stepRecord().status, StepStatus::UNRATED);
    EXPECT_EQ(s.rate(i % 2 ? RatingChoice::CLEAN : RatingChoice::TOLERABLE), Next::RUN_STEP);
    EXPECT_EQ(s.profiles[0].steps[i].status, StepStatus::DONE);
  }
  // Endurance: rating, then residue.
  EXPECT_EQ(s.seriesDone(someFigures(300)), Next::ASK_RATING);
  EXPECT_EQ(s.rate(RatingChoice::TOLERABLE), Next::ASK_RESIDUE);
  EXPECT_EQ(s.residue(ResidueChoice::YES), Next::PROFILE_SUMMARY);
  EXPECT_EQ(s.profiles[0].status, ProfileStatus::DONE);
  EXPECT_EQ(s.profiles[0].steps[9].residue, Residue::YES);
  EXPECT_EQ(s.profiles[0].steps[9].rating, Rating::TOLERABLE);
  EXPECT_EQ(s.profiles[0].skippedFrom, -1);

  ASSERT_NE(s.nextProfile(), nullptr);
  EXPECT_EQ(s.nextProfile()->id, ProfileId::TRIAL_A);
  EXPECT_EQ(s.summary(SummaryChoice::NEXT), Next::RUN_STEP);
  EXPECT_EQ(s.profile().id, ProfileId::TRIAL_A);
  EXPECT_EQ(s.stepIndex(), 0);
  EXPECT_EQ(s.runnableOrdinal(), 2);
}

TEST(BenchPlan, RepeatStepRunsTheSameStepAgainAndCountsAttempts) {
  Sweep s;
  s.plan(PanelId::SIMULATED, false);
  s.start();
  runSteps(s, 3);
  ASSERT_EQ(s.stepIndex(), 3);
  EXPECT_EQ(s.seriesDone(someFigures(150)), Next::ASK_RATING);
  EXPECT_EQ(s.rate(RatingChoice::REPEAT), Next::RUN_STEP);
  EXPECT_EQ(s.stepIndex(), 3) << "the same step";
  EXPECT_EQ(s.seriesDone(someFigures(150)), Next::ASK_RATING);
  EXPECT_EQ(s.stepRecord().attempts, 2);
  EXPECT_EQ(s.rate(RatingChoice::UNREADABLE), Next::RUN_STEP);
  EXPECT_EQ(s.profiles[0].steps[3].rating, Rating::UNREADABLE);
  EXPECT_EQ(s.stepIndex(), 4) << "Unreadable alone carries on with the profile";

  // Repeat from the residue prompt runs the endurance step again and asks for its rating again.
  runSteps(s, 9);
  ASSERT_EQ(s.stepIndex(), 9);
  s.seriesDone(someFigures(300));
  EXPECT_EQ(s.rate(RatingChoice::CLEAN), Next::ASK_RESIDUE);
  EXPECT_EQ(s.residue(ResidueChoice::REPEAT), Next::RUN_STEP);
  EXPECT_EQ(s.stepRecord().rating, Rating::NONE);
  EXPECT_EQ(s.stepIndex(), 9);
}

TEST(BenchPlan, UnreadableSkipEndsTheProfileAndRecordsWhatWasSkipped) {
  Sweep s;
  s.plan(PanelId::SIMULATED, false);
  s.start();
  runSteps(s, STEP_COUNT);  // Stock, all Clean
  ASSERT_EQ(s.profiles[0].status, ProfileStatus::DONE);
  ASSERT_EQ(s.summary(SummaryChoice::NEXT), Next::RUN_STEP);  // Trial A
  runSteps(s, 4);
  ASSERT_EQ(s.stepIndex(), 4);  // playback-180-1
  s.seriesDone(someFigures(150));
  EXPECT_EQ(s.rate(RatingChoice::UNREADABLE_SKIP), Next::PROFILE_SUMMARY);
  const ProfileRecord& a = s.profiles[1];
  EXPECT_EQ(a.status, ProfileStatus::SKIPPED_PART_WAY);
  EXPECT_EQ(a.skippedFrom, 5);
  EXPECT_EQ(a.steps[4].status, StepStatus::DONE);
  EXPECT_EQ(a.steps[4].rating, Rating::UNREADABLE);
  for (size_t i = 5; i < STEP_COUNT; ++i) EXPECT_EQ(a.steps[i].status, StepStatus::SKIPPED);
  // The sweep carries on with the next profile.
  EXPECT_EQ(s.summary(SummaryChoice::NEXT), Next::RUN_STEP);
  EXPECT_EQ(s.profile().id, ProfileId::TRIAL_B);
}

TEST(BenchPlan, StoppingASeriesOffersRepeatSkipOrEnd) {
  Sweep s;
  s.plan(PanelId::SIMULATED, false);
  s.start();
  // Repeat discards the partial series.
  s.seriesStopped(someFigures(12));
  EXPECT_EQ(s.stopped(StopChoice::REPEAT), Next::RUN_STEP);
  EXPECT_EQ(s.stepIndex(), 0);
  EXPECT_EQ(s.stepRecord().status, StepStatus::PENDING);
  EXPECT_FALSE(s.stepRecord().hasFigures);
  // Skip: the stopped step keeps its partial figures, the rest of the profile is skipped.
  s.seriesStopped(someFigures(40));
  EXPECT_EQ(s.stopped(StopChoice::SKIP_PROFILE), Next::PROFILE_SUMMARY);
  const ProfileRecord& stock = s.profiles[0];
  EXPECT_EQ(stock.status, ProfileStatus::SKIPPED_PART_WAY);
  EXPECT_EQ(stock.skippedFrom, 0);
  EXPECT_EQ(stock.steps[0].status, StepStatus::STOPPED);
  EXPECT_EQ(stock.steps[0].attempts, 2);
  ASSERT_TRUE(stock.steps[0].hasFigures);
  EXPECT_EQ(stock.steps[0].figures.updates.count, 40u);
  for (size_t i = 1; i < STEP_COUNT; ++i) EXPECT_EQ(stock.steps[i].status, StepStatus::SKIPPED);

  // On to Trial A; stopping there and ending the run marks every later profile as not run.
  EXPECT_EQ(s.summary(SummaryChoice::NEXT), Next::RUN_STEP);
  runSteps(s, 2);
  s.seriesStopped(someFigures(3));
  EXPECT_EQ(s.stopped(StopChoice::END_RUN), Next::RUN_SUMMARY);
  EXPECT_FALSE(s.active());
  EXPECT_EQ(s.status(), RunStatus::ENDED_BY_OWNER);
  EXPECT_EQ(s.profiles[1].status, ProfileStatus::SKIPPED_PART_WAY);
  EXPECT_EQ(s.profiles[1].skippedFrom, 2);
  EXPECT_EQ(s.profiles[1].steps[1].status, StepStatus::DONE);
  EXPECT_EQ(s.profiles[1].steps[2].status, StepStatus::STOPPED);
  for (int i = 2; i < 5; ++i) {
    EXPECT_EQ(s.profiles[i].status, ProfileStatus::NOT_RUN);
    EXPECT_EQ(s.profiles[i].reason, NotRunReason::ENDED_BY_OWNER);
    EXPECT_EQ(s.profiles[i].steps[0].status, StepStatus::SKIPPED);
  }
}

TEST(BenchPlan, EndFromTheSummaryAndFinishingTheLastProfile) {
  Sweep s;
  s.plan(PanelId::SE0400NQW47, false);  // Stock and Trial D
  s.start();
  runSteps(s, STEP_COUNT);
  EXPECT_EQ(s.summary(SummaryChoice::NEXT), Next::RUN_STEP);
  EXPECT_EQ(s.profile().id, ProfileId::TRIAL_D) << "A to C are not allowed on this panel";
  runSteps(s, STEP_COUNT);
  EXPECT_EQ(s.nextProfile(), nullptr);
  EXPECT_EQ(s.summary(SummaryChoice::NEXT), Next::RUN_SUMMARY);
  EXPECT_EQ(s.status(), RunStatus::COMPLETE);

  Sweep e;
  e.plan(PanelId::SIMULATED, false);
  e.start();
  runSteps(e, STEP_COUNT);
  EXPECT_EQ(e.summary(SummaryChoice::END_RUN), Next::RUN_SUMMARY);
  EXPECT_EQ(e.status(), RunStatus::ENDED_BY_OWNER);
  EXPECT_EQ(e.profiles[0].status, ProfileStatus::DONE);
  EXPECT_EQ(e.profiles[1].status, ProfileStatus::NOT_RUN);
}

TEST(BenchPlan, AbandonKeepsAnsweredStepsAndMarksTheRest) {
  Sweep s;
  s.plan(PanelId::SIMULATED, false);
  s.start();
  runSteps(s, 4);
  s.seriesDone(someFigures(150));  // step 4 waits for its rating
  s.abandon(RunStatus::INTERRUPTED);
  EXPECT_FALSE(s.active());
  EXPECT_EQ(s.status(), RunStatus::INTERRUPTED);
  const ProfileRecord& stock = s.profiles[0];
  EXPECT_EQ(stock.status, ProfileStatus::SKIPPED_PART_WAY);
  EXPECT_EQ(stock.steps[3].status, StepStatus::DONE);
  EXPECT_EQ(stock.steps[4].status, StepStatus::UNRATED) << "its figures are kept";
  EXPECT_EQ(stock.skippedFrom, 5);
  EXPECT_EQ(stock.steps[5].status, StepStatus::SKIPPED);
  EXPECT_EQ(s.profiles[1].status, ProfileStatus::NOT_RUN);
  EXPECT_EQ(s.profiles[1].reason, NotRunReason::INTERRUPTED);

  // A series stopped by the owner whose prompt was never answered is kept as stopped.
  Sweep t;
  t.plan(PanelId::SIMULATED, false);
  t.start();
  t.seriesStopped(someFigures(7));
  t.abandon(RunStatus::INTERRUPTED);
  EXPECT_EQ(t.profiles[0].steps[0].status, StepStatus::STOPPED);
  EXPECT_EQ(t.profiles[0].steps[0].figures.updates.count, 7u);
  EXPECT_EQ(t.profiles[0].skippedFrom, 1);

  // Abandoning a run that is not running changes nothing.
  Sweep idle;
  idle.plan(PanelId::SIMULATED, false);
  idle.abandon(RunStatus::INTERRUPTED);
  EXPECT_EQ(idle.status(), RunStatus::RUNNING);
  EXPECT_EQ(idle.profiles[1].status, ProfileStatus::PENDING);
}

TEST(BenchPlan, ResultFileNamesSortByRunNumberAndParseBack) {
  char name[24];
  resultFileName(7, ResultKind::BENCHMARK, name, sizeof(name));
  EXPECT_STREQ(name, "0007-bench.json");
  resultFileName(1234, ResultKind::CONTROLS, name, sizeof(name));
  EXPECT_STREQ(name, "1234-controls.json");
  EXPECT_EQ(runNumberOf("0007-bench.json"), 7);
  EXPECT_EQ(runNumberOf("9999-controls.json"), 9999);
  EXPECT_EQ(runNumberOf("0007-bench.json.tmp"), 7) << "a leftover temporary file still holds its number";
  EXPECT_EQ(runNumberOf("bench.json"), 0);
  EXPECT_EQ(runNumberOf("12a4-bench.json"), 0);
  EXPECT_EQ(runNumberOf("00012-bench.json"), 0);
  EXPECT_EQ(runNumberOf(""), 0);
  // Lexical order is run order.
  char a[24];
  char b[24];
  resultFileName(9, ResultKind::CONTROLS, a, sizeof(a));
  resultFileName(10, ResultKind::BENCHMARK, b, sizeof(b));
  EXPECT_LT(std::strcmp(a, b), 0);
}

TEST(BenchPlan, NamesInTheFileAreStable) {
  EXPECT_STREQ(stepKindName(StepKind::FULL_SCREEN), "full-screen");
  EXPECT_STREQ(ratingName(Rating::UNREADABLE), "unreadable");
  EXPECT_STREQ(residueName(Residue::YES), "yes");
  EXPECT_STREQ(stepStatusName(StepStatus::UNRATED), "unrated");
  EXPECT_STREQ(profileStatusName(ProfileStatus::SKIPPED_PART_WAY), "skipped-part-way");
  EXPECT_STREQ(notRunReasonName(NotRunReason::PANEL_NO_TABLES), "panel-no-tables");
  EXPECT_STREQ(runStatusName(RunStatus::ENDED_BY_OWNER), "ended-by-owner");
}

TEST(BenchPlan, ARefusedProfileIsRecordedAndTheSweepMovesOn) {
  Sweep s;
  s.plan(PanelId::SIMULATED, false);
  s.start();
  runSteps(s, STEP_COUNT);
  ASSERT_EQ(s.summary(SummaryChoice::NEXT), Next::RUN_STEP);
  ASSERT_EQ(s.profile().id, ProfileId::TRIAL_A);
  EXPECT_EQ(s.refuse(), Next::RUN_STEP);
  EXPECT_EQ(s.profiles[1].status, ProfileStatus::NOT_ALLOWED);
  EXPECT_EQ(s.profiles[1].reason, NotRunReason::REFUSED);
  EXPECT_EQ(s.profiles[1].steps[0].status, StepStatus::SKIPPED);
  EXPECT_EQ(s.profile().id, ProfileId::TRIAL_B);
}

TEST(BenchPlan, EstimateOfAProfilesDuration) {
  // Stock at the Xteink X4 timings the simulator models (540 and 1,960 ms; README.md shows the sum):
  // 10 opening clears 19.6 s, word band 60 s + 1 clear, full screen 5.4 s, clearing 9.8 s, four
  // single-word passages 90 s + 2 clears each, two words 45 s + 1 clear, three words 37.5 s,
  // endurance 180 s + 5 clears, and 7 ratings of 15 s.
  EXPECT_EQ(estimateProfileSeconds(540, 1960, 60, OWNER_SECONDS_PER_RATING), 852u);
  // A trial of about 200 ms per update: the pacing, not the panel, sets most of the time.
  EXPECT_LT(estimateProfileSeconds(198, 1960, 30, OWNER_SECONDS_PER_RATING), 852u);
  EXPECT_EQ(estimateProfileSeconds(0, 0, 60, 0), 348u) << "pacing and dwell alone";
}
