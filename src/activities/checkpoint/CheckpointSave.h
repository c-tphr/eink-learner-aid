#pragma once

// What a checkpoint transition writes to the book's state file (docs/PLAN.md section 7, "Grade rules"
// and "every transition saves state first"), and the grade time. Pure: host-tested in
// test/checkpoint_activity. The activity writes through reading::editBookState(), whose edit calls
// applyChange() on the state just read from the file, so members the checkpoint does not own
// (overrides, grades of other prompts) stay as the file has them.

#include <BookState.h>
#include <CheckpointFlow.h>
#include <GradeTable.h>

#include <cstddef>
#include <cstdint>

namespace checkpointui {

// The part of the state file one accepted transition changes.
struct StateChange {
  // The transition stored the prompt's first grade (Transition::gradeRecorded). Skips never do.
  bool gradeRecorded = false;
  char promptId[checkpoint::ID_BUFFER_BYTES] = {};
  checkpoint::Grade grade = checkpoint::Grade::Ungraded;
  checkpoint::Timestamp at = checkpoint::NO_TIMESTAMP;
  // The checkpoint in progress after the transition (cleared by a finish).
  bool hasActive = false;
  checkpoint::ActiveCheckpoint active;
  // A finish moved the reading position to the first word after the break.
  bool positionMoved = false;
  checkpoint::Place position;
};

// The change for transition `t`, read from the session's state after the flow made it. `promptId` is
// the prompt the event was given for (read before the event: Correct moves to the next prompt).
StateChange changeFor(const checkpoint::BookState& session, const checkpoint::Transition& t, const char* promptId);

// Applies `change` to `file`, a state just read from the file. A grade is recorded only as a first
// grade: a grade the file already has for that prompt is kept. A state without book_sha256 takes
// `bookSha256` (the rule of checkpoint::applyBookChangeRule()).
void applyChange(checkpoint::BookState& file, const StateChange& change, const char* bookSha256);

// The grade time "YYYY-MM-DDTHH:MM:SS" from a local calendar time (struct tm fields: years since
// 1900, months 0-11). False, with `out` empty, for a clock that is not set: a year before 2020 or
// after 2135 (the state file's last year). Then the grade is written without a time.
bool formatGradeTime(int tmYear, int tmMon, int tmMday, int tmHour, int tmMin, int tmSec,
                     char out[checkpoint::TIMESTAMP_CHARS + 1]);

}  // namespace checkpointui
