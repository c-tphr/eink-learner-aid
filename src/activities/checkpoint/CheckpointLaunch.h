#pragma once

// The way into the checkpoint screens (task C3a): a caller that knows "this checkpoint of this book
// fired" creates the activity, pushes it, and reads how it ended from the shared Outcome once it has
// finished (in its result handler when it used startActivityForResult()). Until task C2 fires
// checkpoints, only the simulator-only entry (CheckpointSimEntry.h) calls this.

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <CheckpointTrigger.h>
#include <CheckpointTypes.h>

#include <cstdint>
#include <memory>
#include <string>

#include "CheckpointScreens.h"

class Activity;
class GfxRenderer;
class MappedInputManager;

namespace reading {
class CheckpointBook;
}

namespace checkpointentry {

struct Request {
  // A Fire step of the trigger rules (checkpoint::rsvpStep(), pageAdvance(), evaluateMove()): the
  // checkpoint, its section's word range and the first word after the break.
  checkpoint::Step fire;
  // The mode in use when it fired: where reading resumes, and the default reread after Incorrect.
  checkpoint::ReadingMode mode = checkpoint::ReadingMode::Page;
};

// How the checkpoint ended, filled when the activity finishes.
struct Outcome {
  checkpointui::End end = checkpointui::End::None;
  uint16_t checkpoint = checkpoint::NO_INDEX;  // index in the book's CheckpointIndex
  uint16_t promptIndex = 0;                    // the prompt shown last; after a reread request, the one to return to
  checkpoint::Place resume;                    // Finished, Skipped: the first word after the break, in Request::mode
  checkpoint::Place reread;                    // RereadRsvp, RereadPage: the section's first word, in the chosen mode
  bool saved = true;                           // every state file write of the checkpoint succeeded
};

// The activity for `request` over `book` (open, with the checkpoint data of the book at `bookPath`).
// It shows prompt 1 with its answer hidden and writes each transition to `<bookPath>.rsvp-state.json`
// before showing the next screen. Returns nullptr when out of memory (logged).
std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& input, std::string bookPath,
                                 std::shared_ptr<reading::CheckpointBook> book, const Request& request,
                                 std::shared_ptr<Outcome> outcome);

}  // namespace checkpointentry

#endif  // CROSSPOINT_RSVP_ENABLED
