#pragma once

// The per-book state file, `<book file name>.rsvp-state.json` beside the book (docs/PLAN.md sections 4
// and 7), read and written through lib/CheckpointCore's StateCodec. RSVP (task R4b) writes the
// overrides and the reading position; the checkpoint tasks (C3, C5) add their edits through
// editBookState(), which keeps every member an edit does not touch (grades, the checkpoint in
// progress, book_sha256).
//
// Memory: each call allocates one checkpoint::BookState (78 KB, almost all of it the grade table; on
// the Picco the allocator places a block this size in PSRAM) and a buffer the size of the file, both
// freed before it returns. Encoding streams to the file through a 128-byte buffer. Call from one task
// at a time; none of this runs per word.

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <BookState.h>
#include <CheckpointIndex.h>

#include <string>

#include "BookStateRules.h"

namespace reading {

// The book_sha256 of a book without checkpoint data: none.
inline constexpr const char* NO_BOOK_SHA256 = "";

// The open book as its state file sees it (ReadingSession::identity()): the checkpoint file's
// book.source_sha256 and index for a prepared book; NO_BOOK_SHA256 and null for any other. Every read
// applies checkpoint::applyBookChangeRule() for it (BookStateRules.h).
struct BookIdentity {
  const char* sha256 = NO_BOOK_SHA256;
  const checkpoint::CheckpointIndex* index = nullptr;
};

// `<bookPath>.rsvp-state.json`.
std::string bookStatePath(const std::string& bookPath);

// The book has a state file: the file, or a complete `<file>.tmp` that a power cut left before its
// rename (below). No allocation: a book without one opens without reading anything.
bool bookStateExists(const std::string& bookPath);

// Reads the book's state into `out` (a fresh state unless the result is Read). Never changes the file.
// When the file is missing but `<file>.tmp` is there and reads as a valid state, that is read instead:
// a write is cut between removing the old file and renaming the new one only after the new one was
// written and closed in full (logged as recovered; the next write renames it into place).
StateLoad loadBookState(const std::string& bookPath, const BookIdentity& book, checkpoint::BookState& out);

// Writes `state` as the whole file, through `<file>.tmp` and a rename (as editBookState() does). For a
// caller that holds the complete state already, read from the file (task C5: the restore at open,
// which keeps its state in the session's checkpoint block): no second state is allocated.
bool writeBookState(const std::string& bookPath, const checkpoint::BookState& state);

// Changes the state passed to it; `context` is the caller's.
using StateEdit = void (*)(checkpoint::BookState& state, const void* context);

// Reads the file, applies `edit` and writes the result to `<path>.tmp`, which then replaces the file.
// A file that is invalid or unreadable is first moved aside to `<path>.bad` (replacing an older
// one) and the edit applies to a fresh state. False, with the file untouched, when memory is short;
// false when the write fails (the old file stays, unless the final rename was cut short).
bool editBookState(const std::string& bookPath, const BookIdentity& book, StateEdit edit, const void* context);

}  // namespace reading

#endif  // CROSSPOINT_RSVP_ENABLED
