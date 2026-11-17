#pragma once

// The exposure limits of the trial profiles (plan section 6, "Clearing" and "Safety limits"),
// enforced by every backend on every refresh, so no caller can skip them. Portable.
//
// The rules, in terms of what the backend runs for each request:
//
// 1. Trial waveforms are only for word updates. A screen update (anything not marked as a word:
//    the pause screen, a highlight move, "Preparing...") and a clear never run one.
// 2. A trial run starts from a clearing refresh: the first trial word update after anything other
//    than a clear (or after start-up) runs as a clearing refresh instead. This also keeps the
//    driver's own promotions (first paint, after grayscale content, after a wake resync; S2
//    section 8) on the stock path.
// 3. A trial run is bounded: once the profile's hard clearing limit of consecutive trial word
//    updates has run, the next trial word update runs as a clearing refresh instead. The word
//    still appears (the clearing refresh shows the same frame buffer), the residue is wiped, and
//    the run starts again from 0. The scheduler normally clears first (same limit), so this only
//    acts when a caller ignores the limits.
// 4. After a trial word update, whatever comes next that is not a trial word update of the same
//    profile (a screen update when playback pauses, a stock word update, a word update with
//    another trial) runs as a clearing refresh instead: a trial run always ends with one.
//
// Why "run a clearing refresh instead" and not "refuse and fall back to a stock update" at the
// hard limit: a stock fast update is differential too and does not remove the residue the trial
// left, so falling back would let the run go on unbounded (stock words after it would still sit on
// trial residue, and trial words would resume after them). A clearing refresh is the only refresh
// that ends the exposure, and it shows the requested frame, so nothing is dropped; the cost is one
// slow update (about 1 s on DEPG0397) where a caller broke the limit.

#include <cstdint>

#include "DisplayProfiles.h"

namespace displayprofile {

// What the caller asked for.
enum class RefreshRequest : uint8_t {
  SCREEN,  // DisplayBackend::update() with UpdateKind::SCREEN (the default)
  WORD,    // DisplayBackend::update() with UpdateKind::WORD (ProfileDisplaySink::showFrame)
  CLEAR,   // DisplayBackend::clear()
};

// What the backend runs.
enum class RefreshAction : uint8_t {
  STOCK_UPDATE,  // the stock profile's word update
  TRIAL_UPDATE,  // a word update with the trial's waveform
  CLEAR,         // a clearing refresh (the stock HALF refresh, any table released)
};

// Why a request did not run as asked.
enum class Promotion : uint8_t {
  NONE,
  NOT_CLEARED,      // rule 2: a trial word update not preceded by a clear
  HARD_LIMIT,       // rule 3
  RESIDUE,          // rule 4: a screen or stock update after trial word updates
  PROFILE_CHANGED,  // rule 4: a trial word update after another trial's run
};

// "none", "not-cleared", "hard-limit", "residue", "profile-changed".
const char* promotionName(Promotion promotion);

struct RefreshDecision {
  RefreshRequest request = RefreshRequest::SCREEN;
  RefreshAction action = RefreshAction::STOCK_UPDATE;
  ProfileId waveform = ProfileId::STOCK;  // the profile whose word waveform runs (TRIAL_UPDATE only)
  Promotion promotion = Promotion::NONE;
};

class TrialExposure {
 public:
  // What to run for `request`, when the profile in force for word updates is `profile` (the
  // selected profile if the allow rule accepts it now, otherwise STOCK). Does not change state.
  RefreshDecision decide(RefreshRequest request, ProfileId profile) const;
  // Records that `decision` ran.
  void record(const RefreshDecision& decision);

  // Consecutive trial word updates since the last clearing refresh.
  uint16_t trialRun() const { return run; }
  // A trial word update was the last refresh: the next refresh that is not one is a clear.
  bool residuePending() const { return base == Base::TRIAL; }
  // Requests that ran as clearing refreshes because of rules 2 to 4, since construction.
  uint32_t promotions() const { return promoted; }

 private:
  enum class Base : uint8_t {
    UNKNOWN,  // since start-up, before the first clear: rule 2 applies
    CLEARED,  // the last refresh was a clear
    STOCK,    // the last refresh was a stock update
    TRIAL,    // the last refresh was a trial word update with `runProfile`
  };
  Base base = Base::UNKNOWN;
  ProfileId runProfile = ProfileId::STOCK;
  uint16_t run = 0;
  uint32_t promoted = 0;
};

}  // namespace displayprofile
