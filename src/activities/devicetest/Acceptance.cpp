#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include "Acceptance.h"

namespace devicetest {

// The steps, written against fixture.prepared.epub (fixtures/books/README.md): checkpoint c0001 (one
// question) follows section s0001 in chapter 1 "The Lamp Room", after the words "no other light on
// the coast did the same."; c0002 (three questions) follows section s0004 "The Visitor from the
// Mainland", the end of chapter 2 "The Fog Season", after "to put out the lamp.". fixture.epub is the
// same book without checkpoints; it has an image in chapter 1, a table in chapter 2 and a code block
// in chapter 3. Navigation by touch: the middle of a page opens the toolbar, its More button (three
// dots) opens the list with Speed read and Select Chapter. Key paths: S4 rows 17 to 29.
const WalkStep WALK_STEPS[WALK_STEP_COUNT] = {
    {"c0001-rsvp-touch", criterionBit(1) | criterionBit(3), "PRD 1: single-question checkpoint fires in RSVP, by touch",
     "C2, C3", "Short checkpoint in RSVP, by touch",
     "Use touch only. Open Browse Files, then books, then fixture.prepared.epub. Tap the middle of the page. "
     "Tap More (three dots), then Select Chapter, then The Lamp Room. Tap the middle of the page. Tap More, "
     "then Speed read. Tap Play. At the picture a banner appears: tap Continue speed reading, then Play.",
     "Playback stops after \"no other light on the coast did the same.\" The checkpoint shows question 1 of 1. "
     "Tap Reveal answer, then Correct. RSVP comes back after the break, with Play highlighted. Nothing plays "
     "by itself."},
    {"c0001-page-keys", criterionBit(1), "PRD 1: single-question checkpoint fires in page view, by keys", "C2, C3",
     "Short checkpoint in page view, by keys",
     "Tap Page view. Tap the middle of the page, More, Select Chapter, The Lamp Room. From here use the side "
     "keys only. Press the next-page key to turn pages. Stop on the page that shows \"did the same.\" Press "
     "the next-page key once more.",
     "The checkpoint shows question 1 of 1 and \"Recorded earlier: Correct\". Press Down twice, then hold Down: "
     "Skip checkpoint. Page view shows the page after the break. Nothing else happens."},
    {"toc-jump", criterionBit(1), "PRD 1: a jump past a section end does not fire its checkpoint", "C2",
     "A jump does not fire a checkpoint",
     "Use touch. Open Select Chapter and choose The Fog Season. Open Select Chapter again and choose The "
     "Keeper's Log. Open Select Chapter again and choose The Lamp Room.",
     "No checkpoint appears after any of the three jumps."},
    {"c0002-rsvp-keys", criterionBit(1) | criterionBit(2),
     "PRD 1 and 2: multi-question checkpoint fires in RSVP, by keys; question 2 marked Incorrect", "C2, C3",
     "Long checkpoint in RSVP, by keys",
     "Open Select Chapter and choose The Fog Season. Turn pages to the last page of the chapter. It ends with "
     "\"to put out the lamp.\" From here use the side keys only. Hold Down: Speed read opens. Hold Down: Play.",
     "Playback stops after \"to put out the lamp.\" The checkpoint shows question 1 of 3. Hold Down: Reveal "
     "answer. Press Down, then hold: Correct. Question 2 of 3 shows. Hold Down: Reveal answer. Press Down "
     "twice, then hold: Incorrect. The screen says \"marked Incorrect\". Reread in RSVP is highlighted."},
    {"reread-rsvp-end", criterionBit(2), "PRD 2: reread in RSVP to the end of the section, back to the question", "C4",
     "Reread in RSVP to the end",
     "Hold Down: Reread in RSVP. Hold Down: Play. Let it play to the end of the section. You may raise the WPM "
     "first to make it shorter.",
     "The top line starts with \"Rereading\". Under Exit, Menu and Page view is a wide row \"Back to the "
     "question\". Play is highlighted. Near the bottom: \"The Visitor from the Mainland On the\". After "
     "\"to put out the lamp.\" question 2 of 3 shows again, answer hidden, \"Recorded earlier: Incorrect\". "
     "Hold Down: Reveal answer. Press Down twice, then hold: Incorrect."},
    {"reread-page-early", criterionBit(2),
     "PRD 2: reread in page view, return early, retry with the answer hidden, then Correct", "C4",
     "Reread in page view, return early, retry",
     "Use touch. Tap Reread in page view. Swipe to turn one page. At the bottom of the screen, where the "
     "battery and page number usually are, a strip shows \"Rereading\" and a small framed button \"Back to "
     "the question\". Tap the button. It is small: if it is hard to hit, write that in the notes and hold a "
     "side key instead, then hold again on \"Back to the question\".",
     "Page view first shows the page with the heading \"The Visitor from the Mainland\". The strip covers no "
     "line of text. After the tap or the hold, question 2 of 3 shows again, with the answer hidden and "
     "\"Recorded earlier: Incorrect\". Tap Reveal answer, then Correct. Question 3 of 3 shows."},
    {"skip-question-resume", criterionBit(3),
     "PRD 3: skip a question; reading resumes after the break in the original mode, without playing", "C3",
     "Skip a question, reading resumes", "On question 3 of 3, tap Skip question.",
     "The checkpoint ends. RSVP shows its pause screen at the start of The Keeper's Log. Play is highlighted. "
     "Nothing plays by itself."},
    {"later-pass-grades", criterionBit(1) | criterionBit(2) | criterionBit(3) | criterionBit(4),
     "PRD 1 to 4: multi-question checkpoint fires in page view, by touch; first grades kept; skip, leave part-way",
     "C2, C3", "Read it again: the first grades are kept",
     "Use touch. Tap Page view. Open Select Chapter and choose The Fog Season. Turn to the last page of the "
     "chapter. Swipe to the next page.",
     "Question 1 of 3 shows \"Recorded earlier: Correct\". Tap Skip question. Question 2 of 3 shows \"Recorded "
     "earlier: Incorrect\", not Correct. Tap Skip checkpoint. Page view shows the start of The Keeper's Log."},
    {"close-in-question", criterionBit(4), "PRD 4: close the book during a question; reopening restores it",
     "C2, C3, C5", "Close the book during a question",
     "Open Select Chapter and choose The Lamp Room. Turn pages until question 1 of 1 shows. Swipe up from the "
     "bottom edge: Home. Open fixture.prepared.epub again.",
     "The book opens directly at question 1 of 1, with the answer hidden and \"Recorded earlier: Correct\". "
     "No page of the book shows first. Tap Skip checkpoint. Page view shows the page with \"did the same.\" "
     "and the text after it. No question shows."},
    {"sleep-in-reread", criterionBit(4),
     "PRD 4: sleep during a reread; waking restores the reread and the way back to the question", "C2, C3, C4, C5",
     "Sleep during a reread",
     "Open Select Chapter and choose The Lamp Room. Turn pages until question 1 of 1 shows. Tap Reveal answer, "
     "then Incorrect, then Reread in page view. Turn one page. Hold the power button until the device sleeps. "
     "Wake it. If the book is not open, open fixture.prepared.epub.",
     "Page view is in the reread, on the same page. At the bottom, the strip shows \"Rereading\" and the "
     "\"Back to the question\" button. Tap the button (or hold a side key, then hold again). Question 1 of 1 "
     "shows, answer hidden, \"Recorded earlier: Correct\". Tap Skip checkpoint. Page view shows the page with "
     "\"did the same.\" again. No question shows."},
    {"ordinary-book-items", 0,
     "Plan section 1: RSVP on an ordinary EPUB; an image, a table and a code block pause it with a banner", "",
     "An ordinary book: pictures, tables, code",
     "Go Home. Open Browse Files, books, fixture.epub (the copy without checkpoints). Choose The Lamp Room. Open "
     "Speed read. Tap Play. At the picture tap Continue speed reading, then Play. Choose The Fog Season, open "
     "Speed read, Play. Choose The Keeper's Log, open Speed read, Play.",
     "RSVP stops before the picture, the table and the code block. Each time a banner says \"... Speed reading "
     "stopped here.\" with Continue speed reading and Stay in page view. No checkpoint appears in this book."},
    {"pause-screen-600", criterionBit(5), "PRD 5: the real pause screen shows the delivered WPM below a 600 target", "",
     "The pause screen shows the actual pace",
     "Stay in fixture.epub. Open Speed read. Tap + next to the WPM until it shows 600. Tap Play. After about 20 "
     "seconds, touch the screen to pause.",
     "The top line reads \"Paused · 600 WPM target · N actual\". N is below 600. The words came one at a time, "
     "slower than 600 WPM."},
};

static_assert(sizeof(WALK_STEPS) / sizeof(WALK_STEPS[0]) == WALK_STEP_COUNT, "one entry per step");

const char* answerName(const Answer answer) {
  switch (answer) {
    case Answer::NONE:
      return "none";
    case Answer::YES:
      return "yes";
    case Answer::NO:
      return "no";
  }
  return "?";
}

const char* runStateName(const RunState state) {
  switch (state) {
    case RunState::PENDING:
      return "pending";
    case RunState::DONE:
      return "done";
    case RunState::STOPPED:
      return "stopped";
  }
  return "?";
}

const char* partAStatusName(const PartAStatus status) {
  switch (status) {
    case PartAStatus::PENDING:
      return "pending";
    case PartAStatus::RUNNING:
      return "running";
    case PartAStatus::DONE:
      return "done";
    case PartAStatus::SKIPPED:
      return "skipped";
  }
  return "?";
}

const char* stepResultName(const StepResult result) {
  switch (result) {
    case StepResult::NOT_DONE:
      return "not-done";
    case StepResult::PASS:
      return "pass";
    case StepResult::FAIL:
      return "fail";
    case StepResult::SKIPPED:
      return "skipped";
  }
  return "?";
}

const char* failReasonName(const FailReason reason) {
  switch (reason) {
    case FailReason::NONE:
      return "none";
    case FailReason::DID_NOT_APPEAR:
      return "did-not-appear";
    case FailReason::WRONG_PLACE:
      return "wrong-place";
    case FailReason::WRONG_MODE:
      return "wrong-mode";
    case FailReason::GRADE_WRONG:
      return "grade-wrong";
    case FailReason::OTHER:
      return "other";
  }
  return "?";
}

StepResult combine(const StepResult a, const StepResult b) {
  if (a == StepResult::FAIL || b == StepResult::FAIL) return StepResult::FAIL;
  if (a == StepResult::NOT_DONE || b == StepResult::NOT_DONE) return StepResult::NOT_DONE;
  if (a == StepResult::SKIPPED || b == StepResult::SKIPPED) return StepResult::SKIPPED;
  return StepResult::PASS;
}

bool runMeasuredOk(const StepSpec& spec, const RunRecord& run) {
  if (run.state != RunState::DONE || !run.hasFigures || !run.figures.hasPlayback) return false;
  const PlaybackFigures& p = run.figures.playback;
  return p.inOrder && p.wordsDrawn == spec.count && p.wordsShown == spec.count && p.violations == 0 && p.gaps == 0 &&
         p.repeats == 0 && p.deliveredWpmTenths > 0 && p.deliveredWpmTenths <= static_cast<uint32_t>(spec.wpm) * 10u;
}

// ---------------------------------------------------------------------------------------------------

void Walkthrough::reset() {
  partA = PartARecord{};
  partB = PartBRecord{};
  run = 0;
  haveStoppedFigures = false;
}

Walk Walkthrough::resume() const {
  if (complete()) return Walk::SUMMARY;
  if (partA.status != PartAStatus::DONE && partA.status != PartAStatus::SKIPPED) return Walk::START;
  return partB.away ? Walk::ASK_RESULT : Walk::STEP;
}

Walk Walkthrough::start(const StartChoice choice) {
  switch (choice) {
    case StartChoice::START:
      // Part A always starts from its first run: the owner must see a run to answer for it.
      partA.status = PartAStatus::RUNNING;
      for (RunRecord& r : partA.runs) {
        r.state = RunState::PENDING;
        r.readable = Answer::NONE;
        r.hasFigures = false;
        r.figures = StepFigures{};
      }
      partA.slower = Answer::NONE;
      run = 0;
      haveStoppedFigures = false;
      return Walk::RUN;
    case StartChoice::SKIP_PART_A:
      partA.status = PartAStatus::SKIPPED;
      return Walk::PART_A_DONE;
    case StartChoice::BACK:
      break;
  }
  return Walk::MENU;
}

Walk Walkthrough::runDone(const StepFigures& figures) {
  RunRecord& r = partA.runs[run];
  ++r.attempts;
  r.state = RunState::DONE;
  r.figures = figures;
  r.hasFigures = true;
  r.readable = Answer::NONE;
  haveStoppedFigures = false;
  return Walk::ASK_READABLE;
}

void Walkthrough::runStopped(const StepFigures& figures) {
  ++partA.runs[run].attempts;
  stoppedFigures = figures;
  haveStoppedFigures = true;
}

Walk Walkthrough::stopped(const RunStopChoice choice) {
  if (choice == RunStopChoice::REPEAT) {
    haveStoppedFigures = false;
    return Walk::RUN;
  }
  RunRecord& r = partA.runs[run];
  r.state = RunState::STOPPED;
  if (haveStoppedFigures) {
    r.figures = stoppedFigures;
    r.hasFigures = true;
  }
  haveStoppedFigures = false;
  partA.status = PartAStatus::SKIPPED;
  return Walk::PART_A_DONE;
}

Walk Walkthrough::readable(const ReadableChoice choice) {
  if (choice == ReadableChoice::REPEAT) return Walk::RUN;
  partA.runs[run].readable = choice == ReadableChoice::YES ? Answer::YES : Answer::NO;
  if (run + 1u < ACCEPTANCE_RUN_COUNT) {
    ++run;
    return Walk::RUN;
  }
  return Walk::ASK_SLOWER;
}

Walk Walkthrough::slower(const SlowerChoice choice) {
  if (choice == SlowerChoice::REPEAT) {
    partA.runs[run].readable = Answer::NONE;  // the run is played and answered again
    return Walk::RUN;
  }
  partA.slower = choice == SlowerChoice::YES ? Answer::YES : Answer::NO;
  partA.status = PartAStatus::DONE;
  return Walk::PART_A_DONE;
}

Walk Walkthrough::partADone() const { return afterPartA(); }

Walk Walkthrough::afterPartA() const {
  if (partB.current >= WALK_STEP_COUNT) return Walk::SUMMARY;
  return partB.away ? Walk::ASK_RESULT : Walk::STEP;
}

Walk Walkthrough::step(const StepChoice choice) {
  switch (choice) {
    case StepChoice::LEAVE:
      partB.away = true;
      return Walk::LEAVE;
    case StepChoice::DONE:
      return Walk::ASK_RESULT;
    case StepChoice::MENU:
      break;
  }
  return Walk::MENU;
}

Walk Walkthrough::result(const ResultChoice choice) {
  if (partB.current >= WALK_STEP_COUNT) return Walk::SUMMARY;
  StepAnswer& a = partB.steps[partB.current];
  switch (choice) {
    case ResultChoice::NOT_DONE:
      partB.away = false;
      return Walk::STEP;
    case ResultChoice::PASS:
      a.result = StepResult::PASS;
      a.reason = FailReason::NONE;
      ++a.answers;
      return advanceStep();
    case ResultChoice::FAIL:
      return Walk::ASK_REASON;
    case ResultChoice::SKIP:
      a.result = StepResult::SKIPPED;
      a.reason = FailReason::NONE;
      ++a.answers;
      return advanceStep();
  }
  return Walk::STEP;
}

Walk Walkthrough::reason(const uint8_t row) {
  if (partB.current >= WALK_STEP_COUNT) return Walk::SUMMARY;
  if (row == 0 || row >= REASON_CHOICES) return Walk::ASK_RESULT;
  StepAnswer& a = partB.steps[partB.current];
  a.result = StepResult::FAIL;
  a.reason = static_cast<FailReason>(row);  // rows 1.. follow FailReason after NONE
  ++a.answers;
  return advanceStep();
}

Walk Walkthrough::advanceStep() {
  partB.away = false;
  if (partB.current < WALK_STEP_COUNT) ++partB.current;
  return partB.current >= WALK_STEP_COUNT ? Walk::SUMMARY : Walk::STEP;
}

void Walkthrough::abandonRun(const StepFigures& figures) {
  (void)figures;
  ++partA.runs[run].attempts;
  haveStoppedFigures = false;
}

bool Walkthrough::complete() const {
  return (partA.status == PartAStatus::DONE || partA.status == PartAStatus::SKIPPED) &&
         partB.current >= WALK_STEP_COUNT;
}

uint8_t Walkthrough::answered() const {
  uint8_t n = 0;
  for (const StepAnswer& a : partB.steps) n = static_cast<uint8_t>(n + (a.result != StepResult::NOT_DONE ? 1 : 0));
  return n;
}

Criterion5 Walkthrough::criterion5() const {
  Criterion5 c;
  if (partA.status == PartAStatus::SKIPPED) {
    c.measured = StepResult::SKIPPED;
    c.owner = StepResult::SKIPPED;
  } else {
    bool allRan = true;
    bool allOk = true;
    bool allAnswered = partA.slower != Answer::NONE;
    bool allYes = partA.slower == Answer::YES;
    for (size_t i = 0; i < ACCEPTANCE_RUN_COUNT; ++i) {
      const RunRecord& r = partA.runs[i];
      allRan = allRan && r.state == RunState::DONE;
      allOk = allOk && runMeasuredOk(ACCEPTANCE_RUNS[i], r);
      allAnswered = allAnswered && r.readable != Answer::NONE;
      allYes = allYes && r.readable == Answer::YES;
    }
    if (allRan) c.measured = allOk ? StepResult::PASS : StepResult::FAIL;
    if (partA.status == PartAStatus::DONE && allAnswered) c.owner = allYes ? StepResult::PASS : StepResult::FAIL;
  }
  for (size_t i = 0; i < WALK_STEP_COUNT; ++i) {
    if (WALK_STEPS[i].criteria & criterionBit(5)) c.pauseScreen = partB.steps[i].result;
  }
  return c;
}

StepResult Walkthrough::criterion(const unsigned k) const {
  bool any = false;
  StepResult r = StepResult::PASS;
  if (k == 5) {
    const Criterion5 c = criterion5();
    r = combine(combine(c.measured, c.owner), c.pauseScreen);
    any = true;
  } else {
    for (size_t i = 0; i < WALK_STEP_COUNT; ++i) {
      if ((WALK_STEPS[i].criteria & criterionBit(k)) == 0) continue;
      r = any ? combine(r, partB.steps[i].result) : partB.steps[i].result;
      any = true;
    }
  }
  return any ? r : StepResult::NOT_DONE;
}

}  // namespace devicetest

#endif  // CROSSPOINT_RSVP_ENABLED
