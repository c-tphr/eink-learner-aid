#pragma once

// TEMPORARY (task C3a): a simulator-only way to open the checkpoint screens before task C2 fires
// checkpoints. Task C3 (#27) removes it, and its call in main.cpp, when it wires the activity to
// C2's triggers.
//
// With CROSSPOINT_SIM_CHECKPOINT=<checkpoint id>[,rsvp|page] set (mode page when left out), once an
// EPUB is open in the reader with a page laid out, it opens the book's checkpoint data
// (reading::CheckpointBook), resolves the checkpoint's section through the chapter's word index,
// makes the Fire step that reading past the section's last word would make
// (checkpoint::rsvpStep() on that word), and pushes CheckpointActivity over the reader. Once per
// process. A book without checkpoint data or an unknown id logs an error line and does nothing.
//
// Only simulator builds with the RSVP flag have it; everywhere else, the device included, the call
// in main.cpp is the empty inline function below.

#include "RsvpFeature.h"

class GfxRenderer;
class MappedInputManager;

#if CROSSPOINT_RSVP_ENABLED && defined(SIMULATOR)
// Called once per main-loop iteration, after activityManager.loop(), on the main task.
void runCheckpointSimEntryIfRequested(GfxRenderer& renderer, MappedInputManager& input);
#else
inline void runCheckpointSimEntryIfRequested(GfxRenderer&, MappedInputManager&) {}
#endif
