// Cumulative refresh statistics.

#include <gtest/gtest.h>

#include "DisplayStats.h"

using displayprofile::DisplayStats;
using displayprofile::RefreshStats;

TEST(RefreshStats, EmptyIsAllZero) {
  const RefreshStats s;
  EXPECT_EQ(s.count, 0u);
  EXPECT_EQ(s.minMs, 0u);
  EXPECT_EQ(s.maxMs, 0u);
  EXPECT_EQ(s.lastMs, 0u);
  EXPECT_EQ(s.totalMs, 0u);
  EXPECT_EQ(s.meanMs(), 0u);
}

TEST(RefreshStats, CountMinMaxMeanLast) {
  RefreshStats s;
  for (const uint32_t ms : {540u, 520u, 610u, 545u}) s.add(ms);
  EXPECT_EQ(s.count, 4u);
  EXPECT_EQ(s.minMs, 520u);
  EXPECT_EQ(s.maxMs, 610u);
  EXPECT_EQ(s.lastMs, 545u);
  EXPECT_EQ(s.totalMs, 2215u);
  EXPECT_EQ(s.meanMs(), 554u);  // 553.75 rounded
}

TEST(RefreshStats, FirstSampleSetsMinEvenWhenZero) {
  RefreshStats s;
  s.add(0);
  s.add(5);
  EXPECT_EQ(s.minMs, 0u);
  EXPECT_EQ(s.maxMs, 5u);
  EXPECT_EQ(s.meanMs(), 3u);  // 2.5 rounds half up
}

TEST(RefreshStats, TotalDoesNotOverflow) {
  RefreshStats s;
  for (int i = 0; i < 3; ++i) s.add(0xFFFFFFFFu);
  EXPECT_EQ(s.totalMs, 3ull * 0xFFFFFFFFull);
  EXPECT_EQ(s.meanMs(), 0xFFFFFFFFu);
}

TEST(DisplayStats, ResetClearsBoth) {
  DisplayStats s;
  s.updates.add(540);
  s.clears.add(1960);
  s.reset();
  EXPECT_EQ(s.updates.count, 0u);
  EXPECT_EQ(s.clears.count, 0u);
  EXPECT_EQ(s.clears.maxMs, 0u);
}
