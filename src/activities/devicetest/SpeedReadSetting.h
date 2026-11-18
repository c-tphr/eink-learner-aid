#pragma once

// Settings' row "Speed read display" (task D3b; docs/PLAN.md section 6, "Outcome"): in Settings >
// Display, after "Device test", and only once the owner has kept a trial display profile in the
// device test's "Choose a profile". It offers Stock and the kept trials made for this panel, and
// changes only which one RSVP plays with (ProfileChoice.h has the rules, host-tested). Without the
// flag visible() is false and the rest are empty inline functions, so Settings needs no #if of its
// own (docs/ARCHITECTURE-RSVP.md).

#include <cstdint>

#include "RsvpFeature.h"

namespace speedreadsetting {

// The row's options, in order: profile display names ("Stock", "Trial B") and their ids.
struct Options {
  static constexpr int MAX = 5;
  const char* names[MAX] = {};
  uint8_t ids[MAX] = {};
  int count = 0;
  int current = 0;  // the option RSVP plays with now
};

#if CROSSPOINT_RSVP_ENABLED
bool visible();
Options options();
// The name of the profile RSVP plays with: the row's value.
const char* currentName();
// Stores option `index` of `options` (the chosen profile only) and saves the settings file.
void select(const Options& options, int index);
#else
inline bool visible() { return false; }
inline Options options() { return {}; }
inline const char* currentName() { return ""; }
inline void select(const Options&, int) {}
#endif

}  // namespace speedreadsetting
