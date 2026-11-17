// The modelled backend (the simulator's logic) through the fake: each call lasts the profile's
// modelled time, the remainder after the real refresh is slept and never more, profile selection
// and its allow hook, statistics.

#include <gtest/gtest.h>

#include <cstdio>

#include "FakeDisplayBackend.h"

using namespace displayprofile;

namespace {

TimingModel modelOf(const char* updateMs, const char* clearMs) {
  TimingOverrides env;
  EXPECT_EQ(parseOverrides(updateMs, clearMs, env), OverrideError::NONE);
  TimingModel model;
  EXPECT_TRUE(model.resolve(TimingFile{}, env));
  return model;
}

}  // namespace

TEST(ModelledBackend, HundredUpdatesAt540TakeExactly54000Ms) {
  FakeClock clock(1000);
  FakeDisplayBackend display(clock);  // built-in defaults: stock is 540 ms
  ASSERT_EQ(display.model().timing(ProfileId::STOCK).updateMs, 540u);
  for (int i = 0; i < 100; ++i) EXPECT_EQ(display.update(), 540u);
  EXPECT_EQ(clock.now - 1000, 54000u);
  std::printf("100 word updates at 540 ms: %u ms of fake time\n", clock.now - 1000);
  const RefreshStats& s = display.stats().updates;
  EXPECT_EQ(s.count, 100u);
  EXPECT_EQ(s.totalMs, 54000u);
  EXPECT_EQ(s.minMs, 540u);
  EXPECT_EQ(s.maxMs, 540u);
  EXPECT_EQ(s.meanMs(), 540u);
  EXPECT_EQ(s.lastMs, 540u);
}

TEST(ModelledBackend, RealRefreshPlusRemainderEqualsTheModel) {
  FakeClock clock;
  FakeDisplayBackend display(clock);
  display.setRealMs(37, 120);
  EXPECT_EQ(display.update(), 540u);
  EXPECT_EQ(display.lastCall().realMs, 37u);
  EXPECT_EQ(display.lastCall().sleptMs, 503u);
  EXPECT_EQ(display.lastCall().modelMs, 540u);
  EXPECT_EQ(display.clear(), 1960u);
  EXPECT_EQ(display.lastCall().kind, RefreshKind::CLEAR);
  EXPECT_EQ(display.lastCall().realMs, 120u);
  EXPECT_EQ(display.lastCall().sleptMs, 1840u);
  EXPECT_EQ(clock.now, 2500u);
  EXPECT_EQ(display.sleptMs(), 503u + 1840u);
}

TEST(ModelledBackend, NeverSleepsWhenTheRealCallWasSlowerThanTheModel) {
  FakeClock clock;
  FakeDisplayBackend display(clock, modelOf("200", "400"));
  display.setRealMs(250, 400);  // slower than the update model; exactly the clear model
  EXPECT_EQ(display.update(), 250u);
  EXPECT_EQ(display.lastCall().sleptMs, 0u);
  EXPECT_EQ(display.clear(), 400u);
  EXPECT_EQ(display.lastCall().sleptMs, 0u);
  display.setRealMs(5000, 0);  // far slower: still no sleep, and the duration is the real one
  EXPECT_EQ(display.update(), 5000u);
  EXPECT_EQ(display.sleeps(), 0u);
  EXPECT_EQ(display.sleptMs(), 0u);
  EXPECT_EQ(clock.now, 5650u);
}

TEST(ModelledBackend, ZeroModelAddsNoDelay) {
  FakeClock clock;
  FakeDisplayBackend display(clock, modelOf("0", "0"));
  display.setRealMs(3, 4);
  EXPECT_EQ(display.update(), 3u);
  EXPECT_EQ(display.clear(), 4u);
  display.setRealMs(0, 0);
  EXPECT_EQ(display.update(), 0u);
  EXPECT_EQ(display.sleeps(), 0u);
  EXPECT_EQ(clock.now, 7u);
}

TEST(ModelledBackend, UsesTheSelectedProfilesTimes) {
  FakeClock clock;
  FakeDisplayBackend display(clock);
  ASSERT_TRUE(display.select(ProfileId::TRIAL_C));
  EXPECT_EQ(display.profile(), ProfileId::TRIAL_C);
  EXPECT_STREQ(display.info().key, "trial-c");
  EXPECT_EQ(display.update(), 198u);
  EXPECT_EQ(display.clear(), 1960u);
  ASSERT_TRUE(display.select(ProfileId::STOCK));
  EXPECT_EQ(display.update(), 540u);
}

TEST(ModelledBackend, WorksAcrossClockWrap) {
  FakeClock clock(0xFFFFFF00u);
  FakeDisplayBackend display(clock);
  display.setRealMs(10, 0);
  for (int i = 0; i < 5; ++i) EXPECT_EQ(display.update(), 540u);
  EXPECT_EQ(clock.now, 0xFFFFFF00u + 2700u);  // wrapped
  EXPECT_EQ(display.stats().updates.totalMs, 2700u);
}

TEST(ModelledBackend, AllowHookGatesSelection) {
  FakeClock clock;
  // A real panel: until D2, only stock is allowed by the default rule.
  FakeDisplayBackend device(clock, TimingModel(), PanelId::DEPG0397);
  EXPECT_EQ(device.panel(), PanelId::DEPG0397);
  EXPECT_TRUE(device.allows(ProfileId::STOCK));
  EXPECT_FALSE(device.allows(ProfileId::TRIAL_A));
  EXPECT_FALSE(device.select(ProfileId::TRIAL_A));
  EXPECT_EQ(device.profile(), ProfileId::STOCK);
  EXPECT_FALSE(device.select(static_cast<ProfileId>(9)));
  EXPECT_TRUE(device.select(ProfileId::STOCK));
  // The simulator allows every profile.
  FakeDisplayBackend simulator(clock);
  for (const ProfileInfo& info : PROFILES) EXPECT_TRUE(simulator.select(info.id)) << info.key;
}

TEST(ModelledBackend, StatisticsAreSeparateAndResettable) {
  FakeClock clock;
  FakeDisplayBackend display(clock, modelOf("200", "400"));
  display.setRealMs(250, 0);
  display.update();  // 250 (slower than the model)
  display.setRealMs(0, 0);
  display.update();  // 200
  display.clear();   // 400
  const DisplayStats& s = display.stats();
  EXPECT_EQ(s.updates.count, 2u);
  EXPECT_EQ(s.updates.minMs, 200u);
  EXPECT_EQ(s.updates.maxMs, 250u);
  EXPECT_EQ(s.updates.meanMs(), 225u);
  EXPECT_EQ(s.updates.lastMs, 200u);
  EXPECT_EQ(s.clears.count, 1u);
  EXPECT_EQ(s.clears.lastMs, 400u);
  display.resetStats();
  EXPECT_EQ(display.stats().updates.count, 0u);
  EXPECT_EQ(display.stats().clears.count, 0u);
  display.update();
  EXPECT_EQ(display.stats().updates.count, 1u);
  EXPECT_EQ(display.stats().updates.minMs, 200u);
}

TEST(FakeDisplayBackend, EventLogRecordsTheCallSequence) {
  FakeClock clock(100);
  FakeDisplayBackend display(clock, modelOf("200", "400"));
  ASSERT_TRUE(display.select(ProfileId::TRIAL_B));
  display.clear();
  display.update();
  ASSERT_EQ(display.events.size(), 3u);
  EXPECT_EQ(display.events[0].kind, FakeEvent::SELECT);
  EXPECT_EQ(display.events[0].profile, ProfileId::TRIAL_B);
  EXPECT_EQ(display.events[1].kind, FakeEvent::CLEAR);
  EXPECT_EQ(display.events[1].startMs, 100u);
  EXPECT_EQ(display.events[1].endMs, 500u);
  EXPECT_EQ(display.events[2].kind, FakeEvent::UPDATE);
  EXPECT_EQ(display.events[2].profile, ProfileId::TRIAL_B);
  EXPECT_EQ(display.events[2].startMs, 500u);
  EXPECT_EQ(display.events[2].endMs, 700u);
  EXPECT_EQ(display.events[2].call.sleptMs, 200u);
}
