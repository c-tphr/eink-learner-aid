// Restore order (docs/PLAN.md section 7, "State file": "an active reread,
// then an active prompt with its answer hidden, then the plain reading
// position"), its fallbacks, and the rule for a state from another book.

#include <gtest/gtest.h>

#include "StateTestSupport.h"

using namespace checkpoint;
using checkpoint_test::decodeString;
using checkpoint_test::encodeToString;
using checkpoint_test::FixtureBook;
using checkpoint_test::ReaderSim;

namespace {

const char* AT = "2026-12-01T19:04:00";

// Positions of c0002's section s0004 (reader chapter 3) in fixture.expected.json.
Position lastWord(const FixtureBook& book) { return book.lastWordOf("s0004"); }
// s0004 ends its chapter: the first word after the break is the next chapter's word 0.
Position resume(const FixtureBook& book) { return Position{book.nextChapter(lastWord(book).chapter), 0}; }
// A word inside the section, for a reread in progress.
Position inside(const FixtureBook& book) {
  const SectionRange r = book.range("s0004");
  return Position{r.chapter, r.start + 45};
}

// A state with c0002 on prompt 1, resume after s0004's break in RSVP,
// position on s0004's last word and, if asked, a reread inside s0004 in page
// view.
void activeState(BookState& s, const FixtureBook& book, bool reread) {
  s.reset();
  memcpy(s.bookSha256, book.index->sourceSha256(), SHA256_HEX_CHARS + 1);
  s.hasPosition = true;
  s.position = Place{lastWord(book), ReadingMode::Rsvp};
  s.grades.recordFirst("c0002-p1", Grade::Correct, AT);
  s.hasActive = true;
  strcpy(s.active.id, "c0002");
  s.active.promptIndex = 1;
  s.active.resume = Place{resume(book), ReadingMode::Rsvp};
  s.active.rereading = reread;
  if (reread) s.active.reread = Place{inside(book), ReadingMode::Page};
}

}  // namespace

TEST(StateRestore, RestoreOrder_ActiveRereadThenActivePromptThenPositionThenBookStart) {
  FixtureBook book;
  ASSERT_TRUE(book.ok);
  auto s = std::make_unique<BookState>();

  activeState(*s, book, true);
  RestorePlan plan = planRestore(*s, book.index.get(), book.layout);
  EXPECT_EQ(plan.view, OpenView::Reread);
  EXPECT_EQ(plan.notes, 0);
  EXPECT_EQ(plan.checkpoint, book.checkpoint("c0002"));
  EXPECT_EQ(plan.section.section, book.section("s0004"));
  EXPECT_EQ(plan.place, (Place{inside(book), ReadingMode::Page}));
  CheckpointFlow flow(*s, *book.index);
  Transition t = flow.restore(plan);
  EXPECT_TRUE(t.accepted);
  EXPECT_EQ(t.state, FlowState::Rereading);
  EXPECT_EQ(t.save, Save::No);

  activeState(*s, book, false);
  plan = planRestore(*s, book.index.get(), book.layout);
  EXPECT_EQ(plan.view, OpenView::Prompt);
  EXPECT_EQ(plan.place, s->active.resume);
  CheckpointFlow flow2(*s, *book.index);
  t = flow2.restore(plan);
  EXPECT_EQ(t.state, FlowState::PromptHidden);  // answer hidden
  EXPECT_EQ(flow2.promptIndex(), 1);

  s->hasActive = false;
  plan = planRestore(*s, book.index.get(), book.layout);
  EXPECT_EQ(plan.view, OpenView::Position);
  EXPECT_EQ(plan.place, (Place{lastWord(book), ReadingMode::Rsvp}));
  CheckpointFlow flow3(*s, *book.index);
  EXPECT_EQ(flow3.restore(plan).refusal, Refusal::BadRestore);

  s->hasPosition = false;
  plan = planRestore(*s, book.index.get(), book.layout);
  EXPECT_EQ(plan.view, OpenView::BookStart);
  EXPECT_EQ(plan.notes, 0);
}

TEST(StateRestore, UnrestorableCheckpointsFallBackSafelyAndSaySo) {
  FixtureBook book;
  ASSERT_TRUE(book.ok);
  auto s = std::make_unique<BookState>();
  struct Case {
    const char* what;
    void (*edit)(BookState&, FixtureBook&);
    OpenView view;
    uint16_t notes;
  };
  const Case cases[] = {
      {"unknown checkpoint id", [](BookState& st, FixtureBook&) { strcpy(st.active.id, "c9999"); }, OpenView::Position,
       NOTE_UNKNOWN_CHECKPOINT},
      {"prompt index out of range", [](BookState& st, FixtureBook&) { st.active.promptIndex = 3; }, OpenView::Position,
       NOTE_PROMPT_OUT_OF_RANGE},
      {"section anchors not found", [](BookState&, FixtureBook& b) { b.layout.sections.erase(b.section("s0004")); },
       OpenView::Position, NOTE_SECTION_UNRESOLVED},
      {"resume outside the book", [](BookState& st, FixtureBook&) { st.active.resume.position = Position{9, 0}; },
       OpenView::Position, NOTE_RESUME_OUTSIDE_BOOK},
      {"resume past the chapter end",
       [](BookState& st, FixtureBook& b) { st.active.resume.position = Position{3, b.chapterWords(3) + 1}; },
       OpenView::Position, NOTE_RESUME_OUTSIDE_BOOK},
      {"reread before the section",
       [](BookState& st, FixtureBook& b) { st.active.reread.position.word = b.range("s0004").start - 1; },
       OpenView::Prompt, NOTE_REREAD_OUTSIDE_SECTION},
      {"reread in another chapter", [](BookState& st, FixtureBook&) { st.active.reread.position.chapter = 2; },
       OpenView::Prompt, NOTE_REREAD_OUTSIDE_SECTION},
      {"reread at the section end",
       [](BookState& st, FixtureBook& b) { st.active.reread.position.word = b.range("s0004").end; },
       OpenView::Prompt, NOTE_REREAD_OUTSIDE_SECTION},
  };
  for (const Case& c : cases) {
    FixtureBook fresh;
    activeState(*s, fresh, true);
    c.edit(*s, fresh);
    const RestorePlan plan = planRestore(*s, fresh.index.get(), fresh.layout);
    EXPECT_EQ(plan.view, c.view) << c.what;
    EXPECT_EQ(plan.notes, c.notes) << c.what;
    EXPECT_TRUE(stateInvariantsHold(*s));
    if (c.view == OpenView::Position) {
      EXPECT_FALSE(s->hasActive) << c.what;
      // Reading continues after the break when the resume position is good,
      // else at the saved position.
      const Place expected = c.notes == NOTE_RESUME_OUTSIDE_BOOK ? Place{lastWord(fresh), ReadingMode::Rsvp}
                                                                 : Place{resume(fresh), ReadingMode::Rsvp};
      EXPECT_EQ(plan.place, expected) << c.what;
    } else {
      EXPECT_TRUE(s->hasActive);
      EXPECT_FALSE(s->active.rereading);
      CheckpointFlow flow(*s, *fresh.index);
      EXPECT_EQ(flow.restore(plan).state, FlowState::PromptHidden);
    }
    EXPECT_EQ(s->grades.find("c0002-p1"), Grade::Correct) << c.what;  // grades are never dropped here
  }
  // No checkpoint data: the checkpoint is dropped, grades and hash are kept.
  activeState(*s, book, false);
  RestorePlan plan = planRestore(*s, nullptr, book.layout);
  EXPECT_EQ(plan.view, OpenView::Position);
  EXPECT_EQ(plan.notes, NOTE_NO_CHECKPOINT_DATA);
  EXPECT_EQ(s->grades.count(), 1);
  // Positions outside the book.
  for (Position bad : {Position{9, 0}, Position{3, book.chapterWords(3) + 1}, Position{0xFFFE, 0}}) {
    s->reset();
    s->hasPosition = true;
    s->position = Place{bad, ReadingMode::Page};
    plan = planRestore(*s, book.index.get(), book.layout);
    EXPECT_EQ(plan.view, OpenView::BookStart);
    EXPECT_EQ(plan.notes, NOTE_POSITION_OUTSIDE_BOOK);
    EXPECT_FALSE(s->hasPosition);
  }
  // Both resume and position outside: book start, both noted.
  activeState(*s, book, false);
  s->active.resume.position = Position{9, 0};
  s->position.position = Position{9, 0};
  plan = planRestore(*s, book.index.get(), book.layout);
  EXPECT_EQ(plan.view, OpenView::BookStart);
  EXPECT_EQ(plan.notes, NOTE_RESUME_OUTSIDE_BOOK | NOTE_POSITION_OUTSIDE_BOOK);
}

TEST(StateRestore, BookChangeRule_SameBookKeepsAllAndAnotherEditionKeepsOnlyKnownPromptGrades) {
  FixtureBook book;
  ASSERT_TRUE(book.ok);
  auto s = std::make_unique<BookState>();
  activeState(*s, book, true);
  s->grades.recordFirst("gone-p1", Grade::Incorrect, AT);
  s->overrides.hasWpm = true;
  s->overrides.wpm = 300;

  BookChange change = applyBookChangeRule(*s, book.index.get());
  EXPECT_FALSE(change.shaMismatch);
  EXPECT_EQ(s->grades.count(), 2);  // same book: unknown ids are kept too
  EXPECT_TRUE(s->hasActive);

  change = applyBookChangeRule(*s, nullptr);  // opened without checkpoint data
  EXPECT_FALSE(change.shaMismatch);
  EXPECT_EQ(s->grades.count(), 2);

  memset(s->bookSha256, 'a', SHA256_HEX_CHARS);  // another edition
  change = applyBookChangeRule(*s, book.index.get());
  EXPECT_TRUE(change.shaMismatch);
  EXPECT_EQ(change.gradesDropped, 1);
  EXPECT_TRUE(change.positionDropped);
  EXPECT_TRUE(change.activeDropped);
  EXPECT_EQ(s->grades.find("c0002-p1"), Grade::Correct);
  EXPECT_EQ(s->grades.find("gone-p1"), Grade::Ungraded);
  EXPECT_FALSE(s->hasPosition);
  EXPECT_FALSE(s->hasActive);
  EXPECT_EQ(s->overrides.wpm, 300);
  EXPECT_STREQ(s->bookSha256, book.index->sourceSha256());
  EXPECT_EQ(planRestore(*s, book.index.get(), book.layout).view, OpenView::BookStart);

  // A state from an unprepared copy takes the book's hash and keeps its position.
  s->reset();
  s->hasPosition = true;
  s->position = Place{Position{2, 5}, ReadingMode::Page};
  change = applyBookChangeRule(*s, book.index.get());
  EXPECT_TRUE(change.adoptedSha);
  EXPECT_TRUE(s->hasPosition);
  EXPECT_STREQ(s->bookSha256, book.index->sourceSha256());
}

// Close and reopen through the encoding: the answer is never shown revealed,
// and the choice after Incorrect is not kept.
TEST(StateRestore, ReopenAlwaysShowsThePromptWithItsAnswerHidden) {
  FixtureBook book;
  ASSERT_TRUE(book.ok);
  for (int stage = 0; stage < 2; ++stage) {
    ReaderSim sim(book);
    sim.openAt(Place{Position{3, 255}, ReadingMode::Rsvp});
    sim.play();
    ASSERT_EQ(sim.flow->state(), FlowState::PromptHidden);
    sim.handle(FlowEvent::Reveal);
    if (stage == 1) sim.handle(FlowEvent::Incorrect, AT);
    const std::string file = encodeToString(*sim.state);
    auto again = ReaderSim::reopen(book, file);
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(again->flow->state(), FlowState::PromptHidden);
    EXPECT_EQ(again->flow->promptIndex(), 0);
  }
}
