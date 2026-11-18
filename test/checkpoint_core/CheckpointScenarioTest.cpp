// The four checkpoint acceptance criteria of docs/PRD.md ("v1 acceptance
// criteria"), replayed end to end at the logic level on the synthetic book:
// c0001 (one prompt) on s0001, in reader chapter 2, ends mid-chapter; c0002
// (three prompts) on s0004, in reader chapter 3, ends its chapter. Word
// positions come from fixtures/books/fixture.expected.json (FixtureBook), in
// its word model: chapter word indexes count table, code and footnote-body
// words too. Pages hold 40 word indexes.

#include <gtest/gtest.h>

#include "StateTestSupport.h"

using namespace checkpoint;
using checkpoint_test::encodeToString;
using checkpoint_test::FixtureBook;
using checkpoint_test::ReaderSim;

namespace {

const char* AT = "2026-12-01T19:04:00";

// Next page until something stops ordinary reading, at most `limit` pages.
void pageUntilStop(ReaderSim& sim, int limit = 40) {
  for (int i = 0; i < limit && (sim.reading() || sim.rereading()); ++i) {
    const FlowState before = sim.flow->state();
    sim.nextPage();
    if (sim.flow->state() != before) return;
  }
}

}  // namespace

TEST(CheckpointScenario, FixtureBookMatchesTheAcceptanceSetup) {
  FixtureBook book;
  ASSERT_TRUE(book.ok);
  EXPECT_EQ(std::string(book.index->sourceSha256()), book.epubSha256);  // the checkpoint file is from this EPUB
  const uint16_t c1 = book.checkpoint("c0001");
  const uint16_t c2 = book.checkpoint("c0002");
  EXPECT_EQ(book.index->promptCount(c1), 1);
  EXPECT_EQ(book.index->promptCount(c2), 3);
  EXPECT_EQ(book.index->checkpointSection(c1), book.section("s0001"));
  EXPECT_EQ(book.index->checkpointSection(c2), book.section("s0004"));
  const SectionRange s1 = book.layout.sections[book.section("s0001")];
  const SectionRange s4 = book.layout.sections[book.section("s0004")];
  EXPECT_EQ(s1.chapter, 2);
  EXPECT_EQ(s1.start, 0u);  // the chapter's first section
  EXPECT_LT(s1.end, static_cast<uint32_t>(book.layout.words[2]));  // ends mid-chapter
  EXPECT_EQ(s4.chapter, 3);
  EXPECT_EQ(s4.end, static_cast<uint32_t>(book.layout.words[3]));  // ends its chapter
  // The committed checkpoint file agrees with the fixture's word counts.
  for (const auto& entry : book.layout.sections) {
    EXPECT_EQ(book.index->sectionWordCount(entry.first), entry.second.end - entry.second.start);
  }
}

// PRD: "A prepared test book includes both a single-prompt checkpoint and a
// multi-prompt checkpoint. Reading into either checkpoint triggers it in both
// reading modes. Jumping past it does not."
TEST(CheckpointScenario, Prd1_ReadingIntoEitherCheckpointFiresItInBothModesAndJumpingPastDoesNot) {
  FixtureBook book;
  ASSERT_TRUE(book.ok);
  const Position s1Last = book.lastWordOf("s0001");
  const Position s4Last = book.lastWordOf("s0004");
  // s0001 ends mid-chapter: reading resumes at its end. s0004 ends its chapter: at the next chapter's word 0.
  const Position s1Resume{s1Last.chapter, s1Last.word + 1};
  const Position s4Resume{book.nextChapter(s4Last.chapter), 0};
  // RSVP from the start of chapter 1.
  {
    ReaderSim sim(book);
    sim.openAt(Place{Position{2, 0}, ReadingMode::Rsvp});
    Step s = sim.play();
    ASSERT_EQ(s.kind, StepKind::Fire);
    EXPECT_STREQ(sim.state->active.id, "c0001");
    EXPECT_EQ(sim.place.position, s1Last);  // the section's last word is on screen
    EXPECT_EQ(sim.state->active.resume, (Place{s1Resume, ReadingMode::Rsvp}));
    sim.handle(FlowEvent::SkipCheckpoint);
    s = sim.play();
    ASSERT_EQ(s.kind, StepKind::Fire);
    EXPECT_STREQ(sim.state->active.id, "c0002");
    EXPECT_EQ(sim.place.position, s4Last);
    EXPECT_EQ(sim.state->active.resume, (Place{s4Resume, ReadingMode::Rsvp}));
  }
  // Page view from the start of chapter 1.
  {
    ReaderSim sim(book);
    sim.openAt(Place{Position{2, 0}, ReadingMode::Page});
    pageUntilStop(sim);
    ASSERT_EQ(sim.flow->state(), FlowState::PromptHidden);
    EXPECT_STREQ(sim.state->active.id, "c0001");
    // Leaving the page that shows the section's last word.
    EXPECT_EQ(sim.place.position, (Position{2, sim.pages.firstWord(s1Last.word)}));
    EXPECT_EQ(sim.state->active.resume, (Place{s1Resume, ReadingMode::Page}));
    sim.handle(FlowEvent::SkipCheckpoint);
    pageUntilStop(sim);
    ASSERT_EQ(sim.flow->state(), FlowState::PromptHidden);
    EXPECT_STREQ(sim.state->active.id, "c0002");
    const uint32_t chapterLast = book.chapterWords(s4Last.chapter) - 1;
    EXPECT_EQ(sim.place.position, (Position{3, sim.pages.firstWord(chapterLast)}));  // the chapter's last page
    EXPECT_EQ(sim.state->active.resume, (Place{s4Resume, ReadingMode::Page}));
  }
  // Jumps past each checkpoint, in each mode, then ordinary reading on.
  const NavSource jumps[] = {NavSource::TableOfContents, NavSource::GoToPercent, NavSource::Bookmark,
                             NavSource::ProgressSync, NavSource::SentenceStep};
  for (ReadingMode mode : {ReadingMode::Rsvp, ReadingMode::Page}) {
    for (NavSource source : jumps) {
      ReaderSim sim(book);
      // Inside s0001, past its end in the same chapter, inside s0004, then past it in the next chapter.
      const SectionRange s1 = book.range("s0001");
      const SectionRange s4 = book.range("s0004");
      sim.openAt(Place{Position{2, (s1.start + s1.end) / 2}, mode});
      EXPECT_EQ(sim.jump(Position{2, s1.end + 67}, source).kind, StepKind::None) << navSourceName(source);
      EXPECT_EQ(sim.jump(Position{3, (s4.start + s4.end) / 2}, source).kind, StepKind::None);
      EXPECT_EQ(sim.jump(Position{book.nextChapter(3), 10}, source).kind, StepKind::None);
      EXPECT_EQ(sim.flow->state(), FlowState::Idle);
      // Reading on from after the breaks fires nothing that was passed.
      if (mode == ReadingMode::Rsvp) {
        sim.play();
      } else {
        pageUntilStop(sim);
      }
      EXPECT_EQ(sim.flow->state(), FlowState::Idle);
      EXPECT_EQ(sim.saves, 0);
    }
  }
}

// PRD: "A reader can mark the second prompt Incorrect, reread its section in
// either mode, return early or at the end, and retry with the answer hidden.
// A later Correct leaves the original Incorrect saved and preserves the grade
// on the first prompt."
TEST(CheckpointScenario, Prd2_IncorrectRereadEitherModeReturnEarlyOrAtEndRetryCorrectKeepsIncorrect) {
  FixtureBook book;
  ASSERT_TRUE(book.ok);
  const uint16_t c2 = book.checkpoint("c0002");
  const Position s4First = book.firstWordOf("s0004");
  const Position s4Resume{book.nextChapter(s4First.chapter), 0};  // s0004 ends its chapter
  for (FlowEvent reread : {FlowEvent::RereadRsvp, FlowEvent::RereadPage}) {
    for (bool early : {true, false}) {
      ReaderSim sim(book);
      sim.openAt(Place{s4First, ReadingMode::Rsvp});
      ASSERT_EQ(sim.play().kind, StepKind::Fire);
      sim.handle(FlowEvent::Reveal);
      sim.handle(FlowEvent::Correct, AT);  // first prompt
      ASSERT_EQ(sim.flow->promptIndex(), 1);
      sim.handle(FlowEvent::Reveal);
      ASSERT_TRUE(sim.handle(FlowEvent::Incorrect, AT).gradeRecorded);
      ASSERT_TRUE(sim.handle(reread).accepted);
      const ReadingMode mode = reread == FlowEvent::RereadRsvp ? ReadingMode::Rsvp : ReadingMode::Page;
      EXPECT_EQ(sim.place, (Place{s4First, mode}));  // the section's start
      if (early) {
        if (mode == ReadingMode::Rsvp) {
          sim.play(30);
        } else {
          sim.nextPage();
          sim.nextPage();
        }
        ASSERT_EQ(sim.flow->state(), FlowState::Rereading);
        EXPECT_GT(sim.state->active.reread.position.word, s4First.word);
        sim.handle(FlowEvent::ReturnEarly);
      } else if (mode == ReadingMode::Rsvp) {
        sim.play();
      } else {
        pageUntilStop(sim);
      }
      // Back at the same prompt, answer hidden; the checkpoint did not fire again.
      ASSERT_EQ(sim.flow->state(), FlowState::PromptHidden);
      EXPECT_EQ(sim.flow->promptIndex(), 1);
      EXPECT_FALSE(sim.state->active.rereading);
      EXPECT_EQ(sim.state->active.resume, (Place{s4Resume, ReadingMode::Rsvp}));
      sim.handle(FlowEvent::Reveal);
      const Transition t = sim.handle(FlowEvent::Correct, "2026-12-01T19:30:00");
      EXPECT_TRUE(t.accepted);
      EXPECT_FALSE(t.gradeRecorded);
      EXPECT_EQ(sim.flow->promptIndex(), 2);
      EXPECT_EQ(sim.stored(c2, 1), Grade::Incorrect);
      EXPECT_EQ(sim.stored(c2, 0), Grade::Correct);
      // And so in the saved file.
      auto saved = std::make_unique<BookState>();
      ASSERT_EQ(checkpoint_test::decodeString(sim.lastSaved, *saved).status, DecodeStatus::Ok);
      EXPECT_EQ(saved->grades.find("c0002-p2"), Grade::Incorrect);
      EXPECT_EQ(saved->grades.find("c0002-p1"), Grade::Correct);
    }
  }
}

// PRD: "A reader can skip one prompt or exit a partially completed
// checkpoint. Unanswered prompts stay ungraded, saved grades survive, and
// reading resumes after the break in the original mode without
// auto-advancing."
TEST(CheckpointScenario, Prd3_SkipOnePromptOrExitPartwayKeepsGradesAndResumesAfterTheBreakWaiting) {
  FixtureBook book;
  ASSERT_TRUE(book.ok);
  const uint16_t c1 = book.checkpoint("c0001");
  const uint16_t c2 = book.checkpoint("c0002");
  const Position s4First = book.firstWordOf("s0004");
  const Position s4Resume{book.nextChapter(s4First.chapter), 0};  // s0004 ends its chapter
  // Skip one prompt, page view.
  {
    ReaderSim sim(book);
    sim.openAt(Place{s4First, ReadingMode::Page});
    pageUntilStop(sim);
    ASSERT_EQ(sim.flow->state(), FlowState::PromptHidden);
    sim.handle(FlowEvent::Reveal);
    sim.handle(FlowEvent::Correct, AT);
    sim.handle(FlowEvent::SkipPrompt);
    sim.handle(FlowEvent::Reveal);
    sim.handle(FlowEvent::Incorrect, AT);
    const Transition t = sim.handle(FlowEvent::Continue);
    ASSERT_TRUE(t.finished);
    EXPECT_EQ(sim.stored(c2, 0), Grade::Correct);
    EXPECT_EQ(sim.stored(c2, 1), Grade::Ungraded);
    EXPECT_EQ(sim.stored(c2, 2), Grade::Incorrect);
    EXPECT_EQ(t.resume, (Place{s4Resume, ReadingMode::Page}));
    EXPECT_EQ(sim.place, t.resume);  // the next chapter's first page, waiting
    EXPECT_EQ(sim.state->position, t.resume);
  }
  // Exit partway, RSVP, after a reread in page view.
  {
    ReaderSim sim(book);
    sim.openAt(Place{s4First, ReadingMode::Rsvp});
    sim.play();
    sim.handle(FlowEvent::Reveal);
    sim.handle(FlowEvent::Incorrect, AT);
    sim.handle(FlowEvent::RereadPage);
    sim.handle(FlowEvent::ReturnEarly);
    sim.handle(FlowEvent::SkipPrompt);
    sim.handle(FlowEvent::Reveal);
    const Transition t = sim.handle(FlowEvent::SkipCheckpoint);
    ASSERT_TRUE(t.finished);
    EXPECT_EQ(sim.stored(c2, 0), Grade::Incorrect);
    EXPECT_EQ(sim.stored(c2, 1), Grade::Ungraded);
    EXPECT_EQ(sim.stored(c2, 2), Grade::Ungraded);
    EXPECT_EQ(t.resume, (Place{s4Resume, ReadingMode::Rsvp}));  // original mode, not the reread's
    EXPECT_EQ(sim.place, t.resume);
    EXPECT_FALSE(sim.state->hasActive);
    // Playback waits for Play: nothing moves until the reader acts.
    EXPECT_EQ(sim.flow->state(), FlowState::Finished);
    EXPECT_EQ(sim.place.position, s4Resume);
  }
  // Exit the single-prompt checkpoint mid-chapter in page view: the same page
  // comes back, positioned after the break, and the next action reads on.
  {
    ReaderSim sim(book);
    const Position s1Last = book.lastWordOf("s0001");
    const uint32_t endPage = sim.pages.firstWord(s1Last.word);  // the page that shows the section's last word
    ASSERT_GE(endPage, sim.pages.wordsPerPage);
    sim.openAt(Place{Position{2, endPage - sim.pages.wordsPerPage}, ReadingMode::Page});  // the page before it
    pageUntilStop(sim);
    const Transition t = sim.handle(FlowEvent::SkipCheckpoint);
    EXPECT_EQ(t.resume, (Place{Position{2, s1Last.word + 1}, ReadingMode::Page}));  // s0001 ends mid-chapter
    EXPECT_EQ(sim.stored(c1, 0), Grade::Ungraded);
    EXPECT_EQ(sim.pages.firstWord(sim.place.position.word), endPage);  // the page that was left
    sim.nextPage();
    EXPECT_EQ(sim.flow->state(), FlowState::Finished);  // not fired again
    EXPECT_EQ(sim.place.position, (Position{2, endPage + sim.pages.wordsPerPage}));
  }
}

// PRD: "Closing and reopening the book restores an interrupted prompt or
// reread with the correct return position. Rereading the book later shows
// checkpoints again with first grades intact."
TEST(CheckpointScenario, Prd4_CloseAndReopenRestoresPromptOrRereadAndALaterPassShowsGradesIntact) {
  FixtureBook book;
  ASSERT_TRUE(book.ok);
  const uint16_t c2 = book.checkpoint("c0002");
  const Position s4First = book.firstWordOf("s0004");
  const Position s4Resume{book.nextChapter(s4First.chapter), 0};  // s0004 ends its chapter
  ReaderSim sim(book);
  sim.openAt(Place{s4First, ReadingMode::Page});
  pageUntilStop(sim);
  ASSERT_EQ(sim.flow->state(), FlowState::PromptHidden);
  sim.handle(FlowEvent::Reveal);
  sim.handle(FlowEvent::Correct, AT);
  sim.handle(FlowEvent::Reveal);  // prompt 2's answer is showing when the book closes

  // Close mid-prompt, reopen.
  RestorePlan plan;
  auto second = ReaderSim::reopen(book, sim.lastSaved, &plan);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(plan.view, OpenView::Prompt);
  EXPECT_EQ(plan.notes, 0);
  EXPECT_EQ(second->flow->state(), FlowState::PromptHidden);
  EXPECT_EQ(second->flow->promptIndex(), 1);
  EXPECT_EQ(second->state->active.resume, (Place{s4Resume, ReadingMode::Page}));
  EXPECT_EQ(second->stored(c2, 0), Grade::Correct);
  second->handle(FlowEvent::Reveal);
  second->handle(FlowEvent::Incorrect, AT);
  second->handle(FlowEvent::RereadRsvp);
  second->play(25);
  ASSERT_EQ(second->flow->state(), FlowState::Rereading);
  const Place rereadAt = second->state->active.reread;
  EXPECT_EQ(rereadAt, (Place{Position{3, s4First.word + 25}, ReadingMode::Rsvp}));  // 25 words into the reread

  // Close mid-reread (the reread position is saved at the position cadence), reopen.
  auto third = ReaderSim::reopen(book, encodeToString(*second->state), &plan);
  ASSERT_NE(third, nullptr);
  EXPECT_EQ(plan.view, OpenView::Reread);
  EXPECT_EQ(plan.place, rereadAt);
  EXPECT_EQ(third->flow->state(), FlowState::Rereading);
  EXPECT_EQ(third->place, rereadAt);
  EXPECT_EQ(third->state->active.resume, (Place{s4Resume, ReadingMode::Page}));
  // Continue: read to the section end, back to the same prompt, hidden.
  third->play();
  ASSERT_EQ(third->flow->state(), FlowState::PromptHidden);
  EXPECT_EQ(third->flow->promptIndex(), 1);
  third->handle(FlowEvent::Reveal);
  third->handle(FlowEvent::Correct, AT);
  const Transition t = third->handle(FlowEvent::SkipPrompt);
  ASSERT_TRUE(t.finished);
  EXPECT_EQ(t.resume, (Place{s4Resume, ReadingMode::Page}));  // the original mode
  EXPECT_EQ(third->stored(c2, 0), Grade::Correct);
  EXPECT_EQ(third->stored(c2, 1), Grade::Incorrect);
  EXPECT_EQ(third->stored(c2, 2), Grade::Ungraded);

  // Close after finishing: reopen at the resume position.
  auto fourth = ReaderSim::reopen(book, third->lastSaved, &plan);
  ASSERT_NE(fourth, nullptr);
  EXPECT_EQ(plan.view, OpenView::Position);
  EXPECT_EQ(plan.place, (Place{s4Resume, ReadingMode::Page}));

  // A later pass in the other mode shows the checkpoint again, grades intact.
  fourth->jump(Position{3, 0}, NavSource::TableOfContents);
  fourth->place.mode = ReadingMode::Rsvp;
  ASSERT_EQ(fourth->play().kind, StepKind::Fire);
  const Grade expected[] = {Grade::Correct, Grade::Incorrect, Grade::Ungraded};
  for (uint16_t p = 0; p < 3; ++p) {
    ASSERT_EQ(fourth->flow->state(), FlowState::PromptHidden);
    EXPECT_EQ(fourth->flow->promptIndex(), p);
    EXPECT_EQ(fourth->flow->storedGrade(), expected[p]);
    fourth->handle(FlowEvent::Reveal);
    fourth->handle(p == 1 ? FlowEvent::Correct : FlowEvent::Incorrect, "2027-01-01T10:00:00");
    if (fourth->flow->state() == FlowState::IncorrectChoice) fourth->handle(FlowEvent::Continue);
  }
  EXPECT_EQ(fourth->flow->state(), FlowState::Finished);
  EXPECT_EQ(fourth->stored(c2, 0), Grade::Correct);
  EXPECT_EQ(fourth->stored(c2, 1), Grade::Incorrect);
  EXPECT_EQ(fourth->stored(c2, 2), Grade::Incorrect);  // first graded on this pass
  // The single-prompt checkpoint also fires again on a later pass, in both modes.
  for (ReadingMode mode : {ReadingMode::Rsvp, ReadingMode::Page}) {
    fourth->jump(Position{2, 0}, NavSource::TableOfContents);
    fourth->place.mode = mode;
    if (mode == ReadingMode::Rsvp) {
      fourth->play();
    } else {
      pageUntilStop(*fourth);
    }
    ASSERT_EQ(fourth->flow->state(), FlowState::PromptHidden);
    EXPECT_STREQ(fourth->state->active.id, "c0001");
    fourth->handle(FlowEvent::SkipCheckpoint);
  }
}

// A page that shows the ends of two armed sections presents both, in order,
// before reading continues, also across a close and reopen between them.
TEST(CheckpointScenario, TwoCheckpointsEndingOnOnePageAreBothPresentedInOrder) {
  // One chapter of 100 words with sections ending at 49 and 59, both on page [40, 79].
  const std::string sha = "b07c3ea9857b00644d284c0eae083bc504bfc895525d0a864c183b916e17c82c";
  const std::string json =
      "{\"format\":\"rsvp-checkpoints\",\"version\":1,\"book\":{\"title\":\"T\",\"language\":\"en\","
      "\"source_sha256\":\"" +
      sha +
      "\",\"instructions_version\":\"1\"},\"sections\":["
      "{\"id\":\"s1\",\"chapter_href\":\"text/chapter-1.xhtml\",\"start_anchor\":\"rsvp-s1-start\","
      "\"end_anchor\":\"rsvp-s1-end\",\"label\":\"L\",\"word_count\":50},"
      "{\"id\":\"s2\",\"chapter_href\":\"text/chapter-1.xhtml\",\"start_anchor\":\"rsvp-s2-start\","
      "\"end_anchor\":\"rsvp-s2-end\",\"label\":\"L\",\"word_count\":10}],\"checkpoints\":["
      "{\"id\":\"c1\",\"section\":\"s1\",\"prompts\":[{\"id\":\"c1-p1\",\"prompt\":\"Q\",\"answer\":\"A\"}]},"
      "{\"id\":\"c2\",\"section\":\"s2\",\"prompts\":[{\"id\":\"c2-p1\",\"prompt\":\"Q\",\"answer\":\"A\"}]}]}";
  FixtureBook book;
  book.bytes.assign(json.begin(), json.end());
  book.source = std::make_unique<MemoryCheckpointSource>(book.bytes.data(), book.bytes.size());
  ASSERT_EQ(book.index->open(*book.source).verdict, Verdict::Valid);
  book.layout.words = {0, 0, 100};
  book.layout.sections.clear();
  book.layout.sections[0] = SectionRange{0, 2, 0, 50};
  book.layout.sections[1] = SectionRange{1, 2, 50, 60};
  book.chapterOfHref = {{"text/chapter-1.xhtml", 2}};

  ReaderSim sim(book);
  sim.openAt(Place{Position{2, 40}, ReadingMode::Page});
  sim.nextPage();
  ASSERT_STREQ(sim.state->active.id, "c1");
  const Transition t = sim.handle(FlowEvent::SkipCheckpoint);
  EXPECT_TRUE(t.finished);
  // The chain: c2 is presented next, before the page is shown again.
  ASSERT_EQ(sim.flow->state(), FlowState::PromptHidden);
  EXPECT_STREQ(sim.state->active.id, "c2");
  EXPECT_EQ(sim.state->active.resume, (Place{Position{2, 60}, ReadingMode::Page}));
  // Close here and reopen: c2 again, then reading resumes on the same page.
  auto again = ReaderSim::reopen(book, sim.lastSaved);
  ASSERT_NE(again, nullptr);
  EXPECT_STREQ(again->state->active.id, "c2");
  again->handle(FlowEvent::SkipCheckpoint);
  EXPECT_EQ(again->place, (Place{Position{2, 60}, ReadingMode::Page}));
  again->nextPage();
  EXPECT_EQ(again->flow->state(), FlowState::Finished);
  EXPECT_EQ(again->place.position, (Position{2, 80}));
  // RSVP stops at each end in turn.
  ReaderSim rsvp(book);
  rsvp.openAt(Place{Position{2, 40}, ReadingMode::Rsvp});
  ASSERT_EQ(rsvp.play().checkpoint, 0);
  rsvp.handle(FlowEvent::SkipCheckpoint);
  ASSERT_EQ(rsvp.play().checkpoint, 1);
  EXPECT_EQ(rsvp.place.position, (Position{2, 59}));
}
