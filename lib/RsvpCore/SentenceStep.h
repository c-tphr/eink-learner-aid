#pragma once

// Sentence stepping for the pause screen: where "back one sentence" and "forward one sentence"
// land, computed from a window of classified words around the current position.

#include <cstdint>

#include "WordClassifier.h"

namespace rsvp {

// Classifications of the consecutive chapter words [firstWordIndex, firstWordIndex + count).
// A sentence starts at word 0 of the chapter and after every word whose WordClass::isBoundary()
// is true (sentence end, paragraph end, section end, non-prose marker, chapter end).
struct ClassWindow {
  const WordClass* classes = nullptr;
  uint32_t count = 0;
  uint32_t firstWordIndex = 0;
};

// Start of the sentence that contains `wordIndex`. Returns false when `wordIndex` is outside the
// window, or the window does not reach back to that start (load an earlier window and retry).
bool sentenceStart(const ClassWindow& window, uint32_t wordIndex, uint32_t& out);

// Start of the sentence before the one that contains `wordIndex`. Returns false at the chapter's
// first sentence or when the window does not reach back far enough.
bool previousSentenceStart(const ClassWindow& window, uint32_t wordIndex, uint32_t& out);

// First word after the sentence that contains `wordIndex`. The result can be one past the window
// (and past the chapter, if the window ends at its last word); the caller checks it against the
// chapter's word count. Returns false when no boundary lies at or after `wordIndex` in the window.
bool nextSentenceStart(const ClassWindow& window, uint32_t wordIndex, uint32_t& out);

}  // namespace rsvp
