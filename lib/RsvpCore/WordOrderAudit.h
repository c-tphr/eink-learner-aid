#pragma once

// Records every chapter word index that playback draws and whether each was drawn exactly once
// and in order: no gap, no repeat, nothing out of order. The device acceptance test reads it
// (plan section 10, "The PRD's on-device criterion").
//
// Segments: an intentional jump (seek, sentence step, reread, next chapter) opens a new segment.
// The first index after beginSegment() is accepted whatever it is; within a segment each index
// must be the previous one plus 1. allowForwardSkip() accepts one forward jump at the next index,
// for word indexes the source does not hand out as RSVP words (the inside of a table or code
// block after a non-prose stop); the skipped count is recorded. declareSkip() moves the expected
// index on by the count a source declares it passed over on purpose (Token::skippedBefore, a
// footnote body), counted apart. Any other difference is a violation: a gap (index too high) or a
// repeat (index at or below one already drawn).

#include <cstdint>

namespace rsvp {

class WordOrderAudit {
 public:
  // Clears all counts; the next index opens the first segment.
  void reset() { *this = WordOrderAudit{}; }

  // The next index opens a new segment.
  void beginSegment() {
    segmentOpen = false;
    pendingDeclared = 0;
  }

  // The next index may be any index after the last one drawn.
  void allowForwardSkip() { forwardSkip = true; }

  // The source passed over `count` indexes on purpose right before the next one (a footnote
  // body, Token::skippedBefore): the next index is expected that much further on. Counted in
  // declaredSkips(), not skippedWords(). Ignored when the next index opens a segment.
  void declareSkip(uint32_t count) { pendingDeclared += count; }

  // Records one drawn word.
  void record(uint32_t wordIndex);

  // Records a frame of `wordCount` words from `firstWordIndex` to `lastWordIndex`. A frame whose
  // range does not hold exactly `wordCount` indexes is itself a violation.
  void recordFrame(uint32_t firstWordIndex, uint32_t lastWordIndex, uint8_t wordCount);

  bool inOrder() const { return violationCount == 0; }
  uint32_t wordsDrawn() const { return drawn; }
  uint32_t segments() const { return segmentCount; }
  uint32_t violations() const { return violationCount; }
  uint32_t gaps() const { return gapCount; }
  uint32_t repeats() const { return repeatCount; }
  uint32_t skippedWords() const { return skipped; }    // indexes passed over by allowed forward skips
  uint32_t declaredSkips() const { return declared; }  // indexes passed over by declareSkip()

  // The first violation: the index that was expected and the one that was drawn.
  bool hasViolation() const { return violationCount != 0; }
  uint32_t firstViolationExpected() const { return violationExpected; }
  uint32_t firstViolationDrawn() const { return violationDrawn; }

  // Index the current segment expects next (valid once a segment is open).
  uint32_t nextExpected() const { return expected; }

 private:
  void violation(uint32_t expectedIndex, uint32_t drawnIndex);

  uint32_t drawn = 0;
  uint32_t segmentCount = 0;
  uint32_t violationCount = 0;
  uint32_t gapCount = 0;
  uint32_t repeatCount = 0;
  uint32_t skipped = 0;
  uint32_t declared = 0;
  uint32_t pendingDeclared = 0;
  uint32_t violationExpected = 0;
  uint32_t violationDrawn = 0;
  uint32_t expected = 0;
  bool segmentOpen = false;
  bool forwardSkip = false;
};

}  // namespace rsvp
