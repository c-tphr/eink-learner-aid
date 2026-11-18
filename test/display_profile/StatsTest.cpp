// Cumulative refresh statistics.

#include <gtest/gtest.h>

#include <cstdio>

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

// Median and percentiles (task D1): what the benchmark (D3a) reports for each series.

TEST(RefreshStats, MedianOfOddAndEvenCounts) {
  RefreshStats s;
  EXPECT_EQ(s.medianMs(), 0u);
  for (const uint32_t ms : {420u, 380u, 401u}) s.add(ms);
  EXPECT_EQ(s.medianMs(), 401u);
  s.add(390);  // 380 390 | 401 420: mean of the middle two, 395.5, rounded half up
  EXPECT_EQ(s.medianMs(), 396u);
  s.reset();
  s.add(7);
  EXPECT_EQ(s.medianMs(), 7u);
}

TEST(RefreshStats, NearestRankPercentilesOfAHundredUpdates) {
  RefreshStats s;
  // 1..100 ms, added out of order.
  for (uint32_t i = 0; i < 100; ++i) s.add((i * 37) % 100 + 1);
  EXPECT_EQ(s.windowCount(), 100u);
  EXPECT_EQ(s.percentileMs(95), 95u);
  EXPECT_EQ(s.percentileMs(50), 50u);
  EXPECT_EQ(s.percentileMs(1), 1u);
  EXPECT_EQ(s.percentileMs(100), 100u);
  EXPECT_EQ(s.medianMs(), 51u);  // (50 + 51) / 2 = 50.5, rounded half up
  EXPECT_EQ(s.percentileMs(0), 0u);
  EXPECT_EQ(s.percentileMs(101), 0u);
}

TEST(RefreshStats, PercentileOfASmallSeriesRoundsTheRankUp) {
  RefreshStats s;
  // Five clears: p95 rank is ceil(4.75) = 5, the maximum; the median is the third.
  for (const uint32_t ms : {980u, 1002u, 960u, 1500u, 990u}) s.add(ms);
  EXPECT_EQ(s.percentileMs(95), 1500u);
  EXPECT_EQ(s.medianMs(), 990u);
  // Twenty updates, one slow: p95 rank is 19, so a single outlier stays out of it.
  RefreshStats u;
  for (int i = 0; i < 19; ++i) u.add(400);
  u.add(2000);
  EXPECT_EQ(u.percentileMs(95), 400u);
  EXPECT_EQ(u.maxMs, 2000u);
}

TEST(RefreshStats, DuplicatesAreHandled) {
  RefreshStats s;
  for (const uint32_t ms : {5u, 5u, 5u, 9u}) s.add(ms);
  EXPECT_EQ(s.percentileMs(75), 5u);
  EXPECT_EQ(s.percentileMs(76), 9u);
  EXPECT_EQ(s.medianMs(), 5u);
}

TEST(RefreshStats, WindowKeepsTheMostRecentSamples) {
  RefreshStats s;
  const uint32_t window = static_cast<uint32_t>(RefreshStats::SAMPLE_WINDOW);
  for (uint32_t i = 0; i < window; ++i) s.add(1000);  // older samples, pushed out below
  for (uint32_t i = 0; i < window; ++i) s.add(i + 1);
  EXPECT_EQ(s.count, 2 * window);
  EXPECT_EQ(s.windowCount(), RefreshStats::SAMPLE_WINDOW);
  EXPECT_EQ(s.percentileMs(100), window);  // no 1000 left in the window
  EXPECT_EQ(s.maxMs, 1000u);               // but the cumulative figures keep it
  EXPECT_EQ(s.medianMs(), window / 2 + 1);  // (64 + 65) / 2 rounded half up
  s.add(500);  // wraps: overwrites the oldest sample, 1
  EXPECT_EQ(s.windowCount(), RefreshStats::SAMPLE_WINDOW);
  EXPECT_EQ(s.percentileMs(1), 3u);  // rank ceil(1.28) = 2 among 2..128 and 500
  EXPECT_EQ(s.percentileMs(100), 500u);
  EXPECT_EQ(s.medianMs(), 66u);  // ranks 64 and 65: (65 + 66) / 2 rounded half up
}

TEST(RefreshStats, LongDurationsSaturateInTheWindowOnly) {
  RefreshStats s;
  s.add(30020);  // the SDK's BUSY wait gives up after 30 s
  s.add(70000);
  EXPECT_EQ(s.maxMs, 70000u);
  EXPECT_EQ(s.percentileMs(100), 65535u);
  EXPECT_EQ(s.percentileMs(50), 30020u);
}

TEST(RefreshStats, TimeoutsAreCountedAndReset) {
  RefreshStats s;
  s.add(30020);
  s.addTimeout();
  EXPECT_EQ(s.timeouts, 1u);
  s.reset();
  EXPECT_EQ(s.timeouts, 0u);
  EXPECT_EQ(s.windowCount(), 0u);
  EXPECT_EQ(s.medianMs(), 0u);
  EXPECT_EQ(s.percentileMs(95), 0u);
  s.add(12);
  EXPECT_EQ(s.medianMs(), 12u);  // nothing from before the reset
  EXPECT_EQ(s.percentileMs(95), 12u);
}

TEST(DisplayStats, SizeIsFixed) {
  // RAM cost of the statistics: one DisplayStats per backend, no heap. Each kind holds a window of
  // 128 x 2 B plus its counters.
  EXPECT_LE(sizeof(DisplayStats), 2 * (RefreshStats::SAMPLE_WINDOW * sizeof(uint16_t) + 48));
  std::printf("sizeof(DisplayStats) = %zu bytes\n", sizeof(DisplayStats));
}
