// The trial waveform tables (task D2; docs/spikes/S2-waveforms.md sections 4, 6 and 7), checked from
// their committed bytes: the SSD1677 table format, frame counts and drive time recomputed from the
// timing bytes, voltages equal to the panel's stock table, unchanged pixels undriven, and every
// carried-over byte equal to the pinned SDK header, which this test re-reads from the submodule.

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "DisplayProfiles.h"
#include "TrialUnlock.h"
#include "TrialWaveforms.h"

using namespace displayprofile;

namespace {

using Bytes = std::vector<int>;

// Every `name[...] PROGMEM = { 0x.., ... };` array in the SDK's SSD1677 table header.
const std::map<std::string, Bytes>& sdkTables() {
  static const std::map<std::string, Bytes> tables = [] {
    std::map<std::string, Bytes> out;
    std::ifstream in(RSVP_REPO_ROOT "/freeink-sdk/libs/display/FreeInkDisplay/src/lut/Ssd1677Luts.h");
    std::stringstream text;
    text << in.rdbuf();
    const std::string src = std::regex_replace(text.str(), std::regex("//[^\\n]*"), "");
    const std::regex decl(R"((\w+)\s*\[[^\]]*\]\s*(?:PROGMEM)?\s*=\s*\{([^}]*)\})");
    const std::regex hex("0x[0-9A-Fa-f]+");
    for (auto it = std::sregex_iterator(src.begin(), src.end(), decl); it != std::sregex_iterator(); ++it) {
      Bytes bytes;
      const std::string body = (*it)[2];
      for (auto h = std::sregex_iterator(body.begin(), body.end(), hex); h != std::sregex_iterator(); ++h) {
        bytes.push_back(std::stoi(h->str(), nullptr, 16));
      }
      out[(*it)[1]] = bytes;
    }
    return out;
  }();
  return tables;
}

const Bytes& sdk(const std::string& name) {
  static const Bytes empty;
  const auto it = sdkTables().find(name);
  return it == sdkTables().end() ? empty : it->second;
}

Bytes bytesOf(const uint8_t* table) { return Bytes(table, table + LUT_SIZE); }

// Frame time for a frame-rate code (S2 section 4): codes 2, 4 and 8 are documented in the SDK
// (50 Hz, 100 Hz, 5 ms); any other code is "unknown" here (-1).
double frameMs(const int code) {
  switch (code) {
    case 2:
      return 20.0;
    case 4:
      return 10.0;
    case 8:
      return 5.0;
    default:
      return -1.0;
  }
}

struct Decoded {
  int frames = 0;
  double ms = 0;  // -1 when a group's frame rate is not documented
  // Per LUT entry (L0..L4): frames driven at VSH1, VSL, VSH2.
  int vsh1[5] = {};
  int vsl[5] = {};
  int vsh2[5] = {};
  int groupsUsed = 0;
};

Decoded decode(const Bytes& b) {
  Decoded d;
  for (int g = 0; g < 10; ++g) {
    const int* tp = &b[50 + 5 * g];
    const int rp = b[50 + 5 * g + 4];
    const int frames = (tp[0] + tp[1] + tp[2] + tp[3]) * (rp + 1);
    if (frames == 0) continue;
    ++d.groupsUsed;
    const int frByte = b[100 + g / 2];
    const int code = g % 2 == 0 ? frByte >> 4 : frByte & 0x0F;
    d.frames += frames;
    if (frameMs(code) < 0 || d.ms < 0) {
      d.ms = -1;
    } else {
      d.ms += frames * frameMs(code);
    }
    for (int e = 0; e < 5; ++e) {
      for (int p = 0; p < 4; ++p) {
        const int sel = (b[e * 10 + g] >> (6 - 2 * p)) & 3;
        const int n = tp[p] * (rp + 1);
        if (sel == 1) d.vsh1[e] += n;
        if (sel == 2) d.vsl[e] += n;
        if (sel == 3) d.vsh2[e] += n;
      }
    }
  }
  return d;
}

struct TrialCase {
  ProfileId id;
  const uint8_t* table;
  const char* stockTable;  // the panel's shipped fast table: voltages come from it
  int tp[4];
  int frames;
  int drivenFrames;
  int ms;
};

// S2 section 6's table, per panel family.
const TrialCase TRIAL_CASES[] = {
    {ProfileId::TRIAL_A, LUT_TRIAL_A_DEPG0397, "lut_picco_depg_custom_fast", {7, 1, 7, 0}, 15, 14, 300},
    {ProfileId::TRIAL_B, LUT_TRIAL_B_DEPG0397, "lut_picco_depg_custom_fast", {5, 1, 5, 0}, 11, 10, 220},
    {ProfileId::TRIAL_C, LUT_TRIAL_C_DEPG0397, "lut_picco_depg_custom_fast", {4, 0, 4, 0}, 8, 8, 160},
    {ProfileId::TRIAL_A, LUT_TRIAL_A_YRD0426_OPM040B3, "lut_picco_fast", {7, 1, 7, 0}, 15, 14, 300},
    {ProfileId::TRIAL_B, LUT_TRIAL_B_YRD0426_OPM040B3, "lut_picco_fast", {5, 1, 5, 0}, 11, 10, 220},
    {ProfileId::TRIAL_C, LUT_TRIAL_C_YRD0426_OPM040B3, "lut_picco_fast", {4, 0, 4, 0}, 8, 8, 160},
};

}  // namespace

// The derivation rests on two SDK tables being what S2 says. If either changes, these fail first.
TEST(TrialWaveforms, SdkSourceTablesAreWhatS2Describes) {
  ASSERT_FALSE(sdkTables().empty()) << "SDK header not found: is the freeink-sdk submodule checked out?";
  const Bytes& depg = sdk("lut_picco_depg_custom_fast");
  ASSERT_EQ(depg.size(), LUT_SIZE);
  EXPECT_EQ(depg[0], 0x01);  // L0: one-frame black top-up in phase D
  EXPECT_EQ(depg[10], 0x88);
  EXPECT_EQ(depg[20], 0x44);
  EXPECT_EQ(Bytes(depg.begin() + 50, depg.begin() + 55), (Bytes{8, 1, 9, 1, 0}));
  EXPECT_EQ(decode(depg).frames, 19);
  EXPECT_DOUBLE_EQ(decode(depg).ms, 380.0);
  EXPECT_EQ(Bytes(depg.begin() + 105, depg.end()), (Bytes{0x17, 0x41, 0xA8, 0x32, 0x30}));

  const Bytes& yrd = sdk("lut_picco_fast");
  ASSERT_EQ(yrd.size(), LUT_SIZE);
  EXPECT_EQ(yrd[10], 0x80);
  EXPECT_EQ(yrd[20], 0x40);
  EXPECT_EQ(Bytes(yrd.begin() + 50, yrd.begin() + 55), (Bytes{2, 0, 0, 0, 0}));
  EXPECT_EQ(yrd[100], 0x18);  // group 0 at frame-rate code 1: not documented, so no time here
  EXPECT_EQ(Bytes(yrd.begin() + 105, yrd.end()), (Bytes{0x17, 0x41, 0xA8, 0x34, 0x30}));

  const Bytes& depgUi = sdk("lut_picco_depg_fast");
  ASSERT_EQ(depgUi.size(), LUT_SIZE);
  EXPECT_DOUBLE_EQ(decode(depgUi).ms, 160.0);
}

TEST(TrialWaveforms, TablesAToCAreDerivedFromTheSdkAsS2Says) {
  const Bytes& base = sdk("lut_picco_depg_custom_fast");
  ASSERT_EQ(base.size(), LUT_SIZE);
  for (const TrialCase& c : TRIAL_CASES) {
    const Bytes& stock = sdk(c.stockTable);
    ASSERT_EQ(stock.size(), LUT_SIZE) << c.stockTable;
    // Rebuild the table from the SDK bytes, as docs/spikes/s2_lut_times.py trial() does.
    Bytes want(LUT_SIZE, 0);
    want[10] = base[10];  // L1: VSL in phases A and C
    want[20] = base[20];  // L2: VSH1 in phases A and C
    for (int p = 0; p < 4; ++p) want[50 + p] = c.tp[p];
    for (int i = 100; i < 105; ++i) want[i] = base[i];  // 0x22: 20 ms frames
    for (int i = 105; i < 110; ++i) want[i] = stock[i];
    EXPECT_EQ(bytesOf(c.table), want) << profileInfo(c.id).key << " from " << c.stockTable;
  }
}

TEST(TrialWaveforms, StructureFrameCountsAndDriveTime) {
  for (const TrialCase& c : TRIAL_CASES) {
    const Bytes b = bytesOf(c.table);
    const Bytes& stock = sdk(c.stockTable);
    ASSERT_EQ(stock.size(), LUT_SIZE);
    const Decoded d = decode(b);
    SCOPED_TRACE(std::string(profileInfo(c.id).key) + " / " + c.stockTable);
    // One group, run once, at frame-rate code 2 (20 ms), the stock DEPG frame rate.
    EXPECT_EQ(d.groupsUsed, 1);
    EXPECT_EQ(b[54], 0);  // RP
    for (int i = 100; i < 105; ++i) EXPECT_EQ(b[i], 0x22);
    EXPECT_EQ(d.frames, c.frames);
    EXPECT_DOUBLE_EQ(d.ms, c.ms);
    // Voltages: exactly the panel's stock fast table's VGH, VSH1, VSH2, VSL, VCOM.
    EXPECT_EQ(Bytes(b.begin() + 105, b.end()), Bytes(stock.begin() + 105, stock.end()));
    // Unchanged pixels undriven: L0 (black stays black), L3 (white stays white), L4 (VCOM row)
    // are all 0 V in every phase.
    for (const int e : {0, 3, 4}) {
      EXPECT_EQ(d.vsh1[e] + d.vsl[e] + d.vsh2[e], 0) << "L" << e;
      for (int g = 0; g < 10; ++g) EXPECT_EQ(b[e * 10 + g], 0) << "L" << e << " group " << g;
    }
    // Changing pixels: black to white only at VSL, white to black only at VSH1, never VSH2, and
    // for the same number of frames (DC-balanced over a pair of transitions).
    EXPECT_EQ(d.vsl[1], c.drivenFrames);
    EXPECT_EQ(d.vsh1[1] + d.vsh2[1], 0);
    EXPECT_EQ(d.vsh1[2], c.drivenFrames);
    EXPECT_EQ(d.vsl[2] + d.vsh2[2], 0);
    // No trial drives less than a waveform the vendor ships for the panel (S2 section 6): the
    // DEPG0397 UI table drives 7 frames.
    EXPECT_GE(c.drivenFrames, decode(sdk("lut_picco_depg_fast")).vsl[1]);
  }
}

TEST(TrialWaveforms, TrialDTablesAreTheSdkStockTablesUnchanged) {
  EXPECT_EQ(bytesOf(LUT_TRIAL_D_DEPG0397), sdk("lut_picco_depg_fast"));
  EXPECT_EQ(bytesOf(LUT_TRIAL_D_YRD0426_OPM040B3), sdk("lut_picco_fast"));
  const Decoded depg = decode(bytesOf(LUT_TRIAL_D_DEPG0397));
  EXPECT_EQ(depg.frames, 8);
  EXPECT_DOUBLE_EQ(depg.ms, 160.0);
  const Decoded yrd = decode(bytesOf(LUT_TRIAL_D_YRD0426_OPM040B3));
  EXPECT_EQ(yrd.frames, 2);
  EXPECT_LT(yrd.ms, 0);  // frame-rate code 1: about 80 ms only if code 1 is 40 ms (assumed in S2)
}

TEST(TrialWaveforms, EachPanelGetsItsOwnTableAndOnlyWhereAllowed) {
  const PanelId real[] = {PanelId::UNKNOWN, PanelId::YRD0426, PanelId::DEPG0397, PanelId::OPM040B3,
                          PanelId::SE0400NQW47};
  for (const ProfileInfo& info : PROFILES) {
    for (const PanelId panel : real) {
      const WordWaveform w = wordWaveform(info.id, panel);
      SCOPED_TRACE(std::string(info.key) + " on " + panelName(panel));
      // A trial waveform exists exactly where the allow rule permits the trial (unlocked, not inverted).
      EXPECT_EQ(w.path != WordPath::STOCK, info.id != ProfileId::STOCK && profileAllowed(info.id, panel, false, true));
      EXPECT_EQ(w.path == WordPath::TABLE, w.table != nullptr);
      if (w.path == WordPath::TABLE) {
        // Only the three SSD1677 panels ever get a table.
        EXPECT_TRUE(panel == PanelId::YRD0426 || panel == PanelId::DEPG0397 || panel == PanelId::OPM040B3);
        const Decoded d = decode(bytesOf(w.table));
        EXPECT_EQ(w.predictedMs, d.ms < 0 ? 0 : static_cast<int>(d.ms));
        // Voltages are those of this panel's own stock fast table.
        const Bytes& stock =
            sdk(panel == PanelId::DEPG0397 && info.id != ProfileId::TRIAL_D ? "lut_picco_depg_custom_fast"
                : panel == PanelId::DEPG0397                                ? "lut_picco_depg_fast"
                                                                            : "lut_picco_fast");
        EXPECT_EQ(Bytes(w.table + 105, w.table + LUT_SIZE), Bytes(stock.begin() + 105, stock.end()));
      }
    }
  }
  EXPECT_EQ(wordWaveform(ProfileId::TRIAL_D, PanelId::SE0400NQW47).path, WordPath::STOCK_UI);
  EXPECT_EQ(wordWaveform(ProfileId::TRIAL_C, PanelId::SIMULATED).path, WordPath::STOCK);
  EXPECT_EQ(wordWaveform(static_cast<ProfileId>(9), PanelId::DEPG0397).path, WordPath::STOCK);
}

TEST(TrialWaveforms, SimulatorDefaultsFollowTheTables) {
  // The simulator models each trial as its DEPG0397 drive time plus the frame transfer.
  for (const ProfileId id : {ProfileId::TRIAL_A, ProfileId::TRIAL_B, ProfileId::TRIAL_C, ProfileId::TRIAL_D}) {
    const WordWaveform w = wordWaveform(id, PanelId::DEPG0397);
    EXPECT_EQ(profileInfo(id).simulatorDefault.updateMs, w.predictedMs + FRAME_TRANSFER_MS) << profileInfo(id).key;
  }
}

TEST(TrialWaveforms, PrintTheTables) {
  for (const TrialCase& c : TRIAL_CASES) {
    const Decoded d = decode(bytesOf(c.table));
    std::printf("%s from %s: TP %d,%d,%d,%d = %d frames, %d driven, %.0f ms, tail %02X %02X %02X %02X %02X\n",
                profileInfo(c.id).key, c.stockTable, c.table[50], c.table[51], c.table[52], c.table[53], d.frames,
                d.vsl[1], d.ms, c.table[105], c.table[106], c.table[107], c.table[108], c.table[109]);
  }
}
