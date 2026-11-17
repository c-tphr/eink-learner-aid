#pragma once

// Seeds the RSVP control defaults from the binding table before the settings file is read, the way
// main.cpp seeds the touch boards' reader menu style: fromJson() keeps a seeded value only when the
// file has no value of its own, so a saved choice always wins. Does nothing without the RSVP flag.

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include "ControlMap.h"
#include "CrossPointSettings.h"

inline void seedRsvpControlDefaults() { SETTINGS.longPressButtonBehavior = controls::KEYS.longPressDefault; }

#else

inline void seedRsvpControlDefaults() {}

#endif
