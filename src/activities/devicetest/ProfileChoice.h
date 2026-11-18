#pragma once

// "Choose a profile" and the Settings row "Speed read display" (task D3b; docs/PLAN.md section 6,
// "Outcome"): which profiles the owner may choose, what the choice stores, and which profiles ordinary
// reading may then select. Portable: host-tested in test/device_test for every panel.
//
// Two stored values (CrossPoint's settings file, persisted only):
// - rsvpDisplayProfile, the byte of lib/DisplayProfileCore/TrialUnlock.h: the unlocked trials and the
//   chosen profile. The benchmark unlocks the trials it runs (task D3a); the choice clears every
//   unlock bit except the chosen trial's and the kept trials'.
// - rsvpProfilesKept, a TrialMask: the trials the owner kept selectable (the chosen one included).
//   The benchmark never changes it, so a later benchmark run, which unlocks every trial it runs again,
//   does not make them selectable for reading. Only a choice changes it.
// Choosing Stock and keeping nothing stores 0 and 0: the factory state.

#include <DisplayProfiles.h>
#include <PanelRecords.h>
#include <TrialUnlock.h>

#include <cstddef>
#include <cstdint>

#include "BenchPlan.h"

namespace devicetest {

using displayprofile::TrialMask;
using displayprofile::TrialSetting;

// A list of profiles in table order, Stock first.
struct ProfileList {
  ProfileId ids[displayprofile::PROFILE_COUNT] = {};
  uint8_t count = 0;
  bool contains(ProfileId id) const;
  // The position of `id`, or `fallback` when it is not in the list.
  uint8_t indexOf(ProfileId id, uint8_t fallback = 0) const;
};

// --- Choose a profile ------------------------------------------------------------------------------

// Stock, then each trial that is unlocked in `setting` and that the allow rule accepts on `panel` now
// (inverted: none), as DisplayBackend::allows() would.
ProfileList choiceCandidates(const TrialSetting& setting, PanelId panel, bool inverted);

// The trials of `candidates` that may be kept besides `chosen`.
TrialMask keepableTrials(const ProfileList& candidates, ProfileId chosen);

struct ChoiceOutcome {
  uint8_t setting = 0;   // the new rsvpDisplayProfile
  TrialMask kept = 0;    // the new rsvpProfilesKept
  TrialMask locked = 0;  // trials unlocked before and locked again now
};
// The stored values after the owner chose `chosen` and kept `keep` (other trials; anything else in
// it is ignored): only the chosen trial and the kept ones stay unlocked, and they are what ordinary
// reading may select.
ChoiceOutcome applyChoice(uint8_t settingBefore, ProfileId chosen, TrialMask keep);

// --- The Settings row "Speed read display" ---------------------------------------------------------

// Shown only when the owner kept at least one trial (or chose one) and that trial is still unlocked:
// never with the factory byte 0, and not after a benchmark alone.
bool speedReadRowVisible(uint8_t setting, TrialMask kept);

struct SpeedReadOptions {
  ProfileList list;     // Stock, then the kept trials that are unlocked and made for this panel
  uint8_t current = 0;  // the row of the profile RSVP plays with (Stock when the chosen one is not offered)
};
// The row's options on `panel`. Inversion is left out: night mode is a passing state, and RSVP itself
// falls back to Stock while the screen is inverted (DisplayBackend::allows()).
SpeedReadOptions speedReadOptions(uint8_t setting, TrialMask kept, PanelId panel);

// The byte after the owner picked option `id` in the row: only `chosen` changes, never the unlock
// bits. Unchanged when `id` is not offered.
uint8_t selectSpeedReadProfile(uint8_t setting, TrialMask kept, PanelId panel, ProfileId id);

// --- What the benchmark said, for the choice ------------------------------------------------------

// The rated steps of one profile in the newest benchmark results file (BenchPlan.h, STEPS 3 to 9).
inline constexpr size_t RATED_STEP_COUNT = 7;
struct ProfileRatings {
  bool found = false;                     // the file has this profile with at least one rating or a 240 WPM figure
  Rating ratings[RATED_STEP_COUNT] = {};  // playback-120-1 ... playback-240-3, endurance
  Residue residue = Residue::NONE;
  bool hasDelivered240 = false;
  uint32_t delivered240Tenths = 0;  // playback-240-1's delivered WPM
};
struct BenchRatings {
  char file[24] = {};  // "0003-bench.json"; empty when no benchmark file could be read
  ProfileRatings profiles[displayprofile::PROFILE_COUNT];
};
// Index into ProfileRatings::ratings of a step key ("playback-180-1" -> 1), or -1.
int ratedStepIndex(const char* stepKey);
// "6 clean, 1 tolerable" (only the ratings given), or "not rated".
void ratingCounts(const ProfileRatings& r, char* out, size_t size);

}  // namespace devicetest
