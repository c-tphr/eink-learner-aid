#pragma once

// Byte-level encoding of BookState as the version 1 state file (docs/PLAN.md
// section 7, "State file"). No file I/O: the caller reads the file into a
// buffer for decodeState() and writes encodeState()'s output through a
// temporary file and a rename.

#include <cstddef>
#include <cstdint>

#include "BookState.h"

namespace checkpoint {

// Receives the encoded bytes in order.
class StateSink {
 public:
  virtual ~StateSink() = default;
  // False stops the encoding.
  virtual bool write(const char* data, size_t length) = 0;
};

// Writes into a caller buffer; fails when it is full. No NUL is added.
class BufferStateSink final : public StateSink {
 public:
  BufferStateSink(char* out, size_t outSize) : buffer(out), capacity(outSize) {}
  bool write(const char* data, size_t length) override;
  size_t length() const { return used; }
  bool overflowed() const { return overflow; }

 private:
  char* buffer;
  size_t capacity;
  size_t used = 0;
  bool overflow = false;
};

// Exact size of the longest encoding: every member present with its widest
// value and GRADE_CAPACITY grades with MAX_ID_CHARS-character ids and times.
inline constexpr size_t STATE_ENCODED_FIXED_BYTES = 528;
inline constexpr size_t STATE_ENCODED_GRADE_BYTES = 127;
inline constexpr size_t STATE_ENCODED_MAX_BYTES =
    STATE_ENCODED_FIXED_BYTES + static_cast<size_t>(GRADE_CAPACITY) * STATE_ENCODED_GRADE_BYTES - 1;  // 130,575

// decodeState() refuses larger input (room for unknown members a later
// version-1 writer might add).
inline constexpr size_t STATE_DECODE_MAX_BYTES = 256u * 1024u;

// Writes `state` as indented JSON in the member order of the plan's example,
// ending in a newline. book_sha256 is written when set; grades when the sha is
// set or there are grades; position and active_checkpoint when present. A
// state for a book without checkpoint data therefore holds only version,
// overrides and position.
bool encodeState(const BookState& state, StateSink& sink);

enum class DecodeStatus : uint8_t {
  Ok,
  TooLarge,            // more than STATE_DECODE_MAX_BYTES
  Syntax,              // not strict JSON (RFC 8259, UTF-8, no lone surrogates, no trailing data)
  UnsupportedVersion,  // version is an integer above STATE_VERSION
  Limit,               // more grades than GRADE_CAPACITY, or nesting deeper than MAX_DEPTH
  Schema,              // a known member is missing, repeated, of the wrong type or out of range
};

const char* decodeStatusName(DecodeStatus status);

struct DecodeResult {
  DecodeStatus status = DecodeStatus::Syntax;
  uint32_t offset = 0;      // byte offset of the first problem
  const char* detail = "";  // static English text for logs
  // Ok only: the file's book_sha256 and `openedBookSha256` are both set and differ.
  bool shaMismatch = false;
};

// Decodes a state file. Unknown members are ignored at every level. On any
// status but Ok, `out` is reset to a fresh state; the caller keeps the bad
// file aside and starts from that. `openedBookSha256` is the open book's
// CheckpointIndex::sourceSha256(), or ""/nullptr for a book without
// checkpoint data. Uses under 1 KiB of stack and no heap.
DecodeResult decodeState(const uint8_t* data, size_t size, const char* openedBookSha256, BookState& out);

}  // namespace checkpoint
