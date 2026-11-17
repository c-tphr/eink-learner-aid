// The guided acceptance and the profile choice (task D3b): DeviceTestActivity's screens for the
// device session's steps 10 and 11 (docs/PLAN.md section 10; section 6, "Outcome"). The decisions are
// in Acceptance.h and ProfileChoice.h (portable, host-tested); this file draws, reads input answers,
// runs Part A's two playback runs through the same SeriesRunner as the benchmark, and writes the
// files. README.md in this directory, "Guided acceptance" and "Choose a profile".

#include "DeviceTestActivity.h"

#if CROSSPOINT_RSVP_ENABLED

#include <ArduinoJson.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <TrialUnlock.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "AcceptanceProgress.h"
#include "CrossPointSettings.h"
#include "ResultsFile.h"

using namespace devicetest;
using displayprofile::ProfileId;
using displayprofile::profileInfo;

namespace {

const char* resultText(const StepResult r) {
  switch (r) {
    case StepResult::PASS:
      return tr(STR_DT_PASS);
    case StepResult::FAIL:
      return tr(STR_DT_FAIL);
    case StepResult::SKIPPED:
      return tr(STR_DT_SKIPPED_RESULT);
    case StepResult::NOT_DONE:
      break;
  }
  return tr(STR_DT_NOT_DONE);
}

const char* answerText(const Answer a) {
  switch (a) {
    case Answer::YES:
      return tr(STR_DT_YES);
    case Answer::NO:
      return tr(STR_DT_NO);
    case Answer::NONE:
      break;
  }
  return "-";
}

const char* reasonRowText(const uint8_t row) {
  switch (static_cast<FailReason>(row)) {
    case FailReason::DID_NOT_APPEAR:
      return tr(STR_DT_REASON_NOT_APPEAR);
    case FailReason::WRONG_PLACE:
      return tr(STR_DT_REASON_WRONG_PLACE);
    case FailReason::WRONG_MODE:
      return tr(STR_DT_REASON_WRONG_MODE);
    case FailReason::GRADE_WRONG:
      return tr(STR_DT_REASON_GRADE);
    case FailReason::OTHER:
      return tr(STR_DT_REASON_OTHER);
    case FailReason::NONE:
      break;
  }
  return tr(STR_DT_BACK);
}

const char* walkName(const Walk w) {
  switch (w) {
    case Walk::START:
      return "start";
    case Walk::RUN:
      return "run";
    case Walk::ASK_READABLE:
      return "readable";
    case Walk::ASK_SLOWER:
      return "slower";
    case Walk::RUN_STOPPED:
      return "stopped";
    case Walk::PART_A_DONE:
      return "part-a";
    case Walk::STEP:
      return "step";
    case Walk::ASK_RESULT:
      return "result";
    case Walk::ASK_REASON:
      return "reason";
    case Walk::LEAVE:
      return "leave";
    case Walk::SUMMARY:
      return "summary";
    case Walk::MENU:
      return "menu";
  }
  return "?";
}

// "trial-a,trial-c", or "none".
void trialKeys(const displayprofile::TrialMask mask, char* out, const size_t size) {
  size_t used = 0;
  out[0] = '\0';
  for (const auto& info : displayprofile::PROFILES) {
    if (!displayprofile::isTrial(info.id) || (mask & displayprofile::trialBit(info.id)) == 0) continue;
    const int w = snprintf(out + used, size - used, "%s%s", used ? "," : "", info.key);
    if (w > 0 && static_cast<size_t>(w) < size - used) used += static_cast<size_t>(w);
  }
  if (used == 0) snprintf(out, size, "%s", "none");
}

// "Trial A, Trial C", or `none`.
void trialNames(const displayprofile::TrialMask mask, const char* none, char* out, const size_t size) {
  size_t used = 0;
  out[0] = '\0';
  for (const auto& info : displayprofile::PROFILES) {
    if (!displayprofile::isTrial(info.id) || (mask & displayprofile::trialBit(info.id)) == 0) continue;
    const int w = snprintf(out + used, size - used, "%s%s", used ? ", " : "", info.name);
    if (w > 0 && static_cast<size_t>(w) < size - used) used += static_cast<size_t>(w);
  }
  if (used == 0) snprintf(out, size, "%s", none);
}

Rating ratingFromName(const char* name) {
  if (name == nullptr) return Rating::NONE;
  if (strcmp(name, "clean") == 0) return Rating::CLEAN;
  if (strcmp(name, "tolerable") == 0) return Rating::TOLERABLE;
  if (strcmp(name, "unreadable") == 0) return Rating::UNREADABLE;
  return Rating::NONE;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// The guided acceptance: state

void DeviceTestActivity::loadAcceptance() {
  // The caller holds the RenderLock. Reads the progress file, if any, so the menu row shows where the
  // owner is and Guided acceptance resumes there.
  walk.reset();
  acc = AcceptanceRecord{};
  accAllocated = false;
  HalFile file;
  if (!Storage.exists(ACCEPTANCE_PROGRESS_PATH) || !Storage.openFileForRead("DTS", ACCEPTANCE_PROGRESS_PATH, file)) {
    LOG_DBG("DTS", "acceptance progress none");
    return;
  }
  // About 1 KB for the screen's open only: the decoder keeps its own copy of the record, so a damaged
  // file leaves the walk-through untouched. Freed before this returns.
  auto decoder = makeUniqueNoThrow<ProgressDecoder>();
  if (!decoder) {
    LOG_ERR("DTS", "OOM: acceptance progress (%u bytes)", static_cast<unsigned>(sizeof(ProgressDecoder)));
    file.close();
    return;
  }
  char line[PROGRESS_LINE_BYTES];
  size_t used = 0;
  bool tooLong = false;
  for (int c = file.read(); c >= 0; c = file.read()) {
    if (c == '\n') {
      line[used] = '\0';
      decoder->line(tooLong ? "!" : line);
      used = 0;
      tooLong = false;
    } else if (used + 1 < sizeof(line)) {
      line[used++] = static_cast<char>(c);
    } else {
      tooLong = true;
    }
  }
  if (used > 0) {
    line[used] = '\0';
    decoder->line(tooLong ? "!" : line);
  }
  file.close();
  fillHeader(acc.header);  // the device's own; the progress gives the run, the file and the conditions
  if (!decoder->finish(acc, walk)) {
    LOG_ERR("DTS", "acceptance progress %s is damaged: the walk-through starts again", ACCEPTANCE_PROGRESS_PATH);
    walk.reset();
    acc = AcceptanceRecord{};
    return;
  }
  accAllocated = true;
  LOG_INF("DTS", "acceptance resume run=%u file=%s/%s part_a=%s step=%u/%u answered=%u away=%d status=%s",
          acc.header.run, RESULTS_DIR, acc.header.file, partAStatusName(walk.partA.status),
          static_cast<unsigned>(walk.stepIndex() + 1), static_cast<unsigned>(WALK_STEP_COUNT),
          static_cast<unsigned>(walk.answered()), walk.partB.away ? 1 : 0, runStatusName(walk.status()));
}

void DeviceTestActivity::openAcceptance() {
  // The caller holds the RenderLock.
  if (!accAllocated) {
    // Shown on the start screen; fixed when Part A starts.
    acc.trialSetting = SETTINGS.rsvpDisplayProfile;
    acc.profile = displayprofile::resolveProfile(displayprofile::decodeTrialSetting(acc.trialSetting), backend.panel(),
                                                 display.isInverted());
  }
  const Walk next = walk.resume();
  LOG_DBG("DTS", "acceptance open state=%s step=%u/%u answered=%u", walkName(next),
          static_cast<unsigned>(walk.stepIndex() + 1), static_cast<unsigned>(WALK_STEP_COUNT),
          static_cast<unsigned>(walk.answered()));
  goWalk(next);
}

void DeviceTestActivity::beginAcceptanceRun() {
  // The caller holds the RenderLock. The first answer of a walk-through gives it a run number and a
  // results file; later answers rewrite them.
  if (accAllocated) return;
  const uint8_t setting = acc.trialSetting;
  const ProfileId profile = acc.profile;
  acc = AcceptanceRecord{};
  accSaved = allocateRun(ResultKind::ACCEPTANCE, acc.header);
  acc.trialSetting = setting;
  acc.profile = profile;
  accAllocated = true;
  LOG_INF("DTS", "acceptance start run=%u file=%s/%s profile=%s setting=%u panel=%s", acc.header.run, RESULTS_DIR,
          acc.header.file, profileInfo(acc.profile).key, static_cast<unsigned>(acc.trialSetting),
          displayprofile::panelName(backend.panel()));
}

bool DeviceTestActivity::saveAcceptance() {
  // The caller holds the RenderLock. The progress first: it is what a restart reads.
  if (!accAllocated) return false;
  size_t bytes = 0;
  progressSaved =
      writeAtomically(ACCEPTANCE_PROGRESS_PATH, [this](JsonSink& sink) { encodeProgress(acc, walk, sink); }, bytes);
  Storage.mkdir(RESULTS_DIR);
  char path[64];
  snprintf(path, sizeof(path), "%s/%s", RESULTS_DIR, acc.header.file);
  accSaved = writeAtomically(path, [this](JsonSink& sink) { writeAcceptance(acc, walk, sink); }, bytes);
  LOG_INF("DTS", "saved file=%s bytes=%lu status=%s ok=%d progress=%d", path, static_cast<unsigned long>(bytes),
          runStatusName(walk.status()), accSaved ? 1 : 0, progressSaved ? 1 : 0);
  return accSaved && progressSaved;
}

void DeviceTestActivity::logStepAnswer(const uint8_t index) {
  if (index >= WALK_STEP_COUNT) return;
  const StepAnswer& a = walk.partB.steps[index];
  LOG_INF("DTS", "acceptance step n=%u id=%s result=%s reason=%s answers=%u", static_cast<unsigned>(index + 1),
          WALK_STEPS[index].id, stepResultName(a.result), failReasonName(a.reason), static_cast<unsigned>(a.answers));
}

void DeviceTestActivity::logCriteria() {
  const Criterion5 c5 = walk.criterion5();
  LOG_INF("DTS",
          "acceptance criteria c1=%s c2=%s c3=%s c4=%s c5=%s measured=%s owner=%s pause_screen=%s answered=%u "
          "status=%s",
          stepResultName(walk.criterion(1)), stepResultName(walk.criterion(2)), stepResultName(walk.criterion(3)),
          stepResultName(walk.criterion(4)), stepResultName(walk.criterion(5)), stepResultName(c5.measured),
          stepResultName(c5.owner), stepResultName(c5.pauseScreen), static_cast<unsigned>(walk.answered()),
          runStatusName(walk.status()));
}

// ---------------------------------------------------------------------------------------------
// The guided acceptance: navigation (the caller holds the RenderLock)

void DeviceTestActivity::goWalk(const Walk next) {
  switch (next) {
    case Walk::START:
      show(Screen::AccStart, START_CHOICES, 0, false);
      return;
    case Walk::RUN:
      seriesOwner = SeriesOwner::Acceptance;
      if (!backend.select(acc.profile)) {
        // Not allowed now (the screen was inverted since): the run plays on Stock and says so.
        LOG_ERR("DTS", "acceptance profile %s refused by the backend: Stock plays", profileInfo(acc.profile).key);
        acc.profile = ProfileId::STOCK;
      }
      screen.store(Screen::Step);
      stepPending = true;
      seriesDone.store(false);
      wakeValid.store(false);
      requestUpdate();
      return;
    case Walk::ASK_READABLE:
      // Drawn with a clearing refresh, which also ends a trial's run.
      show(Screen::AccReadable, READABLE_CHOICES, 0, true);
      return;
    case Walk::ASK_SLOWER:
      show(Screen::AccSlower, SLOWER_CHOICES, 0, false);
      return;
    case Walk::RUN_STOPPED:
      show(Screen::AccRunStopped, RUN_STOP_CHOICES, 0, true);
      return;
    case Walk::PART_A_DONE:
      show(Screen::AccPartADone, 2, 0, false);
      return;
    case Walk::STEP:
      show(Screen::AccStep, STEP_CHOICES, 0, false);
      return;
    case Walk::ASK_RESULT:
      show(Screen::AccResult, RESULT_CHOICES, 0, false);
      return;
    case Walk::ASK_REASON:
      show(Screen::AccReason, REASON_CHOICES, 0, false);
      return;
    case Walk::LEAVE:
      leaveRequested = true;
      return;
    case Walk::SUMMARY:
      show(Screen::AccSummary, SUMMARY_CHOICES, 0, false);
      return;
    case Walk::MENU:
      show(Screen::Menu, MENU_ROW_COUNT, 0, false);
      return;
  }
}

void DeviceTestActivity::chooseAcceptance(const Screen s, const uint8_t row) {
  switch (s) {
    case Screen::AccStart: {
      static constexpr const char* NAMES[START_CHOICES] = {"start", "skip-part-a", "back"};
      const auto choice = static_cast<StartChoice>(row < START_CHOICES ? row : START_CHOICES - 1);
      if (choice == StartChoice::START) {
        acc.trialSetting = SETTINGS.rsvpDisplayProfile;
        acc.profile = displayprofile::resolveProfile(displayprofile::decodeTrialSetting(acc.trialSetting),
                                                     backend.panel(), display.isInverted());
      }
      if (choice != StartChoice::BACK) beginAcceptanceRun();
      LOG_INF("DTS", "acceptance part_a choice=%s profile=%s", NAMES[static_cast<uint8_t>(choice)],
              profileInfo(acc.profile).key);
      const Walk next = walk.start(choice);
      if (choice != StartChoice::BACK) saveAcceptance();
      goWalk(next);
      return;
    }
    case Screen::AccRunStopped: {
      static constexpr const char* NAMES[RUN_STOP_CHOICES] = {"repeat", "skip-part-a"};
      const auto choice = static_cast<RunStopChoice>(row < RUN_STOP_CHOICES ? row : 0);
      LOG_INF("DTS", "acceptance stopped run=%s choice=%s", walk.runSpec().key, NAMES[static_cast<uint8_t>(choice)]);
      const Walk next = walk.stopped(choice);
      saveAcceptance();
      goWalk(next);
      return;
    }
    case Screen::AccReadable: {
      static constexpr const char* NAMES[READABLE_CHOICES] = {"repeat", "yes", "no"};
      const auto choice = static_cast<ReadableChoice>(row < READABLE_CHOICES ? row : 0);
      LOG_INF("DTS", "acceptance readable run=%s choice=%s", walk.runSpec().key, NAMES[static_cast<uint8_t>(choice)]);
      const Walk next = walk.readable(choice);
      saveAcceptance();
      goWalk(next);
      return;
    }
    case Screen::AccSlower: {
      static constexpr const char* NAMES[SLOWER_CHOICES] = {"repeat", "yes", "no"};
      const auto choice = static_cast<SlowerChoice>(row < SLOWER_CHOICES ? row : 0);
      LOG_INF("DTS", "acceptance slower run=%s choice=%s", walk.runSpec().key, NAMES[static_cast<uint8_t>(choice)]);
      const Walk next = walk.slower(choice);
      if (next == Walk::PART_A_DONE) {
        const Criterion5 c = walk.criterion5();
        LOG_INF("DTS", "acceptance part_a status=%s measured=%s owner=%s", partAStatusName(walk.partA.status),
                stepResultName(c.measured), stepResultName(c.owner));
      }
      saveAcceptance();
      goWalk(next);
      return;
    }
    case Screen::AccPartADone:
      goWalk(row == 0 ? walk.partADone() : Walk::MENU);
      return;
    case Screen::AccStep: {
      const auto choice = static_cast<StepChoice>(row < STEP_CHOICES ? row : static_cast<uint8_t>(StepChoice::MENU));
      const Walk next = walk.step(choice);
      if (next == Walk::LEAVE) {
        LOG_INF("DTS", "acceptance leave step n=%u id=%s", static_cast<unsigned>(walk.stepIndex() + 1),
                WALK_STEPS[walk.stepIndex()].id);
        saveAcceptance();
      }
      goWalk(next);
      return;
    }
    case Screen::AccResult: {
      const auto choice = static_cast<ResultChoice>(row < RESULT_CHOICES ? row : 0);
      const uint8_t index = walk.stepIndex();
      const bool wasAway = walk.partB.away;
      const Walk next = walk.result(choice);
      if (choice == ResultChoice::PASS || choice == ResultChoice::SKIP) {
        logStepAnswer(index);
        logCriteria();
        saveAcceptance();
      } else if (choice == ResultChoice::NOT_DONE && wasAway) {
        LOG_INF("DTS", "acceptance step n=%u id=%s not-done", static_cast<unsigned>(index + 1), WALK_STEPS[index].id);
        saveAcceptance();
      }
      goWalk(next);
      return;
    }
    case Screen::AccReason: {
      const uint8_t index = walk.stepIndex();
      const Walk next = walk.reason(row);
      if (row > 0 && row < REASON_CHOICES) {
        logStepAnswer(index);
        logCriteria();
        saveAcceptance();
      }
      goWalk(next);
      return;
    }
    case Screen::AccSummary:
      if (row == static_cast<uint8_t>(SummaryRow::START_AGAIN)) {
        // The finished walk-through's results file stays; the next answer starts a new one.
        LOG_INF("DTS", "acceptance new walk-through (run %u kept)", acc.header.run);
        Storage.remove(ACCEPTANCE_PROGRESS_PATH);
        walk.reset();
        acc = AcceptanceRecord{};
        accAllocated = false;
        openAcceptance();
        return;
      }
      goWalk(Walk::MENU);
      return;
    default:
      return;
  }
}

void DeviceTestActivity::acceptanceSeriesEnded(const StepFigures& f, const bool stopped) {
  // The caller holds the RenderLock.
  if (stopped) {
    walk.runStopped(f);
    saveAcceptance();
    backend.select(ProfileId::STOCK);
    goWalk(Walk::RUN_STOPPED);
    return;
  }
  walk.runDone(f);
  saveAcceptance();
  backend.select(ProfileId::STOCK);  // refreshes nothing: the last frame stays until the owner has looked
  screen.store(Screen::Look);
}

void DeviceTestActivity::abandonAcceptanceRun() {
  // The caller holds the RenderLock: the screen is left or the device sleeps during a run.
  seriesActive.store(false);
  walk.abandonRun(runner.figures());
  LOG_INF("DTS", "acceptance run=%s interrupted", walk.runSpec().key);
  saveAcceptance();
}

// ---------------------------------------------------------------------------------------------
// The guided acceptance: screens (render task)

const char* DeviceTestActivity::menuAcceptanceLabel(char* out, const size_t size) const {
  if (!accAllocated) {
    snprintf(out, size, "%s", tr(STR_DT_GUIDED_ACCEPTANCE));
  } else if (walk.complete()) {
    snprintf(out, size, "%s: %s", tr(STR_DT_GUIDED_ACCEPTANCE), tr(STR_DT_PROFILE_DONE));
  } else if (walk.partA.status != PartAStatus::DONE && walk.partA.status != PartAStatus::SKIPPED) {
    snprintf(out, size, "%s: %s", tr(STR_DT_GUIDED_ACCEPTANCE), tr(STR_DT_PART_A));
  } else {
    snprintf(out, size, "%s: %s %u %s %u", tr(STR_DT_GUIDED_ACCEPTANCE), tr(STR_DT_STEP),
             static_cast<unsigned>(walk.stepIndex() + 1), tr(STR_DT_OF), static_cast<unsigned>(WALK_STEP_COUNT));
  }
  return out;
}

void DeviceTestActivity::describeAcceptanceScreen(const Screen s, const char*& title, char* subtitle,
                                                  const char** labels, uint8_t& lines) {
  const auto line = [&](const char* fmt, auto... args) {
    if (lines < INFO_LINES) snprintf(info[lines++], LINE_BYTES, fmt, args...);
  };
  // A run's figures, as on the benchmark's rating prompt.
  const auto runLines = [&](const StepSpec& spec, const RunRecord& r) {
    if (!r.hasFigures || !r.figures.hasPlayback) {
      line("%s %u WPM: %s", tr(STR_DT_TARGET), spec.wpm, tr(STR_DT_NOT_DONE));
      return;
    }
    const PlaybackFigures& p = r.figures.playback;
    line("%s %u WPM · %s %lu.%lu WPM", tr(STR_DT_TARGET), spec.wpm, tr(STR_DT_DELIVERED),
         static_cast<unsigned long>(p.deliveredWpmTenths / 10), static_cast<unsigned long>(p.deliveredWpmTenths % 10));
    if (runMeasuredOk(spec, r)) {
      line("%s: %lu / %u, %s", tr(STR_DT_WORDS), static_cast<unsigned long>(p.wordsDrawn), spec.count,
           tr(STR_DT_ORDER_OK));
    } else {
      line("%s: %lu / %u. %s: %s %lu, %s %lu, %s %lu", tr(STR_DT_WORDS), static_cast<unsigned long>(p.wordsDrawn),
           spec.count, tr(STR_DT_ORDER_BAD), tr(STR_DT_GAPS), static_cast<unsigned long>(p.gaps), tr(STR_DT_REPEATS),
           static_cast<unsigned long>(p.repeats), tr(STR_DT_OUT_OF_ORDER), static_cast<unsigned long>(p.violations));
    }
    const SeriesFigures& u = r.figures.updates;
    line("%s: %s %lu ms, p95 %lu ms, max %lu ms", tr(STR_DT_UPDATES), tr(STR_DT_MEDIAN),
         static_cast<unsigned long>(u.medianMs), static_cast<unsigned long>(u.p95Ms),
         static_cast<unsigned long>(u.maxMs));
    line("%s: %lu · %s %lu · %s %lu", tr(STR_DT_CLEARING_REFRESH), static_cast<unsigned long>(p.clearingRefreshes),
         tr(STR_DT_TIMEOUTS), static_cast<unsigned long>(u.timeouts), tr(STR_DT_PROMOTIONS),
         static_cast<unsigned long>(r.figures.promotions));
  };
  const uint8_t index = walk.stepIndex() < WALK_STEP_COUNT ? walk.stepIndex() : WALK_STEP_COUNT - 1;
  const WalkStep& step = WALK_STEPS[index];

  switch (s) {
    case Screen::AccStart:
      title = tr(STR_DT_GUIDED_ACCEPTANCE);
      snprintf(subtitle, LINE_BYTES, "%s: %s", tr(STR_DT_PROFILE_NOW), profileInfo(acc.profile).name);
      labels[0] = tr(STR_DT_START_PART_A);
      labels[1] = tr(STR_DT_SKIP_PART_A);
      labels[2] = tr(STR_DT_BACK);
      longText[0].text = tr(STR_DT_ACC_INTRO);
      return;
    case Screen::AccRunStopped:
      title = tr(STR_DT_RUN_STOPPED);
      snprintf(subtitle, LINE_BYTES, "%s · %s %u / %u", tr(STR_DT_PART_A), tr(STR_DT_RUN),
               static_cast<unsigned>(walk.runIndex() + 1), static_cast<unsigned>(ACCEPTANCE_RUN_COUNT));
      labels[0] = tr(STR_DT_REPEAT_RUN);
      labels[1] = tr(STR_DT_SKIP_PART_A);
      return;
    case Screen::AccReadable:
    case Screen::AccSlower: {
      const bool readable = s == Screen::AccReadable;
      title = readable ? tr(STR_DT_READABLE_TITLE) : tr(STR_DT_SLOWER_TITLE);
      snprintf(subtitle, LINE_BYTES, "%s · %s %u / %u · %s", tr(STR_DT_PART_A), tr(STR_DT_RUN),
               static_cast<unsigned>(walk.runIndex() + 1), static_cast<unsigned>(ACCEPTANCE_RUN_COUNT),
               profileInfo(acc.profile).name);
      labels[0] = tr(STR_DT_REPEAT_RUN);
      labels[1] = readable ? tr(STR_DT_YES_READABLE) : tr(STR_DT_YES_SLOWER);
      labels[2] = tr(STR_DT_NO);
      runLines(walk.runSpec(), walk.partA.runs[walk.runIndex()]);
      if (!readable) longText[0].text = tr(STR_DT_SLOWER_HELP);
      return;
    }
    case Screen::AccPartADone: {
      title = walk.partA.status == PartAStatus::SKIPPED ? tr(STR_DT_PART_A_SKIPPED) : tr(STR_DT_PART_A_DONE);
      snprintf(subtitle, LINE_BYTES, "%s: %s", tr(STR_DT_PROFILE_NOW), profileInfo(acc.profile).name);
      labels[0] = tr(STR_DT_CONTINUE_PART_B);
      labels[1] = tr(STR_DT_DEVICE_TEST_MENU);
      for (size_t i = 0; i < ACCEPTANCE_RUN_COUNT; ++i) {
        const RunRecord& r = walk.partA.runs[i];
        const StepSpec& spec = ACCEPTANCE_RUNS[i];
        if (r.state == RunState::DONE && r.figures.hasPlayback) {
          const PlaybackFigures& p = r.figures.playback;
          line("%u WPM: %s %lu.%lu WPM, %lu / %u, %s · %s: %s", spec.wpm, tr(STR_DT_DELIVERED),
               static_cast<unsigned long>(p.deliveredWpmTenths / 10),
               static_cast<unsigned long>(p.deliveredWpmTenths % 10), static_cast<unsigned long>(p.wordsDrawn),
               spec.count, runMeasuredOk(spec, r) ? "order ok" : "ORDER VIOLATED", tr(STR_DT_READABLE),
               answerText(r.readable));
        } else {
          line("%u WPM: %s", spec.wpm, tr(STR_DT_NOT_DONE));
        }
      }
      line("%s: %s", tr(STR_DT_SLOWER), answerText(walk.partA.slower));
      const Criterion5 c = walk.criterion5();
      line("%s: %s · %s: %s", tr(STR_DT_AUTO_CHECK), resultText(c.measured), tr(STR_DT_YOUR_ANSWERS),
           resultText(c.owner));
      return;
    }
    case Screen::AccStep:
      snprintf(labelText[0], LINE_BYTES, "%s %u %s %u", tr(STR_DT_STEP_CAP), static_cast<unsigned>(index + 1),
               tr(STR_DT_OF), static_cast<unsigned>(WALK_STEP_COUNT));
      title = labelText[0];
      snprintf(subtitle, LINE_BYTES, "%s", step.title);
      labels[0] = tr(STR_DT_LEAVE_TO_DO);
      labels[1] = tr(STR_DT_STEP_DONE);
      labels[2] = tr(STR_DT_DEVICE_TEST_MENU);
      longText[0].text = step.instructions;
      longText[1].label = tr(STR_DT_EXPECTED);
      longText[1].text = step.expected;
      return;
    case Screen::AccResult:
      snprintf(labelText[4], LINE_BYTES, "%s %u: %s", tr(STR_DT_STEP_CAP), static_cast<unsigned>(index + 1),
               tr(STR_DT_HOW_DID_IT_GO));
      title = labelText[4];
      snprintf(subtitle, LINE_BYTES, "%s", step.title);
      labels[0] = tr(STR_DT_NOT_DONE_YET);
      labels[1] = tr(STR_DT_PASS);
      labels[2] = tr(STR_DT_FAIL);
      labels[3] = tr(STR_DT_SKIP_STEP);
      longText[0].label = tr(STR_DT_EXPECTED);
      longText[0].text = step.expected;
      return;
    case Screen::AccReason:
      title = tr(STR_DT_WHAT_WENT_WRONG);
      snprintf(subtitle, LINE_BYTES, "%s %u · %s", tr(STR_DT_STEP_CAP), static_cast<unsigned>(index + 1), step.title);
      for (uint8_t r = 0; r < REASON_CHOICES; ++r) labels[r] = reasonRowText(r);
      return;
    case Screen::AccSummary: {
      title = walk.complete() ? tr(STR_DT_ACC_DONE) : tr(STR_DT_ACC_SO_FAR);
      if (accSaved) {
        snprintf(subtitle, LINE_BYTES, "%s %s/%s", tr(STR_DT_SAVED_AS), RESULTS_DIR, acc.header.file);
      } else {
        snprintf(subtitle, LINE_BYTES, "%s", tr(STR_DT_NOT_SAVED));
      }
      labels[0] = tr(STR_DT_BACK_TO_MENU);
      labels[1] = tr(STR_DT_START_AGAIN);
      for (unsigned k = 1; k < CRITERION_COUNT; ++k) {
        line("%s %u: %s", tr(STR_DT_CRITERION), k, resultText(walk.criterion(k)));
      }
      // Criterion 5 with its three parts: the automatic verdict apart from the owner's answers.
      const Criterion5 c = walk.criterion5();
      line("%s 5: %s. %s %s, %s %s, %s %s.", tr(STR_DT_CRITERION), resultText(walk.criterion(5)), tr(STR_DT_AUTO_CHECK),
           resultText(c.measured), tr(STR_DT_YOUR_ANSWERS), resultText(c.owner), tr(STR_DT_PAUSE_SCREEN),
           resultText(c.pauseScreen));
      for (size_t i = 0; i < WALK_STEP_COUNT; ++i) {
        const StepAnswer& a = walk.partB.steps[i];
        if (a.result == StepResult::FAIL) {
          line("%s %u: %s · %s", tr(STR_DT_STEP_CAP), static_cast<unsigned>(i + 1), tr(STR_DT_FAIL),
               reasonRowText(static_cast<uint8_t>(a.reason)));
        }
      }
      line("%s", tr(STR_DT_CRITERION_6));
      return;
    }
    default:
      return;
  }
}

// ---------------------------------------------------------------------------------------------
// The profile choice (the caller holds the RenderLock)

void DeviceTestActivity::openChoice() {
  choice = ChoiceRecord{};
  keepMask = displayprofile::NO_TRIALS;
  choice.settingBefore = SETTINGS.rsvpDisplayProfile;
  choice.keptBefore = SETTINGS.rsvpProfilesKept;
  const displayprofile::TrialSetting setting = displayprofile::decodeTrialSetting(choice.settingBefore);
  choice.candidates = choiceCandidates(setting, backend.panel(), display.isInverted());
  readBenchRatings();
  const ProfileId current = displayprofile::resolveProfile(setting, backend.panel(), display.isInverted());
  char keys[64] = {};
  size_t used = 0;
  for (uint8_t i = 0; i < choice.candidates.count; ++i) {
    const int w =
        snprintf(keys + used, sizeof(keys) - used, "%s%s", used ? "," : "", profileInfo(choice.candidates.ids[i]).key);
    if (w > 0 && static_cast<size_t>(w) < sizeof(keys) - used) used += static_cast<size_t>(w);
  }
  LOG_INF("DTS", "choose open candidates=%s current=%s setting=%u kept=%u bench=%s", keys, profileInfo(current).key,
          static_cast<unsigned>(choice.settingBefore), static_cast<unsigned>(choice.keptBefore),
          choice.bench.file[0] != '\0' ? choice.bench.file : "none");
  // The default row is the profile RSVP plays with now (S4 row 34); Back is the last row.
  show(Screen::ChooseProfile, static_cast<uint8_t>(choice.candidates.count + 1), 0, false,
       choice.candidates.indexOf(current, 0));
}

void DeviceTestActivity::readBenchRatings() {
  // The newest benchmark results file (the largest run number), its ratings and its 240 WPM figure.
  BenchRatings& out = choice.bench;
  out = BenchRatings{};
  uint16_t newest = 0;
  char name[48] = {};
  HalFile dir = Storage.open(RESULTS_DIR);
  if (dir && dir.isDirectory()) {
    for (HalFile f = dir.openNextFile(); f; f = dir.openNextFile()) {
      char n[48] = {};
      f.getName(n, sizeof(n));
      f.close();
      const size_t length = strlen(n);
      constexpr size_t SUFFIX = sizeof("-bench.json") - 1;
      const uint16_t run = runNumberOf(n);
      if (run > newest && length > SUFFIX && strcmp(n + length - SUFFIX, "-bench.json") == 0) {
        newest = run;
        snprintf(name, sizeof(name), "%s", n);
      }
    }
  }
  if (dir) dir.close();
  if (newest == 0) return;

  char path[64];
  snprintf(path, sizeof(path), "%s/%s", RESULTS_DIR, name);
  HalFile file;
  if (!Storage.openFileForRead("DTS", path, file)) return;
  // A transient DOM of the filtered members only (5 profiles of 10 steps: the step names, ratings,
  // residue and one delivered WPM; a few KB), freed before this returns.
  JsonDocument doc;
  DeserializationError err;
  {
    JsonDocument filter;
    filter["profiles"][0]["key"] = true;
    filter["profiles"][0]["steps"][0]["step"] = true;
    filter["profiles"][0]["steps"][0]["rating"] = true;
    filter["profiles"][0]["steps"][0]["residue"] = true;
    filter["profiles"][0]["steps"][0]["playback"]["delivered_wpm"] = true;
    err = deserializeJson(doc, file, DeserializationOption::Filter(filter));
  }
  file.close();
  if (err) {
    LOG_ERR("DTS", "choose: %s cannot be read: %s", path, err.c_str());
    return;
  }
  snprintf(out.file, sizeof(out.file), "%.23s", name);  // "NNNN-bench.json": 15 bytes
  for (JsonObjectConst p : doc["profiles"].as<JsonArrayConst>()) {
    const displayprofile::ProfileInfo* info = displayprofile::findProfile(p["key"] | "");
    if (info == nullptr) continue;
    ProfileRatings& r = out.profiles[static_cast<size_t>(info->id)];
    for (JsonObjectConst st : p["steps"].as<JsonArrayConst>()) {
      const char* key = st["step"] | "";
      const int k = ratedStepIndex(key);
      const Rating rating = ratingFromName(st["rating"] | static_cast<const char*>(nullptr));
      if (k >= 0 && rating != Rating::NONE) {
        r.ratings[k] = rating;
        r.found = true;
      }
      if (strcmp(key, "endurance") == 0) {
        const char* residue = st["residue"] | "";
        r.residue = strcmp(residue, "yes") == 0  ? Residue::YES
                    : strcmp(residue, "no") == 0 ? Residue::NO
                                                 : Residue::NONE;
      }
      if (strcmp(key, "playback-240-1") == 0 && !st["playback"]["delivered_wpm"].isNull()) {
        const float wpm = st["playback"]["delivered_wpm"].as<float>();
        r.delivered240Tenths = static_cast<uint32_t>(lroundf(wpm * 10.0f));
        r.hasDelivered240 = true;
        r.found = true;
      }
    }
  }
}

uint8_t DeviceTestActivity::keepRows() const { return static_cast<uint8_t>(keepable.count + 2); }

void DeviceTestActivity::chooseProfile(const Screen s, const uint8_t row) {
  switch (s) {
    case Screen::ChooseProfile: {
      if (row >= choice.candidates.count) {
        show(Screen::Menu, MENU_ROW_COUNT, 0, false);
        return;
      }
      choice.chosen = choice.candidates.ids[row];
      keepMask = displayprofile::NO_TRIALS;
      keepable = ProfileList{};
      for (uint8_t i = 0; i < choice.candidates.count; ++i) {
        const ProfileId id = choice.candidates.ids[i];
        if (displayprofile::isTrial(id) && id != choice.chosen) keepable.ids[keepable.count++] = id;
      }
      LOG_INF("DTS", "choose chosen=%s keepable=%u", profileInfo(choice.chosen).key,
              static_cast<unsigned>(keepable.count));
      // Default: the first trial's toggle (it commits nothing); with none, Back.
      show(Screen::KeepTrials, keepRows(), 0, false, keepable.count > 0 ? 0 : static_cast<uint8_t>(keepRows() - 1));
      return;
    }
    case Screen::KeepTrials:
      if (row < keepable.count) {
        const ProfileId id = keepable.ids[row];
        keepMask = static_cast<displayprofile::TrialMask>(keepMask ^ displayprofile::trialBit(id));
        LOG_INF("DTS", "choose keep %s=%d", profileInfo(id).key, (keepMask & displayprofile::trialBit(id)) ? 1 : 0);
        requestUpdate();
        return;
      }
      if (row == keepable.count) {
        saveChoice();
        show(Screen::ChoiceDone, 1, 0, false);
        return;
      }
      show(Screen::ChooseProfile, static_cast<uint8_t>(choice.candidates.count + 1), 0, false,
           choice.candidates.indexOf(choice.chosen, 0));
      return;
    case Screen::ChoiceDone:
      show(Screen::Menu, MENU_ROW_COUNT, 0, false);
      return;
    default:
      return;
  }
}

void DeviceTestActivity::saveChoice() {
  const ChoiceOutcome out = applyChoice(choice.settingBefore, choice.chosen, keepMask);
  choice.settingAfter = out.setting;
  choice.keptAfter = out.kept;
  choice.locked = out.locked;
  SETTINGS.rsvpDisplayProfile = out.setting;
  SETTINGS.rsvpProfilesKept = out.kept;
  choice.saved = SETTINGS.saveToFile();
  // The device test plays on Stock; leaving it selects the chosen profile (restoreProfile()).
  backend.setUnlockedTrials(displayprofile::decodeTrialSetting(out.setting).unlocked);

  choiceFileSaved = allocateRun(ResultKind::CHOICE, choice.header);
  char path[64];
  snprintf(path, sizeof(path), "%s/%s", RESULTS_DIR, choice.header.file);
  size_t bytes = 0;
  choiceFileSaved =
      writeAtomically(path, [this](JsonSink& sink) { writeChoice(choice, sink); }, bytes) && choiceFileSaved;
  char kept[48];
  char locked[48];
  trialKeys(out.kept, kept, sizeof(kept));
  trialKeys(out.locked, locked, sizeof(locked));
  LOG_INF("DTS",
          "choice run=%u file=%s chosen=%s kept=%s locked=%s setting_before=%u setting_after=%u kept_before=%u "
          "kept_after=%u settings_saved=%d row=%s",
          choice.header.run, path, profileInfo(choice.chosen).key, kept, locked,
          static_cast<unsigned>(choice.settingBefore), static_cast<unsigned>(choice.settingAfter),
          static_cast<unsigned>(choice.keptBefore), static_cast<unsigned>(choice.keptAfter), choice.saved ? 1 : 0,
          speedReadRowVisible(out.setting, out.kept) ? "shown" : "hidden");
  LOG_INF("DTS", "saved file=%s bytes=%lu status=complete ok=%d", path, static_cast<unsigned long>(bytes),
          choiceFileSaved ? 1 : 0);
}

void DeviceTestActivity::describeChoiceScreen(const Screen s, const char*& title, char* subtitle, const char** labels,
                                              uint8_t& lines) {
  const auto line = [&](const char* fmt, auto... args) {
    if (lines < INFO_LINES) snprintf(info[lines++], LINE_BYTES, fmt, args...);
  };
  switch (s) {
    case Screen::ChooseProfile: {
      title = tr(STR_DT_CHOOSE_PROFILE);
      const displayprofile::TrialSetting setting = displayprofile::decodeTrialSetting(choice.settingBefore);
      snprintf(subtitle, LINE_BYTES, "%s: %s", tr(STR_DT_NOW),
               profileInfo(displayprofile::resolveProfile(setting, backend.panel(), display.isInverted())).name);
      const uint8_t n = choice.candidates.count;
      for (uint8_t i = 0; i < n && i + 1u < controls::CHOICE_ROWS_MAX; ++i) {
        const ProfileId id = choice.candidates.ids[i];
        const ProfileRatings& r = choice.bench.profiles[static_cast<size_t>(id)];
        unsigned clean = 0;
        for (const Rating rating : r.ratings) clean += rating == Rating::CLEAN ? 1u : 0u;
        if (r.found && r.hasDelivered240) {
          snprintf(labelText[i], LINE_BYTES, "%s · %u %s · %lu WPM", profileInfo(id).name, clean, tr(STR_DT_CLEAN_LC),
                   static_cast<unsigned long>((r.delivered240Tenths + 5) / 10));
        } else if (r.found) {
          snprintf(labelText[i], LINE_BYTES, "%s · %u %s", profileInfo(id).name, clean, tr(STR_DT_CLEAN_LC));
        } else {
          snprintf(labelText[i], LINE_BYTES, "%s", profileInfo(id).name);
        }
        labels[i] = labelText[i];
      }
      labels[n] = tr(STR_DT_BACK);
      if (choice.bench.file[0] != '\0') {
        line("%s %s/%s", tr(STR_DT_RATINGS_FROM), RESULTS_DIR, choice.bench.file);
        for (uint8_t i = 0; i < n; ++i) {
          const ProfileId id = choice.candidates.ids[i];
          const ProfileRatings& r = choice.bench.profiles[static_cast<size_t>(id)];
          if (!r.found) {
            line("%s: %s", profileInfo(id).name, tr(STR_DT_NOT_RATED));
            continue;
          }
          char counts[64];
          ratingCounts(r, counts, sizeof(counts));
          line("%s: %s; %s %s", profileInfo(id).name, counts, tr(STR_DT_RESIDUE), residueName(r.residue));
        }
      } else {
        line("%s", tr(STR_DT_NO_BENCH));
      }
      longText[0].text = n > 1 ? tr(STR_DT_CHOOSE_HELP) : tr(STR_DT_CHOOSE_ONLY_STOCK);
      return;
    }
    case Screen::KeepTrials:
      title = tr(STR_DT_KEEP_TITLE);
      snprintf(subtitle, LINE_BYTES, "%s: %s", tr(STR_DT_CHOSEN), profileInfo(choice.chosen).name);
      for (uint8_t i = 0; i < keepable.count; ++i) {
        const ProfileId id = keepable.ids[i];
        const bool kept = (keepMask & displayprofile::trialBit(id)) != 0;
        snprintf(labelText[i], LINE_BYTES, "%s: %s", profileInfo(id).name,
                 kept ? tr(STR_DT_KEEP_ON) : tr(STR_DT_KEEP_OFF));
        labels[i] = labelText[i];
      }
      labels[keepable.count] = tr(STR_DT_SAVE_CHOICE);
      labels[keepable.count + 1] = tr(STR_DT_BACK);
      if (keepable.count == 0) line("%s", tr(STR_DT_KEEP_NONE_HERE));
      longText[0].text = tr(STR_DT_KEEP_HELP);
      return;
    case Screen::ChoiceDone: {
      title = choice.saved ? tr(STR_DT_CHOICE_SAVED) : tr(STR_DT_CHOICE_NOT_SAVED);
      if (choiceFileSaved) {
        snprintf(subtitle, LINE_BYTES, "%s %s/%s", tr(STR_DT_SAVED_AS), RESULTS_DIR, choice.header.file);
      } else {
        snprintf(subtitle, LINE_BYTES, "%s", tr(STR_DT_NOT_SAVED));
      }
      labels[0] = tr(STR_DT_BACK_TO_MENU);
      line("%s: %s", tr(STR_DT_READING_USES), profileInfo(choice.chosen).name);
      char names[64];
      trialNames(choice.keptAfter, tr(STR_DT_NONE), names, sizeof(names));
      line("%s: %s", tr(STR_DT_KEPT), names);
      trialNames(choice.locked, tr(STR_DT_NONE), names, sizeof(names));
      line("%s: %s", tr(STR_DT_LOCKED), names);
      longText[0].text =
          speedReadRowVisible(choice.settingAfter, choice.keptAfter) ? tr(STR_DT_ROW_SHOWN) : tr(STR_DT_ROW_HIDDEN);
      return;
    }
    default:
      return;
  }
}

#endif  // CROSSPOINT_RSVP_ENABLED
