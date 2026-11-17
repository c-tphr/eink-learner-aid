#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

#include "SampleRuns.h"
#include "activities/devicetest/Acceptance.h"

using namespace devicetest;

namespace {

StepFigures goodRun(const StepSpec& spec, const uint32_t deliveredTenths) {
  StepFigures f;
  f.updates.count = spec.count;
  f.hasPlayback = true;
  f.playback.deliveredWpmTenths = deliveredTenths;
  f.playback.wordsShown = spec.count;
  f.playback.frames = spec.count;
  f.playback.inOrder = true;
  f.playback.wordsDrawn = spec.count;
  f.playback.segments = 1;
  return f;
}

// Part A answered: both runs measured as given, the answers as given.
void finishPartA(Walkthrough& w, const StepFigures& first, const StepFigures& second, const ReadableChoice readable,
                 const SlowerChoice slower) {
  ASSERT_EQ(w.start(StartChoice::START), Walk::RUN);
  ASSERT_EQ(w.runIndex(), 0);
  ASSERT_EQ(w.runDone(first), Walk::ASK_READABLE);
  ASSERT_EQ(w.readable(readable), Walk::RUN);
  ASSERT_EQ(w.runIndex(), 1);
  ASSERT_EQ(w.runDone(second), Walk::ASK_READABLE);
  ASSERT_EQ(w.readable(readable), Walk::ASK_SLOWER);
  ASSERT_EQ(w.slower(slower), Walk::PART_A_DONE);
}

void finishPartAWell(Walkthrough& w) {
  finishPartA(w, goodRun(ACCEPTANCE_RUNS[0], 1250), goodRun(ACCEPTANCE_RUNS[1], 1310), ReadableChoice::YES,
              SlowerChoice::YES);
}

}  // namespace

TEST(AcceptanceSteps, PartAPlaysThePassageAtTheDefaultAndAt600) {
  // docs/PLAN.md section 10, "The PRD's on-device criterion".
  ASSERT_EQ(ACCEPTANCE_RUN_COUNT, 2u);
  EXPECT_EQ(ACCEPTANCE_RUNS[0].wpm, 240);
  EXPECT_EQ(ACCEPTANCE_RUNS[1].wpm, 600);
  for (const StepSpec& s : ACCEPTANCE_RUNS) {
    EXPECT_EQ(s.kind, StepKind::PLAYBACK);
    EXPECT_EQ(s.count, PASSAGE_WORDS);
    EXPECT_EQ(s.wordsPerUpdate, 1);
  }
}

TEST(AcceptanceSteps, TheTableCoversCriteriaOneToFiveInOneSitting) {
  ASSERT_GE(WALK_STEP_COUNT, 10u);
  ASSERT_LE(WALK_STEP_COUNT, 14u);
  std::set<std::string> ids;
  CriteriaMask covered = 0;
  for (const WalkStep& s : WALK_STEPS) {
    ASSERT_NE(s.id, nullptr);
    EXPECT_TRUE(ids.insert(s.id).second) << "duplicate id " << s.id;
    EXPECT_LT(strlen(s.id), 24u) << s.id;
    for (const char* text : {s.title, s.instructions, s.expected, s.trace}) {
      ASSERT_NE(text, nullptr);
      EXPECT_GT(strlen(text), 10u) << s.id;
    }
    // The instructions and the expected result fit the step screens (README.md, "Screens").
    EXPECT_LE(strlen(s.instructions), 420u) << s.id;
    EXPECT_LE(strlen(s.expected), 380u) << s.id;
    EXPECT_LE(strlen(s.title), 44u) << s.id;
    ASSERT_NE(s.dependsOn, nullptr);
    covered = static_cast<CriteriaMask>(covered | s.criteria);
  }
  for (unsigned k = 1; k <= 4; ++k) EXPECT_TRUE(covered & criterionBit(k)) << "criterion " << k;
  // Criterion 5's Part B step: the real pause screen.
  EXPECT_TRUE(covered & criterionBit(5));
}

TEST(AcceptanceSteps, CriterionOneIsShownInBothModesByTouchAndByKeys) {
  // The single- and the multi-question checkpoint each fire once in RSVP and once in page view, one
  // by touch and one by keys, and one step jumps past them.
  const char* expected[] = {"c0001-rsvp-touch", "c0001-page-keys", "c0002-rsvp-keys", "later-pass-grades", "toc-jump"};
  for (const char* id : expected) {
    bool found = false;
    for (const WalkStep& s : WALK_STEPS) {
      if (strcmp(s.id, id) == 0) {
        found = true;
        EXPECT_TRUE(s.criteria & criterionBit(1)) << id;
      }
    }
    EXPECT_TRUE(found) << id;
  }
}

TEST(AcceptanceSteps, RereadAndRestoreStepsNameTheTasksTheyDependOn) {
  for (const WalkStep& s : WALK_STEPS) {
    const std::string id = s.id;
    if (id.find("reread") != std::string::npos) {
      EXPECT_NE(strstr(s.dependsOn, "C4"), nullptr) << id;
    }
    if (id == "close-in-question" || id == "sleep-in-reread") {
      EXPECT_NE(strstr(s.dependsOn, "C5"), nullptr) << id;
    }
  }
}

TEST(Walkthrough, AFreshOneStartsAtPartA) {
  Walkthrough w;
  w.reset();
  EXPECT_EQ(w.resume(), Walk::START);
  EXPECT_FALSE(w.complete());
  EXPECT_EQ(w.status(), RunStatus::RUNNING);
  EXPECT_EQ(w.start(StartChoice::BACK), Walk::MENU);
  EXPECT_EQ(w.resume(), Walk::START);
}

TEST(Walkthrough, PartAAsksReadabilityAfterEachRunAndTheSlowdownAfterTheLast) {
  Walkthrough w;
  w.reset();
  finishPartAWell(w);
  EXPECT_EQ(w.partA.status, PartAStatus::DONE);
  EXPECT_EQ(w.partA.runs[0].readable, Answer::YES);
  EXPECT_EQ(w.partA.runs[1].readable, Answer::YES);
  EXPECT_EQ(w.partA.slower, Answer::YES);
  EXPECT_EQ(w.partADone(), Walk::STEP);
  EXPECT_EQ(w.resume(), Walk::STEP);
  EXPECT_EQ(w.stepIndex(), 0);
}

TEST(Walkthrough, RepeatRunsTheSameRunAgain) {
  Walkthrough w;
  w.reset();
  w.start(StartChoice::START);
  w.runDone(goodRun(ACCEPTANCE_RUNS[0], 1250));
  EXPECT_EQ(w.readable(ReadableChoice::REPEAT), Walk::RUN);
  EXPECT_EQ(w.runIndex(), 0);
  w.runDone(goodRun(ACCEPTANCE_RUNS[0], 1240));
  EXPECT_EQ(w.partA.runs[0].attempts, 2);
  EXPECT_EQ(w.partA.runs[0].figures.playback.deliveredWpmTenths, 1240u);
  w.readable(ReadableChoice::NO);
  w.runDone(goodRun(ACCEPTANCE_RUNS[1], 1300));
  w.readable(ReadableChoice::YES);
  // Repeat on the slowdown question plays the 600 run again and asks both of its questions again.
  EXPECT_EQ(w.slower(SlowerChoice::REPEAT), Walk::RUN);
  EXPECT_EQ(w.runIndex(), 1);
  EXPECT_EQ(w.partA.runs[1].readable, Answer::NONE);
  EXPECT_EQ(w.partA.runs[0].readable, Answer::NO);
}

TEST(Walkthrough, AStoppedRunIsRepeatedOrPartAIsSkipped) {
  Walkthrough w;
  w.reset();
  w.start(StartChoice::START);
  StepFigures partial = goodRun(ACCEPTANCE_RUNS[0], 1200);
  partial.playback.wordsShown = 40;
  w.runStopped(partial);
  EXPECT_EQ(w.stopped(RunStopChoice::REPEAT), Walk::RUN);
  EXPECT_EQ(w.partA.runs[0].state, RunState::PENDING);
  EXPECT_EQ(w.partA.runs[0].attempts, 1);
  w.runStopped(partial);
  EXPECT_EQ(w.stopped(RunStopChoice::SKIP_PART_A), Walk::PART_A_DONE);
  EXPECT_EQ(w.partA.status, PartAStatus::SKIPPED);
  EXPECT_EQ(w.partA.runs[0].state, RunState::STOPPED);
  EXPECT_TRUE(w.partA.runs[0].hasFigures);
  EXPECT_EQ(w.partA.runs[0].figures.playback.wordsShown, 40u);
  EXPECT_EQ(w.partA.runs[1].state, RunState::PENDING);
  const Criterion5 c = w.criterion5();
  EXPECT_EQ(c.measured, StepResult::SKIPPED);
  EXPECT_EQ(c.owner, StepResult::SKIPPED);
  EXPECT_EQ(w.resume(), Walk::STEP);
}

TEST(Walkthrough, StartingPartAAgainClearsItsAnswersButKeepsTheAttempts) {
  Walkthrough w;
  w.reset();
  w.start(StartChoice::START);
  w.runDone(goodRun(ACCEPTANCE_RUNS[0], 1250));
  w.readable(ReadableChoice::YES);
  // The device slept during the 600 run: the attempt counts, Part A is still running.
  w.abandonRun(StepFigures{});
  EXPECT_EQ(w.resume(), Walk::START);
  EXPECT_EQ(w.start(StartChoice::START), Walk::RUN);
  EXPECT_EQ(w.runIndex(), 0);
  EXPECT_EQ(w.partA.runs[0].readable, Answer::NONE);
  EXPECT_EQ(w.partA.runs[0].state, RunState::PENDING);
  EXPECT_EQ(w.partA.runs[0].attempts, 1);
  EXPECT_EQ(w.partA.runs[1].attempts, 1);
}

TEST(Walkthrough, PartBResumesAtTheStepInProgressAndAsksForItsResult) {
  Walkthrough w;
  w.reset();
  finishPartAWell(w);
  EXPECT_EQ(w.step(StepChoice::LEAVE), Walk::LEAVE);
  EXPECT_TRUE(w.partB.away);
  // Back from the reader: the question for the same step.
  EXPECT_EQ(w.resume(), Walk::ASK_RESULT);
  EXPECT_EQ(w.stepIndex(), 0);
  // "Not done yet" goes back to the instructions and commits nothing.
  EXPECT_EQ(w.result(ResultChoice::NOT_DONE), Walk::STEP);
  EXPECT_FALSE(w.partB.away);
  EXPECT_EQ(w.partB.steps[0].result, StepResult::NOT_DONE);
  EXPECT_EQ(w.partB.steps[0].answers, 0);
  EXPECT_EQ(w.resume(), Walk::STEP);
  EXPECT_EQ(w.step(StepChoice::DONE), Walk::ASK_RESULT);
  EXPECT_EQ(w.result(ResultChoice::PASS), Walk::STEP);
  EXPECT_EQ(w.stepIndex(), 1);
  EXPECT_EQ(w.partB.steps[0].result, StepResult::PASS);
  EXPECT_EQ(w.step(StepChoice::MENU), Walk::MENU);
  EXPECT_FALSE(w.partB.away);
}

TEST(Walkthrough, AFailNeedsAReasonAndBackCommitsNothing) {
  Walkthrough w;
  w.reset();
  finishPartAWell(w);
  EXPECT_EQ(w.result(ResultChoice::FAIL), Walk::ASK_REASON);
  EXPECT_EQ(w.partB.steps[0].result, StepResult::NOT_DONE);
  EXPECT_EQ(w.reason(0), Walk::ASK_RESULT);
  EXPECT_EQ(w.partB.steps[0].result, StepResult::NOT_DONE);
  EXPECT_EQ(w.stepIndex(), 0);
  for (uint8_t row = 1; row < REASON_CHOICES; ++row) {
    ASSERT_EQ(w.stepIndex(), row - 1);
    w.result(ResultChoice::FAIL);
    EXPECT_EQ(w.reason(row), Walk::STEP);
    EXPECT_EQ(w.partB.steps[row - 1].result, StepResult::FAIL);
    EXPECT_EQ(w.partB.steps[row - 1].reason, static_cast<FailReason>(row));
    EXPECT_EQ(w.partB.steps[row - 1].answers, 1);
  }
  EXPECT_STREQ(failReasonName(FailReason::DID_NOT_APPEAR), "did-not-appear");
  EXPECT_STREQ(failReasonName(FailReason::OTHER), "other");
}

TEST(Walkthrough, TheLastAnswerCompletesItAndShowsTheSummary) {
  Walkthrough w;
  w.reset();
  w.start(StartChoice::SKIP_PART_A);
  EXPECT_EQ(w.partADone(), Walk::STEP);
  for (size_t i = 0; i + 1 < WALK_STEP_COUNT; ++i) EXPECT_EQ(w.result(ResultChoice::SKIP), Walk::STEP);
  EXPECT_FALSE(w.complete());
  EXPECT_EQ(w.result(ResultChoice::PASS), Walk::SUMMARY);
  EXPECT_TRUE(w.complete());
  EXPECT_EQ(w.status(), RunStatus::COMPLETE);
  EXPECT_EQ(w.resume(), Walk::SUMMARY);
  EXPECT_EQ(w.answered(), WALK_STEP_COUNT);
  // Answers after the end change nothing.
  EXPECT_EQ(w.result(ResultChoice::FAIL), Walk::SUMMARY);
  EXPECT_EQ(w.reason(1), Walk::SUMMARY);
}

TEST(Criterion5, MeasuredPassNeedsEveryWordOnceInOrderAndNoFasterThanTheTarget) {
  RunRecord r;
  r.state = RunState::DONE;
  r.hasFigures = true;
  r.figures = goodRun(ACCEPTANCE_RUNS[1], 1312);
  EXPECT_TRUE(runMeasuredOk(ACCEPTANCE_RUNS[1], r));
  // Exactly the target is allowed; above it is not.
  r.figures.playback.deliveredWpmTenths = 6000;
  EXPECT_TRUE(runMeasuredOk(ACCEPTANCE_RUNS[1], r));
  r.figures.playback.deliveredWpmTenths = 6001;
  EXPECT_FALSE(runMeasuredOk(ACCEPTANCE_RUNS[1], r));
  r.figures.playback.deliveredWpmTenths = 1312;
  for (int fault = 0; fault < 6; ++fault) {
    RunRecord b = r;
    PlaybackFigures& p = b.figures.playback;
    switch (fault) {
      case 0:
        p.inOrder = false;
        break;
      case 1:
        p.wordsDrawn = 149;
        break;
      case 2:
        p.gaps = 1;
        break;
      case 3:
        p.repeats = 1;
        break;
      case 4:
        p.violations = 1;
        break;
      case 5:
        p.wordsShown = 151;
        break;
    }
    EXPECT_FALSE(runMeasuredOk(ACCEPTANCE_RUNS[1], b)) << "fault " << fault;
  }
  r.state = RunState::STOPPED;
  EXPECT_FALSE(runMeasuredOk(ACCEPTANCE_RUNS[1], r));
}

TEST(Criterion5, TheAutomaticVerdictAndTheOwnersAnswersAreKeptApart) {
  {
    Walkthrough w;
    w.reset();
    EXPECT_EQ(w.criterion5().measured, StepResult::NOT_DONE);
    EXPECT_EQ(w.criterion5().owner, StepResult::NOT_DONE);
    finishPartAWell(w);
    EXPECT_EQ(w.criterion5().measured, StepResult::PASS);
    EXPECT_EQ(w.criterion5().owner, StepResult::PASS);
    EXPECT_EQ(w.criterion5().pauseScreen, StepResult::NOT_DONE);
    EXPECT_EQ(w.criterion(5), StepResult::NOT_DONE);
  }
  {
    // Measured fine, but the owner could not read the last sentence: the owner's verdict fails alone.
    Walkthrough w;
    w.reset();
    finishPartA(w, goodRun(ACCEPTANCE_RUNS[0], 1250), goodRun(ACCEPTANCE_RUNS[1], 1310), ReadableChoice::NO,
                SlowerChoice::YES);
    EXPECT_EQ(w.criterion5().measured, StepResult::PASS);
    EXPECT_EQ(w.criterion5().owner, StepResult::FAIL);
    EXPECT_EQ(w.criterion(5), StepResult::FAIL);
  }
  {
    // A dropped word: the measured verdict fails, the owner's answers pass.
    Walkthrough w;
    w.reset();
    StepFigures dropped = goodRun(ACCEPTANCE_RUNS[1], 1310);
    dropped.playback.gaps = 1;
    dropped.playback.wordsDrawn = 149;
    finishPartA(w, goodRun(ACCEPTANCE_RUNS[0], 1250), dropped, ReadableChoice::YES, SlowerChoice::YES);
    EXPECT_EQ(w.criterion5().measured, StepResult::FAIL);
    EXPECT_EQ(w.criterion5().owner, StepResult::PASS);
  }
  {
    Walkthrough w;
    w.reset();
    finishPartAWell(w);
    for (size_t i = 0; i < WALK_STEP_COUNT; ++i) w.result(ResultChoice::PASS);
    EXPECT_EQ(w.criterion5().pauseScreen, StepResult::PASS);
    for (unsigned k = 1; k <= CRITERION_COUNT; ++k) EXPECT_EQ(w.criterion(k), StepResult::PASS) << k;
  }
}

TEST(Criteria, AnyFailFailsThenNotDoneThenSkipped) {
  EXPECT_EQ(combine(StepResult::PASS, StepResult::PASS), StepResult::PASS);
  EXPECT_EQ(combine(StepResult::PASS, StepResult::SKIPPED), StepResult::SKIPPED);
  EXPECT_EQ(combine(StepResult::SKIPPED, StepResult::NOT_DONE), StepResult::NOT_DONE);
  EXPECT_EQ(combine(StepResult::NOT_DONE, StepResult::FAIL), StepResult::FAIL);

  Walkthrough w;
  w.reset();
  w.start(StartChoice::SKIP_PART_A);
  for (unsigned k = 1; k <= 4; ++k) EXPECT_EQ(w.criterion(k), StepResult::NOT_DONE);
  EXPECT_EQ(w.criterion(5), StepResult::NOT_DONE);  // the pause screen step is not done
  // Step 1 (criteria 1 and 3) fails: criterion 1 fails even though later steps pass.
  w.result(ResultChoice::FAIL);
  w.reason(static_cast<uint8_t>(FailReason::WRONG_PLACE));
  for (size_t i = 1; i < WALK_STEP_COUNT; ++i) w.result(ResultChoice::PASS);
  EXPECT_EQ(w.criterion(1), StepResult::FAIL);
  EXPECT_EQ(w.criterion(2), StepResult::PASS);
  EXPECT_EQ(w.criterion(3), StepResult::FAIL);
  EXPECT_EQ(w.criterion(4), StepResult::PASS);
  EXPECT_EQ(w.criterion(5), StepResult::SKIPPED);  // Part A skipped, the pause screen passed
}

TEST(AcceptanceJson, ThePartWayFileRecordsTheRunsTheAnswersAndTheVerdicts) {
  const std::string json = devicetest_samples::acceptancePartWayJson();
  for (const char* expected : {"\"format\":\"rsvp-bench\"",
                               "\"version\":2",
                               "\"kind\":\"acceptance\"",
                               "\"file\":\"0005-acceptance.json\"",
                               "\"status\":\"running\"",
                               "\"profile\":{\"key\":\"trial-b\",\"name\":\"Trial B\"",
                               "\"run\":\"acceptance-240-1\",\"target_wpm\":240",
                               "\"run\":\"acceptance-600-1\",\"target_wpm\":600",
                               "\"attempts\":2",
                               "\"words_drawn\":150",
                               "\"measured_ok\":true",
                               "\"readable\":\"yes\"",
                               "\"slower\":\"yes\"",
                               "\"book\":\"fixture.prepared.epub\"",
                               "\"current\":\"c0002-rsvp-keys\"",
                               "\"away\":true",
                               "\"answered\":3",
                               "\"result\":\"fail\",\"reason\":\"wrong-mode\"",
                               "\"depends_on\":[\"C2\",\"C3\"]",
                               "\"depends_on\":[\"C2\",\"C3\",\"C4\",\"C5\"]",
                               "\"depends_on\":[]",
                               "\"criterion\":5,\"result\":\"not-done\",\"evidence\":[\"part-a\",\"pause-screen-600\"]",
                               "\"measured_pass\":true,\"owner_pass\":true"}) {
    EXPECT_NE(json.find(expected), std::string::npos) << expected;
  }
}

TEST(AcceptanceJson, ACompleteFileSaysComplete) {
  const std::string json = devicetest_samples::acceptanceCompleteJson();
  EXPECT_NE(json.find("\"status\":\"complete\""), std::string::npos);
  EXPECT_NE(json.find("\"current\":null"), std::string::npos);
  EXPECT_NE(json.find("\"panel\":\"SE0400NQW47\""), std::string::npos);
  EXPECT_NE(json.find("\"criterion\":1,\"result\":\"pass\""), std::string::npos);
}
