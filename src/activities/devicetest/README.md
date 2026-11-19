# src/activities/devicetest

**DeviceTestActivity** (`docs/PLAN.md` sections 4 and 10): the benchmark sweep over display profiles, the ratings, the guided acceptance walk-through and the results files under `/rsvp-bench/`.

Filled by tasks D3a (#32) and D3b (#33).

Everything here is compiled only with the RSVP feature flag: every file includes `RsvpFeature.h` and wraps its code in `#if CROSSPOINT_RSVP_ENABLED` (see `docs/ARCHITECTURE-RSVP.md`). `DeviceTestActivity.cpp` is a placeholder until then.
