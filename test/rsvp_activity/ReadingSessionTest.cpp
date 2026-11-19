// Task C2: the pure decisions of ReadingSession (src/reading/ReadingSessionRules) on the prepared
// fixture book: its sections from the word index's anchors, the section-end flags RSVP's word source
// carries, the step at an RSVP checkpoint stop, the words of a page in page view, and where reading
// goes on after a checkpoint (trigger rules 3, 4, 5, 7 and 8 of lib/CheckpointCore/README.md). Also
// the state file rule for another edition with the open checkpoint index (src/reading/BookStateRules).

#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "FixtureBook.h"
#include "lib/CheckpointCore/BookState.h"
#include "lib/CheckpointCore/CheckpointFlow.h"
#include "lib/CheckpointCore/CheckpointIndex.h"
#include "lib/CheckpointCore/CheckpointSource.h"
#include "lib/CheckpointCore/CheckpointTrigger.h"
#include "lib/CheckpointCore/StateCodec.h"
#include "lib/RsvpCore/FrameBuilder.h"
#include "lib/WordIndex/IndexWordSource.h"
#include "reading/BookStateRules.h"
#include "reading/ReadingSessionRules.h"
#include "test/word_index/WordIndexTestSupport.h"

using checkpoint::ChapterSections;
using checkpoint::NO_CHAPTER;
using checkpoint::NO_INDEX;
using checkpoint::Position;
using checkpoint::StepKind;

namespace {

// Reader chapters (spine indexes) of the fixture: title 0, chapters 1 to 3, endnotes 4.
constexpr uint16_t CHAPTER_1 = 1;
constexpr uint16_t CHAPTER_2 = 2;
constexpr uint16_t CHAPTERS = 5;

std::string preparedFile(const char* zipPath) {
  return wixtest::readFile(std::string(WIX_DATA_DIR "/prepared/") + zipPath);
}

// The prepared fixture's checkpoint file, opened.
struct Checkpoints {
  Checkpoints() : bytes(preparedFile("OEBPS/rsvp/checkpoints.json")) {
    source = std::make_unique<checkpoint::MemoryCheckpointSource>(reinterpret_cast<const uint8_t*>(bytes.data()),
                                                                  bytes.size());
    EXPECT_EQ(index.open(*source).verdict, checkpoint::Verdict::Valid);
  }
  std::string bytes;
  std::unique_ptr<checkpoint::MemoryCheckpointSource> source;
  checkpoint::CheckpointIndex index;
};

// A prepared chapter's word index and its sections, loaded the way ReadingSession loads them.
struct Chapter {
  Chapter(Checkpoints& checkpoints, const char* zipPath, const char* href, uint16_t spine)
      : built(wixtest::buildIndex(preparedFile(zipPath))) {
    EXPECT_EQ(built.status, wordindex::BuildStatus::Ok);
    EXPECT_EQ(reader.open(built.file), wordindex::ReadStatus::Ok);
    const int hrefChapter = checkpoints.index.findChapter(href);
    EXPECT_GE(hrefChapter, 0);
    reading::ChapterLayout layout(checkpoints.index, CHAPTERS, spine, reader);
    const uint16_t next = spine + 1 < CHAPTERS ? static_cast<uint16_t>(spine + 1) : NO_CHAPTER;
    load = sections.load(checkpoints.index, static_cast<uint16_t>(hrefChapter), layout, spine, next);
  }
  uint32_t startOf(uint32_t word) {
    wordindex::WordInfo w;
    EXPECT_TRUE(reader.word(word, w));
    return w.start;
  }
  wixtest::Built built;
  wordindex::WordIndexReader reader;
  ChapterSections sections;
  ChapterSections::LoadResult load;
};

const char* const CH1 = "OEBPS/text/chapter-1.xhtml";
const char* const CH2 = "OEBPS/text/chapter-2.xhtml";

long expectedNumber(const char* section, const char* key) {
  return rsvptest::requireIndex(rsvptest::fixtureSection(section), key, section);
}

}  // namespace

// --- Sections from the anchors ---

TEST(ReadingSessionSections, AChaptersSectionsComeFromItsAnchors) {
  Checkpoints cp;
  Chapter ch1(cp, CH1, "text/chapter-1.xhtml", CHAPTER_1);
  ASSERT_TRUE(ch1.load.chapterKnown);
  EXPECT_EQ(ch1.load.added, 2u);
  EXPECT_EQ(ch1.load.failed, 0u);
  ASSERT_EQ(ch1.sections.count(), 2u);
  EXPECT_EQ(ch1.sections.span(0).start, static_cast<uint32_t>(expectedNumber("s0001", "start")));
  EXPECT_EQ(ch1.sections.span(0).end, static_cast<uint32_t>(expectedNumber("s0001", "end")));
  EXPECT_EQ(ch1.sections.span(0).checkpoint, static_cast<uint16_t>(cp.index.findCheckpoint("c0001")));
  EXPECT_EQ(ch1.sections.span(1).end, static_cast<uint32_t>(expectedNumber("s0002", "end")));
  EXPECT_EQ(ch1.sections.span(1).checkpoint, NO_INDEX);

  Chapter ch2(cp, CH2, "text/chapter-2.xhtml", CHAPTER_2);
  ASSERT_EQ(ch2.sections.count(), 2u);
  EXPECT_EQ(ch2.sections.span(1).start, static_cast<uint32_t>(expectedNumber("s0004", "start")));
  EXPECT_EQ(ch2.sections.span(1).end, ch2.reader.wordCount());
  EXPECT_EQ(ch2.sections.span(1).checkpoint, static_cast<uint16_t>(cp.index.findCheckpoint("c0002")));
}

TEST(ReadingSessionSections, OnlyItsOwnChapterIsKnown) {
  Checkpoints cp;
  Chapter ch1(cp, CH1, "text/chapter-1.xhtml", CHAPTER_1);
  reading::ChapterLayout layout(cp.index, CHAPTERS, CHAPTER_1, ch1.reader);
  uint32_t words = 0;
  EXPECT_TRUE(layout.chapterWordCount(CHAPTER_1, words));
  EXPECT_EQ(words, ch1.reader.wordCount());
  EXPECT_FALSE(layout.chapterWordCount(CHAPTER_2, words));
  // s0004's anchors are in chapter 2, not in this index.
  checkpoint::SectionRange range;
  EXPECT_FALSE(layout.sectionRange(static_cast<uint16_t>(cp.index.findSection("s0004")), range));
}

// --- RSVP (trigger rule 3) ---

TEST(ReadingSessionRsvp, TheSectionEndFlagsNameTheWordsStopAfterWordNames) {
  Checkpoints cp;
  for (const auto& [zip, href, spine] :
       {std::tuple{CH1, "text/chapter-1.xhtml", CHAPTER_1}, std::tuple{CH2, "text/chapter-2.xhtml", CHAPTER_2}}) {
    Chapter ch(cp, zip, href, spine);
    std::set<uint32_t> sectionLast;
    for (uint16_t i = 0; i < ch.sections.count(); ++i) sectionLast.insert(ch.sections.span(i).end - 1);
    wordindex::IndexWordSource source(
        ch.reader,
        [](void* context, uint32_t first, uint32_t last) {
          return reading::sectionEndFlags(*static_cast<const ChapterSections*>(context), first, last);
        },
        &ch.sections);
    rsvp::Token token;
    std::set<uint32_t> ends;
    std::set<uint32_t> checkpoints;
    while (source.peek(token)) {
      EXPECT_EQ((token.flags & rsvp::TOKEN_CHECKPOINT) != 0, checkpoint::stopAfterWord(ch.sections, token.wordIndex))
          << href << " word " << token.wordIndex;
      if (token.flags & rsvp::TOKEN_SECTION_END) ends.insert(token.wordIndex);
      if (token.flags & rsvp::TOKEN_CHECKPOINT) checkpoints.insert(token.wordIndex);
      source.advance();
    }
    EXPECT_EQ(ends, sectionLast) << href;
    EXPECT_EQ(checkpoints.size(), 1u) << href;
  }
}

TEST(ReadingSessionRsvp, AGroupOfTwoNeverCrossesASectionEnd) {
  Checkpoints cp;
  Chapter ch(cp, CH1, "text/chapter-1.xhtml", CHAPTER_1);
  const uint32_t last = ch.sections.span(0).end - 1;  // "same."
  wordindex::IndexWordSource source(
      ch.reader,
      [](void* context, uint32_t first, uint32_t lastAnchor) {
        return reading::sectionEndFlags(*static_cast<const ChapterSections*>(context), first, lastAnchor);
      },
      &ch.sections);
  rsvp::FrameBuilder frames(source);
  frames.setWordsPerFrame(2);
  rsvp::Frame frame;
  bool stopped = false;
  while (frames.next(frame)) {
    EXPECT_FALSE(frame.firstWordIndex <= last && frame.lastWordIndex > last)
        << "frame " << frame.firstWordIndex << "-" << frame.lastWordIndex;
    if (frame.stops & rsvp::STOP_CHECKPOINT) {
      EXPECT_EQ(frame.lastWordIndex, last);
      stopped = true;
    }
  }
  EXPECT_TRUE(stopped);
}

TEST(ReadingSessionRsvp, AGroupNeverCrossesASectionEndInTheMiddleOfASentence) {
  // The fixtures' section ends all fall on sentence ends; here the end anchor sits mid-sentence.
  const std::string xhtml =
      "<html xmlns=\"http://www.w3.org/1999/xhtml\"><body><p><span id=\"rsvp-s1-start\"></span>One two three "
      "four<span id=\"rsvp-s1-end\"></span> five six seven eight.</p></body></html>";
  wixtest::Built built = wixtest::buildIndex(xhtml);
  ASSERT_EQ(built.status, wordindex::BuildStatus::Ok);
  wordindex::WordIndexReader reader;
  ASSERT_EQ(reader.open(built.file), wordindex::ReadStatus::Ok);
  wordindex::Anchor start;
  wordindex::Anchor end;
  ASSERT_TRUE(reader.findAnchor("rsvp-s1-start", start));
  ASSERT_TRUE(reader.findAnchor("rsvp-s1-end", end));
  ASSERT_EQ(end.wordIndex, 4u);
  ChapterSections sections;
  sections.reset(0, reader.wordCount(), NO_CHAPTER);
  ASSERT_EQ(sections.add(0, start.wordIndex, end.wordIndex, 0), ChapterSections::AddResult::Added);
  wordindex::IndexWordSource source(
      reader,
      [](void* context, uint32_t first, uint32_t last) {
        return reading::sectionEndFlags(*static_cast<const ChapterSections*>(context), first, last);
      },
      &sections);
  rsvp::FrameBuilder frames(source);
  frames.setWordsPerFrame(2);
  rsvp::Frame frame;
  std::vector<std::pair<uint32_t, uint32_t>> groups;
  while (frames.next(frame)) {
    groups.emplace_back(frame.firstWordIndex, frame.lastWordIndex);
    if (frame.lastWordIndex == 3) EXPECT_TRUE(frame.stops & rsvp::STOP_CHECKPOINT);
  }
  // "One two", "three four" (stop), then "five six", "seven eight.": never "four five".
  const std::vector<std::pair<uint32_t, uint32_t>> expected = {{0, 1}, {2, 3}, {4, 5}, {6, 7}};
  EXPECT_EQ(groups, expected);
}

TEST(ReadingSessionRsvp, TheStopFiresWithTheResumeAfterTheBreak) {
  Checkpoints cp;
  Chapter ch1(cp, CH1, "text/chapter-1.xhtml", CHAPTER_1);
  checkpoint::Step step = reading::rsvpCheckpointStep(ch1.sections, 232, 233);
  ASSERT_EQ(step.kind, StepKind::Fire);
  EXPECT_EQ(step.checkpoint, static_cast<uint16_t>(cp.index.findCheckpoint("c0001")));
  EXPECT_EQ(step.resume, (Position{CHAPTER_1, 233}));
  // Rule 8: a section that ends its chapter resumes at the next chapter's first word.
  Chapter ch2(cp, CH2, "text/chapter-2.xhtml", CHAPTER_2);
  step = reading::rsvpCheckpointStep(ch2.sections, ch2.reader.wordCount() - 1, ch2.reader.wordCount());
  ASSERT_EQ(step.kind, StepKind::Fire);
  EXPECT_EQ(step.resume, (Position{3, 0}));
  // Any other word does not fire.
  EXPECT_EQ(reading::rsvpCheckpointStep(ch1.sections, 231, 232).kind, StepKind::None);
}

TEST(ReadingSessionRsvp, ASectionThatEndsInNoteWordsStillFires) {
  // Words 8 and 9 are note words RSVP passes over: the stop is on word 7, the next prose word is 10.
  ChapterSections sections;
  sections.reset(0, 20, 1);
  ASSERT_EQ(sections.add(0, 0, 10, 0), ChapterSections::AddResult::Added);
  const checkpoint::Step step = reading::rsvpCheckpointStep(sections, 7, 10);
  ASSERT_EQ(step.kind, StepKind::Fire);
  EXPECT_EQ(step.resume, (Position{0, 10}));
  EXPECT_EQ(reading::sectionEndFlags(sections, 8, 10), rsvp::TOKEN_SECTION_END | rsvp::TOKEN_CHECKPOINT);
}

// --- Page view (trigger rules 4, 5, 7 and 8) ---

TEST(ReadingSessionPage, ThePageShowingTheLastCharacterOfTheLastWordFires) {
  Checkpoints cp;
  Chapter ch(cp, CH1, "text/chapter-1.xhtml", CHAPTER_1);
  const uint32_t last = ch.sections.span(0).end - 1;  // 232
  // Page A ends inside the section's last word (hyphenated across pages): the word belongs to page B.
  const uint32_t pageA = ch.startOf(200);
  const uint32_t pageB = ch.startOf(last) + 1;
  const uint32_t pageC = ch.startOf(240);
  const reading::PageWords a = reading::pageWords(ch.reader, pageA, pageB);
  ASSERT_TRUE(a.any);
  EXPECT_EQ(a.first, 200u);
  EXPECT_EQ(a.last, last - 1);
  EXPECT_EQ(checkpoint::pageAdvance(ch.sections, a.first, a.last).kind, StepKind::None);
  const reading::PageWords b = reading::pageWords(ch.reader, pageB, pageC);
  EXPECT_EQ(b.first, last);
  EXPECT_EQ(b.last, 239u);
  const checkpoint::Step step = checkpoint::pageAdvance(ch.sections, b.first, b.last);
  ASSERT_EQ(step.kind, StepKind::Fire);
  EXPECT_EQ(step.resume, (Position{CHAPTER_1, last + 1}));
  // Backward moves never fire (rule 2).
  EXPECT_EQ(checkpoint::evaluateMove(ch.sections, Position{CHAPTER_1, 240}, Position{CHAPTER_1, 200},
                                     checkpoint::NavSource::PreviousPage)
                .kind,
            StepKind::None);
}

TEST(ReadingSessionPage, AfterTheCheckpointTheSamePageCountsFromTheResumeWord) {
  Checkpoints cp;
  Chapter ch(cp, CH1, "text/chapter-1.xhtml", CHAPTER_1);
  const reading::PageWords page = reading::pageWords(ch.reader, ch.startOf(220), ch.startOf(250));
  const checkpoint::Step step = checkpoint::pageAdvance(ch.sections, page.first, page.last);
  ASSERT_EQ(step.kind, StepKind::Fire);
  EXPECT_EQ(reading::pageResumeFor(CHAPTER_1, ch.reader.wordCount(), page, step.resume), reading::PageResume::ThisPage);
  reading::PageReading reading{true, CHAPTER_1, step.resume.word};
  const uint32_t from = reading::pageFromWord(page, CHAPTER_1, reading);
  EXPECT_EQ(from, step.resume.word);
  EXPECT_EQ(checkpoint::pageAdvance(ch.sections, from, page.last).kind, StepKind::None);
  // Rule 5: once the reading word is forgotten (the reader left the page), the page fires again.
  reading.valid = false;
  EXPECT_EQ(checkpoint::pageAdvance(ch.sections, reading::pageFromWord(page, CHAPTER_1, reading), page.last).kind,
            StepKind::Fire);
  // A reading word of another chapter or off the page is not used.
  EXPECT_EQ(reading::pageFromWord(page, CHAPTER_1, {true, CHAPTER_2, 233}), page.first);
  EXPECT_EQ(reading::pageFromWord(page, CHAPTER_1, {true, CHAPTER_1, 300}), page.first);
}

TEST(ReadingSessionPage, TwoSectionEndsOnOnePageFireInTextOrder) {
  // Rule 7: sections [0,5) and [5,9), both with a checkpoint, end on the page holding words 3 to 12.
  ChapterSections sections;
  sections.reset(0, 30, 1);
  ASSERT_EQ(sections.add(0, 0, 5, 0), ChapterSections::AddResult::Added);
  ASSERT_EQ(sections.add(1, 5, 9, 1), ChapterSections::AddResult::Added);
  const reading::PageWords page{true, 3, 12};
  checkpoint::Step step = checkpoint::pageAdvance(sections, page.first, page.last);
  ASSERT_EQ(step.kind, StepKind::Fire);
  EXPECT_EQ(step.checkpoint, 0u);
  EXPECT_EQ(step.crossed, 2u);
  ASSERT_EQ(reading::pageResumeFor(0, 30, page, step.resume), reading::PageResume::ThisPage);
  reading::PageReading reading{true, 0, step.resume.word};
  step = checkpoint::pageAdvance(sections, reading::pageFromWord(page, 0, reading), page.last);
  ASSERT_EQ(step.kind, StepKind::Fire);
  EXPECT_EQ(step.checkpoint, 1u);
  EXPECT_EQ(step.resume, (Position{0, 9}));
  reading.word = step.resume.word;
  EXPECT_EQ(checkpoint::pageAdvance(sections, reading::pageFromWord(page, 0, reading), page.last).kind, StepKind::None);
}

TEST(ReadingSessionPage, ASectionThatEndsItsChapterFiresOnLeavingTheLastPage) {
  Checkpoints cp;
  Chapter ch(cp, CH2, "text/chapter-2.xhtml", CHAPTER_2);
  const uint32_t words = ch.reader.wordCount();
  const reading::PageWords last = reading::pageWords(ch.reader, ch.startOf(words - 20), reading::NO_NEXT_PAGE);
  EXPECT_EQ(last.last, words - 1);
  const checkpoint::Step step = checkpoint::pageAdvance(ch.sections, last.first, last.last);
  ASSERT_EQ(step.kind, StepKind::Fire);
  EXPECT_EQ(step.resume, (Position{3, 0}));
  EXPECT_EQ(reading::pageResumeFor(CHAPTER_2, words, last, step.resume), reading::PageResume::Chapter);
  // The page before it does not.
  const reading::PageWords before = reading::pageWords(ch.reader, ch.startOf(words - 60), ch.startOf(words - 20));
  EXPECT_EQ(checkpoint::pageAdvance(ch.sections, before.first, before.last).kind, StepKind::None);
}

TEST(ReadingSessionPage, ResumeKinds) {
  const reading::PageWords page{true, 100, 150};
  EXPECT_EQ(reading::pageResumeFor(1, 400, page, {1, 120}), reading::PageResume::ThisPage);
  EXPECT_EQ(reading::pageResumeFor(1, 400, page, {1, 151}), reading::PageResume::LaterPage);
  EXPECT_EQ(reading::pageResumeFor(1, 400, page, {2, 0}), reading::PageResume::Chapter);
  // The last chapter of the book: (chapter, word count).
  EXPECT_EQ(reading::pageResumeFor(1, 400, page, {1, 400}), reading::PageResume::EndOfBook);
  ChapterSections sections;
  sections.reset(4, 20, NO_CHAPTER);
  ASSERT_EQ(sections.add(0, 0, 20, 0), ChapterSections::AddResult::Added);
  const checkpoint::Step step = checkpoint::pageAdvance(sections, 10, 19);
  ASSERT_EQ(step.kind, StepKind::Fire);
  EXPECT_EQ(reading::pageResumeFor(4, 20, reading::PageWords{true, 10, 19}, step.resume),
            reading::PageResume::EndOfBook);
  EXPECT_STREQ(reading::pageResumeName(reading::PageResume::LaterPage), "later-page");
}

TEST(ReadingSessionPage, APageWithoutWordsOfItsOwnFiresNothing) {
  Checkpoints cp;
  Chapter ch(cp, CH1, "text/chapter-1.xhtml", CHAPTER_1);
  // An image page starts at the same offset as the page after it.
  const uint32_t at = ch.startOf(232);
  EXPECT_FALSE(reading::pageWords(ch.reader, at, at).any);
  EXPECT_FALSE(reading::pageWords(ch.reader, at, at - 1).any);
}

// --- The state file of another edition, with the open index ---

TEST(ReadingSessionState, AnotherEditionKeepsOnlyTheGradesOfThisBooksPrompts) {
  Checkpoints cp;
  checkpoint::BookState state;
  const std::string other = R"({
  "version": 1,
  "book_sha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
  "overrides": { "wpm": 300 },
  "position": { "chapter": 1, "word": 100, "mode": "page" },
  "grades": {
    "c0001-p1": { "first": "correct" },
    "c9999-p1": { "first": "incorrect" }
  }
}
)";
  const reading::StateLoad load = reading::decodeForUse(reinterpret_cast<const uint8_t*>(other.data()), other.size(),
                                                        cp.index.sourceSha256(), &cp.index, state);
  EXPECT_EQ(load.source, reading::StateSource::Read);
  EXPECT_TRUE(load.shaMismatch);
  EXPECT_STREQ(state.bookSha256, cp.index.sourceSha256());
  EXPECT_FALSE(state.hasPosition);
  EXPECT_TRUE(state.overrides.hasWpm);
  EXPECT_EQ(state.grades.find("c0001-p1"), checkpoint::Grade::Correct);
  EXPECT_EQ(state.grades.find("c9999-p1"), checkpoint::Grade::Ungraded);

  // The same edition: nothing changes, grades of ids the file no longer has are kept.
  std::string same = other;
  same.replace(same.find("bbbb"), 64, cp.index.sourceSha256());
  const reading::StateLoad again = reading::decodeForUse(reinterpret_cast<const uint8_t*>(same.data()), same.size(),
                                                         cp.index.sourceSha256(), &cp.index, state);
  EXPECT_FALSE(again.shaMismatch);
  EXPECT_TRUE(state.hasPosition);
  EXPECT_EQ(state.grades.find("c9999-p1"), checkpoint::Grade::Incorrect);

  // A file without a hash takes the book's.
  std::string none = other;
  none.erase(none.find("  \"book_sha256\""), none.find("  \"overrides\"") - none.find("  \"book_sha256\""));
  ASSERT_EQ(reading::decodeForUse(reinterpret_cast<const uint8_t*>(none.data()), none.size(), cp.index.sourceSha256(),
                                  &cp.index, state)
                .source,
            reading::StateSource::Read);
  EXPECT_STREQ(state.bookSha256, cp.index.sourceSha256());
  EXPECT_TRUE(state.hasPosition);
}

// --- Rereading (task C4): what ReadingSession passes to the trigger rules and the flow ---

namespace {

// c0001's section s0001 is chapter 1 words [0, 233); s0002 (no checkpoint) follows it to the chapter's end.
uint16_t sectionNumber(const Checkpoints& cp, const char* id) {
  return static_cast<uint16_t>(cp.index.findSection(id));
}

}  // namespace

TEST(ReadingSessionReread, RsvpStopsAtTheRereadSectionsEndWithoutFiring) {
  Checkpoints cp;
  Chapter ch1(cp, CH1, "text/chapter-1.xhtml", CHAPTER_1);
  const uint16_t s0001 = sectionNumber(cp, "s0001");
  // Trigger rule 6: the reread section's own end gives RereadEnd, never Fire.
  const checkpoint::Step step = reading::rsvpCheckpointStep(ch1.sections, 232, 233, s0001);
  EXPECT_EQ(step.kind, StepKind::RereadEnd);
  EXPECT_EQ(step.section.section, s0001);
  EXPECT_EQ(step.section.start, 0u);
  EXPECT_EQ(step.section.end, 233u);
  // While another section is being reread, c0001's end fires nothing either.
  EXPECT_EQ(reading::rsvpCheckpointStep(ch1.sections, 232, 233, sectionNumber(cp, "s0004")).kind, StepKind::None);
  // The word source still flags the end, so the scheduler stops there and asks.
  EXPECT_NE(reading::sectionEndFlags(ch1.sections, 232, 233) & rsvp::TOKEN_CHECKPOINT, 0);
  EXPECT_TRUE(checkpoint::stopAfterWord(ch1.sections, 232, s0001));
}

TEST(ReadingSessionReread, PageViewLeavingTheLastPageOfTheRereadReturnsInsteadOfFiring) {
  Checkpoints cp;
  Chapter ch2(cp, CH2, "text/chapter-2.xhtml", CHAPTER_2);
  const uint16_t s0004 = sectionNumber(cp, "s0004");
  const uint32_t words = ch2.reader.wordCount();
  const reading::PageWords last = reading::pageWords(ch2.reader, ch2.startOf(490), reading::NO_NEXT_PAGE);
  ASSERT_EQ(last.last, words - 1);
  EXPECT_EQ(checkpoint::pageAdvance(ch2.sections, last.first, last.last).kind, StepKind::Fire);
  EXPECT_EQ(checkpoint::pageAdvance(ch2.sections, last.first, last.last, s0004).kind, StepKind::RereadEnd);
  // The pages before it within the section read on.
  const reading::PageWords before = reading::pageWords(ch2.reader, ch2.startOf(381), ch2.startOf(490));
  EXPECT_EQ(checkpoint::pageAdvance(ch2.sections, before.first, before.last, s0004).kind, StepKind::None);
}

TEST(ReadingSessionReread, APageKeepsTheRereadWhileItShowsAWordOfTheSection) {
  Checkpoints cp;
  Chapter ch2(cp, CH2, "text/chapter-2.xhtml", CHAPTER_2);
  const checkpoint::SectionRange range =
      ch2.sections.range(static_cast<uint16_t>(ch2.sections.findSection(sectionNumber(cp, "s0004"))));
  ASSERT_EQ(range.start, 311u);
  ASSERT_EQ(range.end, 608u);
  const reading::PageReading none;
  // The section's first page starts before it (words 282-380): inside, counted from the section's start.
  const reading::PageWords first = reading::pageWords(ch2.reader, ch2.startOf(282), ch2.startOf(381));
  reading::RereadPagePlace p = reading::rereadPagePlace(first, CHAPTER_2, none, range);
  EXPECT_TRUE(p.inside);
  EXPECT_EQ(p.word, 311u);
  // ... or from the reading word when it lies later on that page (RSVP left to page view there).
  p = reading::rereadPagePlace(first, CHAPTER_2, {true, CHAPTER_2, 350}, range);
  EXPECT_TRUE(p.inside);
  EXPECT_EQ(p.word, 350u);
  // A page inside it: its first word.
  const reading::PageWords middle = reading::pageWords(ch2.reader, ch2.startOf(381), ch2.startOf(490));
  p = reading::rereadPagePlace(middle, CHAPTER_2, none, range);
  EXPECT_TRUE(p.inside);
  EXPECT_EQ(p.word, 381u);
  // A page before it (paging back past the start): outside.
  const reading::PageWords earlier = reading::pageWords(ch2.reader, ch2.startOf(200), ch2.startOf(282));
  p = reading::rereadPagePlace(earlier, CHAPTER_2, none, range);
  EXPECT_FALSE(p.inside);
  EXPECT_EQ(p.word, 200u);
  // Another chapter (a table-of-contents jump): outside.
  EXPECT_FALSE(reading::rereadPagePlace(middle, CHAPTER_1, none, range).inside);
  // A page with no word of its own counts from the word after it.
  const reading::PageWords image{false, 400, 0};
  p = reading::rereadPagePlace(image, CHAPTER_2, none, range);
  EXPECT_TRUE(p.inside);
  EXPECT_EQ(p.word, 400u);
  EXPECT_FALSE(reading::rereadPagePlace(reading::PageWords{false, 608, 0}, CHAPTER_2, none, range).inside);
}

TEST(ReadingSessionReread, StepsAndEntriesStayInsideTheSection) {
  const checkpoint::SectionRange range{0, 2, 311, 608};
  EXPECT_EQ(reading::intoSection(range, 0), 311u);
  EXPECT_EQ(reading::intoSection(range, 400), 400u);
  EXPECT_EQ(reading::intoSection(range, 608), 607u);
  EXPECT_TRUE(reading::rereadStepStays(range, 311));
  EXPECT_TRUE(reading::rereadStepStays(range, 607));
  EXPECT_FALSE(reading::rereadStepStays(range, 310)) << "a sentence step back across the start does not move";
  EXPECT_FALSE(reading::rereadStepStays(range, 608)) << "nor one forward across the end";
}

TEST(ReadingSessionReread, AfterARereadPageViewNeverStaysOnThisPage) {
  // The page that fired is no longer the one shown: with no page words, the resume is a later page.
  const reading::PageWords none;
  EXPECT_EQ(reading::pageResumeFor(1, 400, none, {1, 233}), reading::PageResume::LaterPage);
  EXPECT_EQ(reading::pageResumeFor(2, 608, none, {3, 0}), reading::PageResume::Chapter);
}

TEST(ReadingSessionReread, TheFlowFollowsThePagesOfAPageViewReread) {
  // The decisions above driving lib/CheckpointCore's flow as ReadingSession does: c0001 fires, Incorrect,
  // reread in page view, pages inside the section move the reread place, paging past the end or leaving
  // the section goes back to the prompt; the resume place and the first grade never change.
  Checkpoints cp;
  Chapter ch1(cp, CH1, "text/chapter-1.xhtml", CHAPTER_1);
  auto state = std::make_unique<checkpoint::BookState>();
  checkpoint::CheckpointFlow flow(*state, cp.index);
  const reading::PageWords fired = reading::pageWords(ch1.reader, ch1.startOf(184), ch1.startOf(295));
  const checkpoint::Step fire = checkpoint::pageAdvance(ch1.sections, fired.first, fired.last);
  ASSERT_EQ(fire.kind, StepKind::Fire);
  ASSERT_TRUE(flow.start(fire, checkpoint::ReadingMode::Page).accepted);
  ASSERT_TRUE(flow.handle(checkpoint::FlowEvent::Reveal).accepted);
  ASSERT_TRUE(flow.handle(checkpoint::FlowEvent::Incorrect).gradeRecorded);
  ASSERT_TRUE(flow.handle(checkpoint::FlowEvent::RereadPage).accepted);
  const checkpoint::Place resume = state->active.resume;
  EXPECT_EQ(resume.position, (Position{CHAPTER_1, 233}));

  const reading::PageReading none;
  const auto moveTo = [&](const reading::PageWords& page, uint16_t chapter, checkpoint::NavSource source) {
    const reading::RereadPagePlace p = reading::rereadPagePlace(page, chapter, none, flow.section());
    return flow.rereadMoved(checkpoint::Place{Position{chapter, p.word}, checkpoint::ReadingMode::Page}, source);
  };
  const reading::PageWords page2 = reading::pageWords(ch1.reader, ch1.startOf(108), ch1.startOf(184));
  checkpoint::Transition t = moveTo(page2, CHAPTER_1, checkpoint::NavSource::NextPage);
  EXPECT_EQ(t.state, checkpoint::FlowState::Rereading);
  EXPECT_EQ(state->active.reread.position, (Position{CHAPTER_1, 108}));
  EXPECT_EQ(flow.rereadSection(), sectionNumber(cp, "s0001"));
  // The next-page action from the page with the section's last word ends the reread (as the reader asks).
  EXPECT_EQ(checkpoint::pageAdvance(ch1.sections, fired.first, fired.last, flow.rereadSection()).kind,
            StepKind::RereadEnd);
  t = flow.handle(checkpoint::FlowEvent::RereadReachedEnd);
  EXPECT_EQ(t.state, checkpoint::FlowState::PromptHidden);
  EXPECT_EQ(flow.storedGrade(), checkpoint::Grade::Incorrect) << "shown as recorded on the prompt";

  // Again, then a jump to another chapter: back to the prompt as Return early.
  ASSERT_TRUE(flow.handle(checkpoint::FlowEvent::Reveal).accepted);
  ASSERT_FALSE(flow.handle(checkpoint::FlowEvent::Incorrect).gradeRecorded) << "the first grade stays";
  ASSERT_TRUE(flow.handle(checkpoint::FlowEvent::RereadPage).accepted);
  Chapter ch2(cp, CH2, "text/chapter-2.xhtml", CHAPTER_2);
  const reading::PageWords elsewhere = reading::pageWords(ch2.reader, 0, ch2.startOf(50));
  t = moveTo(elsewhere, CHAPTER_2, checkpoint::NavSource::TableOfContents);
  EXPECT_EQ(t.state, checkpoint::FlowState::PromptHidden);
  EXPECT_TRUE(t.leftSection);
  // Correct on the retry moves on and finishes; the resume place is the original one.
  ASSERT_TRUE(flow.handle(checkpoint::FlowEvent::Reveal).accepted);
  t = flow.handle(checkpoint::FlowEvent::Correct);
  EXPECT_TRUE(t.finished);
  EXPECT_FALSE(t.gradeRecorded);
  EXPECT_EQ(t.resume, resume);
  EXPECT_EQ(state->grades.find("c0001-p1"), checkpoint::Grade::Incorrect);
}

// --- Opening a book with a checkpoint in progress (task C5): the plan, over every chapter's index ---

namespace {

// The word index of every reader chapter of the prepared fixture, opened as BookIndexLayout asks.
struct BookIndexes {
  BookIndexes() {
    const char* const files[CHAPTERS] = {"OEBPS/text/title.xhtml", CH1, CH2, "OEBPS/text/chapter-3.xhtml",
                                         "OEBPS/text/endnotes.xhtml"};
    for (uint16_t c = 0; c < CHAPTERS; ++c) {
      built[c] = wixtest::buildIndex(preparedFile(files[c]));
      EXPECT_EQ(built[c].status, wordindex::BuildStatus::Ok);
      EXPECT_EQ(readers[c].open(built[c].file), wordindex::ReadStatus::Ok);
    }
  }
  static wordindex::WordIndexReader* open(void* context, uint16_t chapter) {
    auto* self = static_cast<BookIndexes*>(context);
    return chapter < CHAPTERS ? &self->readers[chapter] : nullptr;
  }
  // "text/chapter-<n>.xhtml" is spine n in the fixture.
  static int spineOf(void* context, uint16_t section) {
    const auto* self = static_cast<BookIndexes*>(context);
    const std::string href = self->index->sectionChapterHref(section);
    if (href.rfind("text/chapter-", 0) != 0) return -1;
    return href[13] - '0';
  }
  wixtest::Built built[CHAPTERS];
  wordindex::WordIndexReader readers[CHAPTERS];
  const checkpoint::CheckpointIndex* index = nullptr;
};

// A state file of the open fixture with `active` as its active_checkpoint member (empty: none).
std::string stateWith(const Checkpoints& cp, const std::string& active) {
  std::string s = std::string("{\"version\": 1, \"book_sha256\": \"") + cp.index.sourceSha256() +
                  "\", \"overrides\": {}, \"position\": {\"chapter\": 2, \"word\": 490, \"mode\": \"page\"}, "
                  "\"grades\": {\"c0002-p1\": {\"first\": \"correct\"}}";
  if (!active.empty()) s += ", \"active_checkpoint\": " + active;
  return s + "}";
}

struct Restored {
  checkpoint::RestorePlan plan;
  reading::OpenAction action;
};

Restored restoreFrom(const Checkpoints& cp, const std::string& file, checkpoint::BookState& state) {
  const reading::StateLoad load = reading::decodeForUse(reinterpret_cast<const uint8_t*>(file.data()), file.size(),
                                                        cp.index.sourceSha256(), &cp.index, state);
  EXPECT_EQ(load.source, reading::StateSource::Read);
  BookIndexes books;
  books.index = &cp.index;
  reading::BookIndexLayout layout(cp.index, CHAPTERS, &BookIndexes::open, &BookIndexes::spineOf, &books);
  Restored r;
  r.plan = checkpoint::planRestore(state, &cp.index, layout);
  r.action = reading::openActionFor(r.plan);
  return r;
}

const char* const C0002_PROMPT_2 = R"({"id": "c0002", "prompt_index": 1,
  "resume": {"chapter": 3, "word": 0, "mode": "page"}})";

}  // namespace

TEST(ReadingSessionRestore, TheLayoutKnowsEveryChapterAndSection) {
  Checkpoints cp;
  BookIndexes books;
  books.index = &cp.index;
  reading::BookIndexLayout layout(cp.index, CHAPTERS, &BookIndexes::open, &BookIndexes::spineOf, &books);
  uint32_t words = 0;
  ASSERT_TRUE(layout.chapterWordCount(CHAPTER_2, words));
  EXPECT_EQ(words, books.readers[CHAPTER_2].wordCount());
  ASSERT_TRUE(layout.chapterWordCount(3, words));
  EXPECT_EQ(words, books.readers[3].wordCount());
  EXPECT_FALSE(layout.chapterWordCount(CHAPTERS, words));
  for (const char* id : {"s0001", "s0002", "s0003", "s0004"}) {
    checkpoint::SectionRange range;
    ASSERT_TRUE(layout.sectionRange(sectionNumber(cp, id), range)) << id;
    EXPECT_EQ(range.start, static_cast<uint32_t>(expectedNumber(id, "start"))) << id;
    EXPECT_EQ(range.end, static_cast<uint32_t>(expectedNumber(id, "end"))) << id;
  }
  checkpoint::SectionRange range;
  ASSERT_TRUE(layout.sectionRange(sectionNumber(cp, "s0004"), range));
  EXPECT_EQ(range.chapter, CHAPTER_2);
  EXPECT_EQ(range.section, sectionNumber(cp, "s0004"));
}

TEST(ReadingSessionRestore, APromptInProgressOpensAtThatPromptWithItsAnswerHidden) {
  Checkpoints cp;
  auto state = std::make_unique<checkpoint::BookState>();
  const Restored r = restoreFrom(cp, stateWith(cp, C0002_PROMPT_2), *state);
  EXPECT_EQ(r.plan.view, checkpoint::OpenView::Prompt);
  EXPECT_EQ(r.plan.notes, 0u);
  EXPECT_EQ(r.plan.checkpoint, static_cast<uint16_t>(cp.index.findCheckpoint("c0002")));
  EXPECT_EQ(r.plan.section.start, static_cast<uint32_t>(expectedNumber("s0004", "start")));
  EXPECT_EQ(r.plan.section.end, static_cast<uint32_t>(expectedNumber("s0004", "end")));
  EXPECT_EQ(r.action.show, reading::OpenAction::Show::Prompt);
  EXPECT_FALSE(r.action.jump) << "no page is drawn before the prompt";
  EXPECT_FALSE(r.action.speedRead);
  checkpoint::CheckpointFlow flow(*state, cp.index);
  ASSERT_TRUE(flow.restore(r.plan).accepted);
  EXPECT_EQ(flow.state(), checkpoint::FlowState::PromptHidden);
  EXPECT_EQ(flow.promptIndex(), 1u) << "question 2 of 3, as saved";
  EXPECT_EQ(state->grades.find("c0002-p1"), checkpoint::Grade::Correct);
  // Skip checkpoint resumes at the saved resume place, in the mode it fired in.
  const checkpoint::Transition t = flow.handle(checkpoint::FlowEvent::SkipCheckpoint);
  EXPECT_TRUE(t.finished);
  EXPECT_EQ(t.resume.position, (Position{3, 0}));
  EXPECT_EQ(t.resume.mode, checkpoint::ReadingMode::Page);
}

TEST(ReadingSessionRestore, AnActiveRereadWinsAndOpensInItsOwnMode) {
  Checkpoints cp;
  for (const std::string mode : {"page", "rsvp"}) {
    auto state = std::make_unique<checkpoint::BookState>();
    const std::string active = R"({"id": "c0002", "prompt_index": 1,
      "resume": {"chapter": 3, "word": 0, "mode": "rsvp"}, "reread": {"mode": ")" +
                               mode + R"(", "chapter": 2, "word": 381}})";
    const Restored r = restoreFrom(cp, stateWith(cp, active), *state);
    EXPECT_EQ(r.plan.view, checkpoint::OpenView::Reread) << mode;
    EXPECT_EQ(r.plan.place.position, (Position{CHAPTER_2, 381})) << mode;
    EXPECT_EQ(r.action.show, reading::OpenAction::Show::Reread) << mode;
    EXPECT_TRUE(r.action.jump) << "the page under it holds the reread word: " << mode;
    EXPECT_EQ(r.action.speedRead, mode == "rsvp") << "the reread's mode, not the mode the checkpoint fired in";
    checkpoint::CheckpointFlow flow(*state, cp.index);
    ASSERT_TRUE(flow.restore(r.plan).accepted);
    EXPECT_EQ(flow.state(), checkpoint::FlowState::Rereading);
    EXPECT_EQ(flow.rereadSection(), sectionNumber(cp, "s0004"));
    // Back at the prompt, the resume place is still the one saved when it fired.
    ASSERT_TRUE(flow.handle(checkpoint::FlowEvent::ReturnEarly).accepted);
    EXPECT_EQ(flow.promptIndex(), 1u);
    EXPECT_EQ(state->active.resume.mode, checkpoint::ReadingMode::Rsvp);
  }
}

TEST(ReadingSessionRestore, ARereadOutsideItsSectionShowsThePromptInstead) {
  Checkpoints cp;
  auto state = std::make_unique<checkpoint::BookState>();
  const Restored r = restoreFrom(cp,
                                 stateWith(cp, R"({"id": "c0002", "prompt_index": 1,
    "resume": {"chapter": 3, "word": 0, "mode": "page"}, "reread": {"mode": "page", "chapter": 2, "word": 100}})"),
                                 *state);
  EXPECT_EQ(r.plan.view, checkpoint::OpenView::Prompt);
  EXPECT_EQ(r.plan.notes, checkpoint::NOTE_REREAD_OUTSIDE_SECTION);
  EXPECT_FALSE(state->active.rereading) << "removed from the state, so the next save drops it";
  EXPECT_EQ(r.action.show, reading::OpenAction::Show::Prompt);
  char names[80];
  reading::restoreNoteNames(r.plan.notes, names, sizeof(names));
  EXPECT_STREQ(names, "reread-outside-section");
}

TEST(ReadingSessionRestore, WhatCannotBeRestoredOpensAtTheBestRemainingView) {
  Checkpoints cp;
  struct Case {
    const char* name;
    const char* active;
    uint16_t note;
    Position at;
  };
  const Case cases[] = {
      {"unknown checkpoint", R"({"id": "c0099", "prompt_index": 0, "resume": {"chapter": 3, "word": 0, "mode": "page"}})",
       checkpoint::NOTE_UNKNOWN_CHECKPOINT, Position{3, 0}},
      {"prompt out of range",
       R"({"id": "c0002", "prompt_index": 5, "resume": {"chapter": 3, "word": 0, "mode": "page"}})",
       checkpoint::NOTE_PROMPT_OUT_OF_RANGE, Position{3, 0}},
      {"resume outside the book",
       R"({"id": "c0002", "prompt_index": 0, "resume": {"chapter": 9, "word": 0, "mode": "page"}})",
       checkpoint::NOTE_RESUME_OUTSIDE_BOOK, Position{CHAPTER_2, 490}},
  };
  for (const Case& c : cases) {
    auto state = std::make_unique<checkpoint::BookState>();
    const Restored r = restoreFrom(cp, stateWith(cp, c.active), *state);
    EXPECT_EQ(r.plan.view, checkpoint::OpenView::Position) << c.name;
    EXPECT_EQ(r.plan.notes, c.note) << c.name;
    EXPECT_FALSE(state->hasActive) << c.name;
    EXPECT_EQ(state->position.position, c.at) << c.name;
    EXPECT_EQ(state->grades.find("c0002-p1"), checkpoint::Grade::Correct) << "no grade is lost: " << c.name;
    EXPECT_EQ(r.action.show, reading::OpenAction::Show::Reading) << c.name;
    EXPECT_TRUE(r.action.jump) << "page view goes to the place, not to its own progress: " << c.name;
    EXPECT_EQ(r.action.at.position, c.at) << c.name;
  }
  // In RSVP the place opens RSVP (by its saved word).
  auto state = std::make_unique<checkpoint::BookState>();
  const Restored r = restoreFrom(
      cp, stateWith(cp, R"({"id": "c0099", "prompt_index": 0, "resume": {"chapter": 3, "word": 0, "mode": "rsvp"}})"),
      *state);
  EXPECT_TRUE(r.action.speedRead);
  EXPECT_FALSE(r.action.jump);
}

TEST(ReadingSessionRestore, AnotherEditionsCheckpointIsNeverRestored) {
  Checkpoints cp;
  std::string file = stateWith(cp, C0002_PROMPT_2);
  file.replace(file.find(cp.index.sourceSha256()), 64, std::string(64, 'b'));
  auto state = std::make_unique<checkpoint::BookState>();
  const reading::StateLoad load = reading::decodeForUse(reinterpret_cast<const uint8_t*>(file.data()), file.size(),
                                                        cp.index.sourceSha256(), &cp.index, *state);
  EXPECT_TRUE(load.shaMismatch);
  EXPECT_FALSE(state->hasActive) << "dropped as the file is read: the book opens at the reader's own page";
  EXPECT_FALSE(state->hasPosition);
  EXPECT_EQ(state->grades.find("c0002-p1"), checkpoint::Grade::Correct);
}

TEST(ReadingSessionRestore, NoteNames) {
  char names[160];
  reading::restoreNoteNames(0, names, sizeof(names));
  EXPECT_STREQ(names, "none");
  reading::restoreNoteNames(checkpoint::NOTE_UNKNOWN_CHECKPOINT | checkpoint::NOTE_RESUME_OUTSIDE_BOOK, names,
                            sizeof(names));
  EXPECT_STREQ(names, "unknown-checkpoint+resume-outside-book");
  char tiny[8];
  reading::restoreNoteNames(checkpoint::NOTE_UNKNOWN_CHECKPOINT, tiny, sizeof(tiny));
  EXPECT_LT(std::string(tiny).size(), sizeof(tiny));
  EXPECT_STREQ(reading::openViewName(checkpoint::OpenView::Reread), "reread");
}
