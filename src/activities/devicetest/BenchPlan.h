#pragma once

// The display benchmark's plan and results (docs/PLAN.md section 10, "Benchmark sweep, per display
// profile"), and the decisions the owner's answers lead to. Portable: no Arduino, HAL or renderer;
// host-tested in test/device_test. DeviceTestActivity runs the series and draws the prompts; this
// file decides what comes next and holds what goes into the results file (BenchJson.h).
//
// Per profile, in this order (STEPS): 100 word-band updates with real words, 10 full-screen
// updates, 5 clearing refreshes, the 150-word passage in RSVP at 120, 180, 240 and 300 WPM with one
// word per update, the same passage at 240 WPM with 2 and with 3 words per update, and an endurance
// run of 300 consecutive word updates under the profile's clearing policy. Stock first, then each
// trial the panel allows, in table order (DisplayProfiles.h).

#include <DisplayProfiles.h>
#include <PanelRecords.h>
#include <TrialUnlock.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace devicetest {

using displayprofile::PanelId;
using displayprofile::ProfileId;

// --- The plan ---------------------------------------------------------------------------------

enum class StepKind : uint8_t { WORD_BAND, FULL_SCREEN, CLEARING, PLAYBACK, ENDURANCE };

struct StepSpec {
  StepKind kind;
  const char* key;         // stable id in the results file and the log ("playback-240-2")
  uint16_t count;          // updates, clearing refreshes, or passage words handed out
  uint16_t wpm;            // scheduler target (word band: WORD_BAND_WPM); 0 for the other series
  uint8_t wordsPerUpdate;  // scheduler steps only; 0 otherwise
  bool rated;              // the owner rates it: Clean, Tolerable or Unreadable
};

// The word-band series runs through the RSVP scheduler at its fastest pace, so consecutive updates
// are separated only by the 60 ms minimum dwell: the series measures the refresh, not the pacing.
inline constexpr uint16_t WORD_BAND_WPM = 1000;
inline constexpr uint16_t ENDURANCE_WPM = 240;

inline constexpr StepSpec STEPS[] = {
    {StepKind::WORD_BAND, "word-band", 100, WORD_BAND_WPM, 1, false},
    {StepKind::FULL_SCREEN, "full-screen", 10, 0, 0, false},
    {StepKind::CLEARING, "clearing", 5, 0, 0, false},
    {StepKind::PLAYBACK, "playback-120-1", 150, 120, 1, true},
    {StepKind::PLAYBACK, "playback-180-1", 150, 180, 1, true},
    {StepKind::PLAYBACK, "playback-240-1", 150, 240, 1, true},
    {StepKind::PLAYBACK, "playback-300-1", 150, 300, 1, true},
    {StepKind::PLAYBACK, "playback-240-2", 150, 240, 2, true},
    {StepKind::PLAYBACK, "playback-240-3", 150, 240, 3, true},
    {StepKind::ENDURANCE, "endurance", 300, ENDURANCE_WPM, 1, true},
};
inline constexpr size_t STEP_COUNT = sizeof(STEPS) / sizeof(STEPS[0]);

// "word-band", "full-screen", "clearing", "playback", "endurance".
const char* stepKindName(StepKind kind);
// Steps the scheduler plays (word band, playback, endurance); the others are plain series.
constexpr bool usesScheduler(const StepKind kind) {
  return kind == StepKind::WORD_BAND || kind == StepKind::PLAYBACK || kind == StepKind::ENDURANCE;
}
constexpr bool asksResidue(const StepSpec& step) { return step.kind == StepKind::ENDURANCE; }

// --- Owner's answers ----------------------------------------------------------------------------

enum class Rating : uint8_t { NONE, CLEAN, TOLERABLE, UNREADABLE };
enum class Residue : uint8_t { NONE, NO, YES };  // NONE: not asked (yet)

// The rating prompt's rows, in order; row 0 is the default.
enum class RatingChoice : uint8_t { REPEAT, CLEAN, TOLERABLE, UNREADABLE, UNREADABLE_SKIP };
inline constexpr uint8_t RATING_CHOICES = 5;
// The endurance run's residue prompt.
enum class ResidueChoice : uint8_t { REPEAT, NO, YES };
inline constexpr uint8_t RESIDUE_CHOICES = 3;
// The prompt after the owner stopped a series (a key or a touch during it).
enum class StopChoice : uint8_t { REPEAT, SKIP_PROFILE, END_RUN };
inline constexpr uint8_t STOP_CHOICES = 3;
// The end-of-profile summary: on to the next profile (or finish), or end the benchmark.
enum class SummaryChoice : uint8_t { NEXT, END_RUN };

const char* ratingName(Rating rating);     // "none", "clean", "tolerable", "unreadable"
const char* residueName(Residue residue);  // "none", "no", "yes"

// --- Results ------------------------------------------------------------------------------------

enum class StepStatus : uint8_t {
  PENDING,  // not reached (yet)
  UNRATED,  // ran to the end; the owner's rating is not given yet (or the run ended before it)
  DONE,     // ran to the end (and was rated, when rated)
  STOPPED,  // the owner stopped it part-way, then skipped the rest or ended the run; figures are partial
  SKIPPED,  // never ran: the owner skipped the rest of the profile or ended the run
};
enum class ProfileStatus : uint8_t {
  NOT_ALLOWED,       // the allow rule refuses it here; reason in NotRunReason
  PENDING,           // will run
  RUNNING,           // the sweep is on it
  DONE,              // every step done
  SKIPPED_PART_WAY,  // the owner skipped the rest of it
  NOT_RUN,           // the owner ended the benchmark before it
};
enum class NotRunReason : uint8_t {
  NONE,
  PANEL_UNRECOGNISED,  // no Picco panel recognised: every trial is off
  PANEL_NO_TABLES,     // SE0400NQW47: trial tables A to C were not designed for it
  SCREEN_INVERTED,     // night mode: trials are refused while the screen is inverted
  REFUSED,             // the backend refused select() at run time
  ENDED_BY_OWNER,      // NOT_RUN: the owner ended the benchmark
  INTERRUPTED,         // NOT_RUN: the device slept or the screen was closed before it
};
enum class RunStatus : uint8_t {
  RUNNING,         // written after every step; a file left so was cut short (crash, power loss)
  COMPLETE,        // every runnable profile finished (done or skipped part-way)
  ENDED_BY_OWNER,  // "End the benchmark"
  INTERRUPTED,     // the device went to sleep, or the screen was left, during the run
};

const char* stepStatusName(StepStatus status);        // "pending", "unrated", "done", "stopped", "skipped"
const char* profileStatusName(ProfileStatus status);  // "not-allowed", "pending", "running", "done", ...
const char* notRunReasonName(NotRunReason reason);    // "none", "panel-unrecognised", ...
const char* runStatusName(RunStatus status);          // "running", "complete", "ended-by-owner", "interrupted"

// One kind of refresh over one series, from displayprofile::RefreshStats after the backend's
// statistics were reset at the start of the series.
struct SeriesFigures {
  uint32_t count = 0;
  uint32_t medianMs = 0;
  uint32_t p95Ms = 0;
  uint32_t maxMs = 0;
  uint32_t minMs = 0;
  uint32_t meanMs = 0;
  uint32_t timeouts = 0;
};

// What the scheduler reports for a playback or endurance step (DeliveredWpmMeter, WordOrderAudit).
struct PlaybackFigures {
  uint32_t deliveredWpmTenths = 0;
  uint32_t wordsShown = 0;
  uint32_t frames = 0;
  uint32_t clearingRefreshes = 0;
  uint64_t activeMs = 0;
  bool inOrder = true;
  uint32_t wordsDrawn = 0;
  uint32_t segments = 0;
  uint32_t violations = 0;
  uint32_t gaps = 0;
  uint32_t repeats = 0;
};

// What one series measured.
struct StepFigures {
  SeriesFigures updates;
  SeriesFigures clears;
  uint32_t promotions = 0;  // requests the backend ran as clearing refreshes during the series
  uint32_t durationMs = 0;  // first display call to the end of the last
  bool hasPlayback = false;
  PlaybackFigures playback;
};

struct StepRecord {
  StepStatus status = StepStatus::PENDING;
  uint8_t attempts = 0;     // series run for this step ("Repeat step" adds one)
  bool hasFigures = false;  // the last attempt's figures are in `figures`
  StepFigures figures;
  Rating rating = Rating::NONE;
  Residue residue = Residue::NONE;
};

struct ProfileRecord {
  ProfileId id = ProfileId::STOCK;
  ProfileStatus status = ProfileStatus::PENDING;
  NotRunReason reason = NotRunReason::NONE;
  int8_t skippedFrom = -1;  // first step that did not finish, when SKIPPED_PART_WAY; -1 otherwise
  StepRecord steps[STEP_COUNT];
};

// --- The trials the benchmark may run, and the stored unlock value --------------------------------

// The trials profileAllowed() accepts on `panel` once unlocked (inverted: none).
displayprofile::TrialMask runnableTrials(PanelId panel, bool inverted);
// Why `id` cannot run here; NONE when it can (Stock always can).
NotRunReason notAllowedReason(ProfileId id, PanelId panel, bool inverted);

// The value stored when the benchmark starts (README.md, "What the benchmark stores"): the trials it
// is about to run are unlocked in addition to any already unlocked, so later device-test runs and
// task D3b's "Choose a profile" can use them; the chosen profile is left as it was (Stock unless D3b
// changed it), so ordinary reading stays on it.
displayprofile::TrialSetting unlockForBenchmark(const displayprofile::TrialSetting& before,
                                                displayprofile::TrialMask runnable);

// --- The sweep ----------------------------------------------------------------------------------

// What the activity shows next.
enum class Next : uint8_t {
  RUN_STEP,         // run step `step()` of profile `profile()` (a new attempt)
  ASK_RATING,       // the rating prompt for the step just run
  ASK_RESIDUE,      // the endurance run's residue prompt
  PROFILE_SUMMARY,  // the profile has ended (done or skipped part-way)
  RUN_SUMMARY,      // the benchmark has ended
};

class Sweep {
 public:
  // Fills `profiles` with every profile in table order (Stock first), marking the ones the allow
  // rule refuses here as NOT_ALLOWED with their reason. Returns the trials that will run.
  displayprofile::TrialMask plan(PanelId panel, bool inverted);
  // Starts at the first step of Stock.
  Next start();

  bool active() const { return running; }
  uint8_t profileIndex() const { return current; }
  uint8_t stepIndex() const { return step; }
  const ProfileRecord& profile() const { return profiles[current]; }
  const StepSpec& stepSpec() const { return STEPS[step]; }
  const StepRecord& stepRecord() const { return profiles[current].steps[step]; }
  // Profiles that ran or will run (status other than NOT_ALLOWED), and the index among them.
  uint8_t runnableCount() const;
  uint8_t runnableOrdinal() const;
  // The next profile that will run after the current one, or nullptr.
  const ProfileRecord* nextProfile() const;

  // A series of the current step ran to its end. Stores its figures (one more attempt).
  Next seriesDone(const StepFigures& figures);
  // The owner stopped the series part-way; `figures` is what it measured so far. The stop prompt
  // follows; nothing changes until stopped() is called.
  void seriesStopped(const StepFigures& figures);

  Next rate(RatingChoice choice);
  Next residue(ResidueChoice choice);
  Next stopped(StopChoice choice);
  Next summary(SummaryChoice choice);
  // The backend refused to select the current profile at run time (it should not: the benchmark
  // unlocks only what the allow rule accepts). It is recorded as not allowed (REFUSED) and the sweep
  // moves on to the next profile, or ends.
  Next refuse();
  // The run ends now (the activity closes, or the device sleeps): the current step and everything
  // after it are recorded as not run. Used for the file written on the way out.
  void abandon(RunStatus why);

  RunStatus status() const { return runStatus; }

  ProfileRecord profiles[displayprofile::PROFILE_COUNT];

 private:
  Next advance();
  Next endProfile(ProfileStatus status);
  Next nextOrSummary();
  void skipRest(uint8_t from, StepStatus first);
  void endRun(RunStatus why);

  uint8_t current = 0;
  uint8_t step = 0;
  bool running = false;
  bool skipAfterResidue = false;  // UNREADABLE_SKIP on the endurance step: skip once residue is answered
  bool haveStoppedFigures = false;
  StepFigures stoppedFigures;
  RunStatus runStatus = RunStatus::RUNNING;
};

// --- How long a profile takes ----------------------------------------------------------------

// Seconds one profile's sweep is expected to take on the panel, for the owner's planning (shown on
// the benchmark's first screen; the arithmetic is in README.md). `updateMs` and `clearMs` are one word
// update and one clearing refresh, transfer included; `ownerSeconds` is the owner's time per rated
// step (looking, then the rating prompt). Every step starts with a clearing refresh; series updates
// follow each other after the 60 ms dwell; playback holds each frame for its paced slot or the update
// plus the dwell, whichever is longer; a clearing refresh is counted every `softLimit` updates.
uint32_t estimateProfileSeconds(uint32_t updateMs, uint32_t clearMs, uint16_t softLimit, uint32_t ownerSeconds);
inline constexpr uint32_t OWNER_SECONDS_PER_RATING = 15;

// --- Results files ------------------------------------------------------------------------------

inline constexpr const char RESULTS_DIR[] = "/rsvp-bench";
enum class ResultKind : uint8_t { BENCHMARK, CONTROLS };
inline constexpr uint16_t MAX_RUN_NUMBER = 9999;

// The run number a results file name starts with ("0007-bench.json" -> 7), or 0 when the name is not
// one of ours (four digits, a hyphen).
uint16_t runNumberOf(std::string_view name);
// "0007-bench.json" or "0007-controls.json". `out` needs at least 24 bytes. The number is shared by
// both kinds and only grows (the largest number in the folder plus one), so names sort by the order
// the runs were made whether or not the device clock is set, and no run overwrites another.
void resultFileName(uint16_t run, ResultKind kind, char* out, size_t size);

}  // namespace devicetest
