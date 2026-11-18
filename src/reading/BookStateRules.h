#pragma once

// What the reading code does with the bytes of a book's state file (docs/PLAN.md section 7; the
// encoding is lib/CheckpointCore's StateCodec). Pure: host-tested in test/rsvp_activity.
// BookStateFile.h does the file I/O around it.

#include <BookState.h>
#include <CheckpointIndex.h>
#include <StateCodec.h>

#include <cstddef>
#include <cstdint>

namespace reading {

enum class StateSource : uint8_t {
  Missing,     // no file: a fresh state
  Read,        // decoded
  Invalid,     // not a usable version 1 state (syntax, schema, limits, too large, a newer version): fresh
  Unreadable,  // the file exists but could not be read: fresh
  NoMemory,    // no buffer for the file: fresh, and the file must not be replaced
};

struct StateLoad {
  StateSource source = StateSource::Missing;
  checkpoint::DecodeStatus status = checkpoint::DecodeStatus::Ok;  // Read and Invalid
  bool shaMismatch = false;                                        // Read: the file is for another edition
};

const char* stateSourceName(StateSource source);

// Decodes a state file for use. `bookSha256` is the open book's book_sha256, "" when it has none
// (see BookStateFile.h). An invalid file gives a fresh state (Invalid). When the file names another edition
// (both hashes set and different), the state takes the open book's hash and drops the reading
// position and the checkpoint in progress, whose word positions may not fit this edition; overrides
// are kept. With the open book's checkpoint index (`index`: a prepared book) this is
// checkpoint::applyBookChangeRule(), which also keeps only the grades of prompt ids the book still
// has; without it (an ordinary book) every grade is kept.
StateLoad decodeForUse(const uint8_t* data, size_t size, const char* bookSha256,
                       const checkpoint::CheckpointIndex* index, checkpoint::BookState& out);
inline StateLoad decodeForUse(const uint8_t* data, size_t size, const char* bookSha256, checkpoint::BookState& out) {
  return decodeForUse(data, size, bookSha256, nullptr, out);
}

}  // namespace reading
