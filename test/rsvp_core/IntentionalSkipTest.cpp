// Word indexes a source passes over on purpose (Token::skippedBefore: a footnote body in
// mid-chapter): the group ends before them, the scheduler declares them to the audit, and the audit
// counts them apart from non-prose skips.

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "PlaybackFakes.h"
#include "TestSupport.h"
#include "lib/RsvpCore/FrameBuilder.h"
#include "lib/RsvpCore/Scheduler.h"
#include "lib/RsvpCore/WordOrderAudit.h"

using rsvp::WordOrderAudit;

namespace {

struct SkipToken {
  std::string text;
  uint32_t wordIndex;
  uint8_t flags;
  uint32_t skippedBefore;
};

// Like rsvptest::VectorSource, but hands out skippedBefore too.
class SkippingSource : public rsvp::WordSource {
 public:
  explicit SkippingSource(std::vector<SkipToken> tokens) : tokens(std::move(tokens)) {}
  bool peek(rsvp::Token& out) override {
    if (position >= tokens.size()) return false;
    const SkipToken& t = tokens[position];
    std::memcpy(scratch, t.text.data(), t.text.size());
    out.text = std::string_view(scratch, t.text.size());
    out.wordIndex = t.wordIndex;
    out.flags = t.flags;
    out.skippedBefore = t.skippedBefore;
    return true;
  }
  void advance() override {
    std::memset(scratch, '#', sizeof(scratch));
    ++position;
  }
  std::vector<SkipToken> tokens;
  size_t position = 0;

 private:
  char scratch[256] = {};
};

// "a b c." | footnote body (indexes 3-6) | "d e f." | table (10-11) | footnote (12) | "g h"
std::vector<SkipToken> chapterWithNotes() {
  return {{"a", 0, 0, 0},  {"b", 1, 0, 0}, {"c.", 2, rsvp::TOKEN_PARAGRAPH_END, 0},
          {"d", 7, 0, 4},  {"e", 8, 0, 0}, {"f.", 9, rsvp::TOKEN_PARAGRAPH_END | rsvp::TOKEN_NON_PROSE_FOLLOWS, 0},
          {"g", 13, 0, 1}, {"h", 14, 0, 0}};
}

}  // namespace

TEST(IntentionalSkip, AuditAcceptsADeclaredSkipAndCountsItApart) {
  WordOrderAudit audit;
  audit.recordFrame(0, 2, 3);
  audit.declareSkip(4);
  audit.recordFrame(7, 8, 2);
  EXPECT_TRUE(audit.inOrder());
  EXPECT_EQ(audit.declaredSkips(), 4u);
  EXPECT_EQ(audit.skippedWords(), 0u);
  EXPECT_EQ(audit.wordsDrawn(), 5u);
  EXPECT_EQ(audit.nextExpected(), 9u);
}

TEST(IntentionalSkip, AuditStillCatchesAWrongCount) {
  WordOrderAudit tooFew;
  tooFew.recordFrame(0, 2, 3);
  tooFew.declareSkip(3);
  tooFew.record(7);  // four indexes were passed over
  EXPECT_FALSE(tooFew.inOrder());
  EXPECT_EQ(tooFew.gaps(), 1u);
  EXPECT_EQ(tooFew.firstViolationExpected(), 6u);

  WordOrderAudit tooMany;
  tooMany.recordFrame(0, 2, 3);
  tooMany.declareSkip(5);
  tooMany.record(7);
  EXPECT_FALSE(tooMany.inOrder());
  EXPECT_EQ(tooMany.repeats(), 1u);
}

TEST(IntentionalSkip, ASkipBeforeANewSegmentIsIgnored) {
  WordOrderAudit audit;
  audit.recordFrame(0, 2, 3);
  audit.declareSkip(10);
  audit.beginSegment();
  audit.record(40);
  audit.record(41);
  EXPECT_TRUE(audit.inOrder());
  EXPECT_EQ(audit.declaredSkips(), 0u);
}

TEST(IntentionalSkip, ADeclaredSkipAndANonProseSkipTogether) {
  WordOrderAudit audit;
  audit.recordFrame(0, 9, 10);
  audit.allowForwardSkip();  // after a table (10-11)
  audit.declareSkip(1);      // and a footnote body (12)
  audit.record(13);
  EXPECT_TRUE(audit.inOrder());
  EXPECT_EQ(audit.declaredSkips(), 1u);
  EXPECT_EQ(audit.skippedWords(), 2u);
}

TEST(IntentionalSkip, AFrameNeverJoinsWordsAcrossASkip) {
  for (uint8_t n = 1; n <= rsvp::MAX_WORDS_PER_FRAME; ++n) {
    SkippingSource source(chapterWithNotes());
    rsvp::FrameBuilder builder(source);
    builder.setWordsPerFrame(n);
    std::vector<rsvp::Frame> frames;
    rsvp::Frame frame;
    while (builder.next(frame)) frames.push_back(frame);
    for (const auto& f : frames) {
      EXPECT_EQ(f.lastWordIndex - f.firstWordIndex + 1u, f.wordCount) << int(n);
      EXPECT_EQ(f.skippedBefore, f.firstWordIndex == 7 ? 4u : (f.firstWordIndex == 13 ? 1u : 0u));
    }
    // With four words per frame: "a b c." | "d e f." | "g h": the skips end no group by themselves
    // here, the paragraph ends do; with "c." not ending a paragraph the skip would.
    if (n == 4) {
      ASSERT_EQ(frames.size(), 3u);
    }
  }
  // Without a paragraph end before the note body, the skip alone ends the group.
  std::vector<SkipToken> tokens = {{"a", 0, 0, 0}, {"b", 1, 0, 0}, {"c", 5, 0, 3}, {"d", 6, 0, 0}};
  SkippingSource source(tokens);
  rsvp::FrameBuilder builder(source);
  builder.setWordsPerFrame(4);
  rsvp::Frame first;
  rsvp::Frame second;
  ASSERT_TRUE(builder.next(first));
  ASSERT_TRUE(builder.next(second));
  EXPECT_EQ(first.wordCount, 2u);
  EXPECT_EQ(std::string(second.view()), "c d");
  EXPECT_EQ(second.skippedBefore, 3u);
}

TEST(IntentionalSkip, SchedulerPassesAMidChapterNoteWithoutAStopOrAViolation) {
  for (uint8_t n = 1; n <= 3; ++n) {
    SkippingSource source(chapterWithNotes());
    rsvptest::FakeClock clock(1000);
    rsvptest::FakeDisplay display(clock, 100, 100);
    rsvp::Scheduler scheduler(source, clock, display);
    scheduler.setWordsPerFrame(n);
    std::vector<uint8_t> stops;
    scheduler.play();
    while (true) {
      const rsvp::StepResult r = scheduler.step();
      if (r.event == rsvp::StepEvent::STOPPED) {
        stops.push_back(r.stops);
        if (r.stops & rsvp::STOP_CHAPTER_END) break;
        scheduler.play();
        continue;
      }
      ASSERT_TRUE(r.wake);
      clock.advanceTo(r.wakeAtMs);
    }
    EXPECT_EQ(stops, (std::vector<uint8_t>{rsvp::STOP_NON_PROSE, rsvp::STOP_CHAPTER_END}));  // none for the notes
    EXPECT_TRUE(scheduler.audit().inOrder());
    EXPECT_EQ(scheduler.audit().wordsDrawn(), 8u);
    EXPECT_EQ(scheduler.audit().declaredSkips(), 5u);
    EXPECT_EQ(scheduler.audit().skippedWords(), 2u);
  }
}

TEST(IntentionalSkip, WithoutTheDeclarationTheAuditReportsAGap) {
  // The same chapter from a source that does not declare the footnote body: the audit sees a gap.
  std::vector<rsvptest::OwnedToken> tokens;
  for (const auto& t : chapterWithNotes()) tokens.push_back({t.text, t.wordIndex, t.flags});
  rsvptest::VectorSource source(tokens);
  rsvptest::FakeClock clock(1000);
  rsvptest::FakeDisplay display(clock, 100, 100);
  rsvp::Scheduler scheduler(source, clock, display);
  scheduler.play();
  for (int i = 0; i < 1000 && scheduler.state() == rsvp::PlaybackState::PLAYING; ++i) {
    const rsvp::StepResult r = scheduler.step();
    if (r.wake) clock.advanceTo(r.wakeAtMs);
  }
  EXPECT_FALSE(scheduler.audit().inOrder());
  EXPECT_EQ(scheduler.audit().firstViolationExpected(), 3u);
  EXPECT_EQ(scheduler.audit().firstViolationDrawn(), 7u);
}
