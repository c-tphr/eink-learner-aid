# src/reading

**ReadingSession** (`docs/PLAN.md` section 4): owns the reading position, the mode and the navigation source; decides when a checkpoint fires; saves and restores the per-book state. It connects `lib/RsvpCore`, `lib/CheckpointCore` and `lib/WordIndex` to the reader.

Filled by task C2 (#26), then extended by C4 (#28) and C5 (#29).

Everything here is compiled only with the RSVP feature flag: every file includes `RsvpFeature.h` and wraps its code in `#if CROSSPOINT_RSVP_ENABLED` (see `docs/ARCHITECTURE-RSVP.md`).

Now: `ReadingSession.cpp` is a placeholder. `RsvpLinkCheck.h/.cpp` is a temporary start-up check, called from `setup()` in `src/main.cpp`, that links every source file of RsvpCore and CheckpointCore into the firmware and logs one debug line. C2 removes both, and the call in `main.cpp`, when the real ReadingSession lands.
