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

#include <string>

#include "BookStateRules.h"

namespace reading {

// The open book's book_sha256: the checkpoint file's book.source_sha256 for a prepared book, "" for
// any other. Until task C2 reads the checkpoint file, every book is opened as one without checkpoint
// data, so a hash already in a file is kept as it is and never compared.
inline constexpr const char* NO_BOOK_SHA256 = "";

// `<bookPath>.rsvp-state.json`.
std::string bookStatePath(const std::string& bookPath);

// Reads the book's state into `out` (a fresh state unless the result is Read). Never changes the file.
StateLoad loadBookState(const std::string& bookPath, const char* bookSha256, checkpoint::BookState& out);

// Changes the state passed to it; `context` is the caller's.
using StateEdit = void (*)(checkpoint::BookState& state, const void* context);

// Reads the file, applies `edit` and writes the result to `<path>.tmp`, which then replaces the file.
// A file that is invalid or unreadable is first moved aside to `<path>.bad` (replacing an older
// one) and the edit applies to a fresh state. False, with the file untouched, when memory is short;
// false when the write fails (the old file stays, unless the final rename was cut short).
bool editBookState(const std::string& bookPath, const char* bookSha256, StateEdit edit, const void* context);

}  // namespace reading

#endif  // CROSSPOINT_RSVP_ENABLED
