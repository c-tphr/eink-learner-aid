#pragma once

// The pure part of the checkpoint screens (docs/PLAN.md section 7; docs/spikes/S4-control-map.md rows
// 17-29): which screen shows each state of checkpoint::CheckpointFlow, its items in focus order (the
// binding table in src/controls/ControlMap.h), the default highlight, which flow event an item sends,
// paging of a long answer, and how a checkpoint ended. No renderer, input or file access:
// host-tested in test/checkpoint_activity.

#include <CheckpointFlow.h>
#include <CheckpointTypes.h>

#include <cstdint>

#include "controls/ControlMap.h"

namespace checkpointui {

enum class Screen : uint8_t {
  None,            // the flow shows nothing (Idle, Rereading, Finished): the activity ends
  Prompt,          // PromptHidden: the prompt, answer hidden
  Answer,          // AnswerShown: the reference answer, then the grades
  AfterIncorrect,  // IncorrectChoice: reread in RSVP or page view, or continue
};

Screen screenFor(checkpoint::FlowState state);

// "Prompt", "Answer", "Incorrect": the screen name in the [CTL] and [CPT] log lines.
const char* screenName(Screen screen);

// The screen's items and zones. Screen::None gives the prompt layout (never shown).
const controls::Screen& layoutOf(Screen screen);

// Index of the default highlight: Reveal, the answer box (never a grade), and after Incorrect the
// reread in the mode that was in use when the checkpoint appeared (plan section 7).
int defaultFocus(Screen screen, checkpoint::ReadingMode modeInUse);

// The flow event an item sends. False for the answer box, which pages the answer instead.
bool flowEventFor(controls::Item item, checkpoint::FlowEvent& event);

// Whether an item may be activated in `state`: its event is one CheckpointFlow::accepts(); the answer
// box is always active on the Answer screen.
bool offered(controls::Item item, checkpoint::FlowState state);

// --- Paging a long answer (S4 row 20). Parts hold `linesPerPart` wrapped lines each.

uint16_t partCount(uint32_t lines, uint16_t linesPerPart);  // at least 1
uint32_t partFirstLine(uint16_t part, uint16_t linesPerPart);
// Lines shown in `part`: linesPerPart, fewer in the last part.
uint32_t partLines(uint32_t lines, uint16_t part, uint16_t linesPerPart);

enum class PageMove : uint8_t {
  Cycle,     // a tap on the box or a key hold: the next part, after the last the first
  Next,      // a swipe up: the next part, stays on the last
  Previous,  // a swipe down: the previous part, stays on the first
};
// The part shown after `move`. A one-part answer never moves.
uint16_t movePart(uint16_t part, uint16_t parts, PageMove move);

// --- How a checkpoint ended, for the caller (src/activities/checkpoint/CheckpointLaunch.h).

enum class End : uint8_t {
  None,        // not ended (or left another way: Home gesture, sleep; the state file has the checkpoint)
  Finished,    // the last prompt was graded, continued or skipped
  Skipped,     // Skip checkpoint
  RereadRsvp,  // Reread in RSVP: the prompt waits (task C4 rereads)
  RereadPage,  // Reread in page view
  Failed,      // could not start (no memory, or the flow refused the request)
};

const char* endName(End end);

// The end that `event`'s accepted transition `t` means, or End::None when the checkpoint goes on.
End endFor(checkpoint::FlowEvent event, const checkpoint::Transition& t);

}  // namespace checkpointui
