#include <gtest/gtest.h>

#include <vector>

#include "TestSupport.h"
#include "lib/RsvpCore/SentenceStep.h"

using rsvp::ClassWindow;

namespace {

// Words 0..12: "It was late. Dr. Vane slept. The Lamp Room He woke at noon."
// Sentence starts: 0 (It), 3 (Dr.), 6 (The), 9 (He, after the heading's paragraph end).
std::vector<rsvp::WordClass> chapterClasses() {
  auto tokens = rsvptest::words("It was late. Dr. Vane slept. The Lamp Room He woke at noon.");
  tokens[8].flags = rsvp::TOKEN_PARAGRAPH_END;
  tokens[12].flags = rsvp::TOKEN_PARAGRAPH_END;
  std::vector<rsvp::Token> views;
  for (const auto& t : tokens) views.push_back({t.text, t.wordIndex, t.flags});
  std::vector<rsvp::WordClass> classes(views.size());
  rsvp::classifyWords(views.data(), views.size(), "", classes.data());
  return classes;
}

}  // namespace

TEST(SentenceStep, SentenceStartOfEachWord) {
  const auto classes = chapterClasses();
  const ClassWindow window{classes.data(), static_cast<uint32_t>(classes.size()), 0};
  const uint32_t expected[] = {0, 0, 0, 3, 3, 3, 6, 6, 6, 9, 9, 9, 9};
  for (uint32_t i = 0; i < classes.size(); ++i) {
    uint32_t start = 99;
    ASSERT_TRUE(rsvp::sentenceStart(window, i, start)) << i;
    EXPECT_EQ(start, expected[i]) << i;
  }
}

TEST(SentenceStep, NextAndPrevious) {
  const auto classes = chapterClasses();
  const ClassWindow window{classes.data(), static_cast<uint32_t>(classes.size()), 0};
  uint32_t out = 0;
  ASSERT_TRUE(rsvp::nextSentenceStart(window, 0, out));
  EXPECT_EQ(out, 3u);
  ASSERT_TRUE(rsvp::nextSentenceStart(window, 4, out));
  EXPECT_EQ(out, 6u);
  ASSERT_TRUE(rsvp::nextSentenceStart(window, 6, out));
  EXPECT_EQ(out, 9u);
  ASSERT_TRUE(rsvp::nextSentenceStart(window, 10, out));
  EXPECT_EQ(out, 13u);  // one past the chapter's last word: the caller checks the word count

  ASSERT_TRUE(rsvp::previousSentenceStart(window, 9, out));
  EXPECT_EQ(out, 6u);
  ASSERT_TRUE(rsvp::previousSentenceStart(window, 11, out));
  EXPECT_EQ(out, 6u);
  ASSERT_TRUE(rsvp::previousSentenceStart(window, 4, out));
  EXPECT_EQ(out, 0u);
  EXPECT_FALSE(rsvp::previousSentenceStart(window, 1, out));  // already in the first sentence
}

TEST(SentenceStep, WindowNotStartingAtTheChapterStart) {
  const auto classes = chapterClasses();
  // Window over words 4..12 only.
  const ClassWindow window{classes.data() + 4, static_cast<uint32_t>(classes.size() - 4), 4};
  uint32_t out = 0;
  EXPECT_FALSE(rsvp::sentenceStart(window, 5, out));  // the start (3) is before the window
  ASSERT_TRUE(rsvp::sentenceStart(window, 7, out));
  EXPECT_EQ(out, 6u);
  EXPECT_FALSE(rsvp::previousSentenceStart(window, 7, out));
  ASSERT_TRUE(rsvp::previousSentenceStart(window, 10, out));
  EXPECT_EQ(out, 6u);
  EXPECT_FALSE(rsvp::sentenceStart(window, 2, out));   // outside the window
  EXPECT_FALSE(rsvp::sentenceStart(window, 13, out));  // outside the window
}

TEST(SentenceStep, NoBoundaryInWindow) {
  auto tokens = rsvptest::words("and so on and");
  std::vector<rsvp::Token> views;
  for (const auto& t : tokens) views.push_back({t.text, t.wordIndex, t.flags});
  std::vector<rsvp::WordClass> classes(views.size());
  rsvp::classifyWords(views.data(), views.size(), "more", classes.data());
  const ClassWindow window{classes.data(), static_cast<uint32_t>(classes.size()), 100};
  uint32_t out = 0;
  EXPECT_FALSE(rsvp::nextSentenceStart(window, 101, out));
  EXPECT_FALSE(rsvp::sentenceStart(window, 101, out));
}

TEST(SentenceStep, BlockBoundariesStartSentences) {
  auto tokens = rsvptest::words("see figure below then section two begins here");
  tokens[2].flags = rsvp::TOKEN_NON_PROSE_FOLLOWS;
  tokens[4].flags = rsvp::TOKEN_SECTION_END;
  std::vector<rsvp::Token> views;
  for (const auto& t : tokens) views.push_back({t.text, t.wordIndex, t.flags});
  std::vector<rsvp::WordClass> classes(views.size());
  rsvp::classifyWords(views.data(), views.size(), "", classes.data());
  const ClassWindow window{classes.data(), static_cast<uint32_t>(classes.size()), 0};
  uint32_t out = 0;
  ASSERT_TRUE(rsvp::nextSentenceStart(window, 0, out));
  EXPECT_EQ(out, 3u);
  ASSERT_TRUE(rsvp::nextSentenceStart(window, 3, out));
  EXPECT_EQ(out, 5u);
  ASSERT_TRUE(rsvp::sentenceStart(window, 7, out));
  EXPECT_EQ(out, 5u);
}
