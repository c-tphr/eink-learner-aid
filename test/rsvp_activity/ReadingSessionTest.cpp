// Task C2: the pure decisions of ReadingSession (src/reading/ReadingSessionRules) on the prepared
// fixture book: its sections from the word index's anchors, the section-end flags RSVP's word source
// carries, the step at an RSVP checkpoint stop, the words of a page in page view, and where reading
// goes on after a checkpoint (trigger rules 3, 4, 5, 7 and 8 of lib/CheckpointCore/README.md). Also
// the state file rule for another edition with the open checkpoint index (src/reading/BookStateRules).

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "FixtureBook.h"
#include "lib/CheckpointCore/BookState.h"
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
