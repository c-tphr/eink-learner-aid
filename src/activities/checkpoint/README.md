# src/activities/checkpoint

**CheckpointActivity** (`docs/PLAN.md` sections 4 and 7): shows a checkpoint prompt, reveals the answer, records the grade, offers the reread choice and the skip actions. It drives `checkpoint::CheckpointFlow` from `lib/CheckpointCore`.

Filled by task C3 (#27), then extended by C4 (#28).

Everything here is compiled only with the RSVP feature flag: every file includes `RsvpFeature.h` and wraps its code in `#if CROSSPOINT_RSVP_ENABLED` (see `docs/ARCHITECTURE-RSVP.md`). `CheckpointActivity.cpp` is a placeholder until then.
