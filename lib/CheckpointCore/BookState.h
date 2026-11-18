#pragma once

// The per-book state (docs/PLAN.md section 7, "State file"): RSVP overrides,
// reading position, first grades and the checkpoint or reread in progress.
// StateCodec writes it as `<book file name>.rsvp-state.json`.

#include <cstddef>
#include <cstdint>

#include "CheckpointLimits.h"
#include "CheckpointTypes.h"
#include "GradeTable.h"

namespace checkpoint {

inline constexpr uint8_t STATE_VERSION = 1;
inline constexpr size_t SHA256_HEX_CHARS = 64;

// Per-book RSVP settings. Each is written only when set.
struct Overrides {
  bool hasWpm = false;
  uint16_t wpm = 0;  // 1..65535
  bool hasWordsPerUpdate = false;
  uint8_t wordsPerUpdate = 0;  // 1..255
};

// The checkpoint being presented. Whether the answer is revealed is not part
// of it: reopening always shows the prompt with its answer hidden.
struct ActiveCheckpoint {
  char id[ID_BUFFER_BYTES] = {};
  uint16_t promptIndex = 0;  // < MAX_PROMPTS_PER_CHECKPOINT
  Place resume;              // first word after the break, mode active when it fired
  bool rereading = false;
  Place reread;  // valid while rereading
};

// About 77 KiB, nearly all of it the grade table; allocate it once per open
// book, not on the stack.
struct BookState {
  BookState() = default;
  BookState(const BookState&) = delete;
  BookState& operator=(const BookState&) = delete;

  // A fresh state: no overrides, no position, no grades, nothing active.
  void reset();
  bool sameAs(const BookState& other) const;

  // The checkpoint file's book.source_sha256; "" for a book without checkpoint data.
  char bookSha256[SHA256_HEX_CHARS + 1] = {};
  Overrides overrides;
  bool hasPosition = false;  // false: open at the start of the book
  Place position;            // normal reading; not moved by a checkpoint or reread
  GradeTable grades;
  bool hasActive = false;
  ActiveCheckpoint active;
};

// True when `state` could have come from decodeState(): every value in range,
// ids valid and unique, the sha empty or 64 lowercase hex digits.
bool stateInvariantsHold(const BookState& state);

// True for 64 lowercase hex digits.
bool isSha256Hex(const char* text, size_t length);

}  // namespace checkpoint
