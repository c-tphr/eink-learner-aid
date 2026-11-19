#pragma once

// RSVP reading and checkpoints (docs/ARCHITECTURE-RSVP.md). The build sets CROSSPOINT_RSVP=1 only
// for the Picco environments (platformio.ini) and simulator_picco (platformio.project.ini).
// Feature code tests CROSSPOINT_RSVP_ENABLED, always 0 or 1, from this header and nowhere else.
#if defined(CROSSPOINT_RSVP) && CROSSPOINT_RSVP
#define CROSSPOINT_RSVP_ENABLED 1
#else
#define CROSSPOINT_RSVP_ENABLED 0
#endif

inline constexpr bool RSVP_FEATURE_ENABLED = CROSSPOINT_RSVP_ENABLED != 0;
