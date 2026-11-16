#include <gtest/gtest.h>

#include "lib/RsvpCore/DeliveredWpmMeter.h"

using rsvp::DeliveredWpmMeter;

TEST(DeliveredWpmMeter, ZeroBeforeAnyActiveTime) {
  DeliveredWpmMeter meter;
  EXPECT_EQ(meter.deliveredWpmTenths(), 0u);
  EXPECT_EQ(meter.meanUpdateMs(), 0u);
  meter.addWordUpdate(1, 100);
  EXPECT_EQ(meter.deliveredWpmTenths(), 0u);
}

TEST(DeliveredWpmMeter, WordsOverActiveTimeInTenths) {
  DeliveredWpmMeter meter;
  for (int i = 0; i < 240; ++i) meter.addWordUpdate(1, 0);
  meter.addActiveMs(60000);
  EXPECT_EQ(meter.deliveredWpmTenths(), 2400u);

  // 100 words in 60,000 ms: 100.0 WPM. 7 words in 1,000 ms: 420.0. 1 word in 7,000 ms: 8.57 -> 8.6.
  DeliveredWpmMeter a;
  for (int i = 0; i < 100; ++i) a.addWordUpdate(1, 540);
  a.addActiveMs(60000);
  EXPECT_EQ(a.deliveredWpmTenths(), 1000u);
  DeliveredWpmMeter b;
  b.addWordUpdate(3, 10);
  b.addWordUpdate(4, 10);
  b.addActiveMs(1000);
  EXPECT_EQ(b.deliveredWpmTenths(), 4200u);
  DeliveredWpmMeter c;
  c.addWordUpdate(1, 10);
  c.addActiveMs(7000);
  EXPECT_EQ(c.deliveredWpmTenths(), 86u);
}

TEST(DeliveredWpmMeter, ResultsFileFigures) {
  DeliveredWpmMeter meter;
  meter.addWordUpdate(2, 500);
  meter.addWordUpdate(1, 541);
  meter.addWordUpdate(3, 2000);
  meter.addClearingRefresh(1960);
  meter.addClearingRefresh(900);
  meter.addActiveMs(12345);
  const rsvp::MeterStats& s = meter.stats();
  EXPECT_EQ(s.wordsShown, 6u);
  EXPECT_EQ(s.updates, 3u);
  EXPECT_EQ(s.updateMsTotal, 3041u);
  EXPECT_EQ(s.updateMsMax, 2000u);
  EXPECT_EQ(meter.meanUpdateMs(), 1014u);  // 3041 / 3 = 1013.67
  EXPECT_EQ(s.clearingRefreshes, 2u);
  EXPECT_EQ(s.clearMsTotal, 2860u);
  EXPECT_EQ(s.clearMsMax, 1960u);
  EXPECT_EQ(s.activeMs, 12345u);
  meter.reset();
  EXPECT_EQ(meter.stats().wordsShown, 0u);
  EXPECT_EQ(meter.stats().activeMs, 0u);
}

TEST(DeliveredWpmMeter, LongSessionsDoNotOverflow) {
  DeliveredWpmMeter meter;
  // 10 million words over 30 days of active time.
  for (int i = 0; i < 2500000; ++i) meter.addWordUpdate(4, 1);
  meter.addActiveMs(30ull * 24 * 3600 * 1000);
  EXPECT_EQ(meter.deliveredWpmTenths(), 2315u);  // 10,000,000 / 43,200 min = 231.48
}
