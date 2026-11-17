// Runs RsvpCore over the synthetic fixture book's ground truth (fixtures/books/fixture.expected.json,
// version 2): its documented word rule cases, and every chapter's word stream as FixtureBook.h hands
// it to RsvpCore (prose words with paragraph, non-prose and section flags).

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "FixtureBook.h"
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

using rsvptest::chapterTokens;

const testjson::Value& expected() { return rsvptest::fixtureExpected(); }
const testjson::Value& documentFor(std::string_view href) { return rsvptest::fixtureDocument(href); }

struct CaseExpectation {
  bool handedToRsvp;  // false: a TABLE or CODE word, which the source never hands to RsvpCore
  Pause pause;
  int letters;
};

constexpr CaseExpectation prose(Pause pause, int letters) { return {true, pause, letters}; }
constexpr CaseExpectation NOT_PROSE{false, Pause::NONE, 0};

// One case's word: its chapter's tokens and the position of the case's word among them.
struct CaseWord {
  std::vector<OwnedToken> tokens;
  size_t position;  // tokens.size() when the word is not handed to RsvpCore
};

CaseWord caseWord(const testjson::Value& c) {
  const std::string& name = rsvptest::requireString(c, "case", "token case");
  const long index = rsvptest::requireIndex(c, "word_index", name);
  const testjson::Value& document = documentFor(rsvptest::requireString(c, "href", name));
  const auto& words = rsvptest::requireArray(document, "words", name);
  EXPECT_LT(index, static_cast<long>(words.size())) << name;
  if (index >= 0 && index < static_cast<long>(words.size())) {
    EXPECT_EQ(words[static_cast<size_t>(index)].string, rsvptest::requireString(c, "word", name)) << name;
  }
  CaseWord out{chapterTokens(document), 0};
  out.position = rsvptest::tokenPosition(out.tokens, static_cast<size_t>(index));
  return out;
}

std::string nextText(const CaseWord& w) {
  return w.position + 1 < w.tokens.size() ? w.tokens[w.position + 1].text : std::string();
}

constexpr const char* ENDNOTES_HREF = "text/endnotes.xhtml";

// A document with every member FixtureBook.h reads missing, for the loud-failure test below.
const testjson::Value& documentWithoutMembers() {
  static const testjson::Value document = [] {
    testjson::Value v;
    // The parser keeps a reference to its text.
    static const std::string text = R"({"href": "text/broken.xhtml", "words": ["a", "b"]})";
    testjson::Parser parser(text);
    EXPECT_TRUE(parser.parse(v));
    return v;
  }();
  return document;
}

}  // namespace

// The failure messages `call` reports, captured instead of failing this test.
template <typename F>
std::string capturedFailures(F call) {
  testing::TestPartResultArray results;
  {
    testing::ScopedFakeTestPartResultReporter reporter(
        testing::ScopedFakeTestPartResultReporter::INTERCEPT_ONLY_CURRENT_THREAD, &results);
    call();
  }
  std::string out;
  for (int i = 0; i < results.size(); ++i) out += std::string(results.GetTestPartResult(i).message()) + "\n";
  return out;
}

TEST(FixtureBook, ReaderFailsLoudlyOnAMissingMember) {
  // No "markers" and no "flags": the reader must fail the test, never read them as empty or zero.
  const std::string failures = capturedFailures([] { chapterTokens(documentWithoutMembers()); });
  EXPECT_NE(failures.find("text/broken.xhtml has no member 'markers'"), std::string::npos) << failures;
  EXPECT_NE(failures.find("text/broken.xhtml has no member 'flags'"), std::string::npos) << failures;
  EXPECT_NE(capturedFailures([] { rsvptest::fixtureTokenCase("no such case"); }).find("no token case named"),
            std::string::npos);
  const std::string wrongType = capturedFailures([] {
    testjson::Value v;
    const std::string text = R"({"end": "12"})";
    testjson::Parser parser(text);
    EXPECT_TRUE(parser.parse(v));
    rsvptest::requireIndex(v, "end", "section");
  });
  EXPECT_NE(wrongType.find("section.end is a string, expected a number"), std::string::npos) << wrongType;
}

TEST(FixtureBook, TokensFollowTheExpectedFile) {
  const std::set<std::string> withCheckpoint = rsvptest::fixtureSectionsWithCheckpoint();
  const auto& documents = rsvptest::requireArray(expected(), "documents", "fixture.expected.json");
  ASSERT_EQ(documents.size(), 5u);  // title page, chapters 1 to 3, endnotes
  for (size_t d = 0; d < documents.size(); ++d) {
    const auto& document = documents[d];
    const std::string& href = rsvptest::requireString(document, "href", "document");
    SCOPED_TRACE(href);
    const auto& words = rsvptest::requireArray(document, "words", href);
    EXPECT_EQ(rsvptest::requireIndex(document, "word_count", href), static_cast<long>(words.size()));
    const auto tokens = chapterTokens(document);
    const auto markers = rsvptest::fixtureMarkers(document);

    // Exactly the words outside TABLE, CODE and NOTE regions, in order, with their own indexes.
    std::vector<size_t> prose;
    for (size_t w = 0; w < words.size(); ++w) {
      bool inside = false;
      for (const auto& m : markers) inside = inside || (m.kind != "IMAGE" && m.start <= w && w < m.end);
      if (!inside) prose.push_back(w);
    }
    ASSERT_EQ(tokens.size(), prose.size());
    for (size_t p = 0; p < tokens.size(); ++p) {
      EXPECT_EQ(tokens[p].wordIndex, prose[p]);
      EXPECT_EQ(tokens[p].text, words[prose[p]].string);
    }

    // A non-prose stop on the word right before every image, table and code block; none for notes.
    std::set<size_t> stopsAt;
    for (const auto& m : markers) {
      if (m.kind == "NOTE") continue;
      ASSERT_GT(m.start, 0u) << "the fixture has no non-prose item before its first word";
      stopsAt.insert(m.start - 1);  // in the fixture the word before a marker is always prose
    }
    std::set<size_t> flagged;
    for (const auto& t : tokens) {
      if (t.flags & rsvp::TOKEN_NON_PROSE_FOLLOWS) flagged.insert(t.wordIndex);
    }
    EXPECT_EQ(flagged, stopsAt);

    // Paragraph ends: every PARA_END or LINE_END word that is handed out, as IndexWordSource marks them.
    std::set<size_t> paraEnds;
    for (const char* flag : {"PARA_END", "LINE_END"}) {
      for (const auto& run : rsvptest::fixtureFlagRuns(document, flag)) {
        for (size_t w = run.first; w < run.second; ++w) paraEnds.insert(w);
      }
    }
    for (const auto& t : tokens) {
      EXPECT_EQ((t.flags & rsvp::TOKEN_PARAGRAPH_END) != 0, paraEnds.count(t.wordIndex) != 0) << t.text;
    }

    // Section ends on sections[].end - 1 (always prose in the fixture), checkpoints from the draft.
    std::map<size_t, bool> sectionEnds;
    const auto& sections = rsvptest::requireArray(expected(), "sections", "fixture.expected.json");
    for (size_t i = 0; i < sections.size(); ++i) {
      if (rsvptest::requireString(sections[i], "chapter_href", "section") != href) continue;
      const long end = rsvptest::requireIndex(sections[i], "end", "section");
      sectionEnds[static_cast<size_t>(end - 1)] =
          withCheckpoint.count(rsvptest::requireString(sections[i], "id", "section"));
    }
    for (const auto& t : tokens) {
      const auto it = sectionEnds.find(t.wordIndex);
      EXPECT_EQ((t.flags & rsvp::TOKEN_SECTION_END) != 0, it != sectionEnds.end()) << t.text;
      EXPECT_EQ((t.flags & rsvp::TOKEN_CHECKPOINT) != 0, it != sectionEnds.end() && it->second) << t.text;
    }
    size_t found = 0;
    for (const auto& t : tokens) found += sectionEnds.count(t.wordIndex);
    EXPECT_EQ(found, sectionEnds.size());
  }
}

// The endnotes document is one NOTE region: chapterTokens() hands RsvpCore an empty list for it,
// without a failure, and a source built on it ends the chapter at once.
TEST(FixtureBook, EndnotesDocumentHandsRsvpCoreNoWords) {
  const auto& document = documentFor(ENDNOTES_HREF);
  const auto& words = rsvptest::requireArray(document, "words", ENDNOTES_HREF);
  ASSERT_GT(words.size(), 0u);
  const auto markers = rsvptest::fixtureMarkers(document);
  ASSERT_EQ(markers.size(), 1u);
  EXPECT_EQ(markers[0].kind, "NOTE");
  EXPECT_EQ(markers[0].start, 0u);
  EXPECT_EQ(markers[0].end, words.size());
  const auto tokens = chapterTokens(document);
  EXPECT_TRUE(tokens.empty());
  rsvptest::VectorSource source(tokens);
  rsvp::Token token;
  EXPECT_FALSE(source.peek(token));
}

TEST(FixtureBook, TokenCasesAreClassifiedAsDocumented) {
  // Keyed by the case names in fixtures/books/README.md, "Word rule cases".
  const std::map<std::string, CaseExpectation> expectations = {
      {"Sentence ending in a full stop", prose(Pause::SENTENCE, 4)},
      {"Decimal number", prose(Pause::NONE, 2)},
      {"Abbreviation with a full stop", prose(Pause::NONE, 2)},
      {"No-break space U+00A0 (numeric reference) separates words: 'Dr.' and 'Vane' are two", prose(Pause::NONE, 4)},
      {"Sentence ending in a question mark", prose(Pause::SENTENCE, 6)},
      {"Comma", prose(Pause::CLAUSE, 8)},
      {"Quoted sentence: opening ASCII double quote attached to the first word", prose(Pause::NONE, 3)},
      {"Quoted sentence: closing double quote after the full stop", prose(Pause::SENTENCE, 7)},
      {"Figure caption: ordinary prose words after the IMAGE marker", prose(Pause::NONE, 6)},
      {"Word followed by a footnote mark (epub:type noteref form)", prose(Pause::SENTENCE, 3)},
      {"Semicolon", prose(Pause::CLAUSE, 7)},
      {"Sentence ending in an exclamation mark", prose(Pause::SENTENCE, 5)},
      {"Apostrophe inside a word", prose(Pause::NONE, 7)},
      {"Table cell: a word in the TABLE region", NOT_PROSE},
      {"Word before a `<br/>` line break", prose(Pause::CLAUSE, 5)},
      {"Word before the second `<br/>`", prose(Pause::SENTENCE, 7)},
      {"Hyphenated compound (one word)", prose(Pause::NONE, 10)},
      {"Em dash U+2014 with no spaces: a word ends after the dash", prose(Pause::CLAUSE, 5)},
      {"Word after an unspaced em dash", prose(Pause::NONE, 3)},
      {"Em dash U+2014 with no spaces (second)", prose(Pause::CLAUSE, 5)},
      {"Decomposed e + U+0301 is composed (NFC) to U+00E9", prose(Pause::NONE, 4)},
      {"Colon", prose(Pause::CLAUSE, 2)},
      {"Soft hyphens U+00AD are removed and never split", prose(Pause::CLAUSE, 15)},
      {"Word followed by a footnote mark (sup > a href, no epub:type)", prose(Pause::SENTENCE, 9)},
      {"Word inside an em element, comma outside it", prose(Pause::CLAUSE, 3)},
      {"Word inside a strong element", prose(Pause::NONE, 5)},
      {"Word followed by a footnote mark (a href > sup, no epub:type)", prose(Pause::SENTENCE, 6)},
      {"En dash U+2013 with no spaces: a word ends after the dash", prose(Pause::CLAUSE, 4)},
      {"Word after an unspaced en dash", prose(Pause::CLAUSE, 4)},
      {"Code in pre: a word in the CODE region", NOT_PROSE},
      {"Page-break marker inside a word: its text is in no word and does not split it", prose(Pause::NONE, 10)},
  };

  const auto& cases = rsvptest::requireArray(expected(), "token_cases", "fixture.expected.json");
  std::set<std::string> seen;
  for (size_t i = 0; i < cases.size(); ++i) {
    const auto& c = cases[i];
    const std::string& name = rsvptest::requireString(c, "case", "token_cases[" + std::to_string(i) + "]");
    seen.insert(name);
    const auto it = expectations.find(name);
    if (it == expectations.end()) {
      ADD_FAILURE() << "token case without an expectation here: " << name;
      continue;
    }
    const CaseWord w = caseWord(c);
    if (!it->second.handedToRsvp) {
      EXPECT_EQ(w.position, w.tokens.size()) << name << ": a non-prose word was handed to RsvpCore";
      continue;
    }
    ASSERT_LT(w.position, w.tokens.size()) << name << ": the word is not handed to RsvpCore";
    const OwnedToken& token = w.tokens[w.position];
    ASSERT_EQ(token.text, rsvptest::requireString(c, "word", name)) << name;
    const rsvp::WordClass cls = rsvp::classifyWord(token.text, token.flags, nextText(w));
    EXPECT_EQ(cls.pause, it->second.pause) << name << ": " << token.text;
    EXPECT_EQ(cls.letters, it->second.letters) << name;

    // The focus letter is always a letter, never a quote, dash, apostrophe or full stop.
    const rsvp::FocusLetter focus = rsvp::focusLetter(token.text);
    uint8_t length;
    EXPECT_TRUE(rsvp::text::isLetter(rsvp::text::decodeAt(token.text, focus.byteOffset, length))) << name;
  }
  for (const auto& entry : expectations) {
    EXPECT_TRUE(seen.count(entry.first)) << "expectation for a case the fixture no longer has: " << entry.first;
  }
}

TEST(FixtureBook, SpecificTokenDetails) {
  // "Dr. Vane": Dr. groups with Vane.
  const CaseWord dr = caseWord(rsvptest::fixtureTokenCase("Abbreviation with a full stop"));
  ASSERT_LT(dr.position + 1, dr.tokens.size());
  EXPECT_EQ(dr.tokens[dr.position + 1].text, "Vane");
  EXPECT_FALSE(
      rsvp::classifyWord(dr.tokens[dr.position].text, dr.tokens[dr.position].flags, nextText(dr)).isBoundary());
  // '"The' focuses on 'h', after the quote.
  const CaseWord quote =
      caseWord(rsvptest::fixtureTokenCase("Quoted sentence: opening ASCII double quote attached to the first word"));
  ASSERT_LT(quote.position, quote.tokens.size());
  EXPECT_EQ(rsvp::focusLetter(quote.tokens[quote.position].text).byteOffset, 2);
  // "café" focuses on 'a'; "phosphorescence," gets the top long-word step.
  const CaseWord cafe = caseWord(rsvptest::fixtureTokenCase("Decomposed e + U+0301 is composed (NFC) to U+00E9"));
  ASSERT_LT(cafe.position, cafe.tokens.size());
  EXPECT_EQ(cafe.tokens[cafe.position].text, "caf\xC3\xA9");
  EXPECT_EQ(rsvp::focusLetter(cafe.tokens[cafe.position].text).byteOffset, 1);
  const CaseWord longWord = caseWord(rsvptest::fixtureTokenCase("Soft hyphens U+00AD are removed and never split"));
  ASSERT_LT(longWord.position, longWord.tokens.size());
  const OwnedToken& t = longWord.tokens[longWord.position];
  EXPECT_EQ(t.text, "phosphorescence,");
  EXPECT_EQ(rsvp::rawWeight(rsvp::classifyWord(t.text, t.flags, nextText(longWord))),
            rsvp::PACING.clause + rsvp::PACING.longWord[2].extra);
  // "lines:", the word before a <br>, ends a block, as IndexWordSource marks LINE_END.
  const CaseWord lineEnd = caseWord(rsvptest::fixtureTokenCase("Word before a `<br/>` line break"));
  ASSERT_LT(lineEnd.position, lineEnd.tokens.size());
  EXPECT_NE(lineEnd.tokens[lineEnd.position].flags & rsvp::TOKEN_PARAGRAPH_END, 0);
}

TEST(FixtureBook, ChaptersGroupWithoutCrossingBoundaries) {
  struct Width {
    size_t maxBytes;
  } width{22};
  auto fits = [](void* context, std::string_view text, uint16_t) {
    return text.size() <= static_cast<Width*>(context)->maxBytes;
  };
  const std::set<std::string> withCheckpoint = rsvptest::fixtureSectionsWithCheckpoint();

  const auto& documents = rsvptest::requireArray(expected(), "documents", "fixture.expected.json");
  ASSERT_EQ(documents.size(), 5u);  // title page, chapters 1 to 3, endnotes
  for (size_t d = 0; d < documents.size(); ++d) {
    const auto& document = documents[d];
    const std::string& href = rsvptest::requireString(document, "href", "document");
    const auto tokens = chapterTokens(document);
    if (tokens.empty()) {
      // Only a document whose every word is a footnote body gives RsvpCore nothing to play.
      EXPECT_EQ(href, ENDNOTES_HREF);
      continue;
    }

    // Expected stops, from the expected file rather than from the tokens: a checkpoint at the last
    // word of every section that has one, a non-prose stop before every image, table and code block.
    std::set<size_t> checkpointWords;
    const auto& sections = rsvptest::requireArray(expected(), "sections", "fixture.expected.json");
    for (size_t i = 0; i < sections.size(); ++i) {
      if (rsvptest::requireString(sections[i], "chapter_href", "section") != href) continue;
      if (withCheckpoint.count(rsvptest::requireString(sections[i], "id", "section"))) {
        checkpointWords.insert(static_cast<size_t>(rsvptest::requireIndex(sections[i], "end", "section") - 1));
      }
    }
    std::set<size_t> nonProseWords;
    for (const auto& m : rsvptest::fixtureMarkers(document)) {
      if (m.kind != "NOTE") nonProseWords.insert(m.start - 1);
    }

    for (uint8_t n = 1; n <= rsvp::MAX_WORDS_PER_FRAME; ++n) {
      rsvptest::VectorSource source(tokens);
      rsvp::FrameBuilder builder(source, fits, &width);
      builder.setWordsPerFrame(n);
      const auto frames = rsvptest::allFrames(builder);

      size_t next = 0;
      std::set<size_t> checkpointStops;
      std::set<size_t> nonProseStops;
      for (size_t f = 0; f < frames.size(); ++f) {
        const rsvp::Frame& frame = frames[f];
        ASSERT_LE(frame.wordCount, n);
        for (uint8_t slot = 0; slot < frame.wordCount; ++slot, ++next) {
          ASSERT_LT(next, tokens.size());
          ASSERT_EQ(std::string(frame.word(slot)), tokens[next].text);
          const std::string following = next + 1 < tokens.size() ? tokens[next + 1].text : std::string();
          const bool boundary = rsvp::classifyWord(tokens[next].text, tokens[next].flags, following).isBoundary();
          // Only the last word of a frame may end a sentence or a block.
          if (slot + 1 < frame.wordCount) {
            EXPECT_FALSE(boundary) << tokens[next].text;
          }
        }
        // A frame never spans words the source did not hand out.
        EXPECT_EQ(frame.lastWordIndex - frame.firstWordIndex + 1u, frame.wordCount);
        const uint8_t lastFlags = tokens[next - 1].flags;
        EXPECT_EQ((frame.stops & rsvp::STOP_CHECKPOINT) != 0, (lastFlags & rsvp::TOKEN_CHECKPOINT) != 0);
        EXPECT_EQ((frame.stops & rsvp::STOP_NON_PROSE) != 0, (lastFlags & rsvp::TOKEN_NON_PROSE_FOLLOWS) != 0);
        EXPECT_EQ((frame.stops & rsvp::STOP_CHAPTER_END) != 0, f + 1 == frames.size());
        if (frame.stops & rsvp::STOP_CHECKPOINT) checkpointStops.insert(frame.lastWordIndex);
        if (frame.stops & rsvp::STOP_NON_PROSE) nonProseStops.insert(frame.lastWordIndex);
        if (frame.wordCount > 1) {
          EXPECT_LE(frame.textLength, width.maxBytes);
        }
      }
      EXPECT_EQ(next, tokens.size());
      EXPECT_EQ(checkpointStops, checkpointWords) << href;
      EXPECT_EQ(nonProseStops, nonProseWords) << href;
    }
    // The fixture's structure: checkpoints in chapters 1 and 2, one image, table or code block in each chapter.
    EXPECT_EQ(checkpointWords.size(), (href == "text/chapter-1.xhtml" || href == "text/chapter-2.xhtml") ? 1u : 0u)
        << href;
    EXPECT_EQ(nonProseWords.size(), href == "text/title.xhtml" ? 0u : 1u) << href;
  }
}
