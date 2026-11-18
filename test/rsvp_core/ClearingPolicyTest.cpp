#include <gtest/gtest.h>

#include "lib/RsvpCore/ClearingPolicy.h"

using rsvp::ClearingLimits;
using rsvp::ClearingPolicy;

namespace {

// Feeds `count` updates that do not end a block.
void plainUpdates(ClearingPolicy& policy, int count) {
  for (int i = 0; i < count; ++i) policy.noteWordUpdate(false);
}

// Index (1-based) of the update after which a clear first falls due, given which updates end a
// block; 0 if none within `limit` updates.
int firstDue(ClearingLimits limits, int blockEvery, int limit = 1000) {
  ClearingPolicy policy(limits);
  for (int i = 1; i <= limit; ++i) {
    policy.noteWordUpdate(blockEvery > 0 && i % blockEvery == 0);
    if (policy.clearDue()) return i;
  }
  return 0;
}

}  // namespace

TEST(ClearingPolicy, DefaultsAreSixtyAndOneHundredTwenty) {
  const ClearingLimits limits;
  EXPECT_EQ(limits.softLimit, 60);
  EXPECT_EQ(limits.hardLimit, 120);
}

TEST(ClearingPolicy, NotDueBeforeTheSoftLimitEvenAtAParagraphEnd) {
  ClearingPolicy policy;
  plainUpdates(policy, 58);
  policy.noteWordUpdate(true);  // update 59 ends a paragraph
  EXPECT_EQ(policy.updatesSinceClear(), 59u);
  EXPECT_FALSE(policy.clearDue());
}

TEST(ClearingPolicy, DueAtAParagraphEndOnceTheSoftLimitIsReached) {
  ClearingPolicy policy;
  plainUpdates(policy, 59);
  EXPECT_FALSE(policy.clearDue());
  policy.noteWordUpdate(true);  // update 60 ends a paragraph
  EXPECT_TRUE(policy.clearDue());
}

TEST(ClearingPolicy, PastTheSoftLimitWaitsForTheNextBlockEnd) {
  ClearingPolicy policy;
  plainUpdates(policy, 80);
  EXPECT_FALSE(policy.clearDue());
  policy.noteWordUpdate(true);
  EXPECT_TRUE(policy.clearDue());
}

TEST(ClearingPolicy, HardLimitClearsWithoutABlockEnd) {
  ClearingPolicy policy;
  plainUpdates(policy, 119);
  EXPECT_FALSE(policy.clearDue());
  policy.noteWordUpdate(false);
  EXPECT_TRUE(policy.clearDue());
}

TEST(ClearingPolicy, ClearingRestartsTheCount) {
  ClearingPolicy policy;
  plainUpdates(policy, 120);
  ASSERT_TRUE(policy.clearDue());
  policy.noteClearingRefresh();
  EXPECT_FALSE(policy.clearDue());
  EXPECT_EQ(policy.updatesSinceClear(), 0u);
  // A clearing refresh run elsewhere (on pause) also forgets a pending paragraph end.
  plainUpdates(policy, 59);
  policy.noteWordUpdate(true);
  ASSERT_TRUE(policy.clearDue());
  policy.noteClearingRefresh();
  EXPECT_FALSE(policy.clearDue());
}

TEST(ClearingPolicy, FirstClearPointForEachProfile) {
  // Paragraph end every 7 updates: soft limit rounds up to the next multiple of 7.
  EXPECT_EQ(firstDue({60, 120}, 7), 63);
  EXPECT_EQ(firstDue({45, 90}, 7), 49);
  EXPECT_EQ(firstDue({30, 60}, 7), 35);
  // No paragraph ends at all: the hard limit decides.
  EXPECT_EQ(firstDue({60, 120}, 0), 120);
  EXPECT_EQ(firstDue({45, 90}, 0), 90);
  EXPECT_EQ(firstDue({30, 60}, 0), 60);
  // Paragraphs longer than the gap between the limits: the hard limit comes first.
  EXPECT_EQ(firstDue({60, 120}, 200), 120);
}

TEST(ClearingPolicy, ZeroTurnsARuleOff) {
  EXPECT_EQ(firstDue({0, 120}, 7), 120);
  EXPECT_EQ(firstDue({60, 0}, 7), 63);
  EXPECT_EQ(firstDue({60, 0}, 0), 0);
  EXPECT_EQ(firstDue({0, 0}, 1), 0);
}

TEST(ClearingPolicy, LimitsCanChangeBetweenUpdates) {
  ClearingPolicy policy;
  plainUpdates(policy, 70);
  EXPECT_FALSE(policy.clearDue());
  policy.setLimits({30, 60});
  EXPECT_TRUE(policy.clearDue());
  EXPECT_EQ(policy.currentLimits().hardLimit, 60);
}
