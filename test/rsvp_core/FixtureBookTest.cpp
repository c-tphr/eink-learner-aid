// Runs RsvpCore over the synthetic fixture book's ground truth (fixtures/books/fixture.expected.json):
// its documented tokenisation cases, and every chapter's word stream with paragraph, non-prose and
// section flags.

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "TestJson.h"
#include "TestSupport.h"
#include "lib/RsvpCore/FocusLetter.h"
#include "lib/RsvpCore/FrameBuilder.h"
#include "lib/RsvpCore/Pacing.h"
#include "lib/RsvpCore/RsvpText.h"
#include "lib/RsvpCore/WordClassifier.h"

using rsvp::Pause;
using rsvptest::OwnedToken;

namespace {

const testjson::Value& loadJson(const char* name) {
  static std::map<std::string, testjson::Value> cache;
  auto it = cache.find(name);
  if (it == cache.end()) {
    const std::string text = rsvptest::readFile(std::string(RSVP_REPO_ROOT "/fixtures/books/") + name);
    testjson::Value value;
    testjson::Parser parser(text);
    EXPECT_TRUE(parser.parse(value)) << name;
    it = cache.emplace(name, std::move(value)).first;
  }
  return it->second;
}

const testjson::Value& expected() { return loadJson("fixture.expected.json"); }

// The chapter's word stream as a WordIndex-backed source would supply it.
std::vector<OwnedToken> chapterTokens(const testjson::Value& document) {
  std::vector<OwnedToken> tokens;
  const auto& words = document["words"];
  for (size_t i = 0; i < words.size(); ++i) tokens.push_back({words[i].string, static_cast<uint32_t>(i), 0});
  const auto& blocks = document["blocks"];
  for (size_t i = 0; i < blocks.size(); ++i) {
    const auto& block = blocks[i];
    const std::string& kind = block["kind"].string;
    if (kind == "heading" || kind == "paragraph") {
      tokens[block["last_word"].asInt()].flags |= rsvp::TOKEN_PARAGRAPH_END;
    } else if (block["after_word"].asInt() >= 0) {
      tokens[block["after_word"].asInt()].flags |= rsvp::TOKEN_NON_PROSE_FOLLOWS;
    }
  }

  std::set<std::string> withCheckpoint;
  const auto& checkpoints = loadJson("fixture.draft.json")["checkpoints"];
  for (size_t i = 0; i < checkpoints.size(); ++i) withCheckpoint.insert(checkpoints[i]["section"].string);
  const auto& sections = expected()["sections"];
  for (size_t i = 0; i < sections.size(); ++i) {
    const auto& section = sections[i];
    if (section["chapter_href"].string != document["href"].string) continue;
    uint8_t& flags = tokens[section["last_word"].asInt()].flags;
    flags |= rsvp::TOKEN_SECTION_END;
    if (withCheckpoint.count(section["id"].string)) flags |= rsvp::TOKEN_CHECKPOINT;
  }
  return tokens;
}

const testjson::Value& documentFor(const std::string& href) {
  const auto& documents = expected()["documents"];
  for (size_t i = 0; i < documents.size(); ++i) {
    if (documents[i]["href"].string == href) return documents[i];
  }
  static const testjson::Value missing;
  ADD_FAILURE() << "no document " << href;
  return missing;
}

struct CaseExpectation {
  Pause pause;
  int letters;  // -1: not checked
};

}  // namespace

TEST(FixtureBook, TokenCasesAreClassifiedAsDocumented) {
  // Keyed by the case names in fixtures/books/README.md, "Tokenisation cases".
  const std::map<std::string, CaseExpectation> expectations = {
      {"Sentence ending in a full stop", {Pause::SENTENCE, 4}},
      {"Decimal number (the point is not a sentence end)", {Pause::NONE, 2}},
      {"Abbreviation with a full stop (not a sentence end)", {Pause::NONE, 2}},
      {"Sentence ending in a question mark", {Pause::SENTENCE, 6}},
      {"Comma", {Pause::CLAUSE, 8}},
      {"Quoted sentence: opening ASCII double quote attached to the first word", {Pause::NONE, 3}},
      {"Quoted sentence: closing double quote after the full stop", {Pause::SENTENCE, 7}},
      {"Semicolon", {Pause::CLAUSE, 7}},
      {"Sentence ending in an exclamation mark", {Pause::SENTENCE, 5}},
      {"Apostrophe inside a word", {Pause::NONE, 7}},
      {"Hyphenated compound (one word)", {Pause::NONE, 10}},
      {"Em dash U+2014 with no spaces: joins its neighbours into one word", {Pause::CLAUSE, 8}},
      {"Em dash U+2014 with no spaces (second)", {Pause::CLAUSE, 10}},
      {"Non-ASCII letter: precomposed e acute U+00E9", {Pause::NONE, 4}},
      {"Colon", {Pause::CLAUSE, 2}},
      {"Long word (15 letters) with trailing comma", {Pause::CLAUSE, 15}},
      {"Word inside an em element, comma outside it", {Pause::CLAUSE, 3}},
      {"Word inside a strong element", {Pause::NONE, 5}},
  };

  const auto& cases = expected()["token_cases"];
  ASSERT_EQ(cases.size(), expectations.size()) << "a token case was added or removed: update this test";
  for (size_t i = 0; i < cases.size(); ++i) {
    const auto& c = cases[i];
    const std::string& name = c["case"].string;
    const auto it = expectations.find(name);
    ASSERT_NE(it, expectations.end()) << "unknown token case: " << name;

    const auto tokens = chapterTokens(documentFor(c["href"].string));
    const auto index = static_cast<size_t>(c["word_index"].asInt());
    ASSERT_LT(index, tokens.size());
    ASSERT_EQ(tokens[index].text, c["word"].string) << name;
    const std::string next = index + 1 < tokens.size() ? tokens[index + 1].text : std::string();
    const rsvp::WordClass cls = rsvp::classifyWord(tokens[index].text, tokens[index].flags, next);

    EXPECT_EQ(cls.pause, it->second.pause) << name << ": " << tokens[index].text;
    if (it->second.letters >= 0) {
      EXPECT_EQ(cls.letters, it->second.letters) << name;
    }

    // The focus letter is always a letter, never a quote, dash, apostrophe or full stop.
    const rsvp::FocusLetter focus = rsvp::focusLetter(tokens[index].text);
    uint8_t length;
    EXPECT_TRUE(rsvp::text::isLetter(rsvp::text::decodeAt(tokens[index].text, focus.byteOffset, length))) << name;
  }
}

TEST(FixtureBook, SpecificTokenDetails) {
  const auto& chapter1 = documentFor("text/chapter-1.xhtml");
  const auto tokens = chapterTokens(chapter1);
  // "Dr. Vane": Dr. groups with Vane.
  EXPECT_FALSE(rsvp::classifyWord(tokens[32].text, tokens[32].flags, tokens[33].text).isBoundary());
  // '"The' focuses on 'h', after the quote.
  EXPECT_EQ(rsvp::focusLetter(tokens[87].text).byteOffset, 2);
  // "café" (chapter 2) focuses on 'a'; "phosphorescence," gets the top long-word step.
  const auto chapter2 = chapterTokens(documentFor("text/chapter-2.xhtml"));
  EXPECT_EQ(rsvp::focusLetter(chapter2[329].text).byteOffset, 1);
  const rsvp::WordClass longWord = rsvp::classifyWord(chapter2[384].text, chapter2[384].flags, chapter2[385].text);
  EXPECT_EQ(rsvp::rawWeight(longWord), rsvp::PACING.clause + rsvp::PACING.longWord[2].extra);
}

TEST(FixtureBook, ChaptersGroupWithoutCrossingBoundaries) {
  struct Width {
    size_t maxBytes;
  } width{22};
  auto fits = [](void* context, std::string_view text, uint16_t) {
    return text.size() <= static_cast<Width*>(context)->maxBytes;
  };

  const auto& documents = expected()["documents"];
  for (size_t d = 0; d < documents.size(); ++d) {
    const auto tokens = chapterTokens(documents[d]);
    for (uint8_t n = 1; n <= rsvp::MAX_WORDS_PER_FRAME; ++n) {
      rsvptest::VectorSource source(tokens);
      rsvp::FrameBuilder builder(source, fits, &width);
      builder.setWordsPerFrame(n);
      const auto frames = rsvptest::allFrames(builder);

      size_t next = 0;
      size_t checkpointStops = 0;
      size_t nonProseStops = 0;
      for (size_t f = 0; f < frames.size(); ++f) {
        const rsvp::Frame& frame = frames[f];
        ASSERT_LE(frame.wordCount, n);
        for (uint8_t slot = 0; slot < frame.wordCount; ++slot, ++next) {
          ASSERT_EQ(std::string(frame.word(slot)), tokens[next].text);
          const std::string following = next + 1 < tokens.size() ? tokens[next + 1].text : std::string();
          const bool boundary = rsvp::classifyWord(tokens[next].text, tokens[next].flags, following).isBoundary();
          // Only the last word of a frame may end a sentence or a block.
          if (slot + 1 < frame.wordCount) {
            EXPECT_FALSE(boundary) << tokens[next].text;
          }
        }
        const uint8_t lastFlags = tokens[next - 1].flags;
        EXPECT_EQ((frame.stops & rsvp::STOP_CHECKPOINT) != 0, (lastFlags & rsvp::TOKEN_CHECKPOINT) != 0);
        EXPECT_EQ((frame.stops & rsvp::STOP_NON_PROSE) != 0, (lastFlags & rsvp::TOKEN_NON_PROSE_FOLLOWS) != 0);
        EXPECT_EQ((frame.stops & rsvp::STOP_CHAPTER_END) != 0, f + 1 == frames.size());
        if (frame.stops & rsvp::STOP_CHECKPOINT) ++checkpointStops;
        if (frame.stops & rsvp::STOP_NON_PROSE) ++nonProseStops;
        if (frame.wordCount > 1) {
          EXPECT_LE(frame.textLength, width.maxBytes);
        }
      }
      EXPECT_EQ(next, tokens.size());

      const std::string& href = documents[d]["href"].string;
      EXPECT_EQ(checkpointStops, (href == "text/chapter-1.xhtml" || href == "text/chapter-2.xhtml") ? 1u : 0u) << href;
      EXPECT_EQ(nonProseStops, href == "text/title.xhtml" ? 0u : 1u) << href;
    }
  }
}
