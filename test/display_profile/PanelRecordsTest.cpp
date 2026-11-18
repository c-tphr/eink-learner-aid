// The per-panel record (task D1): one entry for every panel the SDK's Picco probe can report, the
// controller each runs on, and how the device backend resolves the panel from the probe's variant
// and the driver the SDK selected.

#include <gtest/gtest.h>

#include "PanelRecords.h"

using namespace displayprofile;

TEST(PanelRecords, OneRecordPerPanel) {
  const PanelId panels[] = {PanelId::UNKNOWN,  PanelId::YRD0426,     PanelId::DEPG0397,
                            PanelId::OPM040B3, PanelId::SE0400NQW47, PanelId::SIMULATED};
  ASSERT_EQ(sizeof(PANEL_RECORDS) / sizeof(PANEL_RECORDS[0]), 6u);
  for (size_t i = 0; i < 6; ++i) {
    EXPECT_EQ(PANEL_RECORDS[i].panel, panels[i]);
    EXPECT_EQ(&panelRecord(panels[i]), &PANEL_RECORDS[i]);
    EXPECT_NE(PANEL_RECORDS[i].updateWaveform, nullptr);
    EXPECT_NE(PANEL_RECORDS[i].clearWaveform, nullptr);
  }
  // A value outside the table reads as the unrecognised panel.
  EXPECT_EQ(panelRecord(static_cast<PanelId>(7)).panel, PanelId::UNKNOWN);
}

TEST(PanelRecords, ControllersAndRecognitionFollowS2) {
  // S2 section 1: three panels on the SSD1677 driver, SE0400NQW47 on the "SSD2677" driver, and an
  // unrecognised ID on the generic SSD1677 configuration.
  EXPECT_EQ(panelRecord(PanelId::UNKNOWN).controller, Controller::SSD1677);
  EXPECT_EQ(panelRecord(PanelId::YRD0426).controller, Controller::SSD1677);
  EXPECT_EQ(panelRecord(PanelId::DEPG0397).controller, Controller::SSD1677);
  EXPECT_EQ(panelRecord(PanelId::OPM040B3).controller, Controller::SSD1677);
  EXPECT_EQ(panelRecord(PanelId::SE0400NQW47).controller, Controller::SSD2677);
  EXPECT_EQ(panelRecord(PanelId::SIMULATED).controller, Controller::NONE);

  EXPECT_FALSE(panelRecord(PanelId::UNKNOWN).recognised);
  EXPECT_TRUE(panelRecord(PanelId::YRD0426).recognised);
  EXPECT_TRUE(panelRecord(PanelId::DEPG0397).recognised);
  EXPECT_TRUE(panelRecord(PanelId::OPM040B3).recognised);
  EXPECT_TRUE(panelRecord(PanelId::SE0400NQW47).recognised);
  EXPECT_FALSE(panelRecord(PanelId::SIMULATED).recognised);
}

TEST(PanelRecords, AsyncSplitOnlyWhereTheDriverDefersAndKeepsItsOwnBaseline) {
  EXPECT_FALSE(panelRecord(PanelId::UNKNOWN).asyncSplit);
  EXPECT_TRUE(panelRecord(PanelId::YRD0426).asyncSplit);
  EXPECT_TRUE(panelRecord(PanelId::DEPG0397).asyncSplit);
  EXPECT_TRUE(panelRecord(PanelId::OPM040B3).asyncSplit);
  EXPECT_FALSE(panelRecord(PanelId::SE0400NQW47).asyncSplit);
  EXPECT_FALSE(panelRecord(PanelId::SIMULATED).asyncSplit);
}

TEST(PanelRecords, PredictionsOnlyWhereTheSdkTablesGiveThem) {
  // S2 section 4: only DEPG0397's stock tables are host tables whose length can be computed.
  EXPECT_EQ(panelRecord(PanelId::DEPG0397).predictedUpdateMs, 380u);
  EXPECT_EQ(panelRecord(PanelId::DEPG0397).predictedClearMs, 960u);
  for (const PanelId panel : {PanelId::UNKNOWN, PanelId::YRD0426, PanelId::OPM040B3, PanelId::SE0400NQW47}) {
    EXPECT_EQ(panelRecord(panel).predictedUpdateMs, 0u) << panelName(panel);
    EXPECT_EQ(panelRecord(panel).predictedClearMs, 0u) << panelName(panel);
  }
}

TEST(PanelRecords, ControllerNames) {
  EXPECT_STREQ(controllerName(Controller::SSD1677), "SSD1677");
  EXPECT_STREQ(controllerName(Controller::SSD2677), "SSD2677");
  EXPECT_STREQ(controllerName(Controller::NONE), "none");
  EXPECT_EQ(controllerFromName("SSD1677"), Controller::SSD1677);
  EXPECT_EQ(controllerFromName("SSD2677"), Controller::SSD2677);
  EXPECT_EQ(controllerFromName(nullptr), Controller::NONE);
  EXPECT_EQ(controllerFromName(""), Controller::NONE);
  EXPECT_EQ(controllerFromName("ssd1677"), Controller::NONE);
  EXPECT_EQ(controllerFromName("UC8179"), Controller::NONE);
}

TEST(ResolvePanel, VariantAndDriverAgree) {
  EXPECT_EQ(resolvePanel(1, "SSD1677"), PanelId::YRD0426);
  EXPECT_EQ(resolvePanel(2, "SSD1677"), PanelId::DEPG0397);
  EXPECT_EQ(resolvePanel(3, "SSD1677"), PanelId::OPM040B3);
  EXPECT_EQ(resolvePanel(4, "SSD2677"), PanelId::SE0400NQW47);
  EXPECT_EQ(resolvePanel(0, "SSD1677"), PanelId::UNKNOWN);
}

TEST(ResolvePanel, AnythingElseFallsBackToUnknown) {
  // The driver does not match what the probe reported: do not trust either.
  EXPECT_EQ(resolvePanel(1, "SSD2677"), PanelId::UNKNOWN);
  EXPECT_EQ(resolvePanel(2, "SSD2677"), PanelId::UNKNOWN);
  EXPECT_EQ(resolvePanel(3, "SSD2677"), PanelId::UNKNOWN);
  EXPECT_EQ(resolvePanel(4, "SSD1677"), PanelId::UNKNOWN);
  EXPECT_EQ(resolvePanel(2, nullptr), PanelId::UNKNOWN);
  EXPECT_EQ(resolvePanel(2, "UC8179"), PanelId::UNKNOWN);
  // Values the probe never writes, and the simulator's id, are not panels.
  EXPECT_EQ(resolvePanel(0, "SSD2677"), PanelId::UNKNOWN);
  EXPECT_EQ(resolvePanel(5, "SSD1677"), PanelId::UNKNOWN);
  EXPECT_EQ(resolvePanel(100, nullptr), PanelId::UNKNOWN);
  EXPECT_EQ(resolvePanel(255, "SSD1677"), PanelId::UNKNOWN);
}
