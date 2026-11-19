# src/activities/rsvp

**RsvpActivity** (`docs/PLAN.md` sections 4 and 6): draws the word band, the pause screen and the RSVP settings, and handles touch and keys. It drives `rsvp::Scheduler` from `lib/RsvpCore` through a `DisplayProfile`.

Filled by task R4a (#21), then extended by R4b (#22) and R5 (#23).

Everything here is compiled only with the RSVP feature flag: every file includes `RsvpFeature.h` and wraps its code in `#if CROSSPOINT_RSVP_ENABLED` (see `docs/ARCHITECTURE-RSVP.md`). `RsvpActivity.cpp` is a placeholder until then.
