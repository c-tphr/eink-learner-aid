#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "lib/RsvpCore/Pacing.h"

using rsvp::PACING;
using rsvp::Pause;
using rsvp::rawWeight;
using rsvp::WordClass;

namespace {

WordClass make(Pause pause, uint8_t letters = 4, uint8_t flags = 0) {
  WordClass c;
  c.pause = pause;
  c.letters = letters;
  c.tokenFlags = flags;
  return c;
}

}  // namespace

TEST(Pacing, TableIsOrdered) {
  static_assert(PACING.normal == rsvp::WEIGHT_ONE);
  static_assert(PACING.normal < PACING.clause && PACING.clause < PACING.sentence && PACING.sentence < PACING.paragraph);
  static_assert(PACING.longWord[0].minLetters < PACING.longWord[1].minLetters &&
                PACING.longWord[1].minLetters < PACING.longWord[2].minLetters);
  static_assert(PACING.longWord[0].extra < PACING.longWord[1].extra &&
                PACING.longWord[1].extra < PACING.longWord[2].extra);
}

TEST(Pacing, EachPunctuationHold) {
  EXPECT_EQ(rawWeight(make(Pause::NONE)), PACING.normal);
  EXPECT_EQ(rawWeight(make(Pause::CLAUSE)), PACING.clause);
  EXPECT_EQ(rawWeight(make(Pause::SENTENCE)), PACING.sentence);
  for (const uint8_t flag : {rsvp::TOKEN_PARAGRAPH_END, rsvp::TOKEN_SECTION_END, rsvp::TOKEN_CHECKPOINT,
                             rsvp::TOKEN_NON_PROSE_FOLLOWS, rsvp::TOKEN_CHAPTER_END}) {
    EXPECT_EQ(rawWeight(make(Pause::SENTENCE, 4, flag)), PACING.paragraph) << int(flag);
    EXPECT_EQ(rawWeight(make(Pause::NONE, 4, flag)), PACING.paragraph) << int(flag);
  }
}

TEST(Pacing, LongWordSteps) {
  const uint16_t step1 = PACING.longWord[0].extra;
  const uint16_t step2 = PACING.longWord[1].extra;
  const uint16_t step3 = PACING.longWord[2].extra;
  for (uint8_t letters = 0; letters <= 40; ++letters) {
    uint16_t extra = 0;
    if (letters >= PACING.longWord[2].minLetters) {
      extra = step3;
    } else if (letters >= PACING.longWord[1].minLetters) {
      extra = step2;
    } else if (letters >= PACING.longWord[0].minLetters) {
      extra = step1;
    }
    EXPECT_EQ(rawWeight(make(Pause::NONE, letters)), PACING.normal + extra) << int(letters);
    EXPECT_EQ(rawWeight(make(Pause::CLAUSE, letters)), PACING.clause + extra) << int(letters);
    EXPECT_EQ(rawWeight(make(Pause::NONE, letters, rsvp::TOKEN_PARAGRAPH_END)), PACING.paragraph + extra);
  }
  // The documented boundaries: 7 letters normal, 8 first step, 11 second, 14 third.
  EXPECT_EQ(rawWeight(make(Pause::NONE, 7)), 1000);
  EXPECT_EQ(rawWeight(make(Pause::NONE, 8)), 1100);
  EXPECT_EQ(rawWeight(make(Pause::NONE, 11)), 1200);
  EXPECT_EQ(rawWeight(make(Pause::NONE, 14)), 1300);
  EXPECT_EQ(rawWeight(make(Pause::NONE, 255)), 1300);
}

TEST(Pacing, NormalisedWeightOneIsTheBaseInterval) {
  for (const uint16_t wpm : {120, 240, 300, 600}) {
    rsvp::Pacer pacer(wpm);
    uint64_t total = 0;
    for (int i = 0; i < 1000; ++i) total += pacer.intervalMs(PACING.normalisation);
    EXPECT_EQ(total, 1000ULL * 60000 / wpm) << wpm;
  }
  rsvp::Pacer pacer(240);
  EXPECT_EQ(pacer.intervalMs(PACING.normalisation), 250u);
}

TEST(Pacing, SummedIntervalsAreExactOverTenThousandWords) {
  // A deterministic mix of every weight, including long-word extras.
  std::vector<uint16_t> weights;
  uint32_t state = 12345;
  for (int i = 0; i < 10000; ++i) {
    state = state * 1103515245u + 12345u;
    const Pause pause = static_cast<Pause>((state >> 16) % 3);
    const uint8_t letters = static_cast<uint8_t>((state >> 8) % 18);
    const uint8_t flags = ((state >> 4) % 23 == 0) ? rsvp::TOKEN_PARAGRAPH_END : 0;
    weights.push_back(rawWeight(make(pause, letters, flags)));
  }
  uint64_t weightSum = 0;
  for (const uint16_t w : weights) weightSum += w;

  for (const uint16_t wpm : {120, 240, 300, 600}) {
    rsvp::Pacer pacer(wpm);
    uint64_t total = 0;
    for (const uint16_t w : weights) total += pacer.intervalMs(w);
    const uint64_t denominator = static_cast<uint64_t>(wpm) * PACING.normalisation;
    const uint64_t exactFloor = 60000ULL * weightSum / denominator;
    EXPECT_EQ(total, exactFloor) << wpm;
    const double exact = 60000.0 * static_cast<double>(weightSum) / static_cast<double>(denominator);
    EXPECT_LT(std::fabs(exact - static_cast<double>(total)), 1.0) << wpm;

    // Every prefix is exact too, so no drift builds up along the way.
    rsvp::Pacer again(wpm);
    uint64_t running = 0;
    uint64_t runningWeight = 0;
    for (size_t i = 0; i < weights.size(); ++i) {
      running += again.intervalMs(weights[i]);
      runningWeight += weights[i];
      ASSERT_EQ(running, 60000ULL * runningWeight / denominator) << wpm << " at word " << i;
    }
  }
}

TEST(Pacing, SetWpmClearsTheRemainder) {
  rsvp::Pacer pacer(240);
  pacer.intervalMs(1001);
  pacer.setWpm(240);
  rsvp::Pacer fresh(240);
  EXPECT_EQ(pacer.intervalMs(1777), fresh.intervalMs(1777));
  EXPECT_EQ(pacer.wpm(), 240);
}

TEST(Pacing, ZeroWpmIsTreatedAsOne) {
  rsvp::Pacer pacer(0);
  EXPECT_EQ(pacer.wpm(), 1);
  EXPECT_EQ(pacer.intervalMs(PACING.normalisation), 60000u);
}
