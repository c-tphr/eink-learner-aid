#pragma once

// TEMPORARY (task E3): a simulator-only check of the display timing model, used by the scenarios
// display_timing_model and display_timing_model_zero until the RSVP activity exercises
// DisplayProfile itself; remove it (and its call in main.cpp) then.
//
// With CROSSPOINT_SIM_DISPLAY_SELFTEST=<n> (1..100) set, once Home has been on screen for half a
// second it draws a counter band n times through DisplayProfile's update(), then runs one clear(),
// logging each duration and the statistics, and lets the firmware carry on. The band stays on
// screen until Home next redraws. Without the variable it does nothing.
//
// Only simulator builds with the RSVP flag have it; everywhere else, the device included, the call
// in main.cpp is the empty inline function below.

#include "RsvpFeature.h"

class GfxRenderer;

#if CROSSPOINT_RSVP_ENABLED && defined(SIMULATOR)
// Called once per main-loop iteration, after activityManager.loop(), on the main task.
void runDisplaySelfTestIfRequested(GfxRenderer& renderer);
#else
inline void runDisplaySelfTestIfRequested(GfxRenderer&) {}
#endif
