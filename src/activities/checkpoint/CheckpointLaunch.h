#pragma once

// The way into the checkpoint screens (task C3a): a caller that knows "this checkpoint of this book
// fired" creates the activity, pushes it, and reads how it ended from the shared Outcome once it has
// finished (in its result handler when it used startActivityForResult()). reading::ReadingSession
// calls this when ordinary forward reading fires a checkpoint (task C2), in RSVP and in page view.

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <BookState.h>
#include <CheckpointFlow.h>
#include <CheckpointTrigger.h>
#include <CheckpointTypes.h>

#include <cstdint>
#include <memory>
#include <string>

#include "CheckpointScreens.h"
#include "controls/ControlMap.h"

class Activity;
class GfxRenderer;
class MappedInputManager;

namespace reading {
class CheckpointBook;
}

namespace readingsession {
enum class RereadChoice : uint8_t;  // reading/ReadingSession.h
}

namespace checkpointentry {

// The checkpoint in progress: the flow and the state it changes (task C4). One heap block of about
// 78 KB (the grade table; PSRAM on the Picco), made by reading::ReadingSession when a checkpoint fires
// and kept by it until the checkpoint ends, so that the flow survives while the screens are closed for
// a reread. Everything a restart needs is in `state.active` (and the grades), which every transition
// writes to the state file.
struct Progress {
  explicit Progress(const checkpoint::CheckpointIndex& index) : flow(state, index) {}
  checkpoint::BookState state;
  checkpoint::CheckpointFlow flow;
};

struct Request {
  // A Fire step of the trigger rules (checkpoint::rsvpStep(), pageAdvance(), evaluateMove()): the
  // checkpoint, its section's word range and the first word after the break.
  checkpoint::Step fire;
  // The mode in use when it fired: where reading resumes, and the default reread after Incorrect.
  checkpoint::ReadingMode mode = checkpoint::ReadingMode::Page;
  // The reading position before the break, written with the checkpoint in progress when the activity
  // starts: a book closed while the checkpoint shows reopens before the break, where reading on fires
  // it again (restoring the prompt itself is task C5).
  bool hasBefore = false;
  checkpoint::Place before;
  // The flow and state the screens drive (ReadingSession's). In Idle or Finished the activity reads the
  // state file and starts the flow with `fire`; in PromptHidden it shows that prompt again, as after a
  // reread (task C4), without reading or writing anything first. Null: the activity ends as Failed.
  std::shared_ptr<Progress> progress;
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

// Writes what the accepted transition `t` changed in `state` to `<bookPath>.rsvp-state.json`
// (CheckpointSave.h) and logs `saved after=<reason> ...` (README.md, "Log lines"). `gradedPrompt`: the
// prompt the event was given for, when it may have stored a first grade; `place`, when given, is also
// written as the reading position. False when the write failed (logged as ok=0).
bool saveTransition(const std::string& bookPath, const reading::CheckpointBook& book,
                    const checkpoint::BookState& state, const checkpoint::Transition& t, const char* gradedPrompt,
                    const char* reason, const checkpoint::Place* place = nullptr);

// --- Rereading in page view (task C4; S4 row 31; RereadScreens.cpp).

// Draws the reread bar over the reader's status-bar row in the frame buffer (`bar`,
// controls::rereadBarLayout(): "Rereading this section" and the "Back to the question" button) and
// shows it with a fast refresh. Render task.
void drawRereadBar(GfxRenderer& renderer, const controls::RereadBarLayout& bar);

// The list a side-key hold opens during a page-view reread: "Back to the question" (the default),
// "Speed read from here", "Keep reading" (controls::REREAD_CHOICES). `chosen` is set when it finishes;
// the Back gesture leaves it as Keep reading. nullptr when out of memory.
std::unique_ptr<Activity> createRereadChoices(GfxRenderer& renderer, MappedInputManager& input,
                                              std::shared_ptr<readingsession::RereadChoice> chosen);

}  // namespace checkpointentry

#endif  // CROSSPOINT_RSVP_ENABLED
