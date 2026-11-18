#include <gtest/gtest.h>

#include "lib/RsvpCore/WordOrderAudit.h"

using rsvp::WordOrderAudit;

TEST(WordOrderAudit, ConsecutiveIndexesAreInOrder) {
  WordOrderAudit audit;
  EXPECT_TRUE(audit.inOrder());
  audit.recordFrame(10, 10, 1);
  audit.recordFrame(11, 13, 3);
  audit.record(14);
  EXPECT_TRUE(audit.inOrder());
  EXPECT_EQ(audit.wordsDrawn(), 5u);
  EXPECT_EQ(audit.segments(), 1u);
  EXPECT_EQ(audit.nextExpected(), 15u);
}

TEST(WordOrderAudit, AGapIsAViolation) {
  WordOrderAudit audit;
  audit.recordFrame(0, 1, 2);
  audit.recordFrame(3, 3, 1);  // word 2 never drawn
  audit.recordFrame(4, 4, 1);
  EXPECT_FALSE(audit.inOrder());
  EXPECT_EQ(audit.violations(), 1u);
  EXPECT_EQ(audit.gaps(), 1u);
  EXPECT_EQ(audit.repeats(), 0u);
  EXPECT_EQ(audit.firstViolationExpected(), 2u);
  EXPECT_EQ(audit.firstViolationDrawn(), 3u);
}

TEST(WordOrderAudit, ARepeatOrAStepBackIsAViolation) {
  WordOrderAudit audit;
  for (uint32_t i = 0; i < 5; ++i) audit.record(i);
  audit.record(3);  // repeat
  audit.record(4);  // repeat
  audit.record(5);  // back in order
  EXPECT_EQ(audit.violations(), 2u);
  EXPECT_EQ(audit.repeats(), 2u);
  EXPECT_EQ(audit.firstViolationExpected(), 5u);
  EXPECT_EQ(audit.firstViolationDrawn(), 3u);
  EXPECT_EQ(audit.wordsDrawn(), 8u);
}

TEST(WordOrderAudit, AFrameWhoseRangeDoesNotMatchItsCountIsAViolation) {
  WordOrderAudit audit;
  audit.recordFrame(0, 0, 1);
  audit.recordFrame(1, 3, 2);  // two words but three indexes: one was skipped inside the frame
  EXPECT_FALSE(audit.inOrder());
}

TEST(WordOrderAudit, ASegmentMayStartAnywhere) {
  WordOrderAudit audit;
  audit.recordFrame(0, 9, 10);
  audit.beginSegment();  // a sentence step back
  audit.recordFrame(4, 6, 3);
  audit.beginSegment();  // a jump forward
  audit.recordFrame(100, 100, 1);
  audit.record(101);
  EXPECT_TRUE(audit.inOrder());
  EXPECT_EQ(audit.segments(), 3u);
  EXPECT_EQ(audit.wordsDrawn(), 15u);
  // Without a new segment, the same step back is a repeat.
  audit.record(50);
  EXPECT_FALSE(audit.inOrder());
}

TEST(WordOrderAudit, AnAllowedForwardSkipCountsTheSkippedIndexesOnce) {
  WordOrderAudit audit;
  audit.recordFrame(0, 107, 108);
  audit.allowForwardSkip();  // a table's words, indexes 108 to 140, are not RSVP words
  audit.record(141);
  EXPECT_TRUE(audit.inOrder());
  EXPECT_EQ(audit.skippedWords(), 33u);
  // The allowance is used up by the next index, even when that index skips nothing.
  audit.allowForwardSkip();
  audit.record(142);
  audit.record(150);
  EXPECT_FALSE(audit.inOrder());
  EXPECT_EQ(audit.skippedWords(), 33u);
}

TEST(WordOrderAudit, AForwardSkipNeverAllowsARepeat) {
  WordOrderAudit audit;
  audit.recordFrame(0, 4, 5);
  audit.allowForwardSkip();
  audit.record(4);
  EXPECT_EQ(audit.repeats(), 1u);
}

TEST(WordOrderAudit, ResetClearsEverything) {
  WordOrderAudit audit;
  audit.record(0);
  audit.record(5);
  ASSERT_FALSE(audit.inOrder());
  audit.reset();
  EXPECT_TRUE(audit.inOrder());
  EXPECT_EQ(audit.wordsDrawn(), 0u);
  EXPECT_EQ(audit.segments(), 0u);
  audit.record(7);
  EXPECT_TRUE(audit.inOrder());
  EXPECT_EQ(audit.segments(), 1u);
}
