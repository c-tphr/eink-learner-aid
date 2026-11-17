#pragma once

// Whether a chapter's stored .wix index can be used, from what opening it reported
// (lib/WordIndex/README.md, "Rebuild rules"). Pure: ChapterWordIndex gathers the facts.

#include <WordIndexReader.h>

#include <cstdint>

namespace reading {

enum class IndexState : uint8_t {
  Valid,         // open() Ok and built from the chapter's current zip entry
  Missing,       // no file
  Unreadable,    // open() failed: truncated, unfinished, other format or rules version, corrupt, I/O error
  Stale,         // built from other source bytes (the book was replaced or re-prepared)
  Unverifiable,  // the source has no zip size and CRC-32 to compare (a TXT book): always rebuilt
};

// `sourceKnown`: the chapter's zip entry size and CRC-32 were found.
IndexState checkIndex(bool fileExists, wordindex::ReadStatus openStatus, bool sourceKnown, bool sourceMatches);

inline bool needsBuild(const IndexState state) { return state != IndexState::Valid; }

const char* indexStateName(IndexState state);

}  // namespace reading
