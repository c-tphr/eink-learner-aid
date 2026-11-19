#pragma once

// The guided acceptance walk-through (task D3b; docs/PLAN.md section 10, "The PRD's on-device
// criterion", session step 10; docs/PRD.md, "v1 acceptance criteria"). Portable: no Arduino, HAL or
// renderer; host-tested in test/device_test. DeviceTestActivity runs Part A's two playback runs and
// draws the screens; this file holds the step table, what each answer leads to, the record that goes
// into the results file and the verdicts.
//
// Part A (criterion 5): the fixed passage on the chosen display profile at 240 WPM and at a 600 WPM
// target, one word per update, measured as the benchmark measures it; then the owner's answers.
// Part B (criteria 1 to 4, and the pause screen of criterion 5): numbered steps the owner does in the
// reader, answered Pass, Fail (with a reason) or Skip when they come back. README.md in this
// directory has the step table as the owner reads it.

#include <cstddef>
#include <cstdint>

#include "BenchPlan.h"

namespace devicetest {

// --- Part A ---------------------------------------------------------------------------------------

inline constexpr uint16_t ACCEPTANCE_DEFAULT_WPM = 240;
inline constexpr uint16_t ACCEPTANCE_FAST_WPM = 600;
inline constexpr StepSpec ACCEPTANCE_RUNS[] = {
    {StepKind::PLAYBACK, "acceptance-240-1", 150, ACCEPTANCE_DEFAULT_WPM, 1, true},
    {StepKind::PLAYBACK, "acceptance-600-1", 150, ACCEPTANCE_FAST_WPM, 1, true},
};
inline constexpr size_t ACCEPTANCE_RUN_COUNT = sizeof(ACCEPTANCE_RUNS) / sizeof(ACCEPTANCE_RUNS[0]);

enum class Answer : uint8_t { NONE, YES, NO };
const char* answerName(Answer answer);  // "none", "yes", "no"

enum class RunState : uint8_t {
  PENDING,  // not run to the end yet (a stopped attempt is repeated or Part A is skipped)
  DONE,     // ran to the end; `figures` are its last attempt's
  STOPPED,  // the owner stopped it and then skipped Part A; `figures` are partial
};
const char* runStateName(RunState state);  // "pending", "done", "stopped"

struct RunRecord {
  RunState state = RunState::PENDING;
  uint8_t attempts = 0;
  bool hasFigures = false;
  StepFigures figures;
  Answer readable = Answer::NONE;  // "Was the last sentence readable?"
};

enum class PartAStatus : uint8_t { PENDING, RUNNING, DONE, SKIPPED };
const char* partAStatusName(PartAStatus status);  // "pending", "running", "done", "skipped"

struct PartARecord {
  PartAStatus status = PartAStatus::PENDING;
  RunRecord runs[ACCEPTANCE_RUN_COUNT];
  Answer slower = Answer::NONE;  // after the 600 WPM run: slower than the target, no word skipped
};

// The prompts' rows, in order; row 0 is the default and commits nothing.
enum class StartChoice : uint8_t { START, SKIP_PART_A, BACK };
inline constexpr uint8_t START_CHOICES = 3;
enum class ReadableChoice : uint8_t { REPEAT, YES, NO };
inline constexpr uint8_t READABLE_CHOICES = 3;
enum class SlowerChoice : uint8_t { REPEAT, YES, NO };
inline constexpr uint8_t SLOWER_CHOICES = 3;
enum class RunStopChoice : uint8_t { REPEAT, SKIP_PART_A };
inline constexpr uint8_t RUN_STOP_CHOICES = 2;

// --- Part B ---------------------------------------------------------------------------------------

// One PRD criterion per bit: bit k-1 is criterion k.
using CriteriaMask = uint8_t;
constexpr CriteriaMask criterionBit(const unsigned k) { return static_cast<CriteriaMask>(1u << (k - 1u)); }
inline constexpr unsigned CRITERION_COUNT = 5;  // criterion 6 (the preparation workflow) is not on the device

// One step as the owner reads it. Every text is in this one table, so a later task can adjust the
// wording without touching the logic (README.md, "The steps").
struct WalkStep {
  const char* id;            // stable id in the results file and the log
  CriteriaMask criteria;     // the PRD criteria the step gives evidence for (0: a plan decision only)
  const char* trace;         // what it proves, in a few words, for the results file
  const char* dependsOn;     // tasks whose behaviour it describes and that are not on main yet ("" if none)
  const char* title;         // the subtitle of its screens
  const char* instructions;  // what to do
  const char* expected;      // what should happen; shown again with the Pass / Fail question
};
inline constexpr size_t WALK_STEP_COUNT = 12;
extern const WalkStep WALK_STEPS[WALK_STEP_COUNT];
// The book the steps are written against (the kit's sd-card/books/).
inline constexpr const char WALK_BOOK[] = "fixture.prepared.epub";

enum class StepResult : uint8_t { NOT_DONE, PASS, FAIL, SKIPPED };
const char* stepResultName(StepResult result);  // "not-done", "pass", "fail", "skipped"

enum class FailReason : uint8_t { NONE, DID_NOT_APPEAR, WRONG_PLACE, WRONG_MODE, GRADE_WRONG, OTHER };
inline constexpr uint8_t FAIL_REASON_COUNT = 5;  // the reasons offered (NONE is not one)
const char* failReasonName(FailReason reason);   // "none", "did-not-appear", "wrong-place", ...

struct StepAnswer {
  StepResult result = StepResult::NOT_DONE;
  FailReason reason = FailReason::NONE;
  uint8_t answers = 0;  // answers given for this step ("Not done yet" is not one)
};

// The step screens' rows.
enum class StepChoice : uint8_t { LEAVE, DONE, MENU };  // the instructions
inline constexpr uint8_t STEP_CHOICES = 3;
enum class ResultChoice : uint8_t { NOT_DONE, PASS, FAIL, SKIP };  // "How did it go?"
inline constexpr uint8_t RESULT_CHOICES = 4;
// The fail reasons' rows: BACK (the default), then the reasons in FailReason order.
inline constexpr uint8_t REASON_CHOICES = FAIL_REASON_COUNT + 1;
enum class SummaryRow : uint8_t { MENU, START_AGAIN };
inline constexpr uint8_t SUMMARY_CHOICES = 2;

// --- The record and its verdicts -------------------------------------------------------------------

struct PartBRecord {
  StepAnswer steps[WALK_STEP_COUNT];
  uint8_t current = 0;  // the step in progress: the first not answered; WALK_STEP_COUNT when all are
  bool away = false;    // the owner left the device test to do the current step
};

// What a criterion's evidence says: any Fail fails it; else anything not done leaves it Not done;
// else anything skipped leaves it Skipped; else Pass.
StepResult combine(StepResult a, StepResult b);

struct Criterion5 {
  // The automatic verdict: both runs ran to the end, every word index was drawn exactly once and in
  // order, and each was delivered at or below its target. NOT_DONE until both ran; SKIPPED when Part
  // A was skipped.
  StepResult measured = StepResult::NOT_DONE;
  // The owner's answers: both readable, and slower than the target with no word skipped.
  StepResult owner = StepResult::NOT_DONE;
  // The real RSVP pause screen at 600 WPM (Part B's step "pause-screen-600").
  StepResult pauseScreen = StepResult::NOT_DONE;
};

// The automatic check of one run's figures against its target.
bool runMeasuredOk(const StepSpec& spec, const RunRecord& run);

// --- The walk-through -------------------------------------------------------------------------------

// What the activity shows next.
enum class Walk : uint8_t {
  START,         // Part A's start screen: the chosen profile, Start / Skip part A / Back
  RUN,           // play run runIndex()
  ASK_READABLE,  // after a run (and after the owner has looked)
  ASK_SLOWER,    // after the 600 WPM run's readability answer
  RUN_STOPPED,   // the owner stopped a run
  PART_A_DONE,   // Part A's result
  STEP,          // Part B step stepIndex(): its instructions
  ASK_RESULT,    // "How did it go?" for the step
  ASK_REASON,    // the fail reasons
  LEAVE,         // the owner leaves the device test to do the step (the progress says so)
  SUMMARY,       // every criterion's result
  MENU,          // back to the device test menu
};

class Walkthrough {
 public:
  void reset();  // a new walk-through: nothing done

  // Where entering Guided acceptance goes: the summary once complete; the step's question if the
  // owner left to do it; its instructions if Part B has begun; Part A's start screen otherwise.
  Walk resume() const;

  // Part A.
  Walk start(StartChoice choice);
  uint8_t runIndex() const { return run; }
  const StepSpec& runSpec() const { return ACCEPTANCE_RUNS[run]; }
  Walk runDone(const StepFigures& figures);
  void runStopped(const StepFigures& figures);  // the stop prompt follows
  Walk stopped(RunStopChoice choice);
  Walk readable(ReadableChoice choice);
  Walk slower(SlowerChoice choice);
  Walk partADone() const;  // "Continue to part B"

  // Part B.
  uint8_t stepIndex() const { return partB.current; }
  Walk step(StepChoice choice);
  Walk result(ResultChoice choice);
  Walk reason(uint8_t row);  // a REASON_CHOICES row: 0 Back, 1.. the reasons

  // The run ends now (the screen is left or the device sleeps) while a run plays: the attempt counts.
  void abandonRun(const StepFigures& figures);

  bool complete() const;
  RunStatus status() const { return complete() ? RunStatus::COMPLETE : RunStatus::RUNNING; }
  Criterion5 criterion5() const;
  // The criterion's result over its evidence (k = 1..CRITERION_COUNT).
  StepResult criterion(unsigned k) const;
  // Steps answered (any result other than NOT_DONE).
  uint8_t answered() const;

  PartARecord partA;
  PartBRecord partB;

 private:
  Walk afterPartA() const;
  Walk advanceStep();

  uint8_t run = 0;
  bool haveStoppedFigures = false;
  StepFigures stoppedFigures;
};

}  // namespace devicetest
