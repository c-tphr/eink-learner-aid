#pragma once

// rsvp::WordSource over a chapter's WordIndexReader: hands RsvpCore the prose words with the token
// flags it expects (README.md, "RsvpCore adapter").

#include <WordSource.h>

#include <cstdint>

#include "WordIndexReader.h"

namespace wordindex {

// Section-end flags owned by the caller (the checkpoint code). Returns TOKEN_SECTION_END, plus
// TOKEN_CHECKPOINT when that section has a checkpoint, if a section's end anchor has a word index
// in [firstAnchorIndex, lastAnchorIndex]; otherwise 0.
using SectionEndFn = uint8_t (*)(void* context, uint32_t firstAnchorIndex, uint32_t lastAnchorIndex);

// A ready-made SectionEndFn over a caller-owned array (any order).
struct SectionEnd {
  uint32_t endAnchorIndex = 0;  // the end anchor's word index
  bool checkpoint = false;
};
struct SectionEndTable {
  const SectionEnd* ends = nullptr;
  uint32_t count = 0;
  static uint8_t lookup(void* context, uint32_t firstAnchorIndex, uint32_t lastAnchorIndex);
};

class IndexWordSource : public rsvp::WordSource {
 public:
  // `reader` must be open and outlive the source. `sectionEnds` may be null (no section flags).
  explicit IndexWordSource(WordIndexReader& reader, SectionEndFn sectionEnds = nullptr, void* context = nullptr);

  // Prose words only. TABLE and CODE words are never handed out: the last prose word before an
  // IMAGE marker or such a region carries TOKEN_NON_PROSE_FOLLOWS, and the next token is the first
  // prose word after it. NOTE words are passed over with no stop; the token after them reports how
  // many in `skippedBefore`.
  bool peek(rsvp::Token& out) override;
  void advance() override;

  // Moves to word `index`, or to the first prose word after it when it is not prose (inside a
  // table, code block or note). Then call Scheduler::reposition(). False on a read error.
  bool seek(uint32_t index);
  // Word index of the next token (wordCount() at the chapter end).
  uint32_t position() const { return current; }
  // A read error stopped the source (peek() then returns false as at a chapter end).
  bool failed() const { return error; }

 private:
  // First prose word at or after `from`, counting NOTE words passed over; sets nonProse when an
  // IMAGE marker or a TABLE/CODE region lies in between.
  bool nextProse(uint32_t from, uint32_t& prose, uint32_t& noteWords, bool& nonProse);
  bool prepare();
  // Whether an IMAGE marker outside the notes sits right before word `at`.
  bool imageBefore(uint32_t at, bool& image);

  WordIndexReader& reader;
  SectionEndFn sectionEnds;
  void* context;

  uint32_t current = 0;        // word index of the next token
  uint32_t skippedBefore = 0;  // NOTE words passed over right before `current`
  bool error = false;

  // The token at `current`, prepared by the last peek(): its successor and flags.
  bool prepared = false;
  uint32_t following = 0;  // next prose word after `current`
  uint32_t followingSkipped = 0;
  uint8_t flags = 0;

  bool imageCacheValid = false;
  uint32_t imageCacheFrom = 0;
  uint32_t imageCacheNext = 0;  // first non-note image at or after imageCacheFrom, UINT32_MAX for none
};

}  // namespace wordindex
