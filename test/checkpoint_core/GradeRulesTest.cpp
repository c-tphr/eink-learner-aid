// Grade rules, docs/PLAN.md section 7 "Grade rules": one test per bullet
// (GradeRule1..3), the later-pass rule of "Trigger rules" (GradeRule4), then
// ids, times, capacity and RAM.

#include <gtest/gtest.h>

#include <cstdio>
#include <random>

#include "StateTestSupport.h"

using namespace checkpoint;
using checkpoint_test::decodeString;
using checkpoint_test::encodeToString;
using checkpoint_test::FixtureBook;
using checkpoint_test::ReaderSim;

namespace {

const char* T1 = "2026-12-01T19:04:00";
const char* T2 = "2026-12-02T08:00:00";
const char* T3 = "2027-01-15T21:30:59";

// Reads chapter 3 in RSVP from word 255 until c0002 fires.
void fireC0002(ReaderSim& sim, ReadingMode mode = ReadingMode::Rsvp) {
  sim.openAt(Place{Position{3, 255}, mode});
  const Step s = mode == ReadingMode::Rsvp ? sim.play() : Step{};
  if (mode == ReadingMode::Page) {
    for (int i = 0; i < 20 && sim.reading(); ++i) sim.nextPage();
  } else {
    EXPECT_EQ(s.kind, StepKind::Fire);
  }
  ASSERT_EQ(sim.flow->state(), FlowState::PromptHidden);
  ASSERT_STREQ(sim.state->active.id, "c0002");
}

}  // namespace

// "The first Correct or Incorrect for a prompt is written once and never
// overwritten, across retries, rereads, sessions and later passes."
TEST(GradeRules, GradeRule1_FirstGradeIsWrittenOnceAndNeverOverwritten) {
  // The table itself.
  auto table = std::make_unique<GradeTable>();
  EXPECT_EQ(table->recordFirst("p1", Grade::Incorrect, T1), GradeTable::RecordResult::Recorded);
  EXPECT_EQ(table->recordFirst("p1", Grade::Correct, T2), GradeTable::RecordResult::AlreadyGraded);
  Timestamp at;
  EXPECT_EQ(table->find("p1", &at), Grade::Incorrect);
  char text[TIMESTAMP_CHARS + 1];
  ASSERT_TRUE(formatTimestamp(at, text));
  EXPECT_STREQ(text, T1);

  FixtureBook book;
  ASSERT_TRUE(book.ok);
  const uint16_t c2 = book.checkpoint("c0002");
  ReaderSim sim(book);
  fireC0002(sim);
  // Retry and reread: first Incorrect stays.
  sim.handle(FlowEvent::Reveal);
  EXPECT_TRUE(sim.handle(FlowEvent::Incorrect, T1).gradeRecorded);
  sim.handle(FlowEvent::RereadRsvp);
  sim.play();  // to the section end, back to the prompt
  ASSERT_EQ(sim.flow->state(), FlowState::PromptHidden);
  sim.handle(FlowEvent::Reveal);
  EXPECT_FALSE(sim.handle(FlowEvent::Incorrect, T2).gradeRecorded);
  sim.handle(FlowEvent::RereadPage);
  sim.handle(FlowEvent::ReturnEarly);
  sim.handle(FlowEvent::Reveal);
  EXPECT_FALSE(sim.handle(FlowEvent::Correct, T2).gradeRecorded);
  EXPECT_EQ(sim.stored(c2, 0), Grade::Incorrect);
  // Prompt 1 gets Correct, then the book is closed on prompt 2.
  sim.handle(FlowEvent::Reveal);
  EXPECT_TRUE(sim.handle(FlowEvent::Correct, T1).gradeRecorded);
  // Another session: grading prompt 0 and 1 again on a later pass changes nothing.
  auto again = ReaderSim::reopen(book, sim.lastSaved);
  ASSERT_NE(again, nullptr);
  ASSERT_EQ(again->flow->state(), FlowState::PromptHidden);
  EXPECT_EQ(again->flow->promptIndex(), 2);
  again->handle(FlowEvent::SkipCheckpoint);
  fireC0002(*again);
  again->handle(FlowEvent::Reveal);
  EXPECT_FALSE(again->handle(FlowEvent::Correct, T3).gradeRecorded);
  again->handle(FlowEvent::Reveal);
  EXPECT_FALSE(again->handle(FlowEvent::Incorrect, T3).gradeRecorded);
  EXPECT_EQ(again->stored(c2, 0), Grade::Incorrect);
  EXPECT_EQ(again->stored(c2, 1), Grade::Correct);
  again->state->grades.find(book.promptId(c2, 0).c_str(), &at);
  formatTimestamp(at, text);
  EXPECT_STREQ(text, T1);
}

// "Skipping writes nothing. A skipped prompt stays ungraded until it is
// graded for the first time."
TEST(GradeRules, GradeRule2_SkippingWritesNothingAndASkippedPromptStaysUngradedUntilGraded) {
  FixtureBook book;
  ASSERT_TRUE(book.ok);
  const uint16_t c2 = book.checkpoint("c0002");
  ReaderSim sim(book);
  fireC0002(sim);
  sim.handle(FlowEvent::SkipPrompt);
  sim.handle(FlowEvent::Reveal);
  sim.handle(FlowEvent::SkipPrompt);  // after reveal
  sim.handle(FlowEvent::Reveal);
  sim.handle(FlowEvent::SkipCheckpoint);
  EXPECT_EQ(sim.state->grades.count(), 0);
  for (uint16_t p = 0; p < 3; ++p) EXPECT_EQ(sim.stored(c2, p), Grade::Ungraded);
  // The next pass grades prompt 1 for the first time.
  fireC0002(sim, ReadingMode::Page);
  sim.handle(FlowEvent::SkipPrompt);
  sim.handle(FlowEvent::Reveal);
  EXPECT_TRUE(sim.handle(FlowEvent::Correct, T2).gradeRecorded);
  EXPECT_EQ(sim.stored(c2, 0), Grade::Ungraded);
  EXPECT_EQ(sim.stored(c2, 1), Grade::Correct);
  EXPECT_EQ(sim.stored(c2, 2), Grade::Ungraded);
}

// "A retry after Incorrect still lets the reader mark Correct to move on; the
// stored grade stays Incorrect."
TEST(GradeRules, GradeRule3_RetryAfterIncorrectMarksCorrectToMoveOnWhileTheStoredGradeStaysIncorrect) {
  FixtureBook book;
  ASSERT_TRUE(book.ok);
  const uint16_t c2 = book.checkpoint("c0002");
  ReaderSim sim(book);
  fireC0002(sim);
  sim.handle(FlowEvent::Reveal);
  sim.handle(FlowEvent::Incorrect, T1);
  sim.handle(FlowEvent::RereadPage);
  sim.handle(FlowEvent::ReturnEarly);
  sim.handle(FlowEvent::Reveal);
  const Transition t = sim.handle(FlowEvent::Correct, T2);
  EXPECT_TRUE(t.accepted);
  EXPECT_FALSE(t.gradeRecorded);
  EXPECT_EQ(t.state, FlowState::PromptHidden);
  EXPECT_EQ(sim.flow->promptIndex(), 1);  // moved on
  EXPECT_EQ(sim.stored(c2, 0), Grade::Incorrect);
}

// Trigger rules: "Reading through the section again later fires the
// checkpoint again and presents all its prompts, with saved first grades
// shown as already recorded."
TEST(GradeRules, GradeRule4_LaterPassPresentsEveryPromptWithItsStoredFirstGrade) {
  FixtureBook book;
  ASSERT_TRUE(book.ok);
  ReaderSim sim(book);
  fireC0002(sim);
  sim.handle(FlowEvent::Reveal);
  sim.handle(FlowEvent::Correct, T1);
  sim.handle(FlowEvent::SkipPrompt);
  sim.handle(FlowEvent::Reveal);
  sim.handle(FlowEvent::Incorrect, T1);
  sim.handle(FlowEvent::Continue);
  ASSERT_EQ(sim.flow->state(), FlowState::Finished);
  // A later pass: jump back to the section start, read through it again.
  sim.jump(Position{3, 255}, NavSource::TableOfContents);
  fireC0002(sim);
  const Grade expected[] = {Grade::Correct, Grade::Ungraded, Grade::Incorrect};
  for (uint16_t p = 0; p < 3; ++p) {
    ASSERT_EQ(sim.flow->state(), FlowState::PromptHidden);
    EXPECT_EQ(sim.flow->promptIndex(), p);
    EXPECT_EQ(sim.flow->storedGrade(), expected[p]) << p;
    sim.handle(FlowEvent::SkipPrompt);
  }
  EXPECT_EQ(sim.flow->state(), FlowState::Finished);
}

// Grades are keyed by prompt id: a re-prepared file that reorders prompts,
// moves one to another checkpoint and drops one keeps the matching grades.
TEST(GradeRules, GradesAreKeyedByPromptIdAcrossRepreparation) {
  auto state = std::make_unique<BookState>();
  state->grades.recordFirst("c0002-p1", Grade::Correct, T1);
  state->grades.recordFirst("c0002-p2", Grade::Incorrect, T1);
  state->grades.recordFirst("c0002-p3", Grade::Correct, T1);
  const std::string sha = "b07c3ea9857b00644d284c0eae083bc504bfc895525d0a864c183b916e17c82c";
  const std::string json =
      "{\"format\":\"rsvp-checkpoints\",\"version\":1,\"book\":{\"title\":\"T\",\"language\":\"en\","
      "\"source_sha256\":\"" +
      sha +
      "\",\"instructions_version\":\"1\"},\"sections\":["
      "{\"id\":\"s1\",\"chapter_href\":\"a.xhtml\",\"start_anchor\":\"rsvp-s1-start\",\"end_anchor\":\"rsvp-s1-end\","
      "\"label\":\"L\",\"word_count\":5},"
      "{\"id\":\"s2\",\"chapter_href\":\"a.xhtml\",\"start_anchor\":\"rsvp-s2-start\",\"end_anchor\":\"rsvp-s2-end\","
      "\"label\":\"L\",\"word_count\":5}],\"checkpoints\":["
      "{\"id\":\"c1\",\"section\":\"s1\",\"prompts\":[{\"id\":\"c0002-p3\",\"prompt\":\"Q\",\"answer\":\"A\"},"
      "{\"id\":\"new-p\",\"prompt\":\"Q\",\"answer\":\"A\"}]},"
      "{\"id\":\"c2\",\"section\":\"s2\",\"prompts\":[{\"id\":\"c0002-p1\",\"prompt\":\"Q\",\"answer\":\"A\"}]}]}";
  const std::vector<uint8_t> bytes(json.begin(), json.end());
  MemoryCheckpointSource source(bytes.data(), bytes.size());
  auto index = std::make_unique<CheckpointIndex>();
  ASSERT_EQ(index->open(source).verdict, Verdict::Valid);
  CheckpointFlow flow(*state, *index);
  ChapterSections c;
  c.reset(0, 10, NO_CHAPTER);
  c.add(0, 0, 5, 0);
  c.add(1, 5, 10, 1);
  ASSERT_TRUE(flow.start(rsvpStep(c, 4), ReadingMode::Page).accepted);
  EXPECT_EQ(flow.storedGrade(), Grade::Correct);  // c0002-p3, now first in c1
  flow.handle(FlowEvent::SkipPrompt);
  EXPECT_EQ(flow.storedGrade(), Grade::Ungraded);  // new-p
  flow.handle(FlowEvent::SkipPrompt);
  ASSERT_TRUE(flow.start(rsvpStep(c, 9), ReadingMode::Page).accepted);
  EXPECT_EQ(flow.storedGrade(), Grade::Correct);  // c0002-p1
  // A different source EPUB keeps only ids the book still has (StateRestore).
  EXPECT_EQ(state->grades.retainPromptsOf(*index), 1);  // c0002-p2 dropped
  EXPECT_EQ(state->grades.count(), 2);
  EXPECT_EQ(state->grades.find("c0002-p2"), Grade::Ungraded);
  EXPECT_EQ(state->grades.find("c0002-p3"), Grade::Correct);
}

TEST(GradeRules, TimesAreParsedStrictlyAndAGradeWithoutAValidTimeIsStillRecorded) {
  Timestamp t;
  EXPECT_TRUE(parseTimestamp("2000-01-01T00:00:00", 19, t));
  EXPECT_EQ(t, 0u);
  EXPECT_TRUE(parseTimestamp("2024-02-29T23:59:59", 19, t));
  EXPECT_TRUE(parseTimestamp("2135-12-31T23:59:59", 19, t));
  for (const char* bad : {"2023-02-29T00:00:00", "1999-12-31T23:59:59", "2136-01-01T00:00:00", "2026-13-01T00:00:00",
                          "2026-12-01 19:04:00", "2026-12-01T24:00:00", "2026-12-01T19:60:00", "2026-12-01T19:04:60",
                          "2026-12-01T19:04:0Z", "2026-12-01T19:04:00Z", "", "2026-1-01T19:04:00"}) {
    std::string s(bad);
    EXPECT_FALSE(parseTimestamp(s.c_str(), s.size(), t)) << bad;
    EXPECT_EQ(t, NO_TIMESTAMP);
  }
  std::mt19937 rng(25);
  char text[TIMESTAMP_CHARS + 1];
  for (int i = 0; i < 20000; ++i) {
    const Timestamp v = static_cast<Timestamp>(rng() % 4291747200u);
    ASSERT_TRUE(formatTimestamp(v, text));
    ASSERT_TRUE(parseTimestamp(text, TIMESTAMP_CHARS, t)) << text;
    ASSERT_EQ(t, v);
  }
  EXPECT_FALSE(formatTimestamp(NO_TIMESTAMP, text));
  auto table = std::make_unique<GradeTable>();
  EXPECT_EQ(table->recordFirst("p1", Grade::Correct, "not a time"), GradeTable::RecordResult::Recorded);
  EXPECT_EQ(table->recordFirst("p2", Grade::Correct, nullptr), GradeTable::RecordResult::Recorded);
  EXPECT_EQ(table->at(0), NO_TIMESTAMP);
  EXPECT_EQ(table->recordFirst("-p", Grade::Correct, T1), GradeTable::RecordResult::BadId);
  EXPECT_EQ(table->recordFirst("p 3", Grade::Correct, T1), GradeTable::RecordResult::BadId);
  EXPECT_EQ(table->recordFirst("p3", Grade::Ungraded, T1), GradeTable::RecordResult::BadGrade);
}

TEST(GradeRules, TableHoldsMaxPromptsGradesWithLongestIdsAndReportsItsRam) {
  auto table = std::make_unique<GradeTable>();
  char id[MAX_ID_CHARS + 1];
  for (uint16_t i = 0; i < GRADE_CAPACITY; ++i) {
    snprintf(id, sizeof(id), "%064u", static_cast<unsigned>(i));
    ASSERT_EQ(table->recordFirst(id, Grade::Incorrect, T1), GradeTable::RecordResult::Recorded) << i;
  }
  EXPECT_EQ(table->count(), GRADE_CAPACITY);
  EXPECT_EQ(table->recordFirst("one-more", Grade::Correct, T1), GradeTable::RecordResult::Full);
  snprintf(id, sizeof(id), "%064u", 1000u);
  EXPECT_EQ(table->find(id), Grade::Incorrect);
  std::printf(
      "sizeof(GradeTable) = %zu, sizeof(BookState) = %zu, sizeof(ChapterSections) = %zu, "
      "sizeof(CheckpointFlow) = %zu\n",
      sizeof(GradeTable), sizeof(BookState), sizeof(ChapterSections), sizeof(CheckpointFlow));
  EXPECT_LE(sizeof(GradeTable), 12u * GRADE_CAPACITY + GRADE_ID_POOL_BYTES + 8u);
  EXPECT_LE(sizeof(BookState), sizeof(GradeTable) + 256u);
}

// A full table makes room by dropping grades of ids the book no longer has.
TEST(GradeRules, AFullTableDropsGradesOfUnknownIdsBeforeRefusingAFirstGrade) {
  FixtureBook book;
  ASSERT_TRUE(book.ok);
  ReaderSim sim(book);
  char id[MAX_ID_CHARS + 1];
  for (uint16_t i = 0; i < GRADE_CAPACITY; ++i) {
    snprintf(id, sizeof(id), "old-%u", static_cast<unsigned>(i));
    ASSERT_EQ(sim.state->grades.recordFirst(id, Grade::Correct, T1), GradeTable::RecordResult::Recorded);
  }
  fireC0002(sim);
  sim.handle(FlowEvent::Reveal);
  const Transition t = sim.handle(FlowEvent::Incorrect, T2);
  EXPECT_TRUE(t.accepted);
  EXPECT_TRUE(t.gradeRecorded);
  EXPECT_EQ(sim.state->grades.count(), 1);
  EXPECT_EQ(sim.stored(book.checkpoint("c0002"), 0), Grade::Incorrect);
}
