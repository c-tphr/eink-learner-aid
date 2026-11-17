// The trial profiles' exposure limits (task D2; plan section 6, "Clearing" and "Safety limits";
// TrialExposure.h), enforced by the backend: trial waveforms only for word updates, a trial run
// starts from and ends with a clearing refresh, the hard limit cannot be broken, and on the device
// side the table is loaded for exactly one refresh and released after it, timeouts included.

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "FakeDisplayBackend.h"
#include "ProfileDisplaySink.h"
#include "TimedBackend.h"
#include "TrialExposure.h"
#include "TrialWaveforms.h"

using namespace displayprofile;

namespace {

using Op = FakeRefreshPort::Op;

// A modelled backend (the simulator's logic) on `panel`, every trial unlocked, `id` selected.
struct Modelled {
  explicit Modelled(const ProfileId id, const PanelId panel = PanelId::SIMULATED)
      : backend(clock, TimingModel(), panel) {
    backend.setUnlockedTrials(ALL_TRIALS);
    EXPECT_TRUE(backend.select(id));
    backend.events.clear();
  }
  const FakeEvent& last() const { return backend.events.back(); }
  FakeClock clock;
  FakeDisplayBackend backend;
};

// The device backend's logic on `panel` through a fake port.
struct Device {
  explicit Device(const PanelId panel = PanelId::DEPG0397) : clock(1000), port(clock), backend(port, clock, panel) {
    backend.setUnlockedTrials(ALL_TRIALS);
    port.updateTransferMs = 38;
    port.updateBusyMs = 160;
    port.clearTransferMs = 77;
    port.clearBusyMs = 960;
  }
  FakeClock clock;
  FakeRefreshPort port;
  TimedBackend backend;
};

// Checks the device-side safety invariants over a whole port log: a table is loaded only right
// before a fast refresh and released right after it; no clear and no stock refresh ever starts
// with a table loaded; nothing is left loaded at the end.
void expectTablesNeverLeak(const FakeRefreshPort& port) {
  const std::vector<FakeRefreshPort::Entry>& log = port.log;
  for (size_t i = 0; i < log.size(); ++i) {
    const FakeRefreshPort::Entry& e = log[i];
    if (e.op == Op::TRANSFER_START && e.kind == RefreshKind::CLEAR) EXPECT_EQ(e.table, nullptr) << "entry " << i;
    if (e.op == Op::TRANSFER_START && e.ui) EXPECT_EQ(e.table, nullptr) << "entry " << i;
    if (e.op == Op::LOAD_TABLE) {
      ASSERT_LT(i + 3, log.size());
      EXPECT_EQ(log[i + 1].op, Op::TRANSFER_START);
      EXPECT_EQ(log[i + 1].kind, RefreshKind::UPDATE);
      EXPECT_EQ(log[i + 1].table, e.table);
      EXPECT_EQ(log[i + 2].op, Op::BUSY_END);
      EXPECT_EQ(log[i + 3].op, Op::RELEASE_TABLE);
    }
  }
  EXPECT_EQ(port.loaded, nullptr);
}

}  // namespace

// The rules, on the guard alone.

TEST(TrialExposure, ScreenUpdatesNeverRunATrial) {
  TrialExposure guard;
  guard.record(guard.decide(RefreshRequest::CLEAR, ProfileId::TRIAL_C));
  const RefreshDecision d = guard.decide(RefreshRequest::SCREEN, ProfileId::TRIAL_C);
  EXPECT_EQ(d.action, RefreshAction::STOCK_UPDATE);
  EXPECT_EQ(d.waveform, ProfileId::STOCK);
}

TEST(TrialExposure, ARunStartsFromAClear) {
  TrialExposure guard;
  // After start-up (or a wake, which restarts the firmware): the first trial word is a clear.
  RefreshDecision d = guard.decide(RefreshRequest::WORD, ProfileId::TRIAL_A);
  EXPECT_EQ(d.action, RefreshAction::CLEAR);
  EXPECT_EQ(d.promotion, Promotion::NOT_CLEARED);
  guard.record(d);
  EXPECT_EQ(guard.decide(RefreshRequest::WORD, ProfileId::TRIAL_A).action, RefreshAction::TRIAL_UPDATE);
  // After a stock screen update, again.
  guard.record(guard.decide(RefreshRequest::SCREEN, ProfileId::TRIAL_A));
  EXPECT_EQ(guard.decide(RefreshRequest::WORD, ProfileId::TRIAL_A).promotion, Promotion::NOT_CLEARED);
  EXPECT_EQ(guard.promotions(), 1u);
}

TEST(TrialExposure, HardLimitIsEnforcedForEveryTrial) {
  for (const ProfileId id : {ProfileId::TRIAL_A, ProfileId::TRIAL_B, ProfileId::TRIAL_C, ProfileId::TRIAL_D}) {
    TrialExposure guard;
    guard.record(guard.decide(RefreshRequest::CLEAR, id));
    const uint16_t hard = profileInfo(id).clearing.hardLimit;
    for (uint16_t i = 1; i <= hard; ++i) {
      const RefreshDecision d = guard.decide(RefreshRequest::WORD, id);
      ASSERT_EQ(d.action, RefreshAction::TRIAL_UPDATE) << profileInfo(id).key << " update " << i;
      guard.record(d);
      EXPECT_EQ(guard.trialRun(), i);
    }
    const RefreshDecision over = guard.decide(RefreshRequest::WORD, id);
    EXPECT_EQ(over.action, RefreshAction::CLEAR) << profileInfo(id).key;
    EXPECT_EQ(over.promotion, Promotion::HARD_LIMIT);
    guard.record(over);
    EXPECT_EQ(guard.trialRun(), 0);
    EXPECT_EQ(guard.decide(RefreshRequest::WORD, id).action, RefreshAction::TRIAL_UPDATE);
  }
  // S2 section 8 and plan section 6: the trials' hard limits (and soft limits for the scheduler).
  EXPECT_EQ(profileInfo(ProfileId::TRIAL_A).clearing.hardLimit, 120);
  EXPECT_EQ(profileInfo(ProfileId::TRIAL_B).clearing.hardLimit, 90);
  EXPECT_EQ(profileInfo(ProfileId::TRIAL_C).clearing.hardLimit, 60);
  EXPECT_EQ(profileInfo(ProfileId::TRIAL_D).clearing.hardLimit, 60);
  EXPECT_EQ(profileInfo(ProfileId::TRIAL_A).clearing.softLimit, 60);
  EXPECT_EQ(profileInfo(ProfileId::TRIAL_B).clearing.softLimit, 45);
  EXPECT_EQ(profileInfo(ProfileId::TRIAL_C).clearing.softLimit, 30);
  EXPECT_EQ(profileInfo(ProfileId::TRIAL_D).clearing.softLimit, 30);
}

TEST(TrialExposure, ARunAlwaysEndsWithAClear) {
  for (const RefreshRequest next : {RefreshRequest::SCREEN, RefreshRequest::WORD}) {
    TrialExposure guard;
    guard.record(guard.decide(RefreshRequest::CLEAR, ProfileId::TRIAL_B));
    guard.record(guard.decide(RefreshRequest::WORD, ProfileId::TRIAL_B));
    EXPECT_TRUE(guard.residuePending());
    // A screen update (the pause screen), or a word with Stock now in force.
    const RefreshDecision d = guard.decide(next, ProfileId::STOCK);
    EXPECT_EQ(d.action, RefreshAction::CLEAR);
    EXPECT_EQ(d.promotion, Promotion::RESIDUE);
    guard.record(d);
    EXPECT_FALSE(guard.residuePending());
  }
  // A word with another trial.
  TrialExposure guard;
  guard.record(guard.decide(RefreshRequest::CLEAR, ProfileId::TRIAL_B));
  guard.record(guard.decide(RefreshRequest::WORD, ProfileId::TRIAL_B));
  EXPECT_EQ(guard.decide(RefreshRequest::WORD, ProfileId::TRIAL_C).promotion, Promotion::PROFILE_CHANGED);
}

TEST(TrialExposure, StockIsUnchanged) {
  // Stock words never count, never promote, need no clear first.
  TrialExposure guard;
  for (int i = 0; i < 500; ++i) {
    const RefreshDecision d =
        guard.decide(i % 3 == 0 ? RefreshRequest::SCREEN : RefreshRequest::WORD, ProfileId::STOCK);
    EXPECT_EQ(d.action, RefreshAction::STOCK_UPDATE);
    EXPECT_EQ(d.promotion, Promotion::NONE);
    guard.record(d);
  }
  EXPECT_EQ(guard.promotions(), 0u);
}

// The same rules through the backends (both share DisplayBackend::update/clear).

TEST(TrialExposureBackend, UnmarkedUpdatesNeverUseATrial) {
  Modelled m(ProfileId::TRIAL_C);
  m.backend.clear();
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(m.backend.update(), 540u);  // stock's modelled time, not trial C's 198
    EXPECT_EQ(m.last().kind, FakeEvent::UPDATE);
    EXPECT_EQ(m.last().profile, ProfileId::STOCK);
  }
  EXPECT_EQ(m.backend.stats().updates.count, 5u);
  EXPECT_EQ(m.backend.exposure().trialRun(), 0);
}

TEST(TrialExposureBackend, ClearOnPause) {
  Modelled m(ProfileId::TRIAL_C);
  m.backend.clear();
  for (int i = 0; i < 10; ++i) EXPECT_EQ(m.backend.update(UpdateKind::WORD), 198u);
  EXPECT_TRUE(m.backend.exposure().residuePending());
  // Playback pauses; the pause screen is drawn with a plain update(): it runs as a clear.
  EXPECT_EQ(m.backend.update(), 1960u);
  EXPECT_EQ(m.last().kind, FakeEvent::CLEAR);
  EXPECT_EQ(m.last().decision.request, RefreshRequest::SCREEN);
  EXPECT_EQ(m.last().decision.promotion, Promotion::RESIDUE);
  EXPECT_EQ(m.backend.stats().clears.count, 2u);
  EXPECT_EQ(m.backend.stats().updates.count, 10u);  // the promoted call is a clear in the statistics
  // Highlight moves after it are stock updates.
  EXPECT_EQ(m.backend.update(), 540u);
  EXPECT_EQ(m.last().kind, FakeEvent::UPDATE);
}

TEST(TrialExposureBackend, HardLimitHoldsWhenTheCallerIgnoresIt) {
  Modelled m(ProfileId::TRIAL_C);
  m.backend.clear();
  m.backend.events.clear();
  for (int i = 0; i < 200; ++i) m.backend.update(UpdateKind::WORD);
  uint32_t run = 0;
  uint32_t maxRun = 0;
  uint32_t promoted = 0;
  for (const FakeEvent& e : m.backend.events) {
    if (e.kind == FakeEvent::UPDATE && e.profile == ProfileId::TRIAL_C) maxRun = std::max(maxRun, ++run);
    if (e.kind == FakeEvent::CLEAR) {
      run = 0;
      EXPECT_EQ(e.decision.promotion, Promotion::HARD_LIMIT);
      ++promoted;
    }
  }
  EXPECT_EQ(maxRun, 60u);
  EXPECT_EQ(promoted, 3u);  // words 61, 122 and 183 ran as clears
  EXPECT_EQ(m.backend.exposure().promotions(), 3u);
  EXPECT_EQ(m.backend.stats().updates.count, 197u);
  EXPECT_EQ(m.backend.stats().clears.count, 4u);
}

TEST(TrialExposureBackend, SimulatorPanelRunsTheSameLogic) {
  // SIMULATED: unlocked trials run (modelled time), refused while inverted, same limits.
  Modelled m(ProfileId::TRIAL_B);
  EXPECT_EQ(m.backend.update(UpdateKind::WORD), 1960u);  // not cleared yet: a clear
  EXPECT_EQ(m.last().decision.promotion, Promotion::NOT_CLEARED);
  EXPECT_EQ(m.backend.update(UpdateKind::WORD), 258u);
  m.backend.setInverted(true);
  EXPECT_FALSE(m.backend.allows(ProfileId::TRIAL_B));
  EXPECT_EQ(m.backend.update(UpdateKind::WORD), 1960u);  // residue: a clear
  EXPECT_EQ(m.backend.update(UpdateKind::WORD), 540u);   // then stock
}

// The device side: what reaches the port.

TEST(TrialExposureDevice, ATrialUpdateLoadsTheTableForOneRefreshAndReleasesIt) {
  Device d;
  ASSERT_TRUE(d.backend.select(ProfileId::TRIAL_C));
  d.backend.clear();
  d.port.log.clear();
  EXPECT_EQ(d.backend.update(UpdateKind::WORD), 198u);
  ASSERT_EQ(d.port.log.size(), 5u);
  EXPECT_EQ(d.port.log[0].op, Op::FINISH_PENDING);
  EXPECT_EQ(d.port.log[1].op, Op::LOAD_TABLE);
  EXPECT_EQ(d.port.log[1].table, LUT_TRIAL_C_DEPG0397);
  EXPECT_EQ(d.port.log[2].op, Op::TRANSFER_START);
  EXPECT_EQ(d.port.log[2].table, LUT_TRIAL_C_DEPG0397);
  EXPECT_FALSE(d.port.log[2].ui);
  EXPECT_EQ(d.port.log[3].op, Op::BUSY_END);
  EXPECT_EQ(d.port.log[4].op, Op::RELEASE_TABLE);
  EXPECT_EQ(d.port.loaded, nullptr);
  EXPECT_EQ(d.backend.lastCall().waveform.path, WordPath::TABLE);
  EXPECT_EQ(d.backend.lastCall().waveform.predictedMs, 160);
}

TEST(TrialExposureDevice, ClearsReleaseFirstAndNeverRunATable) {
  Device d;
  d.backend.clear();
  ASSERT_EQ(d.port.log.size(), 4u);
  EXPECT_EQ(d.port.log[1].op, Op::RELEASE_TABLE);
  EXPECT_EQ(d.port.log[2].op, Op::TRANSFER_START);
  EXPECT_EQ(d.port.log[2].kind, RefreshKind::CLEAR);
  EXPECT_EQ(d.port.log[2].table, nullptr);
  // Even if something had left a table loaded.
  d.port.loaded = LUT_TRIAL_A_DEPG0397;
  d.backend.clear();
  expectTablesNeverLeak(d.port);
}

TEST(TrialExposureDevice, ReleasedAfterATimeout) {
  Device d;
  ASSERT_TRUE(d.backend.select(ProfileId::TRIAL_A));
  d.backend.clear();
  d.port.updateBusyMs = 30000;  // BUSY never ended; the SDK gave up after 30 s
  d.backend.update(UpdateKind::WORD);
  EXPECT_TRUE(d.backend.lastCall().timedOut);
  EXPECT_EQ(d.backend.stats().updates.timeouts, 1u);
  EXPECT_EQ(d.port.log.back().op, Op::RELEASE_TABLE);
  EXPECT_EQ(d.port.loaded, nullptr);
  // The next refresh, whatever it is, starts with no table.
  d.port.updateBusyMs = 160;
  d.backend.update();  // the pause screen: runs as a clear
  EXPECT_EQ(d.backend.lastCall().kind, RefreshKind::CLEAR);
  expectTablesNeverLeak(d.port);
}

TEST(TrialExposureDevice, EveryPanelGetsItsOwnPath) {
  struct Case {
    PanelId panel;
    ProfileId id;
    const uint8_t* table;
    bool ui;
  };
  const Case cases[] = {
      {PanelId::DEPG0397, ProfileId::TRIAL_A, LUT_TRIAL_A_DEPG0397, false},
      {PanelId::DEPG0397, ProfileId::TRIAL_D, LUT_TRIAL_D_DEPG0397, false},
      {PanelId::YRD0426, ProfileId::TRIAL_B, LUT_TRIAL_B_YRD0426_OPM040B3, false},
      {PanelId::OPM040B3, ProfileId::TRIAL_C, LUT_TRIAL_C_YRD0426_OPM040B3, false},
      {PanelId::YRD0426, ProfileId::TRIAL_D, LUT_TRIAL_D_YRD0426_OPM040B3, false},
      {PanelId::SE0400NQW47, ProfileId::TRIAL_D, nullptr, true},
  };
  for (const Case& c : cases) {
    Device d(c.panel);
    ASSERT_TRUE(d.backend.select(c.id)) << panelName(c.panel);
    d.backend.clear();
    d.port.log.clear();
    d.backend.update(UpdateKind::WORD);
    const auto starts = d.port.only(Op::TRANSFER_START);
    ASSERT_EQ(starts.size(), 1u);
    EXPECT_EQ(starts[0].table, c.table) << panelName(c.panel) << " " << profileInfo(c.id).key;
    EXPECT_EQ(starts[0].ui, c.ui) << panelName(c.panel) << " " << profileInfo(c.id).key;
    EXPECT_EQ(d.port.only(Op::LOAD_TABLE).size(), c.table != nullptr ? 1u : 0u);
    expectTablesNeverLeak(d.port);
  }
}

TEST(TrialExposureDevice, StockPathIsUnchangedApartFromTheReleaseBeforeAClear) {
  Device d;
  d.backend.update();
  d.backend.update(UpdateKind::WORD);
  // Two stock updates: no table call at all.
  EXPECT_TRUE(d.port.only(Op::LOAD_TABLE).empty());
  EXPECT_TRUE(d.port.only(Op::RELEASE_TABLE).empty());
  for (const auto& e : d.port.only(Op::TRANSFER_START)) {
    EXPECT_EQ(e.table, nullptr);
    EXPECT_FALSE(e.ui);
  }
}

TEST(TrialExposureDevice, LongMixedSessionNeverLeaksATable) {
  Device d;
  ASSERT_TRUE(d.backend.select(ProfileId::TRIAL_C));
  d.backend.clear();
  // Words, pauses, profile switches, inversion, timeouts, in a fixed pseudo-random order.
  uint32_t x = 12345;
  uint32_t trialLoads = 0;
  for (int i = 0; i < 2000; ++i) {
    x = x * 1103515245u + 12345u;
    const bool invertedBefore = d.port.invertedScreen;
    const size_t logBefore = d.port.log.size();
    const uint32_t r = (x >> 16) % 100;
    if (r < 70) {
      d.backend.update(UpdateKind::WORD);
    } else if (r < 80) {
      d.backend.update();
    } else if (r < 85) {
      d.backend.clear();
    } else if (r < 90) {
      d.backend.select(static_cast<ProfileId>((x >> 8) % PROFILE_COUNT));
    } else if (r < 95) {
      d.port.invertedScreen = !d.port.invertedScreen;
    } else {
      d.port.updateBusyMs = d.port.updateBusyMs == 160 ? 6000 : 160;
    }
    EXPECT_LE(d.backend.exposure().trialRun(), 120);  // the largest hard limit
    ASSERT_EQ(d.port.loaded, nullptr) << "step " << i;
    for (size_t k = logBefore; k < d.port.log.size(); ++k) {
      if (d.port.log[k].op != Op::LOAD_TABLE) continue;
      ++trialLoads;
      EXPECT_FALSE(invertedBefore && d.port.invertedScreen) << "a table loaded while inverted, step " << i;
    }
  }
  expectTablesNeverLeak(d.port);
  EXPECT_GT(trialLoads, 100u);  // the sequence did exercise the trial path
}

TEST(TrialExposureDevice, AFreshBackendAfterWakeStartsWithAClear) {
  // Deep sleep is a chip reset (src/main.cpp, deepSleepInProgress): a new backend, nothing carried over.
  Device d;
  ASSERT_TRUE(d.backend.select(ProfileId::TRIAL_B));
  d.backend.update(UpdateKind::WORD);
  EXPECT_EQ(d.backend.lastCall().kind, RefreshKind::CLEAR);
  EXPECT_EQ(d.backend.lastDecision().promotion, Promotion::NOT_CLEARED);
  EXPECT_TRUE(d.port.only(Op::LOAD_TABLE).empty());
}
