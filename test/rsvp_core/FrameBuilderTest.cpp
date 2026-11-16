#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "TestSupport.h"
#include "lib/RsvpCore/FocusLetter.h"
#include "lib/RsvpCore/FrameBuilder.h"

using rsvp::Frame;
using rsvp::FrameBuilder;
using rsvptest::allFrames;
using rsvptest::OwnedToken;
using rsvptest::VectorSource;
using rsvptest::words;

namespace {

struct ByteLimit {
  size_t maxBytes;
  int calls = 0;
};

bool fitsByteLimit(void* context, std::string_view text, uint16_t) {
  auto* limit = static_cast<ByteLimit*>(context);
  ++limit->calls;
  return text.size() <= limit->maxBytes;
}

std::vector<std::string> frameTexts(const std::vector<Frame>& frames) {
  std::vector<std::string> out;
  for (const Frame& f : frames) out.emplace_back(f.view());
  return out;
}

std::vector<Frame> build(std::vector<OwnedToken> tokens, uint8_t perFrame, rsvp::FitsFn fits = nullptr,
                         void* context = nullptr, uint16_t wpm = 240) {
  VectorSource source(std::move(tokens));
  FrameBuilder builder(source, fits, context);
  builder.setWordsPerFrame(perFrame);
  builder.setWpm(wpm);
  return allFrames(builder);
}

// Every word appears exactly once, in order, across the frames.
void expectNoDropOrReorder(const std::vector<Frame>& frames, const std::vector<OwnedToken>& tokens) {
  size_t next = 0;
  for (const Frame& f : frames) {
    ASSERT_GE(f.wordCount, 1);
    EXPECT_EQ(f.firstWordIndex, tokens[next].wordIndex);
    for (uint8_t slot = 0; slot < f.wordCount; ++slot) {
      ASSERT_LT(next, tokens.size());
      EXPECT_EQ(std::string(f.word(slot)), tokens[next].text);
      ++next;
    }
    EXPECT_EQ(f.lastWordIndex, tokens[next - 1].wordIndex);
  }
  EXPECT_EQ(next, tokens.size());
}

}  // namespace

TEST(FrameBuilder, OneWordPerFrameByDefault) {
  const auto tokens = words("The supply boat reached Gannet Rock");
  const auto frames = build(tokens, 1);
  ASSERT_EQ(frames.size(), 6u);
  expectNoDropOrReorder(frames, tokens);
  EXPECT_EQ(frames[0].view(), "The");
  EXPECT_EQ(frames[0].focusWord, 0);
  EXPECT_EQ(frames[0].focusByteOffset, 1);  // 'h'
}

TEST(FrameBuilder, GroupsUpToN) {
  const auto tokens = words("one two three four five six seven eight nine ten");
  EXPECT_EQ(frameTexts(build(tokens, 2)),
            (std::vector<std::string>{"one two", "three four", "five six", "seven eight", "nine ten"}));
  EXPECT_EQ(frameTexts(build(tokens, 3)),
            (std::vector<std::string>{"one two three", "four five six", "seven eight nine", "ten"}));
  EXPECT_EQ(frameTexts(build(tokens, 4)),
            (std::vector<std::string>{"one two three four", "five six seven eight", "nine ten"}));
}

TEST(FrameBuilder, WordsPerFrameIsClamped) {
  VectorSource source(words("a b c d e f"));
  FrameBuilder builder(source);
  builder.setWordsPerFrame(0);
  EXPECT_EQ(builder.wordsPerFrame(), 1);
  builder.setWordsPerFrame(9);
  EXPECT_EQ(builder.wordsPerFrame(), rsvp::MAX_WORDS_PER_FRAME);
}

TEST(FrameBuilder, SentenceEndIsLastInItsGroup) {
  const auto tokens = words("It had crossed. The strait was calm. Dr. Vane stepped onto the stage.");
  EXPECT_EQ(frameTexts(build(tokens, 3)), (std::vector<std::string>{"It had crossed.", "The strait was", "calm.",
                                                                    "Dr. Vane stepped", "onto the stage."}));
  EXPECT_EQ(frameTexts(build(tokens, 4)), (std::vector<std::string>{"It had crossed.", "The strait was calm.",
                                                                    "Dr. Vane stepped onto", "the stage."}));
}

TEST(FrameBuilder, ParagraphEndIsLastInItsGroup) {
  auto tokens = words("The Lamp Room The supply boat");
  tokens[2].flags = rsvp::TOKEN_PARAGRAPH_END;
  EXPECT_EQ(frameTexts(build(tokens, 4)), (std::vector<std::string>{"The Lamp Room", "The supply boat"}));
}

TEST(FrameBuilder, NonProseMarkerIsLastInItsGroupAndStops) {
  auto tokens = words("the landing stage with a bag");
  tokens[1].flags = rsvp::TOKEN_NON_PROSE_FOLLOWS;
  const auto frames = build(tokens, 4);
  EXPECT_EQ(frameTexts(frames), (std::vector<std::string>{"the landing", "stage with a bag"}));
  EXPECT_EQ(frames[0].stops, rsvp::STOP_NON_PROSE);
  EXPECT_EQ(frames[1].stops, rsvp::STOP_CHAPTER_END);
}

TEST(FrameBuilder, SectionEndIsLastInItsGroup) {
  auto tokens = words("did the same Winding the Clockwork and more");
  tokens[2].flags = rsvp::TOKEN_SECTION_END | rsvp::TOKEN_PARAGRAPH_END;  // no checkpoint
  tokens[5].flags = rsvp::TOKEN_SECTION_END;                              // section end without paragraph flag
  const auto frames = build(tokens, 4);
  EXPECT_EQ(frameTexts(frames), (std::vector<std::string>{"did the same", "Winding the Clockwork", "and more"}));
  EXPECT_EQ(frames[0].stops, 0);  // a section end without a checkpoint does not stop playback
  EXPECT_NE(frames[0].lastWord.tokenFlags & rsvp::TOKEN_SECTION_END, 0);
}

TEST(FrameBuilder, CheckpointSectionEndStops) {
  auto tokens = words("did the same. Winding the");
  tokens[2].flags = rsvp::TOKEN_SECTION_END | rsvp::TOKEN_CHECKPOINT | rsvp::TOKEN_PARAGRAPH_END;
  const auto frames = build(tokens, 3);
  ASSERT_EQ(frames.size(), 2u);
  EXPECT_EQ(frames[0].view(), "did the same.");
  EXPECT_EQ(frames[0].stops, rsvp::STOP_CHECKPOINT);
}

TEST(FrameBuilder, ChapterEndFromSourceOrFlag) {
  auto tokens = words("look like.");
  auto frames = build(tokens, 1);
  ASSERT_EQ(frames.size(), 2u);
  EXPECT_EQ(frames[0].stops, 0);
  EXPECT_EQ(frames[1].stops, rsvp::STOP_CHAPTER_END);
  EXPECT_TRUE(frames[1].lastWord.endsBlock());

  // A source may also flag the last word itself.
  tokens = words("a b c");
  tokens[1].flags = rsvp::TOKEN_CHAPTER_END;
  frames = build(tokens, 4);
  EXPECT_EQ(frames[0].view(), "a b");
  EXPECT_EQ(frames[0].stops, rsvp::STOP_CHAPTER_END);
}

TEST(FrameBuilder, StopsCombine) {
  auto tokens = words("end.");
  tokens[0].flags = rsvp::TOKEN_SECTION_END | rsvp::TOKEN_CHECKPOINT | rsvp::TOKEN_NON_PROSE_FOLLOWS;
  const auto frames = build(tokens, 1);
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0].stops, rsvp::STOP_CHECKPOINT | rsvp::STOP_NON_PROSE | rsvp::STOP_CHAPTER_END);
}

TEST(FrameBuilder, EmptySourceGivesNoFrame) {
  VectorSource source({});
  FrameBuilder builder(source);
  Frame frame;
  EXPECT_FALSE(builder.next(frame));
}

TEST(FrameBuilder, WidthLimitEndsTheGroup) {
  const auto tokens = words("a bb ccc dddd ee f");
  ByteLimit limit{8};
  const auto frames = build(tokens, 4, fitsByteLimit, &limit);
  EXPECT_EQ(frameTexts(frames), (std::vector<std::string>{"a bb ccc", "dddd ee", "f"}));
  for (const Frame& f : frames) EXPECT_FALSE(f.overWide);
  expectNoDropOrReorder(frames, tokens);
}

TEST(FrameBuilder, OverWideSingleWordIsShownAloneAndFlagged) {
  const auto tokens = words("a extraordinarily b c");
  ByteLimit limit{6};
  const auto frames = build(tokens, 4, fitsByteLimit, &limit);
  EXPECT_EQ(frameTexts(frames), (std::vector<std::string>{"a", "extraordinarily", "b c"}));
  EXPECT_FALSE(frames[0].overWide);
  EXPECT_TRUE(frames[1].overWide);
  EXPECT_FALSE(frames[2].overWide);
}

TEST(FrameBuilder, OverWideWordAtNEqualsOne) {
  const auto tokens = words("phosphorescence");
  ByteLimit limit{4};
  const auto frames = build(tokens, 1, fitsByteLimit, &limit);
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_TRUE(frames[0].overWide);
  EXPECT_EQ(limit.calls, 1);
}

TEST(FrameBuilder, FitCallbackSeesTheGroupFocus) {
  struct Seen {
    std::vector<std::string> texts;
    std::vector<uint16_t> focus;
  } seen;
  auto record = [](void* context, std::string_view text, uint16_t focus) {
    auto* s = static_cast<Seen*>(context);
    s->texts.emplace_back(text);
    s->focus.push_back(focus);
    return true;
  };
  build(words("The supply boat reached"), 4, record, &seen);
  ASSERT_EQ(seen.texts.size(), 4u);
  EXPECT_EQ(seen.texts[3], "The supply boat reached");
  EXPECT_EQ(seen.focus[0], 1);  // "The": 'h'
  EXPECT_EQ(seen.focus[1], 1);  // pair: first word
  EXPECT_EQ(seen.focus[2], 6);  // three words: "supply" at byte 4, 6 letters -> ordinal 2, 'p'
  EXPECT_EQ(seen.focus[3], 6);  // four words: the middle rounded left is still "supply"
}

TEST(FrameBuilder, FocusIsOnTheMiddleWord) {
  const auto frames = build(words("sat \"in the lamp room"), 3);
  ASSERT_EQ(frames.size(), 2u);
  // "sat "in the": middle word is "in, which focuses on 'n'.
  EXPECT_EQ(frames[0].focusWord, 1);
  EXPECT_EQ(frames[0].focusByteOffset, frames[0].wordOffset[1] + rsvp::focusLetter("\"in").byteOffset);
  EXPECT_EQ(frames[0].text[frames[0].focusByteOffset], 'n');
  EXPECT_EQ(frames[0].focusByteLength, 1);
  // "lamp room": a pair focuses on its first word.
  EXPECT_EQ(frames[1].focusWord, 0);
  EXPECT_EQ(frames[1].text[frames[1].focusByteOffset], 'a');
}

TEST(FrameBuilder, MultiByteFocusInAGroup) {
  const auto frames = build(words("small caf\xC3\xA9 na\xC3\xAFvet\xC3\xA9"), 3);
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0].focusWord, 1);
  EXPECT_EQ(frames[0].focusByteOffset, 7);  // "small " is 6 bytes, then 'a' of café
  const auto single = build(words("na\xC3\xAFvet\xC3\xA9"), 1);
  EXPECT_EQ(single[0].focusByteOffset, 2);
  EXPECT_EQ(single[0].focusByteLength, 2);
}

TEST(FrameBuilder, GroupIntervalIsTheSumOfMemberIntervals) {
  const std::string text =
      "Ione Hartwell, the keeper, met her at the foot of the stairs. She had kept the light for nineteen "
      "years, and she did not waste words. Phosphorescence; extraordinarily long-winded constitutional words!";
  for (const uint16_t wpm : {120, 240, 300, 600}) {
    const auto singles = build(words(text), 1, nullptr, nullptr, wpm);
    for (uint8_t n = 2; n <= 4; ++n) {
      const auto groups = build(words(text), n, nullptr, nullptr, wpm);
      size_t single = 0;
      for (const Frame& g : groups) {
        uint32_t sum = 0;
        for (uint8_t i = 0; i < g.wordCount; ++i) sum += singles[single++].holdMs;
        EXPECT_EQ(g.holdMs, sum) << "wpm " << wpm << " n " << int(n) << " frame " << std::string(g.view());
      }
      EXPECT_EQ(single, singles.size());
    }
  }
}

TEST(FrameBuilder, HoldUsesTheWeightTable) {
  // At 240 WPM a word of normalised weight 1 is 250 ms; check raw weights scale through.
  auto frames = build(words("and the cat."), 1);
  ASSERT_EQ(frames.size(), 3u);
  const uint64_t denominator = 240ULL * rsvp::PACING.normalisation;
  uint64_t numerator = 0;
  uint32_t expected[3];
  const uint16_t raw[3] = {rsvp::PACING.normal, rsvp::PACING.normal, rsvp::PACING.paragraph};
  for (int i = 0; i < 3; ++i) {
    numerator += 60000ULL * raw[i];
    expected[i] = static_cast<uint32_t>(numerator / denominator);
    numerator %= denominator;
  }
  for (int i = 0; i < 3; ++i) EXPECT_EQ(frames[i].holdMs, expected[i]) << i;
}

TEST(FrameBuilder, SetWpmChangesHolds) {
  VectorSource source(words("a b c d"));
  FrameBuilder builder(source);
  Frame frame;
  builder.setWpm(120);
  ASSERT_TRUE(builder.next(frame));
  const uint32_t slow = frame.holdMs;
  builder.setWpm(600);
  ASSERT_TRUE(builder.next(frame));
  EXPECT_GT(slow, frame.holdMs * 4);
  EXPECT_EQ(builder.wpm(), 600);
}

TEST(FrameBuilder, ConsumesOnlyWhatItShows) {
  VectorSource source(words("one two three. Four"));
  FrameBuilder builder(source);
  builder.setWordsPerFrame(4);
  Frame frame;
  ASSERT_TRUE(builder.next(frame));
  EXPECT_EQ(frame.view(), "one two three.");
  EXPECT_EQ(source.consumed(), 3u);  // "four" was peeked, not consumed
  // Repositioning the source is enough to replay; the builder keeps no word state.
  source.seek(1);
  builder.reset();
  ASSERT_TRUE(builder.next(frame));
  EXPECT_EQ(frame.view(), "two three.");
}

TEST(FrameBuilder, OverlongWordIsTruncatedAtACodePoint) {
  std::string longWord(rsvp::MAX_WORD_BYTES - 1, 'a');
  longWord += "\xC3\xA9\xC3\xA9";  // crosses the limit inside a two-byte letter
  VectorSource source({{longWord, 0, 0}, {"next", 1, 0}});
  FrameBuilder builder(source);
  builder.setWordsPerFrame(4);
  Frame frame;
  ASSERT_TRUE(builder.next(frame));
  EXPECT_TRUE(frame.truncated);
  EXPECT_EQ(frame.textLength, rsvp::MAX_WORD_BYTES - 1);
  EXPECT_EQ(frame.wordCount, 1);  // "next" no longer fits the frame buffer
  ASSERT_TRUE(builder.next(frame));
  EXPECT_FALSE(frame.truncated);
  EXPECT_EQ(frame.view(), "next");
}

TEST(FrameBuilder, BufferCapacityEndsTheGroup) {
  const std::string big(200, 'x');
  VectorSource source({{big, 0, 0}, {std::string(60, 'y'), 1, 0}, {"z", 2, 0}});
  FrameBuilder builder(source);
  builder.setWordsPerFrame(4);
  const auto frames = allFrames(builder);
  ASSERT_EQ(frames.size(), 2u);
  EXPECT_EQ(frames[0].wordCount, 1);
  EXPECT_EQ(frames[1].wordCount, 2);
  EXPECT_LE(frames[1].textLength, rsvp::FRAME_TEXT_CAPACITY);
}

TEST(FrameBuilder, PeeksOneTokenAhead) {
  VectorSource source(words("a b c d e f g h"));
  FrameBuilder builder(source);
  builder.setWordsPerFrame(3);
  const auto frames = allFrames(builder);
  EXPECT_EQ(frames.size(), 3u);
  // One peek per word to classify it, one per frame to start it, one final failed peek.
  EXPECT_LE(source.peeks, 8u + frames.size() + 1u);
}
