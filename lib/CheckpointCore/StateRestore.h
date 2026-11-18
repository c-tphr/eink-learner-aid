#pragma once

// What to show when a book opens (docs/PLAN.md section 7: "an active reread,
// then an active prompt with its answer hidden, then the plain reading
// position"), and what to keep of a state file written for another edition of
// the book. README.md states both rules.

#include <cstdint>

#include "BookState.h"
#include "CheckpointIndex.h"
#include "CheckpointTypes.h"

namespace checkpoint {

struct BookChange {
  bool shaMismatch = false;      // the state's book_sha256 differs from the open book's
  bool adoptedSha = false;       // the state had none and now carries the open book's
  uint16_t gradesDropped = 0;    // grades of prompt ids the open book does not have
  bool positionDropped = false;  // the saved position was cleared
  bool activeDropped = false;    // the checkpoint in progress was cleared
};

// The rule for a decoded state and the open book, applied to `state`:
// - No checkpoint data (`index` null or closed): nothing changes, so grades
//   survive a book that opens without its checkpoint file.
// - The state has no book_sha256: it takes the book's.
// - Same book_sha256: nothing changes.
// - Different book_sha256 (another source EPUB): grades for prompt ids the
//   book still has are kept and the rest dropped; the position and any
//   checkpoint in progress are dropped, because word positions may differ;
//   overrides are kept; book_sha256 becomes the book's.
BookChange applyBookChangeRule(BookState& state, const CheckpointIndex* index);

enum class OpenView : uint8_t {
  Reread,     // reading the section again at `place`; the prompt waits
  Prompt,     // the prompt `state.active.promptIndex` with its answer hidden
  Position,   // ordinary reading at `place`
  BookStart,  // ordinary reading from the start of the book
};

// Why something saved was not restored. Bits of RestorePlan::notes.
enum RestoreNote : uint16_t {
  NOTE_NO_CHECKPOINT_DATA = 1u << 0,      // checkpoint in progress, but the book has no checkpoint data
  NOTE_UNKNOWN_CHECKPOINT = 1u << 1,      // its id is not in the index
  NOTE_PROMPT_OUT_OF_RANGE = 1u << 2,     // its prompt_index is not below the prompt count
  NOTE_SECTION_UNRESOLVED = 1u << 3,      // its section's anchors cannot be found
  NOTE_RESUME_OUTSIDE_BOOK = 1u << 4,     // its resume position is not in the book
  NOTE_REREAD_OUTSIDE_SECTION = 1u << 5,  // the reread position is not inside the section
  NOTE_POSITION_OUTSIDE_BOOK = 1u << 6,   // the reading position is not in the book
};

struct RestorePlan {
  OpenView view = OpenView::BookStart;
  uint16_t checkpoint = NO_INDEX;  // Reread, Prompt
  SectionRange section;            // Reread, Prompt: the checkpoint's section
  Place place;                     // Reread: reread place; Prompt: where reading resumes; Position: position
  uint16_t notes = 0;              // RestoreNote bits; 0 when everything saved was restored
};

// True when `position` names a word of the book: a known chapter and a word
// index no larger than its word count (equal means just after its last word).
bool positionInBook(const Position& position, BookLayout& layout);

// Decides what to show on open and removes from `state` what cannot be
// restored, so the next save is consistent. In order: an active reread whose
// position lies inside its section; else the active prompt; else the saved
// position; else the start of the book. A dropped checkpoint whose resume
// position is in the book becomes the reading position, so reading continues
// after its break. Call after applyBookChangeRule(); pass the result to
// CheckpointFlow::restore() for Reread and Prompt.
RestorePlan planRestore(BookState& state, const CheckpointIndex* index, BookLayout& layout);

}  // namespace checkpoint
