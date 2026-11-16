// The profile table (ids, names, clearing limits, simulator defaults) and the panel identity.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "DisplayProfiles.h"

using namespace displayprofile;

TEST(ProfileTable, KeysAreStableAndInEnumOrder) {
  const char* keys[] = {"stock", "trial-a", "trial-b", "trial-c", "trial-d"};
  const char* names[] = {"Stock", "Trial A", "Trial B", "Trial C", "Trial D"};
  ASSERT_EQ(PROFILE_COUNT, 5u);
  for (size_t i = 0; i < PROFILE_COUNT; ++i) {
    EXPECT_EQ(static_cast<size_t>(PROFILES[i].id), i);
    EXPECT_STREQ(PROFILES[i].key, keys[i]);
    EXPECT_STREQ(PROFILES[i].name, names[i]);
    EXPECT_EQ(&profileInfo(PROFILES[i].id), &PROFILES[i]);
  }
}

TEST(ProfileTable, OnlyStockIsNotExperimental) {
  EXPECT_FALSE(profileInfo(ProfileId::STOCK).experimental);
  for (const ProfileId id : {ProfileId::TRIAL_A, ProfileId::TRIAL_B, ProfileId::TRIAL_C, ProfileId::TRIAL_D}) {
    EXPECT_TRUE(profileInfo(id).experimental) << profileInfo(id).key;
  }
}

TEST(ProfileTable, ClearingLimitsFollowS2) {
  struct Expected {
    ProfileId id;
    uint16_t soft;
    uint16_t hard;
  };
  // Plan section 6 (60/120 for every profile by default) and S2 section 8 (lower limits for B to D).
  const Expected expected[] = {{ProfileId::STOCK, 60, 120},
                               {ProfileId::TRIAL_A, 60, 120},
                               {ProfileId::TRIAL_B, 45, 90},
                               {ProfileId::TRIAL_C, 30, 60},
                               {ProfileId::TRIAL_D, 30, 60}};
  for (const Expected& e : expected) {
    EXPECT_EQ(profileInfo(e.id).clearing.softLimit, e.soft) << profileInfo(e.id).key;
    EXPECT_EQ(profileInfo(e.id).clearing.hardLimit, e.hard) << profileInfo(e.id).key;
  }
  // Stock matches RsvpCore's defaults, so a scheduler left at its defaults behaves as stock.
  EXPECT_EQ(profileInfo(ProfileId::STOCK).clearing.softLimit, rsvp::DEFAULT_CLEAR_SOFT_LIMIT);
  EXPECT_EQ(profileInfo(ProfileId::STOCK).clearing.hardLimit, rsvp::DEFAULT_CLEAR_HARD_LIMIT);
}

TEST(ProfileTable, SimulatorDefaults) {
  // Stock: the Xteink X4 figures of plan section 9. Trials: S2's drive time plus 38 ms transfer.
  EXPECT_EQ(profileInfo(ProfileId::STOCK).simulatorDefault.updateMs, 540u);
  EXPECT_EQ(profileInfo(ProfileId::TRIAL_A).simulatorDefault.updateMs, 338u);
  EXPECT_EQ(profileInfo(ProfileId::TRIAL_B).simulatorDefault.updateMs, 258u);
  EXPECT_EQ(profileInfo(ProfileId::TRIAL_C).simulatorDefault.updateMs, 198u);
  EXPECT_EQ(profileInfo(ProfileId::TRIAL_D).simulatorDefault.updateMs, 198u);
  for (const ProfileInfo& info : PROFILES) EXPECT_EQ(info.simulatorDefault.clearMs, 1960u) << info.key;
}

TEST(ProfileTable, FindByKeyIsExact) {
  for (const ProfileInfo& info : PROFILES) EXPECT_EQ(findProfile(info.key), &info);
  EXPECT_EQ(findProfile(""), nullptr);
  EXPECT_EQ(findProfile("Stock"), nullptr);
  EXPECT_EQ(findProfile("trial_a"), nullptr);
  EXPECT_EQ(findProfile("trial-e"), nullptr);
  EXPECT_EQ(findProfile("stock "), nullptr);
}

TEST(ProfileTable, OutOfRangeIdFallsBackToStock) {
  EXPECT_EQ(profileInfo(static_cast<ProfileId>(7)).id, ProfileId::STOCK);
}

TEST(PanelIdentity, NamesAndSdkVariants) {
  EXPECT_STREQ(panelName(PanelId::UNKNOWN), "unknown");
  EXPECT_STREQ(panelName(PanelId::YRD0426), "YRD0426");
  EXPECT_STREQ(panelName(PanelId::DEPG0397), "DEPG0397");
  EXPECT_STREQ(panelName(PanelId::OPM040B3), "OPM040B3");
  EXPECT_STREQ(panelName(PanelId::SE0400NQW47), "SE0400NQW47");
  EXPECT_STREQ(panelName(PanelId::SIMULATOR), "simulator");
  // The SDK's PiccoPanel values 0..4.
  EXPECT_EQ(panelFromVariant(0), PanelId::UNKNOWN);
  EXPECT_EQ(panelFromVariant(1), PanelId::YRD0426);
  EXPECT_EQ(panelFromVariant(2), PanelId::DEPG0397);
  EXPECT_EQ(panelFromVariant(3), PanelId::OPM040B3);
  EXPECT_EQ(panelFromVariant(4), PanelId::SE0400NQW47);
  EXPECT_EQ(panelFromVariant(5), PanelId::UNKNOWN);
  EXPECT_EQ(panelFromVariant(100), PanelId::UNKNOWN);
}

TEST(PanelIdentity, StockIsAlwaysAllowedTrialsOnlyInTheSimulatorUntilD2) {
  for (const PanelId panel : {PanelId::UNKNOWN, PanelId::YRD0426, PanelId::DEPG0397, PanelId::OPM040B3,
                              PanelId::SE0400NQW47, PanelId::SIMULATOR}) {
    EXPECT_TRUE(defaultProfileAllowed(ProfileId::STOCK, panel)) << panelName(panel);
    for (const ProfileId trial : {ProfileId::TRIAL_A, ProfileId::TRIAL_B, ProfileId::TRIAL_C, ProfileId::TRIAL_D}) {
      EXPECT_EQ(defaultProfileAllowed(trial, panel), panel == PanelId::SIMULATOR)
          << panelName(panel) << " " << profileInfo(trial).key;
    }
  }
}
