#include "SentenceStep.h"

namespace rsvp {

namespace {

bool contains(const ClassWindow& window, const uint32_t wordIndex) {
  return window.classes != nullptr && wordIndex >= window.firstWordIndex &&
         wordIndex - window.firstWordIndex < window.count;
}

}  // namespace

bool sentenceStart(const ClassWindow& window, const uint32_t wordIndex, uint32_t& out) {
  if (!contains(window, wordIndex)) return false;
  uint32_t local = wordIndex - window.firstWordIndex;
  while (local > 0) {
    if (window.classes[local - 1].isBoundary()) {
      out = window.firstWordIndex + local;
      return true;
    }
    --local;
  }
  // Reached the window's first word without finding the boundary before it.
  if (window.firstWordIndex != 0) return false;
  out = 0;
  return true;
}

bool previousSentenceStart(const ClassWindow& window, const uint32_t wordIndex, uint32_t& out) {
  uint32_t current;
  if (!sentenceStart(window, wordIndex, current) || current == 0) return false;
  return sentenceStart(window, current - 1, out);
}

bool nextSentenceStart(const ClassWindow& window, const uint32_t wordIndex, uint32_t& out) {
  if (!contains(window, wordIndex)) return false;
  for (uint32_t local = wordIndex - window.firstWordIndex; local < window.count; ++local) {
    if (window.classes[local].isBoundary()) {
      out = window.firstWordIndex + local + 1;
      return true;
    }
  }
  return false;
}

}  // namespace rsvp
