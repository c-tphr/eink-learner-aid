#pragma once

// The checkpoint and reread state machine (docs/PLAN.md section 7, "Per-prompt
// flow"). It owns no storage: what must survive a restart lives in the
// BookState it was given (active checkpoint, grades, position); what may be
// lost (whether the answer is revealed, the choice after Incorrect) lives
// here. README.md has the state diagram and the event/state table.

#include <cstdint>

#include "BookState.h"
#include "CheckpointIndex.h"
#include "CheckpointTrigger.h"
#include "CheckpointTypes.h"
#include "StateRestore.h"

namespace checkpoint {

enum class FlowState : uint8_t {
  Idle,             // ordinary reading, no checkpoint presented yet
  PromptHidden,     // step 1: prompt shown, answer hidden
  AnswerShown,      // step 2: reference answer revealed
  IncorrectChoice,  // step 4: reread in RSVP, reread in page view, or continue
  Rereading,        // step 5: reading the section again
  Finished,         // step 6: done or skipped; reading resumes, waiting
};
inline constexpr uint8_t FLOW_STATE_COUNT = 6;

enum class FlowEvent : uint8_t {
  Reveal,
  Correct,
  Incorrect,
  SkipPrompt,
  SkipCheckpoint,
  RereadRsvp,
  RereadPage,
  Continue,
  ReturnEarly,
  RereadReachedEnd,
};
inline constexpr uint8_t FLOW_EVENT_COUNT = 10;

const char* flowStateName(FlowState state);
const char* flowEventName(FlowEvent event);

// What the caller must persist before the transition shows on screen.
enum class Save : uint8_t {
  No,        // nothing in the state file changed
  Position,  // only the reread position moved: save at the normal position cadence
  Now,       // write the state file before showing the next screen
};

enum class Refusal : uint8_t {
  None,
  InvalidInState,      // the event is not accepted in the current state
  BadStart,            // start(): not a Fire step, or a checkpoint without prompts
  BadRestore,          // restore(): the plan is not Reread/Prompt or does not match the state
  PromptIdUnreadable,  // the prompt id could not be read from the checkpoint file
  GradeTableFull,      // no room for a first grade even after dropping unknown ids
};

struct Transition {
  bool accepted = false;
  Refusal refusal = Refusal::None;
  Save save = Save::No;
  FlowState state = FlowState::Idle;  // state after the call
  bool gradeRecorded = false;         // this event stored the prompt's first grade
  bool leftSection = false;           // rereadMoved(): the move left the section; treated as Return early
  // The checkpoint ended. Show `resume` (also now state.position) in its mode
  // and wait: RSVP stays paused until Play, page view until the next action.
  bool finished = false;
  Place resume;
};

class CheckpointFlow {
 public:
  CheckpointFlow(BookState& state, const CheckpointIndex& checkpoints) : book(state), index(checkpoints) {}
  CheckpointFlow(const CheckpointFlow&) = delete;
  CheckpointFlow& operator=(const CheckpointFlow&) = delete;

  // A trigger fired while reading in `mode`: present prompt 0 of its
  // checkpoint. Accepted in Idle and Finished.
  Transition start(const Step& fire, ReadingMode mode);

  // Reopen: continue the checkpoint in `state.active` as planRestore() said.
  // Accepted in Idle and Finished; gives Rereading or PromptHidden.
  Transition restore(const RestorePlan& plan);

  // A reader action. `at` is the caller's clock ("YYYY-MM-DDTHH:MM:SS") for
  // Correct and Incorrect. A refused event changes nothing.
  Transition handle(FlowEvent event, const char* at = nullptr);

  // The reading position moved during a reread. Ordinary forward reading past
  // the section's last word returns to the prompt, as RereadReachedEnd does;
  // any other move out of the section returns to it as Return early does; a
  // move inside it is recorded as the reread place.
  Transition rereadMoved(const Place& to, NavSource source);

  static bool accepts(FlowState state, FlowEvent event);

  FlowState state() const { return current; }
  uint16_t checkpoint() const { return checkpointNumber; }
  uint16_t promptIndex() const { return book.active.promptIndex; }
  uint16_t promptCount() const;
  const SectionRange& section() const { return range; }
  // Pass to the trigger functions: the section being reread, else NO_INDEX.
  uint16_t rereadSection() const { return current == FlowState::Rereading ? range.section : NO_INDEX; }
  // The current prompt's stored first grade, shown as "already recorded" on a
  // later pass. Ungraded also when the id cannot be read.
  Grade storedGrade(Timestamp* at = nullptr) const;

 private:
  Transition refuse(Refusal refusal) const;
  Transition moved(FlowState to, Save save);
  Transition advance();
  Transition finish();
  Transition backToPrompt(bool leftSection);
  Refusal grade(Grade g, const char* at, bool& recorded);

  BookState& book;
  const CheckpointIndex& index;
  FlowState current = FlowState::Idle;
  uint16_t checkpointNumber = NO_INDEX;
  SectionRange range;
};

}  // namespace checkpoint
