// src/reading/: the index store's decision (valid / stale / corrupt / missing), the page <-> word
// hand-over, and sentence stepping, on real indexes of the fixture book (lib/WordIndex) checked against
// fixtures/books/fixture.expected.json through test/rsvp_core/FixtureBook.h.

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "FixtureBook.h"
#include "lib/WordIndex/IndexWordSource.h"
#include "reading/RsvpPosition.h"
#include "reading/WordIndexCheck.h"
#include "test/word_index/WordIndexTestSupport.h"

using reading::IndexState;
using wordindex::ReadStatus;

namespace {

const char* const CHAPTER_1 = "OEBPS/text/chapter-1.xhtml";
const char* const CHAPTER_2 = "OEBPS/text/chapter-2.xhtml";
const char* const CHAPTER_3 = "OEBPS/text/chapter-3.xhtml";

std::string chapterXhtml(const char* zipPath) {
  return wixtest::readFile(std::string(WIX_DATA_DIR "/original/") + zipPath);
}

uint32_t crcOf(const std::string& bytes) {
  return wordindex::crc32Update(0, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
}

// What ChapterWordIndex checks: open() and matchesSource() against the zip entry's size and CRC-32.
IndexState decide(wixtest::MemoryFile* file, const std::string& source, bool sourceKnown = true) {
  if (file == nullptr) return reading::checkIndex(false, ReadStatus::NotOpen, sourceKnown, false);
  wordindex::WordIndexReader reader;
  const ReadStatus status = reader.open(*file);
  return reading::checkIndex(true, status, sourceKnown,
                             reader.matchesSource(static_cast<uint32_t>(source.size()), crcOf(source)));
}

// Sentence starts as the RSVP stream sees them, from the ground truth: every prose token after a
// boundary word (and the chapter's first token).
std::vector<uint32_t> expectedSentenceStarts(const char* href) {
  const testjson::Value& document = rsvptest::fixtureDocument(href);
  std::vector<rsvptest::OwnedToken> tokens = rsvptest::chapterTokens(document);
  // IndexWordSource also ends a block at a <br> (LINE_END), which chapterTokens() leaves out.
  std::set<uint32_t> lineEnds;
  for (const auto& [start, end] : rsvptest::fixtureFlagRuns(document, "LINE_END")) {
    for (size_t i = start; i < end; ++i) lineEnds.insert(static_cast<uint32_t>(i));
  }
  for (auto& token : tokens) {
    if (lineEnds.count(token.wordIndex) != 0) token.flags |= rsvp::TOKEN_PARAGRAPH_END;
  }
  std::vector<uint32_t> starts;
  bool start = true;
  for (size_t i = 0; i < tokens.size(); ++i) {
    // Section ends come from the checkpoint data, which RSVP does not use yet.
    const uint8_t flags = tokens[i].flags & static_cast<uint8_t>(~(rsvp::TOKEN_SECTION_END | rsvp::TOKEN_CHECKPOINT));
    if (start) starts.push_back(tokens[i].wordIndex);
    const std::string_view next = i + 1 < tokens.size() ? std::string_view(tokens[i + 1].text) : std::string_view{};
    start = rsvp::classifyWord(tokens[i].text, flags, next).isBoundary();
  }
  return starts;
}

struct OpenIndex {
  explicit OpenIndex(const char* zipPath) : built(wixtest::buildIndex(chapterXhtml(zipPath))) {
    EXPECT_EQ(built.status, wordindex::BuildStatus::Ok);
    EXPECT_EQ(reader.open(built.file), ReadStatus::Ok);
  }
  wixtest::Built built;
  wordindex::WordIndexReader reader;
};

}  // namespace

// --- Index store decisions ---

TEST(IndexStore, MissingFileIsBuilt) {
  EXPECT_EQ(decide(nullptr, "x"), IndexState::Missing);
  EXPECT_TRUE(reading::needsBuild(IndexState::Missing));
}

TEST(IndexStore, IndexOfTheCurrentSourceIsValid) {
  const std::string source = chapterXhtml(CHAPTER_1);
  wixtest::Built built = wixtest::buildIndex(source);
  ASSERT_EQ(built.status, wordindex::BuildStatus::Ok);
  EXPECT_EQ(decide(&built.file, source), IndexState::Valid);
  EXPECT_FALSE(reading::needsBuild(IndexState::Valid));
}

TEST(IndexStore, IndexOfAnotherSourceIsStale) {
  const std::string source = chapterXhtml(CHAPTER_1);
  wixtest::Built built = wixtest::buildIndex(source);
  // The book was replaced: chapter 2 now sits where chapter 1 was.
  EXPECT_EQ(decide(&built.file, chapterXhtml(CHAPTER_2)), IndexState::Stale);
  // Same size, other bytes (one letter changed): the CRC-32 catches it.
  std::string edited = source;
  edited[edited.find("lighthouse")] = 'L';
  ASSERT_EQ(edited.size(), source.size());
  EXPECT_EQ(decide(&built.file, edited), IndexState::Stale);
}

TEST(IndexStore, CorruptOrTruncatedIndexIsRebuilt) {
  const std::string source = chapterXhtml(CHAPTER_1);
  wixtest::Built built = wixtest::buildIndex(source);
  wixtest::MemoryFile corrupt = built.file;
  corrupt.bytes[600] ^= 0x40;  // inside the first word block: caught when the block is read...
  wixtest::MemoryFile badTables = built.file;
  badTables.bytes[badTables.bytes.size() - 3] ^= 0x01;  // ...the tables' CRC is checked by open()
  wixtest::MemoryFile truncated = built.file;
  truncated.bytes.resize(truncated.bytes.size() - 20);
  wixtest::MemoryFile unfinished = built.file;
  std::fill(unfinished.bytes.begin(), unfinished.bytes.begin() + 512, 0);  // the header is written last
  EXPECT_EQ(decide(&badTables, source), IndexState::Unreadable);
  EXPECT_EQ(decide(&truncated, source), IndexState::Unreadable);
  EXPECT_EQ(decide(&unfinished, source), IndexState::Unreadable);
  wixtest::MemoryFile empty;
  EXPECT_EQ(decide(&empty, source), IndexState::Unreadable);
  // A damaged word block passes open() and fails on first read: the activity rebuilds then.
  wordindex::WordIndexReader reader;
  ASSERT_EQ(reader.open(corrupt), ReadStatus::Ok);
  wordindex::IndexWordSource words(reader);
  rsvp::Token token;
  while (words.peek(token)) words.advance();
  EXPECT_TRUE(words.failed());
  EXPECT_EQ(reader.status(), ReadStatus::Corrupt);
}

TEST(IndexStore, UnverifiableSourceIsRebuilt) {
  const std::string source = chapterXhtml(CHAPTER_1);
  wixtest::Built built = wixtest::buildIndex(source);
  EXPECT_EQ(decide(&built.file, source, false), IndexState::Unverifiable);
  EXPECT_TRUE(reading::needsBuild(IndexState::Unverifiable));
}

TEST(IndexStore, StateNames) {
  for (const IndexState s :
       {IndexState::Valid, IndexState::Missing, IndexState::Unreadable, IndexState::Stale, IndexState::Unverifiable}) {
    EXPECT_STRNE(reading::indexStateName(s), "?");
  }
}

// --- Page <-> word ---

TEST(Position, EntryIsTheFirstWordEndingAfterThePageStart) {
  OpenIndex ix(CHAPTER_1);
  wordindex::WordInfo w;
  ASSERT_TRUE(ix.reader.word(10, w));
  // A page starting at a word's start begins with that word; one starting inside it too.
  EXPECT_EQ(reading::entryWord(ix.reader, w.start), 10u);
  EXPECT_EQ(reading::entryWord(ix.reader, w.start + 1), 10u);
  // A page starting right at the word's end begins with the next word.
  EXPECT_EQ(reading::entryWord(ix.reader, w.end), 11u);
  EXPECT_EQ(reading::entryWord(ix.reader, 0), 0u);
  EXPECT_EQ(reading::entryWord(ix.reader, ix.reader.header().visibleCodepoints), ix.reader.wordCount());
}

TEST(Position, PageViewGoesToTheWordsStartAndRoundTrips) {
  OpenIndex ix(CHAPTER_1);
  for (const uint32_t word : {0u, 1u, 108u, 200u, 426u}) {
    uint32_t offset = 0;
    ASSERT_TRUE(reading::pageViewOffset(ix.reader, word, offset));
    wordindex::WordInfo w;
    ASSERT_TRUE(ix.reader.word(word, w));
    EXPECT_EQ(offset, w.start);
    // A page starting at that offset enters RSVP at the same word.
    EXPECT_EQ(reading::entryWord(ix.reader, offset), word);
  }
  // At the chapter end: the last word.
  uint32_t offset = 0;
  ASSERT_TRUE(reading::pageViewOffset(ix.reader, ix.reader.wordCount(), offset));
  wordindex::WordInfo last;
  ASSERT_TRUE(ix.reader.word(ix.reader.wordCount() - 1, last));
  EXPECT_EQ(offset, last.start);
}

// --- Where RSVP opens (task R4b) ---

namespace {

uint32_t startOf(wordindex::WordIndexReader& reader, const uint32_t word) {
  wordindex::WordInfo w;
  EXPECT_TRUE(reader.word(word, w));
  return w.start;
}

reading::SavedWord savedAt(const int chapter, const uint32_t word) {
  reading::SavedWord saved;
  saved.valid = true;
  saved.chapter = chapter;
  saved.word = word;
  return saved;
}

}  // namespace

TEST(EntryRule, WithoutASavedWordThePageDecides) {
  OpenIndex ix(CHAPTER_1);
  const uint32_t pageStart = startOf(ix.reader, 108);
  const reading::EntryChoice c =
      reading::chooseEntry(ix.reader, 1, pageStart, startOf(ix.reader, 200), reading::SavedWord{}, false);
  EXPECT_EQ(c.word, 108u);
  EXPECT_EQ(c.from, reading::EntryFrom::Page);
}

TEST(EntryRule, ASavedWordOnThePageShownIsKept) {
  // Page view after RSVP shows the page holding the saved word; entering again from that page starts
  // at that word, not at the page's first one.
  OpenIndex ix(CHAPTER_1);
  const uint32_t pageStart = startOf(ix.reader, 108);
  const uint32_t pageEnd = startOf(ix.reader, 200);
  for (const uint32_t word : {108u, 109u, 150u, 199u}) {
    const reading::EntryChoice c = reading::chooseEntry(ix.reader, 1, pageStart, pageEnd, savedAt(1, word), false);
    EXPECT_EQ(c.word, word);
    EXPECT_EQ(c.from, reading::EntryFrom::Saved);
  }
  // The chapter's last page (no next page): every later word of the chapter is on it.
  const reading::EntryChoice last =
      reading::chooseEntry(ix.reader, 1, pageStart, reading::NO_PAGE_END, savedAt(1, 426), false);
  EXPECT_EQ(last.word, 426u);
  EXPECT_EQ(last.from, reading::EntryFrom::Saved);
}

TEST(EntryRule, ASavedWordOffThePageIsIgnored) {
  OpenIndex ix(CHAPTER_1);
  const uint32_t pageStart = startOf(ix.reader, 108);
  const uint32_t pageEnd = startOf(ix.reader, 200);
  // Before the page (the reader paged on), on the next page (the reader paged back), another chapter,
  // past the chapter's words (a stale file).
  const reading::SavedWord cases[] = {savedAt(1, 107), savedAt(1, 0),   savedAt(1, 200),
                                      savedAt(1, 300), savedAt(2, 150), savedAt(1, ix.reader.wordCount())};
  for (const reading::SavedWord& saved : cases) {
    const reading::EntryChoice c = reading::chooseEntry(ix.reader, 1, pageStart, pageEnd, saved, false);
    EXPECT_EQ(c.word, 108u) << saved.chapter << ":" << saved.word;
    EXPECT_EQ(c.from, reading::EntryFrom::Page);
  }
}

TEST(EntryRule, AWordStraddlingThePageEndBelongsToThisPage) {
  // A page break inside word 199: it starts on this page, so it is this page's; the next page's entry
  // word is also 199 (its end is past that page's start), so both pages accept it.
  OpenIndex ix(CHAPTER_1);
  wordindex::WordInfo w;
  ASSERT_TRUE(ix.reader.word(199, w));
  const uint32_t pageStart = startOf(ix.reader, 108);
  const uint32_t pageBreak = w.start + 1;
  EXPECT_EQ(reading::chooseEntry(ix.reader, 1, pageStart, pageBreak, savedAt(1, 199), false).word, 199u);
  EXPECT_EQ(reading::chooseEntry(ix.reader, 1, pageBreak, reading::NO_PAGE_END, savedAt(1, 199), false).word, 199u);
  EXPECT_EQ(reading::chooseEntry(ix.reader, 1, pageStart, pageBreak, savedAt(1, 200), false).word, 108u);
}

TEST(EntryRule, ResumingTakesTheSavedWordWhateverThePage) {
  // The book opened in RSVP mode: the reader's page may be stale (power lost after a pause).
  OpenIndex ix(CHAPTER_1);
  const reading::EntryChoice c = reading::chooseEntry(ix.reader, 1, 0, startOf(ix.reader, 108), savedAt(1, 300), true);
  EXPECT_EQ(c.word, 300u);
  EXPECT_EQ(c.from, reading::EntryFrom::Resumed);
  // At most the chapter's end.
  EXPECT_EQ(reading::chooseEntry(ix.reader, 1, 0, reading::NO_PAGE_END, savedAt(1, 999999), true).word,
            ix.reader.wordCount());
  // Nothing saved for this chapter: the page decides.
  const reading::EntryChoice none =
      reading::chooseEntry(ix.reader, 1, 0, reading::NO_PAGE_END, reading::SavedWord{}, true);
  EXPECT_EQ(none.word, 0u);
  EXPECT_EQ(none.from, reading::EntryFrom::Page);
  EXPECT_STREQ(reading::entryFromName(reading::EntryFrom::Resumed), "resumed");
}

TEST(Position, ChapterPercent) {
  EXPECT_EQ(reading::chapterPercent(0, 400), 0);
  EXPECT_EQ(reading::chapterPercent(200, 400), 50);
  EXPECT_EQ(reading::chapterPercent(399, 400), 99);
  EXPECT_EQ(reading::chapterPercent(400, 400), 100);
  EXPECT_EQ(reading::chapterPercent(0, 0), 100);
}

// --- Sentence stepping ---

class SentenceStepping : public ::testing::TestWithParam<const char*> {};

TEST_P(SentenceStepping, ForwardVisitsEverySentenceStartInOrder) {
  OpenIndex ix(GetParam());
  const std::vector<uint32_t> starts =
      expectedSentenceStarts((std::string("text/") + (std::string(GetParam()).substr(11))).c_str());
  ASSERT_GT(starts.size(), 10u);
  reading::SentenceWindow window;
  uint32_t at = starts[0];
  std::vector<uint32_t> visited = {at};
  uint32_t next = 0;
  while (reading::sentenceForward(ix.reader, at, window, next)) {
    ASSERT_GT(next, at);
    visited.push_back(next);
    at = next;
  }
  EXPECT_EQ(visited, starts);
}

TEST_P(SentenceStepping, BackGoesToTheSentenceStartThenThePreviousOne) {
  OpenIndex ix(GetParam());
  const std::vector<uint32_t> starts =
      expectedSentenceStarts((std::string("text/") + (std::string(GetParam()).substr(11))).c_str());
  reading::SentenceWindow window;
  for (size_t s = 1; s < starts.size(); ++s) {
    uint32_t out = 0;
    // From a sentence's start: the previous sentence's start.
    ASSERT_TRUE(reading::sentenceBack(ix.reader, starts[s], window, out));
    EXPECT_EQ(out, starts[s - 1]) << "from " << starts[s];
  }
  // From inside a sentence (its second token): the start of that sentence.
  std::vector<rsvptest::OwnedToken> tokens =
      rsvptest::chapterTokens(rsvptest::fixtureDocument(std::string("text/") + std::string(GetParam()).substr(11)));
  const std::set<uint32_t> startSet(starts.begin(), starts.end());
  int checked = 0;
  for (size_t i = 1; i < tokens.size(); ++i) {
    if (startSet.count(tokens[i].wordIndex) != 0) continue;
    uint32_t out = 0;
    ASSERT_TRUE(reading::sentenceBack(ix.reader, tokens[i].wordIndex, window, out));
    const auto it = std::prev(startSet.upper_bound(tokens[i].wordIndex));
    EXPECT_EQ(out, *it) << "from " << tokens[i].wordIndex;
    ++checked;
  }
  EXPECT_GT(checked, 100);
  // From the chapter's first sentence: stays at its start.
  uint32_t out = 99;
  ASSERT_TRUE(reading::sentenceBack(ix.reader, starts[0], window, out));
  EXPECT_EQ(out, starts[0]);
  // From the chapter end: the start of the last sentence.
  ASSERT_TRUE(reading::sentenceBack(ix.reader, ix.reader.wordCount(), window, out));
  EXPECT_EQ(out, starts.back());
}

INSTANTIATE_TEST_SUITE_P(FixtureChapters, SentenceStepping, ::testing::Values(CHAPTER_1, CHAPTER_2, CHAPTER_3));

TEST(SentenceStepping, KnownStartsOfChapterOne) {
  // fixture.expected.json, chapter 1: "The Lamp Room" (heading), "The supply boat ... noon.", "It had ...".
  OpenIndex ix(CHAPTER_1);
  reading::SentenceWindow window;
  uint32_t out = 0;
  ASSERT_TRUE(reading::sentenceForward(ix.reader, 0, window, out));
  EXPECT_EQ(out, 3u);
  ASSERT_TRUE(reading::sentenceForward(ix.reader, 3, window, out));
  EXPECT_EQ(out, 13u);
  ASSERT_TRUE(reading::sentenceBack(ix.reader, 13, window, out));
  EXPECT_EQ(out, 3u);
  ASSERT_TRUE(reading::sentenceBack(ix.reader, 7, window, out));
  EXPECT_EQ(out, 3u);
  wordindex::WordInfo w;
  ASSERT_TRUE(ix.reader.word(13, w));
  EXPECT_EQ(w.text, "It");
}

TEST(SentenceStepping, ForwardStopsAtTheLastSentence) {
  OpenIndex ix(CHAPTER_1);
  const std::vector<uint32_t> starts = expectedSentenceStarts("text/chapter-1.xhtml");
  reading::SentenceWindow window;
  uint32_t out = 0;
  EXPECT_FALSE(reading::sentenceForward(ix.reader, starts.back(), window, out));
  EXPECT_FALSE(reading::sentenceForward(ix.reader, ix.reader.wordCount(), window, out));
}

TEST(SentenceStepping, LongSentencesBeyondTheWindowStillMove) {
  // One 600-word sentence: forward lands on the window's last word, back on a window start; both move.
  std::string body = "<p>";
  for (int i = 0; i < 600; ++i) body += "word ";
  body += "end.</p><p>Next sentence.</p>";
  wixtest::Built built = wixtest::buildIndex(wixtest::xhtml(body));
  ASSERT_EQ(built.status, wordindex::BuildStatus::Ok);
  wordindex::WordIndexReader reader;
  ASSERT_EQ(reader.open(built.file), ReadStatus::Ok);
  reading::SentenceWindow window;
  uint32_t out = 0;
  ASSERT_TRUE(reading::sentenceForward(reader, 0, window, out));
  EXPECT_EQ(out, reading::SentenceWindow::CAPACITY - 1);
  ASSERT_TRUE(reading::sentenceForward(reader, 500, window, out));
  EXPECT_EQ(out, 601u) << "the window reaches the boundary";
  ASSERT_TRUE(reading::sentenceBack(reader, 550, window, out));
  EXPECT_LT(out, 550u);
  ASSERT_TRUE(reading::sentenceBack(reader, 601, window, out));
  EXPECT_LT(out, 601u);
}
