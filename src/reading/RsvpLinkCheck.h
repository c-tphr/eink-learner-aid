#pragma once

#include "RsvpFeature.h"

// Temporary start-up check that RsvpCore and CheckpointCore (and, since E3, DisplayProfile) are
// built and linked into this firmware. ReadingSession (task C2, #26) replaces it and its call in main.cpp.
#if CROSSPOINT_RSVP_ENABLED
void runRsvpLinkCheck();
#else
inline void runRsvpLinkCheck() {}
#endif
