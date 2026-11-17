// The device backend's logic (TimedBackend, task D1) through a fake refresh port and a fake clock:
// each call is timed from the start of the frame transfer to the end of the BUSY wait, a refresh
// started elsewhere is waited out before the clock starts, long calls count as timeouts, and the
// statistics and allow rule are those of every backend.

#include <gtest/gtest.h>

#include <cstdio>

#include "FakeDisplayBackend.h"
#include "PanelRecords.h"
#include "TimedBackend.h"

using namespace displayprofile;

namespace {

using Op = FakeRefreshPort::Op;

struct Rig {
  explicit Rig(const PanelId panel = PanelId::DEPG0397) : clock(10000), port(clock), backend(port, clock, panel) {}
  FakeClock clock;
  FakeRefreshPort port;
  TimedBackend backend;
};

}  // namespace

TEST(TimedBackend, UpdateIsTimedFromTransferStartToBusyEnd) {
  Rig rig;
  // S2's DEPG0397 prediction: at least 38 ms of transfer, 380 ms of waveform.
  rig.port.updateTransferMs = 38;
  rig.port.updateBusyMs = 380;
  EXPECT_EQ(rig.backend.update(), 418u);
  EXPECT_EQ(rig.clock.now, 10418u);

  ASSERT_EQ(rig.port.log.size(), 3u);
  EXPECT_EQ(rig.port.log[0].op, Op::FINISH_PENDING);
  EXPECT_EQ(rig.port.log[1].op, Op::TRANSFER_START);
  EXPECT_EQ(rig.port.log[1].kind, RefreshKind::UPDATE);
  EXPECT_EQ(rig.port.log[2].op, Op::BUSY_END);
  EXPECT_EQ(rig.port.log[2].atMs - rig.port.log[1].atMs, 418u);

  const TimedCall& call = rig.backend.lastCall();
  EXPECT_EQ(call.kind, RefreshKind::UPDATE);
  EXPECT_EQ(call.ms, 418u);
  EXPECT_EQ(call.waitedMs, 0u);
  EXPECT_FALSE(call.timedOut);

  const RefreshStats& u = rig.backend.stats().updates;
  EXPECT_EQ(u.count, 1u);
  EXPECT_EQ(u.lastMs, 418u);
  EXPECT_EQ(u.timeouts, 0u);
  EXPECT_EQ(rig.backend.stats().clears.count, 0u);
}

TEST(TimedBackend, ClearIsTimedTheSameWayAndCountedApart) {
  Rig rig;
  rig.port.clearTransferMs = 77;  // HALF sends both planes
  rig.port.clearBusyMs = 960;     // lut_picco_depg_full
  EXPECT_EQ(rig.backend.clear(), 1037u);
  EXPECT_EQ(rig.port.log[1].kind, RefreshKind::CLEAR);
  EXPECT_EQ(rig.backend.lastCall().kind, RefreshKind::CLEAR);
  EXPECT_EQ(rig.backend.stats().clears.count, 1u);
  EXPECT_EQ(rig.backend.stats().clears.lastMs, 1037u);
  EXPECT_EQ(rig.backend.stats().updates.count, 0u);
}

TEST(TimedBackend, ARefreshStartedElsewhereIsWaitedOutBeforeTheClockStarts) {
  Rig rig;
  rig.port.pendingMs = 250;  // the rest of a deferred refresh some other screen started
  rig.port.updateTransferMs = 40;
  rig.port.updateBusyMs = 380;
  EXPECT_EQ(rig.backend.update(), 420u);  // not 670
  EXPECT_EQ(rig.backend.lastCall().waitedMs, 250u);
  EXPECT_EQ(rig.port.log[1].atMs, 10250u);  // the transfer starts after the wait
  EXPECT_EQ(rig.clock.now, 10670u);
  // Nothing pending the next time.
  EXPECT_EQ(rig.backend.update(), 420u);
  EXPECT_EQ(rig.backend.lastCall().waitedMs, 0u);
}

TEST(TimedBackend, EveryCallChecksForAPendingRefreshFirst) {
  Rig rig;
  rig.backend.update();
  rig.backend.clear();
  rig.backend.update();
  ASSERT_EQ(rig.port.log.size(), 9u);
  for (size_t i = 0; i < rig.port.log.size(); i += 3) {
    EXPECT_EQ(rig.port.log[i].op, Op::FINISH_PENDING) << i;
    EXPECT_EQ(rig.port.log[i + 1].op, Op::TRANSFER_START) << i;
    EXPECT_EQ(rig.port.log[i + 2].op, Op::BUSY_END) << i;
  }
}

TEST(TimedBackend, AStuckBusyLineEndsAsATimeout) {
  Rig rig;
  // BUSY never reports the end: the SDK's wait gives up after 30 s (plus its 20 ms edge wait).
  rig.port.updateTransferMs = 40;
  rig.port.updateBusyMs = 30020;
  EXPECT_EQ(rig.backend.update(), 30060u);  // the call returns: playback is not stuck for good
  EXPECT_TRUE(rig.backend.lastCall().timedOut);
  const RefreshStats& u = rig.backend.stats().updates;
  EXPECT_EQ(u.timeouts, 1u);
  EXPECT_EQ(u.count, 1u);  // still recorded: it is how long the call took
  EXPECT_EQ(u.maxMs, 30060u);

  // The next update is normal again and does not count.
  rig.port.updateBusyMs = 380;
  EXPECT_EQ(rig.backend.update(), 420u);
  EXPECT_FALSE(rig.backend.lastCall().timedOut);
  EXPECT_EQ(rig.backend.stats().updates.timeouts, 1u);
  EXPECT_EQ(rig.backend.stats().clears.timeouts, 0u);
}

TEST(TimedBackend, TimeoutLimitsPerKind) {
  EXPECT_EQ(timeoutMs(RefreshKind::UPDATE), 5000u);
  EXPECT_EQ(timeoutMs(RefreshKind::CLEAR), 10000u);
  // A median the timings file accepts can never be a timeout.
  EXPECT_GE(UPDATE_TIMEOUT_MS, MAX_FILE_UPDATE_MS);
  EXPECT_GE(CLEAR_TIMEOUT_MS, MAX_FILE_CLEAR_MS);

  Rig rig;
  rig.port.updateBusyMs = UPDATE_TIMEOUT_MS - 1;
  rig.backend.update();
  EXPECT_FALSE(rig.backend.lastCall().timedOut);
  rig.port.updateBusyMs = UPDATE_TIMEOUT_MS;
  rig.backend.update();
  EXPECT_TRUE(rig.backend.lastCall().timedOut);

  // A clear may take longer than an update before it counts.
  rig.port.clearBusyMs = UPDATE_TIMEOUT_MS + 100;
  rig.backend.clear();
  EXPECT_FALSE(rig.backend.lastCall().timedOut);
  rig.port.clearBusyMs = CLEAR_TIMEOUT_MS;
  rig.backend.clear();
  EXPECT_TRUE(rig.backend.lastCall().timedOut);

  EXPECT_EQ(rig.backend.stats().updates.timeouts, 1u);
  EXPECT_EQ(rig.backend.stats().clears.timeouts, 1u);
  rig.backend.resetStats();
  EXPECT_EQ(rig.backend.stats().updates.timeouts, 0u);
  EXPECT_EQ(rig.backend.stats().clears.timeouts, 0u);
}

TEST(TimedBackend, TimingIsWrapSafe) {
  Rig rig;
  rig.clock.now = 0xFFFFFF00u;  // millis() wraps after about 49.7 days
  rig.port.updateTransferMs = 40;
  rig.port.updateBusyMs = 380;
  EXPECT_EQ(rig.backend.update(), 420u);
  EXPECT_FALSE(rig.backend.lastCall().timedOut);
}

TEST(TimedBackend, BenchmarkSeriesGivesMedianAndP95) {
  // What D3a does: reset, 100 word updates, read the figures.
  Rig rig;
  rig.port.updateTransferMs = 39;
  rig.backend.update();  // before the series: not counted
  rig.backend.resetStats();
  for (uint32_t i = 0; i < 100; ++i) {
    rig.port.updateBusyMs = 380 + (i * 7) % 20;  // 380..399 ms of waveform, five of each
    rig.backend.update();
  }
  const RefreshStats& u = rig.backend.stats().updates;
  EXPECT_EQ(u.count, 100u);
  EXPECT_EQ(u.minMs, 419u);
  EXPECT_EQ(u.maxMs, 438u);
  // Durations are 419 + v, v = 0..19, five of each: rank r holds v = (r - 1) / 5.
  EXPECT_EQ(u.medianMs(), 429u);        // ranks 50 and 51: (428 + 429) / 2 rounded half up
  EXPECT_EQ(u.percentileMs(95), 437u);  // rank 95: v = 18
  std::printf("100 updates: median %u ms, p95 %u ms, max %u ms\n", u.medianMs(), u.percentileMs(95), u.maxMs);
}

TEST(TimedBackend, OnlyStockOnARealPanelUntilD2) {
  for (const PanelId panel :
       {PanelId::UNKNOWN, PanelId::YRD0426, PanelId::DEPG0397, PanelId::OPM040B3, PanelId::SE0400NQW47}) {
    Rig rig(panel);
    EXPECT_EQ(rig.backend.panel(), panel);
    EXPECT_TRUE(rig.backend.allows(ProfileId::STOCK)) << panelName(panel);
    EXPECT_FALSE(rig.backend.select(ProfileId::TRIAL_A)) << panelName(panel);
    EXPECT_FALSE(rig.backend.select(ProfileId::TRIAL_D)) << panelName(panel);
    EXPECT_EQ(rig.backend.profile(), ProfileId::STOCK);
    // The clearing limits RSVP passes to the scheduler come from the profile table.
    EXPECT_EQ(rig.backend.info().clearing.softLimit, PROFILES[0].clearing.softLimit);
    EXPECT_EQ(rig.backend.info().clearing.hardLimit, PROFILES[0].clearing.hardLimit);
  }
}
