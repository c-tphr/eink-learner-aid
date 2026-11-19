# src/display

**DisplayProfile** (`docs/PLAN.md` sections 4 and 6): named refresh strategies (stock and trial candidates), per-update timing and the clearing policy. It implements `rsvp::DisplaySink` from `lib/RsvpCore` on top of `HalDisplay`.

Filled by task E3 (#8, the simulator timing model), then D1 (#30, the stock profile on the device) and D2 (#31, trial profiles and their safety limits).

Everything here is compiled only with the RSVP feature flag: every file includes `RsvpFeature.h` and wraps its code in `#if CROSSPOINT_RSVP_ENABLED` (see `docs/ARCHITECTURE-RSVP.md`). `DisplayProfile.cpp` is a placeholder until then. A `HalDisplay` method added for a profile also needs a stub in the simulator (`scripts/simulator_patches/`, see `docs/BASE.md`).
