#include <WordClassifier.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "activities/devicetest/Passage.h"

using devicetest::PASSAGE_WORDS;
using devicetest::PassageSource;

namespace {

struct Seen {
  std::string text;
  uint32_t index;
  uint8_t flags;
};

std::vector<Seen> drain(PassageSource& source) {
  std::vector<Seen> out;
  rsvp::Token t;
  while (source.peek(t)) {
    out.push_back({std::string(t.text), t.wordIndex, t.flags});
    source.advance();
  }
  return out;
}

}  // namespace

TEST(Passage, HasExactly150Words) {
  EXPECT_EQ(PASSAGE_WORDS, 150u);
  EXPECT_EQ(devicetest::countPassageWords(), 150u);
}

TEST(Passage, SourceHandsOutEveryWordOnceInOrderWithParagraphEnds) {
  PassageSource source;
  source.reset(PASSAGE_WORDS);
  const std::vector<Seen> words = drain(source);
  ASSERT_EQ(words.size(), 150u);
  for (uint32_t i = 0; i < words.size(); ++i) EXPECT_EQ(words[i].index, i);
  EXPECT_EQ(words.front().text, "The");
  EXPECT_EQ(words.back().text, "alone.");

  std::vector<uint32_t> paragraphEnds;
  for (const Seen& w : words) {
    if (w.flags & rsvp::TOKEN_PARAGRAPH_END) paragraphEnds.push_back(w.index);
  }
  EXPECT_EQ(paragraphEnds, (std::vector<uint32_t>{51, 109, 149}));  // 52 + 58 + 40 words
  EXPECT_EQ(words[149].flags & rsvp::TOKEN_CHAPTER_END, rsvp::TOKEN_CHAPTER_END);
  EXPECT_EQ(words[148].flags & rsvp::TOKEN_CHAPTER_END, 0);
  EXPECT_EQ(source.position(), 150u);
}

TEST(Passage, RepeatsWithCountingIndexesForTheEnduranceRun) {
  PassageSource source;
  source.reset(300);
  const std::vector<Seen> words = drain(source);
  ASSERT_EQ(words.size(), 300u);
  EXPECT_EQ(words[150].text, "The");
  EXPECT_EQ(words[150].index, 150u);
  EXPECT_EQ(words[150 + 7].text, words[7].text);
  // The passage's end is a paragraph end, not the end of the run.
  EXPECT_EQ(words[149].flags, rsvp::TOKEN_PARAGRAPH_END);
  EXPECT_EQ(words[299].flags, rsvp::TOKEN_PARAGRAPH_END | rsvp::TOKEN_CHAPTER_END);
}

TEST(Passage, ShorterLimitsEndEarlyAndPeekIsStable) {
  PassageSource source;
  source.reset(100);
  rsvp::Token a;
  rsvp::Token b;
  ASSERT_TRUE(source.peek(a));
  ASSERT_TRUE(source.peek(b));
  EXPECT_EQ(a.text, b.text);
  EXPECT_EQ(drain(source).size(), 100u);
  EXPECT_FALSE(source.peek(a));
  source.reset(0);
  EXPECT_FALSE(source.peek(a));
}

TEST(Passage, ExercisesEveryPauseClass) {
  PassageSource source;
  source.reset(PASSAGE_WORDS);
  const std::vector<Seen> words = drain(source);
  int clause = 0;
  int sentence = 0;
  int blockEnds = 0;
  int longWords = 0;
  for (size_t i = 0; i < words.size(); ++i) {
    const std::string_view next = i + 1 < words.size() ? std::string_view(words[i + 1].text) : std::string_view();
    const rsvp::WordClass c = rsvp::classifyWord(words[i].text, words[i].flags, next);
    if (c.pause == rsvp::Pause::CLAUSE) ++clause;
    if (c.pause == rsvp::Pause::SENTENCE) ++sentence;
    if (c.endsBlock()) ++blockEnds;
    if (c.letters >= 8) ++longWords;
  }
  EXPECT_GE(clause, 10) << "commas, a semicolon and a colon";
  EXPECT_GE(sentence, 9) << "full stops and a question mark";
  EXPECT_EQ(blockEnds, 3);
  EXPECT_GE(longWords, 4) << "the long-word extras";
}
