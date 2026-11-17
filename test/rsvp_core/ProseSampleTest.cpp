// Measures the pacing normalisation constant on the committed English prose sample
// (prose_sample/README.md) and checks the constant in lib/RsvpCore/Pacing.h against it.
// To re-measure after changing the weight table:
//   build/test/rsvp_core/RsvpCoreTest --gtest_filter='ProseSample.*'
// and copy the printed value (rounded to WEIGHT_ONE units) into PACING.normalisation.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "lib/RsvpCore/FrameBuilder.h"
#include "lib/RsvpCore/Pacing.h"
#include "lib/RsvpCore/WordClassifier.h"

namespace {

const std::vector<rsvptest::OwnedToken>& sampleTokens() {
  static const std::vector<rsvptest::OwnedToken> tokens =
      rsvptest::proseTokens(rsvptest::readFile(RSVP_TEST_DIR "/prose_sample/prose_sample.txt"));
  return tokens;
}

struct Measurement {
  size_t words = 0;
  uint64_t rawWeightSum = 0;
  size_t clauses = 0;
  size_t sentences = 0;
  size_t blocks = 0;
};

Measurement measure() {
  const auto& tokens = sampleTokens();
  Measurement m;
  for (size_t i = 0; i < tokens.size(); ++i) {
    const std::string next = i + 1 < tokens.size() ? tokens[i + 1].text : std::string();
    uint8_t flags = tokens[i].flags;
    if (i + 1 == tokens.size()) flags |= rsvp::TOKEN_CHAPTER_END;
    const rsvp::WordClass c = rsvp::classifyWord(tokens[i].text, flags, next);
    m.rawWeightSum += rsvp::rawWeight(c);
    ++m.words;
    if (c.endsBlock()) {
      ++m.blocks;
    } else if (c.pause == rsvp::Pause::SENTENCE) {
      ++m.sentences;
    } else if (c.pause == rsvp::Pause::CLAUSE) {
      ++m.clauses;
    }
  }
  return m;
}

}  // namespace

TEST(ProseSample, IsLargeEnough) { EXPECT_GE(sampleTokens().size(), 3000u); }

TEST(ProseSample, NormalisationConstantMatchesTheMeasuredMean) {
  const Measurement m = measure();
  const double mean = static_cast<double>(m.rawWeightSum) / static_cast<double>(m.words);
  std::printf("prose sample: %zu words, %zu clause pauses, %zu sentence ends, %zu block ends\n", m.words, m.clauses,
              m.sentences, m.blocks);
  std::printf("measured mean raw weight: %.2f (%.4f x WEIGHT_ONE); PACING.normalisation = %u\n", mean,
              mean / rsvp::WEIGHT_ONE, static_cast<unsigned>(rsvp::PACING.normalisation));
  const double drift = std::fabs(static_cast<double>(rsvp::PACING.normalisation) - mean) / mean;
  EXPECT_LE(drift, 0.005) << "PACING.normalisation drifted from the measured mean " << mean;
}

TEST(ProseSample, AveragesTheTargetWpm) {
  struct Width {
    size_t maxBytes;
  } width{24};
  auto fits = [](void* context, std::string_view text, uint16_t) {
    return text.size() <= static_cast<Width*>(context)->maxBytes;
  };

  const uint16_t wpm = 240;
  for (uint8_t n = 1; n <= 3; ++n) {
    rsvptest::VectorSource source(sampleTokens());
    rsvp::FrameBuilder builder(source, fits, &width);
    builder.setWordsPerFrame(n);
    builder.setWpm(wpm);
    uint64_t totalMs = 0;
    size_t words = 0;
    size_t frames = 0;
    rsvp::Frame frame;
    while (builder.next(frame)) {
      totalMs += frame.holdMs;
      words += frame.wordCount;
      ++frames;
    }
    ASSERT_EQ(words, sampleTokens().size());
    const double achieved = 60000.0 * static_cast<double>(words) / static_cast<double>(totalMs);
    std::printf("N=%u: %zu frames, %llu ms, ideal pace %.2f WPM at target %u\n", static_cast<unsigned>(n), frames,
                static_cast<unsigned long long>(totalMs), achieved, static_cast<unsigned>(wpm));
    EXPECT_NEAR(achieved, wpm, wpm * 0.02) << "N=" << int(n);
  }
}
