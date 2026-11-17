#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include "BenchPlan.h"

#include <cstdio>

namespace devicetest {

using displayprofile::PROFILE_COUNT;
using displayprofile::PROFILES;
using displayprofile::TrialMask;
using displayprofile::TrialSetting;

const char* stepKindName(const StepKind kind) {
  switch (kind) {
    case StepKind::WORD_BAND:
      return "word-band";
    case StepKind::FULL_SCREEN:
      return "full-screen";
    case StepKind::CLEARING:
      return "clearing";
    case StepKind::PLAYBACK:
      return "playback";
    case StepKind::ENDURANCE:
      return "endurance";
  }
  return "?";
}

const char* ratingName(const Rating rating) {
  switch (rating) {
    case Rating::NONE:
      return "none";
    case Rating::CLEAN:
      return "clean";
    case Rating::TOLERABLE:
      return "tolerable";
    case Rating::UNREADABLE:
      return "unreadable";
  }
  return "?";
}

const char* residueName(const Residue residue) {
  switch (residue) {
    case Residue::NONE:
      return "none";
    case Residue::NO:
      return "no";
    case Residue::YES:
      return "yes";
  }
  return "?";
}

const char* stepStatusName(const StepStatus status) {
  switch (status) {
    case StepStatus::PENDING:
      return "pending";
    case StepStatus::UNRATED:
      return "unrated";
    case StepStatus::DONE:
      return "done";
    case StepStatus::STOPPED:
      return "stopped";
    case StepStatus::SKIPPED:
      return "skipped";
  }
  return "?";
}

const char* profileStatusName(const ProfileStatus status) {
  switch (status) {
    case ProfileStatus::NOT_ALLOWED:
      return "not-allowed";
    case ProfileStatus::PENDING:
      return "pending";
    case ProfileStatus::RUNNING:
      return "running";
    case ProfileStatus::DONE:
      return "done";
    case ProfileStatus::SKIPPED_PART_WAY:
      return "skipped-part-way";
    case ProfileStatus::NOT_RUN:
      return "not-run";
  }
  return "?";
}

const char* notRunReasonName(const NotRunReason reason) {
  switch (reason) {
    case NotRunReason::NONE:
      return "none";
    case NotRunReason::PANEL_UNRECOGNISED:
      return "panel-unrecognised";
    case NotRunReason::PANEL_NO_TABLES:
      return "panel-no-tables";
    case NotRunReason::SCREEN_INVERTED:
      return "screen-inverted";
    case NotRunReason::REFUSED:
      return "refused";
    case NotRunReason::ENDED_BY_OWNER:
      return "ended-by-owner";
    case NotRunReason::INTERRUPTED:
      return "interrupted";
  }
  return "?";
}

const char* runStatusName(const RunStatus status) {
  switch (status) {
    case RunStatus::RUNNING:
      return "running";
    case RunStatus::COMPLETE:
      return "complete";
    case RunStatus::ENDED_BY_OWNER:
      return "ended-by-owner";
    case RunStatus::INTERRUPTED:
      return "interrupted";
  }
  return "?";
}

// ---------------------------------------------------------------------------------------------

TrialMask runnableTrials(const PanelId panel, const bool inverted) {
  TrialMask mask = displayprofile::NO_TRIALS;
  for (const auto& info : PROFILES) {
    if (displayprofile::isTrial(info.id) && displayprofile::profileAllowed(info.id, panel, inverted, true)) {
      mask = static_cast<TrialMask>(mask | displayprofile::trialBit(info.id));
    }
  }
  return mask;
}

NotRunReason notAllowedReason(const ProfileId id, const PanelId panel, const bool inverted) {
  if (displayprofile::profileAllowed(id, panel, inverted, true)) return NotRunReason::NONE;
  if (inverted && displayprofile::profileAllowed(id, panel, false, true)) return NotRunReason::SCREEN_INVERTED;
  if (!displayprofile::panelRecord(panel).recognised) return NotRunReason::PANEL_UNRECOGNISED;
  return NotRunReason::PANEL_NO_TABLES;
}

TrialSetting unlockForBenchmark(const TrialSetting& before, const TrialMask runnable) {
  TrialSetting after = before;
  after.unlocked = static_cast<TrialMask>((before.unlocked | runnable) & displayprofile::ALL_TRIALS);
  return after;
}

// ---------------------------------------------------------------------------------------------

TrialMask Sweep::plan(const PanelId panel, const bool inverted) {
  TrialMask runnable = displayprofile::NO_TRIALS;
  for (size_t i = 0; i < PROFILE_COUNT; ++i) {
    ProfileRecord& p = profiles[i];
    p = ProfileRecord{};
    p.id = PROFILES[i].id;
    p.reason = notAllowedReason(p.id, panel, inverted);
    p.status = p.reason == NotRunReason::NONE ? ProfileStatus::PENDING : ProfileStatus::NOT_ALLOWED;
    if (p.status == ProfileStatus::PENDING) {
      runnable = static_cast<TrialMask>(runnable | displayprofile::trialBit(p.id));
    } else {
      for (StepRecord& r : p.steps) r.status = StepStatus::SKIPPED;
    }
  }
  current = 0;
  step = 0;
  running = false;
  runStatus = RunStatus::RUNNING;
  return runnable;
}

Next Sweep::start() {
  current = 0;  // Stock: always allowed
  step = 0;
  running = true;
  skipAfterResidue = false;
  haveStoppedFigures = false;
  runStatus = RunStatus::RUNNING;
  profiles[current].status = ProfileStatus::RUNNING;
  return Next::RUN_STEP;
}

uint8_t Sweep::runnableCount() const {
  uint8_t n = 0;
  for (const auto& p : profiles) n = static_cast<uint8_t>(n + (p.status != ProfileStatus::NOT_ALLOWED ? 1 : 0));
  return n;
}

uint8_t Sweep::runnableOrdinal() const {
  uint8_t n = 0;
  for (uint8_t i = 0; i <= current && i < PROFILE_COUNT; ++i) {
    if (profiles[i].status != ProfileStatus::NOT_ALLOWED) ++n;
  }
  return n;
}

const ProfileRecord* Sweep::nextProfile() const {
  for (size_t i = current + 1u; i < PROFILE_COUNT; ++i) {
    if (profiles[i].status == ProfileStatus::PENDING) return &profiles[i];
  }
  return nullptr;
}

Next Sweep::seriesDone(const StepFigures& figures) {
  StepRecord& r = profiles[current].steps[step];
  ++r.attempts;
  r.figures = figures;
  r.hasFigures = true;
  r.rating = Rating::NONE;
  r.residue = Residue::NONE;
  haveStoppedFigures = false;
  skipAfterResidue = false;
  if (STEPS[step].rated) {
    r.status = StepStatus::UNRATED;
    return Next::ASK_RATING;
  }
  r.status = StepStatus::DONE;
  return advance();
}

void Sweep::seriesStopped(const StepFigures& figures) {
  ++profiles[current].steps[step].attempts;
  stoppedFigures = figures;
  haveStoppedFigures = true;
}

Next Sweep::rate(const RatingChoice choice) {
  StepRecord& r = profiles[current].steps[step];
  switch (choice) {
    case RatingChoice::REPEAT:
      return Next::RUN_STEP;
    case RatingChoice::CLEAN:
      r.rating = Rating::CLEAN;
      break;
    case RatingChoice::TOLERABLE:
      r.rating = Rating::TOLERABLE;
      break;
    case RatingChoice::UNREADABLE:
      r.rating = Rating::UNREADABLE;
      break;
    case RatingChoice::UNREADABLE_SKIP:
      r.rating = Rating::UNREADABLE;
      skipAfterResidue = true;
      break;
  }
  if (asksResidue(STEPS[step])) return Next::ASK_RESIDUE;
  r.status = StepStatus::DONE;
  if (skipAfterResidue) {
    skipAfterResidue = false;
    if (step + 1u < STEP_COUNT) {
      skipRest(static_cast<uint8_t>(step + 1), StepStatus::SKIPPED);
      profiles[current].skippedFrom = static_cast<int8_t>(step + 1);
      return endProfile(ProfileStatus::SKIPPED_PART_WAY);
    }
  }
  return advance();
}

Next Sweep::residue(const ResidueChoice choice) {
  StepRecord& r = profiles[current].steps[step];
  if (choice == ResidueChoice::REPEAT) {
    r.rating = Rating::NONE;  // the step runs again and is rated again
    skipAfterResidue = false;
    return Next::RUN_STEP;
  }
  r.residue = choice == ResidueChoice::YES ? Residue::YES : Residue::NO;
  r.status = StepStatus::DONE;
  if (skipAfterResidue) {
    skipAfterResidue = false;
    if (step + 1u < STEP_COUNT) {
      skipRest(static_cast<uint8_t>(step + 1), StepStatus::SKIPPED);
      profiles[current].skippedFrom = static_cast<int8_t>(step + 1);
      return endProfile(ProfileStatus::SKIPPED_PART_WAY);
    }
  }
  return advance();
}

Next Sweep::stopped(const StopChoice choice) {
  if (choice == StopChoice::REPEAT) {
    haveStoppedFigures = false;
    return Next::RUN_STEP;
  }
  StepRecord& r = profiles[current].steps[step];
  r.status = StepStatus::STOPPED;
  if (haveStoppedFigures) {
    r.figures = stoppedFigures;
    r.hasFigures = true;
  }
  haveStoppedFigures = false;
  skipRest(static_cast<uint8_t>(step + 1), StepStatus::SKIPPED);
  profiles[current].skippedFrom = static_cast<int8_t>(step);
  profiles[current].status = ProfileStatus::SKIPPED_PART_WAY;
  if (choice == StopChoice::SKIP_PROFILE) return Next::PROFILE_SUMMARY;
  endRun(RunStatus::ENDED_BY_OWNER);
  return Next::RUN_SUMMARY;
}

Next Sweep::summary(const SummaryChoice choice) {
  if (choice == SummaryChoice::NEXT) return nextOrSummary();
  endRun(nextProfile() != nullptr ? RunStatus::ENDED_BY_OWNER : RunStatus::COMPLETE);
  return Next::RUN_SUMMARY;
}

Next Sweep::refuse() {
  ProfileRecord& p = profiles[current];
  p.status = ProfileStatus::NOT_ALLOWED;
  p.reason = NotRunReason::REFUSED;
  for (StepRecord& r : p.steps) {
    r = StepRecord{};
    r.status = StepStatus::SKIPPED;
  }
  return nextOrSummary();
}

void Sweep::abandon(const RunStatus why) {
  if (!running) return;
  ProfileRecord& p = profiles[current];
  if (p.status == ProfileStatus::RUNNING) {
    // Steps already answered keep their records; the step on screen and the rest did not finish.
    StepRecord& r = p.steps[step];
    if (r.status == StepStatus::PENDING && haveStoppedFigures) {
      r.status = StepStatus::STOPPED;  // stopped by the owner, the stop prompt not answered
      r.figures = stoppedFigures;
      r.hasFigures = true;
    }
    const uint8_t from = r.status == StepStatus::PENDING ? step : static_cast<uint8_t>(step + 1);
    if (from < STEP_COUNT) {
      skipRest(from, StepStatus::SKIPPED);
      p.skippedFrom = static_cast<int8_t>(from);
      p.status = ProfileStatus::SKIPPED_PART_WAY;
    } else {
      p.status = ProfileStatus::DONE;
    }
  }
  for (size_t i = current + 1u; i < PROFILE_COUNT; ++i) {
    if (profiles[i].status == ProfileStatus::PENDING) {
      profiles[i].status = ProfileStatus::NOT_RUN;
      profiles[i].reason = NotRunReason::INTERRUPTED;
      for (StepRecord& r : profiles[i].steps) r.status = StepStatus::SKIPPED;
    }
  }
  running = false;
  runStatus = why;
}

Next Sweep::advance() {
  if (step + 1u < STEP_COUNT) {
    ++step;
    return Next::RUN_STEP;
  }
  return endProfile(ProfileStatus::DONE);
}

Next Sweep::endProfile(const ProfileStatus status) {
  profiles[current].status = status;
  return Next::PROFILE_SUMMARY;
}

Next Sweep::nextOrSummary() {
  for (size_t i = current + 1u; i < PROFILE_COUNT; ++i) {
    if (profiles[i].status == ProfileStatus::PENDING) {
      current = static_cast<uint8_t>(i);
      step = 0;
      profiles[current].status = ProfileStatus::RUNNING;
      return Next::RUN_STEP;
    }
  }
  endRun(RunStatus::COMPLETE);
  return Next::RUN_SUMMARY;
}

void Sweep::skipRest(const uint8_t from, const StepStatus first) {
  for (size_t i = from; i < STEP_COUNT; ++i) {
    StepRecord& r = profiles[current].steps[i];
    if (r.status == StepStatus::PENDING) r.status = first;
  }
}

void Sweep::endRun(const RunStatus why) {
  for (size_t i = current + 1u; i < PROFILE_COUNT; ++i) {
    if (profiles[i].status == ProfileStatus::PENDING) {
      profiles[i].status = ProfileStatus::NOT_RUN;
      profiles[i].reason = NotRunReason::ENDED_BY_OWNER;
      for (StepRecord& r : profiles[i].steps) r.status = StepStatus::SKIPPED;
    }
  }
  running = false;
  runStatus = why;
}

// ---------------------------------------------------------------------------------------------

uint32_t estimateProfileSeconds(const uint32_t updateMs, const uint32_t clearMs, const uint16_t softLimit,
                                const uint32_t ownerSeconds) {
  constexpr uint64_t DWELL_MS = 60;  // rsvp::DEFAULT_MIN_DWELL_MS
  const uint64_t limit = softLimit == 0 ? 1 : softLimit;
  uint64_t ms = 0;
  for (const StepSpec& s : STEPS) {
    ms += clearMs;  // the clearing refresh each step starts from
    switch (s.kind) {
      case StepKind::FULL_SCREEN:
        ms += static_cast<uint64_t>(s.count) * updateMs;
        break;
      case StepKind::CLEARING:
        ms += static_cast<uint64_t>(s.count) * clearMs;
        break;
      case StepKind::WORD_BAND:
      case StepKind::PLAYBACK:
      case StepKind::ENDURANCE: {
        const uint64_t words = s.wordsPerUpdate == 0 ? 1 : s.wordsPerUpdate;
        const uint64_t frames = (s.count + words - 1) / words;
        const uint64_t paced = s.kind == StepKind::WORD_BAND ? 0 : 60000u * words / s.wpm;
        const uint64_t refresh = updateMs + DWELL_MS;
        ms += frames * (paced > refresh ? paced : refresh) + (frames / limit) * clearMs;
        break;
      }
    }
    if (s.rated) ms += static_cast<uint64_t>(ownerSeconds) * 1000u;
  }
  return static_cast<uint32_t>((ms + 500) / 1000);
}

uint16_t runNumberOf(const std::string_view name) {
  if (name.size() < 5 || name[4] != '-') return 0;
  unsigned value = 0;
  for (size_t i = 0; i < 4; ++i) {
    if (name[i] < '0' || name[i] > '9') return 0;
    value = value * 10u + static_cast<unsigned>(name[i] - '0');
  }
  return static_cast<uint16_t>(value);
}

void resultFileName(const uint16_t run, const ResultKind kind, char* out, const size_t size) {
  snprintf(out, size, "%04u-%s.json", static_cast<unsigned>(run), kind == ResultKind::BENCHMARK ? "bench" : "controls");
}

}  // namespace devicetest

#endif  // CROSSPOINT_RSVP_ENABLED
