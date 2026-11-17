// The trial profiles' allow rule and unlock value (task D2; plan section 6, "Safety limits"):
// the full truth table of profileAllowed(), the same rule as every backend applies it, fallback to
// Stock, and the encode/decode of the one-byte setting.

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "FakeDisplayBackend.h"
#include "PanelRecords.h"
#include "TimedBackend.h"
#include "TrialUnlock.h"

using namespace displayprofile;

namespace {

constexpr PanelId PANELS[] = {PanelId::UNKNOWN,  PanelId::YRD0426,     PanelId::DEPG0397,
                              PanelId::OPM040B3, PanelId::SE0400NQW47, PanelId::SIMULATED};
constexpr ProfileId TRIALS[] = {ProfileId::TRIAL_A, ProfileId::TRIAL_B, ProfileId::TRIAL_C, ProfileId::TRIAL_D};

// The rule written out independently of the implementation: the panels each profile may run on
// when it is unlocked and the screen is not inverted (plan section 6; S2 sections 6, 7 and 9).
bool expectedPanel(const ProfileId id, const PanelId panel) {
  switch (id) {
    case ProfileId::STOCK:
      return true;
    case ProfileId::TRIAL_A:
    case ProfileId::TRIAL_B:
    case ProfileId::TRIAL_C:
      return panel == PanelId::YRD0426 || panel == PanelId::DEPG0397 || panel == PanelId::OPM040B3 ||
             panel == PanelId::SIMULATED;
    case ProfileId::TRIAL_D:
      return panel == PanelId::YRD0426 || panel == PanelId::DEPG0397 || panel == PanelId::OPM040B3 ||
             panel == PanelId::SE0400NQW47 || panel == PanelId::SIMULATED;
  }
  return false;
}

bool expected(const ProfileId id, const PanelId panel, const bool inverted, const bool unlocked) {
  if (id == ProfileId::STOCK) return true;
  return unlocked && !inverted && expectedPanel(id, panel);
}

}  // namespace

// 5 profiles x 6 panels x inverted x unlocked = 120 cases, printed as the grid in the pull request.
TEST(AllowRule, FullTruthTable) {
  int cases = 0;
  std::printf("profile  inverted unlocked | unknown YRD0426 DEPG0397 OPM040B3 SE0400NQW47 simulator\n");
  for (const ProfileInfo& info : PROFILES) {
    for (const bool inverted : {false, true}) {
      for (const bool unlocked : {false, true}) {
        std::string row;
        for (const PanelId panel : PANELS) {
          const bool got = profileAllowed(info.id, panel, inverted, unlocked);
          EXPECT_EQ(got, expected(info.id, panel, inverted, unlocked))
              << info.key << " " << panelName(panel) << " inverted=" << inverted << " unlocked=" << unlocked;
          row += got ? " yes" : "  - ";
          ++cases;
        }
        std::printf("%-8s %-8s %-8s |%s\n", info.key, inverted ? "yes" : "no", unlocked ? "yes" : "no", row.c_str());
      }
    }
  }
  EXPECT_EQ(cases, 120);
}

TEST(AllowRule, Ssd2677PanelDisablesTrialsAToC) {
  // SE0400NQW47 is the only panel on the "SSD2677" driver: A to C never, D only as its stock UI path.
  EXPECT_EQ(panelRecord(PanelId::SE0400NQW47).controller, Controller::SSD2677);
  for (const ProfileId trial : {ProfileId::TRIAL_A, ProfileId::TRIAL_B, ProfileId::TRIAL_C}) {
    EXPECT_FALSE(profileAllowed(trial, PanelId::SE0400NQW47, false, true)) << profileInfo(trial).key;
  }
  EXPECT_TRUE(profileAllowed(ProfileId::TRIAL_D, PanelId::SE0400NQW47, false, true));
  // The same through the device backend: every trial unlocked, A to C still refused.
  FakeClock clock;
  FakeRefreshPort port(clock);
  TimedBackend backend(port, clock, resolvePanel(4, "SSD2677"));
  ASSERT_EQ(backend.panel(), PanelId::SE0400NQW47);
  backend.setUnlockedTrials(ALL_TRIALS);
  for (const ProfileId trial : {ProfileId::TRIAL_A, ProfileId::TRIAL_B, ProfileId::TRIAL_C}) {
    EXPECT_FALSE(backend.select(trial)) << profileInfo(trial).key;
    EXPECT_EQ(backend.profile(), ProfileId::STOCK);
  }
  EXPECT_TRUE(backend.select(ProfileId::TRIAL_D));
  // A panel the probe says is SSD1677 but whose driver is SSD2677 resolves to UNKNOWN: no trial.
  TimedBackend mismatched(port, clock, resolvePanel(2, "SSD2677"));
  mismatched.setUnlockedTrials(ALL_TRIALS);
  for (const ProfileId trial : TRIALS) EXPECT_FALSE(mismatched.select(trial)) << profileInfo(trial).key;
}

TEST(AllowRule, UnknownIdsAndOutOfRangePanelsAreRefused) {
  EXPECT_FALSE(profileAllowed(static_cast<ProfileId>(5), PanelId::DEPG0397, false, true));
  EXPECT_FALSE(profileAllowed(static_cast<ProfileId>(200), PanelId::SIMULATED, false, true));
  EXPECT_FALSE(profileAllowed(ProfileId::TRIAL_A, static_cast<PanelId>(7), false, true));
  EXPECT_TRUE(profileAllowed(ProfileId::STOCK, static_cast<PanelId>(7), true, false));
}

// Every backend applies the same rule, with its own panel, the screen's inversion and the unlocks.
TEST(AllowRule, BackendsApplyTheRuleWithTheirInputs) {
  for (const PanelId panel : PANELS) {
    for (const bool inverted : {false, true}) {
      for (const TrialMask mask : {NO_TRIALS, ALL_TRIALS}) {
        FakeClock clock;
        FakeDisplayBackend modelled(clock, TimingModel(), panel);
        modelled.setInverted(inverted);
        modelled.setUnlockedTrials(mask);
        FakeRefreshPort port(clock);
        port.invertedScreen = inverted;
        TimedBackend device(port, clock, panel);
        device.setUnlockedTrials(mask);
        for (const ProfileInfo& info : PROFILES) {
          const bool want = expected(info.id, panel, inverted, mask != NO_TRIALS);
          EXPECT_EQ(modelled.allows(info.id), want) << info.key << " " << panelName(panel);
          EXPECT_EQ(device.allows(info.id), want) << info.key << " " << panelName(panel);
        }
      }
    }
  }
}

TEST(AllowRule, OnlyTheUnlockedTrialIsAllowed) {
  FakeClock clock;
  FakeDisplayBackend backend(clock, TimingModel(), PanelId::DEPG0397);
  backend.setUnlockedTrials(trialBit(ProfileId::TRIAL_B));
  for (const ProfileId trial : TRIALS) EXPECT_EQ(backend.allows(trial), trial == ProfileId::TRIAL_B);
}

TEST(Selection, DefaultIsStock) {
  for (const PanelId panel : PANELS) {
    FakeClock clock;
    FakeRefreshPort port(clock);
    TimedBackend device(port, clock, panel);
    FakeDisplayBackend modelled(clock, TimingModel(), panel);
    for (DisplayBackend* backend : {static_cast<DisplayBackend*>(&device), static_cast<DisplayBackend*>(&modelled)}) {
      EXPECT_EQ(backend->profile(), ProfileId::STOCK);
      EXPECT_EQ(backend->unlockedTrials(), NO_TRIALS);
      // Nothing unlocked: only Stock exists, on every panel, the simulator included.
      for (const ProfileId trial : TRIALS) {
        EXPECT_FALSE(backend->allows(trial)) << panelName(panel);
        EXPECT_FALSE(backend->select(trial)) << panelName(panel);
        EXPECT_EQ(backend->profile(), ProfileId::STOCK);
      }
    }
    // Nothing stored resolves to Stock.
    EXPECT_EQ(resolveProfile(decodeTrialSetting(TRIAL_SETTING_DEFAULT), panel, false), ProfileId::STOCK);
    EXPECT_EQ(resolveProfile(TrialSetting{}, panel, false), ProfileId::STOCK);
  }
}

TEST(Selection, FallsBackToStockNeverFailsOpen) {
  FakeClock clock;
  FakeDisplayBackend backend(clock, TimingModel(), PanelId::DEPG0397);
  backend.setUnlockedTrials(ALL_TRIALS);
  ASSERT_TRUE(backend.select(ProfileId::TRIAL_C));
  // A refused selection does not keep the previous trial: it falls back to Stock.
  EXPECT_FALSE(backend.select(static_cast<ProfileId>(9)));
  EXPECT_EQ(backend.profile(), ProfileId::STOCK);
  ASSERT_TRUE(backend.select(ProfileId::TRIAL_C));
  backend.setInverted(true);
  EXPECT_FALSE(backend.select(ProfileId::TRIAL_A));
  EXPECT_EQ(backend.profile(), ProfileId::STOCK);
  // Locking the selected trial drops the selection to Stock.
  backend.setInverted(false);
  ASSERT_TRUE(backend.select(ProfileId::TRIAL_C));
  backend.setUnlockedTrials(trialBit(ProfileId::TRIAL_A));
  EXPECT_EQ(backend.profile(), ProfileId::STOCK);
  EXPECT_EQ(backend.info().clearing.hardLimit, PROFILES[0].clearing.hardLimit);
  // Locking another trial keeps it.
  ASSERT_TRUE(backend.select(ProfileId::TRIAL_A));
  backend.setUnlockedTrials(trialBit(ProfileId::TRIAL_A));
  EXPECT_EQ(backend.profile(), ProfileId::TRIAL_A);
}

TEST(Selection, ARuleThatChangesAfterSelectIsCheckedOnEveryWordUpdate) {
  FakeClock clock;
  FakeDisplayBackend backend(clock, TimingModel(), PanelId::DEPG0397);
  backend.setUnlockedTrials(ALL_TRIALS);
  ASSERT_TRUE(backend.select(ProfileId::TRIAL_C));
  backend.clear();
  backend.update(UpdateKind::WORD);
  EXPECT_EQ(backend.events.back().profile, ProfileId::TRIAL_C);
  // Inverted after select(): the selection stays, but no trial waveform runs (the residue rule
  // makes this first one a clearing refresh, then stock updates follow).
  backend.setInverted(true);
  backend.update(UpdateKind::WORD);
  EXPECT_EQ(backend.events.back().kind, FakeEvent::CLEAR);
  EXPECT_EQ(backend.events.back().decision.promotion, Promotion::RESIDUE);
  backend.update(UpdateKind::WORD);
  EXPECT_EQ(backend.events.back().kind, FakeEvent::UPDATE);
  EXPECT_EQ(backend.events.back().profile, ProfileId::STOCK);
  EXPECT_EQ(backend.profile(), ProfileId::TRIAL_C);
}

// The one-byte settings value.

TEST(TrialSetting, EncodesAndDecodesEveryValidValue) {
  int valid = 0;
  for (TrialMask mask = 0; mask <= ALL_TRIALS; ++mask) {
    for (const ProfileInfo& info : PROFILES) {
      if (info.id != ProfileId::STOCK && (mask & trialBit(info.id)) == 0) continue;
      const TrialSetting setting{mask, info.id};
      const uint8_t value = encodeTrialSetting(setting);
      EXPECT_EQ(value & 0x80, 0);
      const TrialSetting back = decodeTrialSetting(value);
      EXPECT_EQ(back.unlocked, mask);
      EXPECT_EQ(back.chosen, info.id);
      EXPECT_EQ(encodeTrialSetting(back), value);
      ++valid;
    }
  }
  EXPECT_EQ(valid, 16 + 4 * 8);  // Stock with every mask, each trial with the 8 masks holding it
  EXPECT_EQ(encodeTrialSetting(TrialSetting{}), TRIAL_SETTING_DEFAULT);
  EXPECT_EQ(encodeTrialSetting({ALL_TRIALS, ProfileId::TRIAL_D}), 0x4F);
}

TEST(TrialSetting, UnknownBitsResolveToStock) {
  // The reserved bit, or a chosen value no profile has: none of the byte is trusted.
  for (const uint8_t value :
       {uint8_t{0x80}, uint8_t{0x8F}, uint8_t{0xCF}, uint8_t{0x5F}, uint8_t{0x6F}, uint8_t{0x7F}, uint8_t{0xFF}}) {
    const TrialSetting s = decodeTrialSetting(value);
    EXPECT_EQ(s.unlocked, NO_TRIALS) << int{value};
    EXPECT_EQ(s.chosen, ProfileId::STOCK) << int{value};
    for (const PanelId panel : PANELS) EXPECT_EQ(resolveProfile(s, panel, false), ProfileId::STOCK);
  }
  // Every byte decodes to something safe: no bit outside the mask, never a chosen trial that is locked.
  for (int value = 0; value < 256; ++value) {
    const TrialSetting s = decodeTrialSetting(static_cast<uint8_t>(value));
    EXPECT_EQ(s.unlocked & ~ALL_TRIALS, 0) << value;
    EXPECT_LT(static_cast<size_t>(s.chosen), PROFILE_COUNT) << value;
    if (s.chosen != ProfileId::STOCK) EXPECT_NE(s.unlocked & trialBit(s.chosen), 0) << value;
  }
}

TEST(TrialSetting, AChosenTrialThatIsNotUnlockedResolvesToStock) {
  // Trial C chosen (3 << 4), only A unlocked.
  const TrialSetting s = decodeTrialSetting(static_cast<uint8_t>(0x30 | trialBit(ProfileId::TRIAL_A)));
  EXPECT_EQ(s.unlocked, trialBit(ProfileId::TRIAL_A));  // the unlocks are kept
  EXPECT_EQ(s.chosen, ProfileId::STOCK);
  // Built by hand too (decode cannot produce it).
  EXPECT_EQ(resolveProfile({trialBit(ProfileId::TRIAL_A), ProfileId::TRIAL_C}, PanelId::DEPG0397, false),
            ProfileId::STOCK);
}

TEST(TrialSetting, AChosenTrialNotAllowedOnThisPanelResolvesToStock) {
  const TrialSetting c{ALL_TRIALS, ProfileId::TRIAL_C};
  EXPECT_EQ(resolveProfile(c, PanelId::DEPG0397, false), ProfileId::TRIAL_C);
  EXPECT_EQ(resolveProfile(c, PanelId::SE0400NQW47, false), ProfileId::STOCK);
  EXPECT_EQ(resolveProfile(c, PanelId::UNKNOWN, false), ProfileId::STOCK);
  EXPECT_EQ(resolveProfile(c, PanelId::DEPG0397, true), ProfileId::STOCK);  // inverted
  const TrialSetting d{trialBit(ProfileId::TRIAL_D), ProfileId::TRIAL_D};
  EXPECT_EQ(resolveProfile(d, PanelId::SE0400NQW47, false), ProfileId::TRIAL_D);
  EXPECT_EQ(resolveProfile(d, PanelId::UNKNOWN, false), ProfileId::STOCK);
  // Out-of-range ids are handled.
  EXPECT_EQ(resolveProfile({ALL_TRIALS, static_cast<ProfileId>(9)}, PanelId::DEPG0397, false), ProfileId::STOCK);
  EXPECT_EQ(encodeTrialSetting({ALL_TRIALS, static_cast<ProfileId>(9)}), ALL_TRIALS);
  EXPECT_EQ(trialBit(ProfileId::STOCK), NO_TRIALS);
  EXPECT_EQ(trialBit(static_cast<ProfileId>(9)), NO_TRIALS);
}
