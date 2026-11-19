#include "WordOrderAudit.h"

namespace rsvp {

void WordOrderAudit::violation(const uint32_t expectedIndex, const uint32_t drawnIndex) {
  if (violationCount == 0) {
    violationExpected = expectedIndex;
    violationDrawn = drawnIndex;
  }
  ++violationCount;
}

void WordOrderAudit::record(const uint32_t wordIndex) {
  ++drawn;
  if (!segmentOpen) {
    segmentOpen = true;
    forwardSkip = false;
    pendingDeclared = 0;
    ++segmentCount;
    expected = wordIndex + 1;
    return;
  }
  declared += pendingDeclared;
  expected += pendingDeclared;
  pendingDeclared = 0;
  if (wordIndex != expected) {
    if (wordIndex > expected && forwardSkip) {
      skipped += wordIndex - expected;
    } else {
      violation(expected, wordIndex);
      if (wordIndex > expected) {
        ++gapCount;
      } else {
        ++repeatCount;
      }
    }
  }
  forwardSkip = false;
  // After a repeat, keep expecting the word after the highest index drawn so far.
  if (wordIndex >= expected) expected = wordIndex + 1;
}

void WordOrderAudit::recordFrame(const uint32_t firstWordIndex, const uint32_t lastWordIndex, const uint8_t wordCount) {
  if (wordCount == 0) return;
  if (lastWordIndex < firstWordIndex || lastWordIndex - firstWordIndex + 1 != wordCount) {
    // The frame's indexes are not consecutive. Only its first and last are known, so the frame
    // counts as one gap and its words are recorded as if consecutive from the first.
    violation(firstWordIndex + wordCount - 1, lastWordIndex);
    ++gapCount;
  }
  for (uint8_t i = 0; i < wordCount; ++i) record(firstWordIndex + i);
}

}  // namespace rsvp
