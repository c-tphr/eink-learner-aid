// Trigger rules, docs/PLAN.md section 7 "Trigger rules": one test per bullet
// (TriggerRule1..6), then the cases the plan leaves open.

#include <gtest/gtest.h>

#include "StateTestSupport.h"

using namespace checkpoint;
using checkpoint_test::FixtureBook;
using checkpoint_test::ReaderSim;

namespace {

constexpr NavSource ALL_SOURCES[] = {
    NavSource::RsvpPlayback, NavSource::NextPage,    NavSource::PreviousPage, NavSource::TableOfContents,
    NavSource::GoToPercent,  NavSource::Bookmark,    NavSource::ProgressSync, NavSource::SentenceStep,
    NavSource::ModeSwitch,   NavSource::OpenRestore, NavSource::RereadStart,  NavSource::RereadReturn,
};
static_assert(sizeof(ALL_SOURCES) / sizeof(ALL_SOURCES[0]) == NAV_SOURCE_COUNT);

// Chapter 5 with 100 words: section 0 [0,30) checkpoint 0, section 1 [30,50)
// none, section 2 [50,52) checkpoint 1, section 3 [52,100) checkpoint 2,
// which ends the chapter. Chapter 6 follows.
std::unique_ptr<ChapterSections> sampleChapter(uint16_t next = 6) {
  auto c = std::make_unique<ChapterSections>();
  c->reset(5, 100, next);
  EXPECT_EQ(c->add(0, 0, 30, 0), ChapterSections::AddResult::Added);
  EXPECT_EQ(c->add(1, 30, 50, NO_INDEX), ChapterSections::AddResult::Added);
  EXPECT_EQ(c->add(3, 52, 100, 2), ChapterSections::AddResult::Added);  // out of order on purpose
  EXPECT_EQ(c->add(2, 50, 52, 1), ChapterSections::AddResult::Added);
  return c;
}

Position at(uint32_t word, uint16_t chapter = 5) { return Position{chapter, word}; }

}  // namespace

// "A checkpoint is armed while the reading position is inside its section and
// fires when ordinary forward reading crosses the section's end anchor.
// 'Ordinary forward reading' means RSVP playback or a next-page action."
TEST(CheckpointTrigger, TriggerRule1_ArmedInsideSectionFiresWhenOrdinaryForwardReadingCrossesEnd) {
  auto c = sampleChapter();
  for (NavSource source : {NavSource::RsvpPlayback, NavSource::NextPage}) {
    // Inside section 0, crossing its last word (29).
    const Step s = evaluateMove(*c, at(10), at(31), source);
    EXPECT_EQ(s.kind, StepKind::Fire) << navSourceName(source);
    EXPECT_EQ(s.checkpoint, 0);
    EXPECT_EQ(s.section.section, 0);
    EXPECT_EQ(s.section.start, 0u);
    EXPECT_EQ(s.section.end, 30u);
    EXPECT_EQ(s.resume, at(30));
    EXPECT_EQ(s.crossed, 1);
    // Still inside: armed, nothing crossed.
    EXPECT_EQ(evaluateMove(*c, at(10), at(29), source).kind, StepKind::None);
    EXPECT_EQ(evaluateMove(*c, at(28), at(29), source).kind, StepKind::None);
    // Already past the end: not armed.
    EXPECT_EQ(evaluateMove(*c, at(30), at(45), source).kind, StepKind::None);
    // A section without a checkpoint never fires.
    EXPECT_EQ(evaluateMove(*c, at(31), at(50), source).kind, StepKind::None);
  }
  EXPECT_EQ(armedCheckpoint(*c, 0), 0);
  EXPECT_EQ(armedCheckpoint(*c, 29), 0);
  EXPECT_EQ(armedCheckpoint(*c, 30), -1);
  EXPECT_EQ(armedCheckpoint(*c, 51), 1);
  EXPECT_EQ(armedCheckpoint(*c, 99), 2);
  int ordinary = 0;
  for (NavSource source : ALL_SOURCES) ordinary += isOrdinaryForward(source) ? 1 : 0;
  EXPECT_EQ(ordinary, 2);
  EXPECT_TRUE(isOrdinaryForward(NavSource::RsvpPlayback));
  EXPECT_TRUE(isOrdinaryForward(NavSource::NextPage));
}

// "Table-of-contents jumps, go-to-percent, bookmarks, progress sync and
// sentence stepping carry a navigation source that never fires checkpoints,
// even when they pass several."
TEST(CheckpointTrigger, TriggerRule2_JumpsNeverFireEvenWhenTheyPassSeveral) {
  auto c = sampleChapter();
  const NavSource neverFire[] = {NavSource::PreviousPage, NavSource::TableOfContents, NavSource::GoToPercent,
                                 NavSource::Bookmark,     NavSource::ProgressSync,    NavSource::SentenceStep,
                                 NavSource::ModeSwitch,   NavSource::OpenRestore,     NavSource::RereadStart,
                                 NavSource::RereadReturn};
  for (NavSource source : neverFire) {
    EXPECT_FALSE(isOrdinaryForward(source)) << navSourceName(source);
    EXPECT_EQ(evaluateMove(*c, at(0), at(99), source).kind, StepKind::None) << navSourceName(source);
    EXPECT_EQ(evaluateMove(*c, at(0), at(3, 6), source).kind, StepKind::None) << navSourceName(source);
    EXPECT_EQ(evaluateMove(*c, at(29), at(30), source).kind, StepKind::None) << navSourceName(source);
    EXPECT_EQ(evaluateMove(*c, at(99), at(0), source).kind, StepKind::None) << navSourceName(source);
  }
  // The same moves by ordinary forward reading cross three checkpoint ends.
  EXPECT_EQ(evaluateMove(*c, at(0), at(3, 6), NavSource::NextPage).crossed, 3);
}

// "In RSVP, playback stops with the last word of the section on screen;
// nothing beyond the break is drawn."
TEST(CheckpointTrigger, TriggerRule3_RsvpStopsWithLastWordOfSectionOnScreen) {
  auto c = sampleChapter();
  for (uint32_t w = 0; w < 100; ++w) {
    const bool stop = stopAfterWord(*c, w);
    EXPECT_EQ(stop, w == 29 || w == 51 || w == 99) << w;
  }
  // Playback from word 0 draws 0..29 and stops before 30.
  uint32_t drawn = 0;
  for (uint32_t w = 0; w < 100; ++w) {
    drawn = w;
    if (stopAfterWord(*c, w)) break;
  }
  EXPECT_EQ(drawn, 29u);
  const Step s = rsvpStep(*c, 29);
  EXPECT_EQ(s.kind, StepKind::Fire);
  EXPECT_EQ(s.resume, at(30));
  // A group of words never crosses a section end, so its last word is asked.
  EXPECT_FALSE(stopAfterWord(*c, 28));
}

// "In page view, the checkpoint appears when the reader advances from the
// page that shows the last character of the section's last word. Text after
// the break on that same page will already have been visible."
TEST(CheckpointTrigger, TriggerRule4_PageViewFiresWhenLeavingThePageThatShowsTheLastWord) {
  auto c = sampleChapter();
  // Page [20, 39] shows word 29 and text after the break (30..39).
  EXPECT_EQ(pageAdvance(*c, 0, 19).kind, StepKind::None);
  const Step s = pageAdvance(*c, 20, 39);
  EXPECT_EQ(s.kind, StepKind::Fire);
  EXPECT_EQ(s.checkpoint, 0);
  EXPECT_EQ(s.resume, at(30));
  // The break as the last word of the page.
  EXPECT_EQ(pageAdvance(*c, 10, 29).kind, StepKind::Fire);
  EXPECT_EQ(pageAdvance(*c, 30, 49).kind, StepKind::None);
  // Hyphenation: word 29 starts on page A and ends on page B. A page's words
  // are those whose last character it shows, so page A ends at 28 and does
  // not fire; page B starts at 29 and does.
  EXPECT_EQ(pageAdvance(*c, 10, 28).kind, StepKind::None);
  EXPECT_EQ(pageAdvance(*c, 29, 60).kind, StepKind::Fire);
  // An empty range (an image-only page) fires nothing.
  EXPECT_EQ(pageAdvance(*c, 40, 39).kind, StepKind::None);
}

// "Reading through the section again later fires the checkpoint again and
// presents all its prompts, with saved first grades shown as already
// recorded." The prompts part is GradeRules.GradeRule4_*.
TEST(CheckpointTrigger, TriggerRule5_ReadingThroughTheSectionAgainFiresAgain) {
  auto c = sampleChapter();
  ASSERT_EQ(rsvpStep(*c, 29).kind, StepKind::Fire);
  // Resume at 30, then go back into the section (sentence step) and read on.
  EXPECT_EQ(evaluateMove(*c, at(30), at(25), NavSource::SentenceStep).kind, StepKind::None);
  EXPECT_EQ(evaluateMove(*c, at(25), at(30), NavSource::RsvpPlayback).kind, StepKind::Fire);
  EXPECT_EQ(rsvpStep(*c, 29).kind, StepKind::Fire);
  EXPECT_EQ(pageAdvance(*c, 0, 39).kind, StepKind::Fire);
}

// "During a reread, the linked section's own checkpoint is suppressed.
// Reaching the end anchor, or choosing Return early, goes back to the
// prompt." Return early itself is CheckpointFlow.FlowStep5_*.
TEST(CheckpointTrigger, TriggerRule6_RereadSuppressesItsCheckpointAndItsEndReturnsToThePrompt) {
  auto c = sampleChapter();
  const Step rsvp = rsvpStep(*c, 29, /*rereadSection=*/0);
  EXPECT_EQ(rsvp.kind, StepKind::RereadEnd);
  EXPECT_EQ(rsvp.checkpoint, NO_INDEX);
  EXPECT_EQ(rsvp.section.section, 0);
  EXPECT_TRUE(stopAfterWord(*c, 29, 0));
  EXPECT_EQ(pageAdvance(*c, 20, 39, 0).kind, StepKind::RereadEnd);
  EXPECT_EQ(evaluateMove(*c, at(5), at(40), NavSource::NextPage, 0).kind, StepKind::RereadEnd);
  // No other checkpoint fires during a reread.
  EXPECT_EQ(rsvpStep(*c, 51, 0).kind, StepKind::None);
  EXPECT_EQ(evaluateMove(*c, at(40), at(60), NavSource::NextPage, 0).kind, StepKind::None);
}

// Decision: one forward action presents every checkpoint whose end it
// crosses, in text order. The trigger returns the first and counts them; when
// the first finishes in page view, the resume word is on the same page and
// pageAdvance() from it presents the next before the page is shown.
TEST(CheckpointTrigger, TwoCheckpointEndsOnOnePageArePresentedInOrderBeforeReadingContinues) {
  auto c = sampleChapter();
  const Step first = pageAdvance(*c, 40, 79);
  ASSERT_EQ(first.kind, StepKind::Fire);
  EXPECT_EQ(first.checkpoint, 1);  // section 2 ends at 51
  EXPECT_EQ(first.crossed, 1);
  // Section ends at 51 and 99 on one page.
  const Step both = pageAdvance(*c, 40, 99);
  ASSERT_EQ(both.kind, StepKind::Fire);
  EXPECT_EQ(both.checkpoint, 1);
  EXPECT_EQ(both.crossed, 2);
  EXPECT_EQ(both.resume, at(52));
  const Step second = pageAdvance(*c, both.resume.word, 99);
  ASSERT_EQ(second.kind, StepKind::Fire);
  EXPECT_EQ(second.checkpoint, 2);
  EXPECT_EQ(second.crossed, 1);
  EXPECT_EQ(second.resume, at(0, 6));
  EXPECT_EQ(pageAdvance(*c, 52, 99, NO_INDEX).checkpoint, 2);
}

// Decision: a section that ends its chapter fires before the next chapter is
// shown, and reading resumes at the next chapter's first word; at the end of
// the book the resume position is (chapter, word count).
TEST(CheckpointTrigger, SectionEndAtChapterEndFiresBeforeTheNextChapter) {
  auto c = sampleChapter();
  EXPECT_TRUE(stopAfterWord(*c, 99));
  EXPECT_EQ(rsvpStep(*c, 99).resume, at(0, 6));
  // Next page from the chapter's last page into the next chapter.
  const Step s = evaluateMove(*c, at(80), at(0, 6), NavSource::NextPage);
  EXPECT_EQ(s.kind, StepKind::Fire);
  EXPECT_EQ(s.checkpoint, 2);
  EXPECT_EQ(s.resume, at(0, 6));
  auto last = sampleChapter(NO_CHAPTER);
  EXPECT_EQ(rsvpStep(*last, 99).resume, at(100));
}

TEST(CheckpointTrigger, ChapterSectionsRefusesRangesThatCanNeverFire) {
  ChapterSections c;
  c.reset(1, 50, 2);
  EXPECT_EQ(c.add(0, 10, 10, 0), ChapterSections::AddResult::EmptyRange);
  EXPECT_EQ(c.add(0, 40, 51, 0), ChapterSections::AddResult::OutOfChapter);
  EXPECT_EQ(c.add(0, 10, 20, 0), ChapterSections::AddResult::Added);
  EXPECT_EQ(c.add(1, 19, 25, 1), ChapterSections::AddResult::Overlap);
  EXPECT_EQ(c.add(1, 5, 11, 1), ChapterSections::AddResult::Overlap);
  EXPECT_EQ(c.add(1, 0, 10, 1), ChapterSections::AddResult::Added);
  EXPECT_EQ(c.count(), 2);
  EXPECT_EQ(c.span(0).section, 1);
  EXPECT_EQ(c.spanContaining(15), 1);
  EXPECT_EQ(c.spanContaining(25), -1);
  // Moves that start in another chapter, or go backwards, never fire.
  EXPECT_EQ(evaluateMove(c, Position{2, 0}, Position{2, 40}, NavSource::NextPage).kind, StepKind::None);
  EXPECT_EQ(evaluateMove(c, Position{1, 40}, Position{0, 0}, NavSource::NextPage).kind, StepKind::None);
}

TEST(CheckpointTrigger, ChapterSectionsLoadFromTheFixtureBook) {
  FixtureBook book;
  ASSERT_TRUE(book.ok);
  ChapterSections c;
  book.loadChapter(c, 2);
  EXPECT_EQ(c.wordCount(), 417u);
  ASSERT_EQ(c.count(), 2);
  EXPECT_EQ(c.span(0).section, book.section("s0001"));
  EXPECT_EQ(c.span(0).checkpoint, book.checkpoint("c0001"));
  EXPECT_EQ(c.span(1).checkpoint, NO_INDEX);
  book.loadChapter(c, 3);
  ASSERT_EQ(c.count(), 2);
  EXPECT_EQ(c.span(1).end, 550u);
  EXPECT_EQ(c.span(1).checkpoint, book.checkpoint("c0002"));
  // A section whose anchors are missing is left out and counted.
  book.layout.sections.erase(book.section("s0004"));
  const int href = book.index->findChapter("text/chapter-2.xhtml");
  ASSERT_GE(href, 0);
  const ChapterSections::LoadResult r = c.load(*book.index, static_cast<uint16_t>(href), book.layout, 3, 4);
  EXPECT_TRUE(r.chapterKnown);
  EXPECT_EQ(r.added, 1);
  EXPECT_EQ(r.failed, 1);
}
