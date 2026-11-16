#include "Pacing.h"

namespace rsvp {

namespace {
constexpr uint64_t MS_PER_MINUTE = 60000;
}

uint16_t rawWeight(const WordClass& word) {
  uint16_t weight = PACING.normal;
  if (word.endsBlock()) {
    weight = PACING.paragraph;
  } else if (word.pause == Pause::SENTENCE) {
    weight = PACING.sentence;
  } else if (word.pause == Pause::CLAUSE) {
    weight = PACING.clause;
  }
  uint16_t extra = 0;
  for (const LongWordStep& step : PACING.longWord) {
    if (word.letters >= step.minLetters) extra = step.extra;
  }
  return static_cast<uint16_t>(weight + extra);
}

void Pacer::setWpm(const uint16_t wpm) {
  targetWpm = wpm == 0 ? 1 : wpm;
  carry = 0;
}

uint32_t Pacer::intervalMs(const uint16_t rawWeight) {
  const uint64_t denominator = static_cast<uint64_t>(targetWpm) * PACING.normalisation;
  const uint64_t numerator = MS_PER_MINUTE * rawWeight + carry;
  carry = numerator % denominator;
  return static_cast<uint32_t>(numerator / denominator);
}

}  // namespace rsvp
