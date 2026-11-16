#pragma once

// Pacing: how long each word should ideally stay on screen at a target WPM.
// interval = 60,000 / WPM * rawWeight / normalisation, in integer milliseconds with the
// remainder carried to the next word, so a run of intervals sums exactly.

#include <cstdint>

#include "WordClassifier.h"

namespace rsvp {

// Weights are fixed-point: WEIGHT_ONE is a weight of 1.0.
inline constexpr uint16_t WEIGHT_ONE = 1000;

struct LongWordStep {
  uint8_t minLetters;  // applies to words with at least this many letters
  uint16_t extra;      // added to the punctuation weight
};

struct PacingTable {
  uint16_t normal;     // no pause
  uint16_t clause;     // Pause::CLAUSE: , ; : dash, or . ! ? … that does not end the sentence
  uint16_t sentence;   // Pause::SENTENCE
  uint16_t paragraph;  // WordClass::endsBlock(): paragraph, section, non-prose or chapter end
  LongWordStep longWord[3];
  // Mean raw weight of the committed English prose sample (test/rsvp_core/prose_sample), so the
  // sample averages exactly the target WPM. test/rsvp_core fails if it drifts by more than 0.5%.
  uint16_t normalisation;
};

// The one weight table. A word takes the highest of normal/clause/sentence/paragraph that
// applies, plus the extra of the highest long-word step it reaches.
inline constexpr PacingTable PACING = {
    /*normal=*/1000,
    /*clause=*/1500,
    /*sentence=*/2000,
    /*paragraph=*/2500,
    /*longWord=*/{{8, 100}, {11, 200}, {14, 300}},
    /*normalisation=*/1117,
};

inline constexpr uint16_t DEFAULT_WPM = 240;

// Raw (not normalised) weight of a classified word, in WEIGHT_ONE units.
uint16_t rawWeight(const WordClass& word);

// Turns raw weights into millisecond intervals for one target WPM. Keep one Pacer per playback
// stream: it carries the sub-millisecond remainder from word to word.
class Pacer {
 public:
  explicit Pacer(uint16_t wpm = DEFAULT_WPM) { setWpm(wpm); }

  // Sets the target WPM (0 is treated as 1) and clears the carried remainder.
  void setWpm(uint16_t wpm);
  uint16_t wpm() const { return targetWpm; }

  // Clears the carried remainder, for example after a jump to another position.
  void reset() { carry = 0; }

  // Ideal interval of the next word in milliseconds. Over any run of words, the sum of the
  // returned intervals is floor(60,000 * sum(rawWeight) / (wpm * normalisation)) for that run.
  uint32_t intervalMs(uint16_t rawWeight);

 private:
  uint16_t targetWpm = DEFAULT_WPM;
  uint64_t carry = 0;  // numerator remainder, always < targetWpm * normalisation
};

}  // namespace rsvp
