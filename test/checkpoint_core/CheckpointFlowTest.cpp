// Checkpoint and reread state machine, docs/PLAN.md section 7 "Per-prompt
// flow": one test per numbered step (FlowStep1..6), then the event/state
// table, save flags and reread edge cases.

#include <gtest/gtest.h>

#include <vector>

#include "StateTestSupport.h"

using namespace checkpoint;
using checkpoint_test::encodeToString;
using checkpoint_test::FixtureBook;

namespace {

constexpr FlowEvent ALL_EVENTS[] = {
    FlowEvent::Reveal,         FlowEvent::Correct,          FlowEvent::Incorrect,  FlowEvent::SkipPrompt,
    FlowEvent::SkipCheckpoint, FlowEvent::RereadRsvp,       FlowEvent::RereadPage, FlowEvent::Continue,
    FlowEvent::ReturnEarly,    FlowEvent::RereadReachedEnd,
};
static_assert(sizeof(ALL_EVENTS) / sizeof(ALL_EVENTS[0]) == FLOW_EVENT_COUNT);

const char* AT = "2026-12-01T19:05:10";

// A flow over the fixture book with c0002 (three prompts, section s0004 =
// chapter 3 words [255, 550), ends its chapter) fired in `mode`.
struct Fixture {
  FixtureBook book;
  std::unique_ptr<BookState> state = std::make_unique<BookState>();
  std::unique_ptr<ChapterSections> chapter = std::make_unique<ChapterSections>();
  std::unique_ptr<CheckpointFlow> flow;
  uint16_t c2 = NO_INDEX;

  explicit Fixture(ReadingMode mode = ReadingMode::Rsvp) {
    EXPECT_TRUE(book.ok);
    flow = std::make_unique<CheckpointFlow>(*state, *book.index);
    c2 = book.checkpoint("c0002");
    book.loadChapter(*chapter, 3);
    state->hasPosition = true;
    state->position = Place{Position{3, 549}, mode};
    const Step fire = rsvpStep(*chapter, 549);
    EXPECT_EQ(fire.kind, StepKind::Fire);
    const Transition t = flow->start(fire, mode);
    EXPECT_TRUE(t.accepted);
  }

  Grade stored(uint16_t prompt) const { return state->grades.find(book.promptId(c2, prompt).c_str()); }
};

// Drives a flow into each state; returns false when the state is not reachable that way.
bool driveTo(Fixture& f, FlowState target) {
  switch (target) {
    case FlowState::PromptHidden:
      return true;
    case FlowState::AnswerShown:
      return f.flow->handle(FlowEvent::Reveal).accepted;
    case FlowState::IncorrectChoice:
      return f.flow->handle(FlowEvent::Reveal).accepted && f.flow->handle(FlowEvent::Incorrect, AT).accepted;
    case FlowState::Rereading:
      return driveTo(f, FlowState::IncorrectChoice) && f.flow->handle(FlowEvent::RereadPage).accepted;
    case FlowState::Finished:
      return f.flow->handle(FlowEvent::SkipCheckpoint).accepted;
    case FlowState::Idle:
      break;
  }
  return false;
}

}  // namespace

// "Show the prompt with the answer hidden. Actions: Reveal answer, Skip
// prompt, Skip checkpoint."
TEST(CheckpointFlow, FlowStep1_PromptShownWithAnswerHiddenOffersRevealSkipPromptSkipCheckpoint) {
  Fixture f;
  EXPECT_EQ(f.flow->state(), FlowState::PromptHidden);
  EXPECT_EQ(f.flow->checkpoint(), f.c2);
  EXPECT_EQ(f.flow->promptIndex(), 0);
  EXPECT_EQ(f.flow->promptCount(), 3);
  std::vector<FlowEvent> accepted;
  for (FlowEvent e : ALL_EVENTS) {
    if (CheckpointFlow::accepts(FlowState::PromptHidden, e)) accepted.push_back(e);
  }
  EXPECT_EQ(accepted, (std::vector<FlowEvent>{FlowEvent::Reveal, FlowEvent::SkipPrompt, FlowEvent::SkipCheckpoint}));
  // Persisted: the checkpoint id, prompt 0 and where reading resumes.
  EXPECT_TRUE(f.state->hasActive);
  EXPECT_STREQ(f.state->active.id, "c0002");
  EXPECT_EQ(f.state->active.promptIndex, 0);
  EXPECT_FALSE(f.state->active.rereading);
}

// "After reveal, show the reference answer. Actions: Correct, Incorrect, Skip
// prompt, Skip checkpoint."
TEST(CheckpointFlow, FlowStep2_AfterRevealOffersCorrectIncorrectSkipPromptSkipCheckpoint) {
  Fixture f;
  const std::string before = encodeToString(*f.state);
  const Transition t = f.flow->handle(FlowEvent::Reveal);
  EXPECT_TRUE(t.accepted);
  EXPECT_EQ(t.state, FlowState::AnswerShown);
  // Revealing is not persisted: reopening shows the answer hidden.
  EXPECT_EQ(t.save, Save::No);
  EXPECT_EQ(encodeToString(*f.state), before);
  std::vector<FlowEvent> accepted;
  for (FlowEvent e : ALL_EVENTS) {
    if (CheckpointFlow::accepts(FlowState::AnswerShown, e)) accepted.push_back(e);
  }
  EXPECT_EQ(accepted, (std::vector<FlowEvent>{FlowEvent::Correct, FlowEvent::Incorrect, FlowEvent::SkipPrompt,
                                              FlowEvent::SkipCheckpoint}));
}

// "Correct moves to the next prompt, or finishes the checkpoint."
TEST(CheckpointFlow, FlowStep3_CorrectMovesToTheNextPromptOrFinishes) {
  Fixture f;
  for (uint16_t p = 0; p < 3; ++p) {
    ASSERT_EQ(f.flow->promptIndex(), p);
    f.flow->handle(FlowEvent::Reveal);
    const Transition t = f.flow->handle(FlowEvent::Correct, AT);
    EXPECT_TRUE(t.accepted);
    EXPECT_TRUE(t.gradeRecorded);
    EXPECT_EQ(t.save, Save::Now);
    EXPECT_EQ(t.state, p < 2 ? FlowState::PromptHidden : FlowState::Finished);
    EXPECT_EQ(t.finished, p == 2);
    EXPECT_EQ(f.stored(p), Grade::Correct);
  }
  EXPECT_FALSE(f.state->hasActive);
}

// "Incorrect offers Reread section (in RSVP or page view) or Continue.
// Continue moves on."
TEST(CheckpointFlow, FlowStep4_IncorrectOffersRereadInEitherModeOrContinue) {
  Fixture f;
  f.flow->handle(FlowEvent::Reveal);
  const Transition t = f.flow->handle(FlowEvent::Incorrect, AT);
  EXPECT_TRUE(t.accepted);
  EXPECT_TRUE(t.gradeRecorded);
  EXPECT_EQ(t.save, Save::Now);
  EXPECT_EQ(t.state, FlowState::IncorrectChoice);
  EXPECT_EQ(f.stored(0), Grade::Incorrect);
  std::vector<FlowEvent> accepted;
  for (FlowEvent e : ALL_EVENTS) {
    if (CheckpointFlow::accepts(FlowState::IncorrectChoice, e)) accepted.push_back(e);
  }
  EXPECT_EQ(accepted, (std::vector<FlowEvent>{FlowEvent::SkipPrompt, FlowEvent::SkipCheckpoint, FlowEvent::RereadRsvp,
                                              FlowEvent::RereadPage, FlowEvent::Continue}));
  const Transition c = f.flow->handle(FlowEvent::Continue);
  EXPECT_TRUE(c.accepted);
  EXPECT_EQ(c.state, FlowState::PromptHidden);
  EXPECT_EQ(f.flow->promptIndex(), 1);
  EXPECT_EQ(f.stored(0), Grade::Incorrect);
}

// "A reread starts at the section's start anchor in the chosen mode and
// returns to the same prompt with the answer hidden. The reader may then
// retry or continue." Both modes, both ways back.
TEST(CheckpointFlow, FlowStep5_RereadStartsAtSectionStartAndReturnsToTheSamePromptHidden) {
  for (FlowEvent reread : {FlowEvent::RereadRsvp, FlowEvent::RereadPage}) {
    for (FlowEvent back : {FlowEvent::ReturnEarly, FlowEvent::RereadReachedEnd}) {
      Fixture f(ReadingMode::Page);
      f.flow->handle(FlowEvent::SkipPrompt);  // to prompt 1
      ASSERT_TRUE(driveTo(f, FlowState::IncorrectChoice));
      const Place resumeBefore = f.state->active.resume;
      const Transition r = f.flow->handle(reread);
      ASSERT_TRUE(r.accepted);
      EXPECT_EQ(r.state, FlowState::Rereading);
      EXPECT_EQ(r.save, Save::Now);
      EXPECT_TRUE(f.state->active.rereading);
      EXPECT_EQ(f.state->active.reread.position, (Position{3, 255}));
      EXPECT_EQ(f.state->active.reread.mode, reread == FlowEvent::RereadRsvp ? ReadingMode::Rsvp : ReadingMode::Page);
      EXPECT_EQ(f.flow->rereadSection(), f.book.section("s0004"));
      EXPECT_EQ(f.flow->section().start, 255u);
      EXPECT_EQ(f.flow->section().end, 550u);
      // Move within the section: only the reread place changes.
      const Transition m =
          f.flow->rereadMoved(Place{Position{3, 300}, f.state->active.reread.mode}, NavSource::RsvpPlayback);
      EXPECT_TRUE(m.accepted);
      EXPECT_EQ(m.save, Save::Position);
      EXPECT_EQ(f.state->active.reread.position.word, 300u);
      const Transition b = f.flow->handle(back);
      ASSERT_TRUE(b.accepted);
      EXPECT_EQ(b.state, FlowState::PromptHidden);
      EXPECT_EQ(b.save, Save::Now);
      EXPECT_EQ(f.flow->promptIndex(), 1);
      EXPECT_FALSE(f.state->active.rereading);
      EXPECT_EQ(f.state->active.resume, resumeBefore);  // a reread never changes the return position
      // Retry: reveal and grade again; or continue (Skip prompt).
      EXPECT_TRUE(CheckpointFlow::accepts(FlowState::PromptHidden, FlowEvent::Reveal));
      EXPECT_TRUE(CheckpointFlow::accepts(FlowState::PromptHidden, FlowEvent::SkipPrompt));
      EXPECT_EQ(f.stored(1), Grade::Incorrect);
    }
  }
}

// "On finish or Skip checkpoint, restore the mode that was active when the
// checkpoint appeared, at the first word after the break. RSVP waits for
// Play; page view waits for the next action."
TEST(CheckpointFlow, FlowStep6_FinishOrSkipCheckpointResumesAfterTheBreakInTheOriginalModeWaiting) {
  for (ReadingMode mode : {ReadingMode::Rsvp, ReadingMode::Page}) {
    for (int way = 0; way < 3; ++way) {
      Fixture f(mode);
      Transition t;
      if (way == 0) {  // finish by grading every prompt
        for (int p = 0; p < 3; ++p) {
          f.flow->handle(FlowEvent::Reveal);
          t = f.flow->handle(FlowEvent::Correct, AT);
        }
      } else if (way == 1) {  // skip checkpoint after a reread in the other mode
        driveTo(f, FlowState::IncorrectChoice);
        f.flow->handle(mode == ReadingMode::Rsvp ? FlowEvent::RereadPage : FlowEvent::RereadRsvp);
        t = f.flow->handle(FlowEvent::SkipCheckpoint);
      } else {  // skip every prompt
        for (int p = 0; p < 3; ++p) t = f.flow->handle(FlowEvent::SkipPrompt);
      }
      ASSERT_TRUE(t.accepted);
      EXPECT_TRUE(t.finished);
      EXPECT_EQ(t.state, FlowState::Finished);
      EXPECT_EQ(t.save, Save::Now);
      // s0004 ends chapter 3: the first word after the break is chapter 4, word 0.
      EXPECT_EQ(t.resume.position, (Position{4, 0}));
      EXPECT_EQ(t.resume.mode, mode);
      EXPECT_EQ(f.state->position, t.resume);
      EXPECT_FALSE(f.state->hasActive);
      // Waiting: the flow accepts no reader event until the next trigger.
      for (FlowEvent e : ALL_EVENTS) EXPECT_FALSE(f.flow->handle(e, AT).accepted);
    }
  }
}

TEST(CheckpointFlow, EveryEventOutsideTheTableIsRejectedWithoutChangingState) {
  constexpr FlowState STATES[] = {FlowState::PromptHidden, FlowState::AnswerShown, FlowState::IncorrectChoice,
                                  FlowState::Rereading, FlowState::Finished};
  int rejected = 0;
  for (FlowState s : STATES) {
    for (FlowEvent e : ALL_EVENTS) {
      Fixture f;
      ASSERT_TRUE(driveTo(f, s)) << flowStateName(s);
      ASSERT_EQ(f.flow->state(), s);
      const std::string before = encodeToString(*f.state);
      const uint16_t prompt = f.flow->promptIndex();
      const Transition t = f.flow->handle(e, AT);
      EXPECT_EQ(t.accepted, CheckpointFlow::accepts(s, e)) << flowStateName(s) << " " << flowEventName(e);
      if (!t.accepted) {
        ++rejected;
        EXPECT_EQ(t.refusal, Refusal::InvalidInState);
        EXPECT_EQ(t.save, Save::No);
        EXPECT_EQ(f.flow->state(), s);
        EXPECT_EQ(f.flow->promptIndex(), prompt);
        EXPECT_EQ(encodeToString(*f.state), before);
      }
    }
  }
  EXPECT_EQ(rejected, 5 * 10 - 3 - 4 - 5 - 3);  // accepted per row: 3, 4, 5, 3, and none when finished
  // Idle accepts no reader event either.
  FixtureBook book;
  auto state = std::make_unique<BookState>();
  CheckpointFlow idle(*state, *book.index);
  for (FlowEvent e : ALL_EVENTS) EXPECT_FALSE(idle.handle(e, AT).accepted);
  EXPECT_FALSE(idle.rereadMoved(Place{}, NavSource::RsvpPlayback).accepted);
}

TEST(CheckpointFlow, StartAndRestoreAreRefusedWhileACheckpointIsInProgress) {
  Fixture f;
  ChapterSections c;
  f.book.loadChapter(c, 2);
  const Step other = rsvpStep(c, 222);
  ASSERT_EQ(other.kind, StepKind::Fire);
  EXPECT_EQ(f.flow->start(other, ReadingMode::Page).refusal, Refusal::InvalidInState);
  RestorePlan plan;
  plan.view = OpenView::Prompt;
  plan.checkpoint = f.c2;
  plan.section = f.flow->section();
  EXPECT_EQ(f.flow->restore(plan).refusal, Refusal::InvalidInState);
  EXPECT_STREQ(f.state->active.id, "c0002");
  // After finishing, the next trigger starts a new checkpoint.
  f.flow->handle(FlowEvent::SkipCheckpoint);
  EXPECT_TRUE(f.flow->start(other, ReadingMode::Page).accepted);
  EXPECT_STREQ(f.state->active.id, "c0001");
  // A step that is not a Fire is refused.
  auto state = std::make_unique<BookState>();
  CheckpointFlow fresh(*state, *f.book.index);
  EXPECT_EQ(fresh.start(Step{}, ReadingMode::Rsvp).refusal, Refusal::BadStart);
  Step wrong = other;
  wrong.section.section = f.book.section("s0004");
  EXPECT_EQ(fresh.start(wrong, ReadingMode::Rsvp).refusal, Refusal::BadStart);
  EXPECT_FALSE(state->hasActive);
}

// Decision: during a reread, leaving the section by any move other than
// reading forward across its end returns to the prompt, as Return early does.
TEST(CheckpointFlow, RereadMovesOutOfTheSectionReturnToThePrompt) {
  struct Case {
    Position to;
    NavSource source;
    bool leftSection;
  };
  const Case cases[] = {
      {{3, 600}, NavSource::NextPage, false},       // forward past the end
      {{4, 0}, NavSource::RsvpPlayback, false},     // forward into the next chapter
      {{3, 254}, NavSource::PreviousPage, true},    // back before the start
      {{2, 10}, NavSource::TableOfContents, true},  // another chapter
      {{3, 550}, NavSource::GoToPercent, true},     // a jump past the end does not count as reaching it
  };
  for (const Case& c : cases) {
    Fixture f;
    driveTo(f, FlowState::Rereading);
    const Transition t = f.flow->rereadMoved(Place{c.to, ReadingMode::Page}, c.source);
    EXPECT_TRUE(t.accepted);
    EXPECT_EQ(t.state, FlowState::PromptHidden);
    EXPECT_EQ(t.save, Save::Now);
    EXPECT_EQ(t.leftSection, c.leftSection) << navSourceName(c.source);
    EXPECT_FALSE(f.state->active.rereading);
  }
  // A mode switch inside the section is saved at once and keeps the reread.
  Fixture f;
  driveTo(f, FlowState::Rereading);
  const Transition t = f.flow->rereadMoved(Place{Position{3, 255}, ReadingMode::Rsvp}, NavSource::ModeSwitch);
  EXPECT_EQ(t.state, FlowState::Rereading);
  EXPECT_EQ(t.save, Save::Now);
  EXPECT_EQ(f.state->active.reread.mode, ReadingMode::Rsvp);
  // Skip checkpoint is accepted mid-reread and resumes after the break.
  const Transition s = f.flow->handle(FlowEvent::SkipCheckpoint);
  EXPECT_TRUE(s.finished);
  EXPECT_EQ(s.resume.position, (Position{4, 0}));
  EXPECT_EQ(s.resume.mode, ReadingMode::Rsvp);
}

namespace {

// Serves a checkpoint file and can be told to fail every later read.
class SwitchableSource final : public CheckpointSource {
 public:
  explicit SwitchableSource(const std::vector<uint8_t>& bytes) : data(bytes) {}
  uint32_t size() const override { return static_cast<uint32_t>(data.size()); }
  size_t read(uint32_t offset, uint8_t* out, size_t count) override {
    if (failing || offset >= data.size()) return 0;
    const size_t n = std::min(count, data.size() - offset);
    memcpy(out, data.data() + offset, n);
    return n;
  }
  bool failing = false;

 private:
  const std::vector<uint8_t>& data;
};

}  // namespace

TEST(CheckpointFlow, AGradeIsRefusedNotLostWhenThePromptIdCannotBeRead) {
  FixtureBook book;
  SwitchableSource source(book.bytes);
  auto index = std::make_unique<CheckpointIndex>();
  ASSERT_EQ(index->open(source).verdict, Verdict::Valid);
  auto state = std::make_unique<BookState>();
  CheckpointFlow flow(*state, *index);
  ChapterSections c;
  book.loadChapter(c, 2);
  ASSERT_TRUE(flow.start(rsvpStep(c, 222), ReadingMode::Rsvp).accepted);
  flow.handle(FlowEvent::Reveal);
  source.failing = true;
  const Transition t = flow.handle(FlowEvent::Correct, AT);
  EXPECT_FALSE(t.accepted);
  EXPECT_EQ(t.refusal, Refusal::PromptIdUnreadable);
  EXPECT_EQ(flow.state(), FlowState::AnswerShown);
  EXPECT_EQ(flow.storedGrade(), Grade::Ungraded);
  source.failing = false;
  EXPECT_TRUE(flow.handle(FlowEvent::Correct, AT).accepted);
  EXPECT_EQ(state->grades.find("c0001-p1"), Grade::Correct);
}
