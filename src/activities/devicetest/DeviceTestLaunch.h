#pragma once

// Settings' entry point to the device test screens (task D3a): one row, "Device test", first in
// Settings > Display, in RSVP builds only. Without the flag AVAILABLE is false and create() returns
// nothing, so Settings needs no #if of its own (docs/ARCHITECTURE-RSVP.md).

#include <memory>

#include "RsvpFeature.h"

class Activity;
class GfxRenderer;
class MappedInputManager;

namespace devicetest {

inline constexpr bool AVAILABLE = RSVP_FEATURE_ENABLED;

#if CROSSPOINT_RSVP_ENABLED
// The Device test menu; nullptr (and an error line) if its memory cannot be allocated.
std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& input);
#else
inline std::unique_ptr<Activity> create(GfxRenderer&, MappedInputManager&) { return nullptr; }
#endif

}  // namespace devicetest
