#include <gtest/gtest.h>

#include <string>

#include "SampleRuns.h"
#include "activities/devicetest/ProfileChoice.h"

using namespace devicetest;
using displayprofile::ALL_TRIALS;
using displayprofile::decodeTrialSetting;
using displayprofile::encodeTrialSetting;
using displayprofile::trialBit;

namespace {

constexpr PanelId ALL_PANELS[] = {PanelId::UNKNOWN,  PanelId::YRD0426,     PanelId::DEPG0397,
                                  PanelId::OPM040B3, PanelId::SE0400NQW47, PanelId::SIMULATED};

constexpr TrialMask A = trialBit(ProfileId::TRIAL_A);
constexpr TrialMask B = trialBit(ProfileId::TRIAL_B);
constexpr TrialMask C = trialBit(ProfileId::TRIAL_C);
constexpr TrialMask D = trialBit(ProfileId::TRIAL_D);

std::string keys(const ProfileList& list) {
  std::string out;
  for (uint8_t i = 0; i < list.count; ++i) out += std::string(i ? "," : "") + displayprofile::profileInfo(list.ids[i]).key;
  return out;
}

// The byte a benchmark leaves on `panel` from the factory state (task D3a).
uint8_t afterBenchmark(const PanelId panel) {
  return encodeTrialSetting(unlockForBenchmark({}, runnableTrials(panel, false)));
}

}  // namespace

TEST(ProfileChoice, CandidatesAreStockAndTheUnlockedTrialsThePanelAllows) {
  EXPECT_EQ(keys(choiceCandidates(decodeTrialSetting(afterBenchmark(PanelId::DEPG0397)), PanelId::DEPG0397, false)),
            "stock,trial-a,trial-b,trial-c,trial-d");
  EXPECT_EQ(keys(choiceCandidates(decodeTrialSetting(afterBenchmark(PanelId::YRD0426)), PanelId::YRD0426, false)),
            "stock,trial-a,trial-b,trial-c,trial-d");
  // SE0400NQW47: Trials A to C were never made for it; only Trial D.
  EXPECT_EQ(afterBenchmark(PanelId::SE0400NQW47), 0x08);
  EXPECT_EQ(keys(choiceCandidates(decodeTrialSetting(0x08), PanelId::SE0400NQW47, false)), "stock,trial-d");
  EXPECT_EQ(keys(choiceCandidates(decodeTrialSetting(0x0F), PanelId::SE0400NQW47, false)), "stock,trial-d");
  // An unrecognised panel: Stock only, whatever is stored.
  EXPECT_EQ(afterBenchmark(PanelId::UNKNOWN), 0x00);
  EXPECT_EQ(keys(choiceCandidates(decodeTrialSetting(0x0F), PanelId::UNKNOWN, false)), "stock");
  // Nothing unlocked (no benchmark yet), or the screen inverted: Stock only.
  EXPECT_EQ(keys(choiceCandidates(decodeTrialSetting(0), PanelId::DEPG0397, false)), "stock");
  EXPECT_EQ(keys(choiceCandidates(decodeTrialSetting(0x0F), PanelId::DEPG0397, true)), "stock");
  // Only what the benchmark unlocked.
  EXPECT_EQ(keys(choiceCandidates(decodeTrialSetting(A | C), PanelId::OPM040B3, false)), "stock,trial-a,trial-c");
}

TEST(ProfileChoice, ChoosingLocksEveryTrialNotKept) {
  for (const PanelId panel : ALL_PANELS) {
    const uint8_t before = afterBenchmark(panel);
    const ProfileList c = choiceCandidates(decodeTrialSetting(before), panel, false);
    for (uint8_t i = 0; i < c.count; ++i) {
      const ProfileId chosen = c.ids[i];
      // Keep nothing: only the chosen trial stays unlocked.
      ChoiceOutcome none = applyChoice(before, chosen, 0);
      EXPECT_EQ(decodeTrialSetting(none.setting).chosen, chosen);
      EXPECT_EQ(decodeTrialSetting(none.setting).unlocked, trialBit(chosen));
      EXPECT_EQ(none.kept, trialBit(chosen));
      EXPECT_EQ(none.locked, decodeTrialSetting(before).unlocked & ~trialBit(chosen) & ALL_TRIALS);
      // Keep every other candidate: nothing is locked.
      const TrialMask keep = keepableTrials(c, chosen);
      EXPECT_EQ(keep & trialBit(chosen), 0);
      ChoiceOutcome all = applyChoice(before, chosen, keep);
      EXPECT_EQ(decodeTrialSetting(all.setting).chosen, chosen);
      EXPECT_EQ(all.kept, keep | trialBit(chosen));
      EXPECT_EQ(all.locked, decodeTrialSetting(before).unlocked & ~all.kept & ALL_TRIALS) << int(panel);
    }
  }
}

TEST(ProfileChoice, StockAndNothingKeptIsTheFactoryState) {
  for (const uint8_t before : {0x00, 0x0F, 0x08, 0x2F, 0x13}) {
    const ChoiceOutcome out = applyChoice(before, ProfileId::STOCK, 0);
    EXPECT_EQ(out.setting, 0);
    EXPECT_EQ(out.kept, 0);
    EXPECT_FALSE(speedReadRowVisible(out.setting, out.kept));
  }
}

TEST(ProfileChoice, TheExampleChoiceOnDepg0397) {
  // Trial C chosen, Trial A kept; Trials B and D locked again.
  const ChoiceOutcome out = applyChoice(0x0F, ProfileId::TRIAL_C, A);
  EXPECT_EQ(out.setting, 0x35);  // chosen 3 (Trial C) in bits 4..6, A and C unlocked
  EXPECT_EQ(out.kept, A | C);
  EXPECT_EQ(out.locked, B | D);
  // Stock chosen, Trial D kept: selectable for reading, RSVP still plays Stock.
  const ChoiceOutcome stockKeepD = applyChoice(0x0F, ProfileId::STOCK, D);
  EXPECT_EQ(stockKeepD.setting, 0x08);
  EXPECT_EQ(decodeTrialSetting(stockKeepD.setting).chosen, ProfileId::STOCK);
  EXPECT_TRUE(speedReadRowVisible(stockKeepD.setting, stockKeepD.kept));
}

TEST(SpeedReadRow, HiddenByDefaultAndAfterABenchmarkAloneShownAfterAChoice) {
  EXPECT_FALSE(speedReadRowVisible(0, 0));
  EXPECT_FALSE(speedReadRowVisible(afterBenchmark(PanelId::DEPG0397), 0));
  EXPECT_FALSE(speedReadRowVisible(0, ALL_TRIALS));  // nothing unlocked: the byte wins
  EXPECT_FALSE(speedReadRowVisible(0xFF, ALL_TRIALS));  // a byte this firmware never writes decodes to 0
  const ChoiceOutcome out = applyChoice(0x0F, ProfileId::TRIAL_B, 0);
  EXPECT_TRUE(speedReadRowVisible(out.setting, out.kept));
  // A later benchmark unlocks every trial again; the row still offers only what was kept.
  const uint8_t rerun = encodeTrialSetting(unlockForBenchmark(decodeTrialSetting(out.setting), ALL_TRIALS));
  EXPECT_TRUE(speedReadRowVisible(rerun, out.kept));
  EXPECT_EQ(keys(speedReadOptions(rerun, out.kept, PanelId::DEPG0397).list), "stock,trial-b");
}

TEST(SpeedReadRow, OffersStockAndTheKeptTrialsMadeForThisPanel) {
  const ChoiceOutcome out = applyChoice(0x0F, ProfileId::TRIAL_C, A | D);
  SpeedReadOptions o = speedReadOptions(out.setting, out.kept, PanelId::DEPG0397);
  EXPECT_EQ(keys(o.list), "stock,trial-a,trial-c,trial-d");
  EXPECT_EQ(o.list.ids[o.current], ProfileId::TRIAL_C);
  // The same values on SE0400NQW47: Trials A and C are not made for it. The stored choice is not
  // allowed there, so Stock is shown (and used: resolveProfile()).
  o = speedReadOptions(out.setting, out.kept, PanelId::SE0400NQW47);
  EXPECT_EQ(keys(o.list), "stock,trial-d");
  EXPECT_EQ(o.list.ids[o.current], ProfileId::STOCK);
  EXPECT_EQ(displayprofile::resolveProfile(decodeTrialSetting(out.setting), PanelId::SE0400NQW47, false),
            ProfileId::STOCK);
  // An unrecognised panel: Stock only.
  o = speedReadOptions(out.setting, out.kept, PanelId::UNKNOWN);
  EXPECT_EQ(keys(o.list), "stock");
  EXPECT_EQ(o.current, 0);
}

TEST(SpeedReadRow, PickingChangesTheChosenProfileOnlyNeverTheUnlockBits) {
  const ChoiceOutcome out = applyChoice(0x0F, ProfileId::TRIAL_C, A);
  const uint8_t stock = selectSpeedReadProfile(out.setting, out.kept, PanelId::DEPG0397, ProfileId::STOCK);
  EXPECT_EQ(decodeTrialSetting(stock).chosen, ProfileId::STOCK);
  EXPECT_EQ(decodeTrialSetting(stock).unlocked, A | C);
  const uint8_t a = selectSpeedReadProfile(stock, out.kept, PanelId::DEPG0397, ProfileId::TRIAL_A);
  EXPECT_EQ(decodeTrialSetting(a).chosen, ProfileId::TRIAL_A);
  EXPECT_EQ(decodeTrialSetting(a).unlocked, A | C);
  // Not offered: a trial not kept, or one not made for the panel. Nothing changes.
  EXPECT_EQ(selectSpeedReadProfile(a, out.kept, PanelId::DEPG0397, ProfileId::TRIAL_B), a);
  EXPECT_EQ(selectSpeedReadProfile(a, out.kept, PanelId::SE0400NQW47, ProfileId::TRIAL_C), a);
  EXPECT_EQ(selectSpeedReadProfile(a, out.kept, PanelId::DEPG0397, static_cast<ProfileId>(9)), a);
}

TEST(ProfileChoice, RatedStepsAndTheirCounts) {
  EXPECT_EQ(ratedStepIndex("playback-120-1"), 0);
  EXPECT_EQ(ratedStepIndex("playback-240-1"), 2);
  EXPECT_EQ(ratedStepIndex("endurance"), 6);
  EXPECT_EQ(ratedStepIndex("word-band"), -1);
  EXPECT_EQ(ratedStepIndex("nonsense"), -1);
  EXPECT_EQ(ratedStepIndex(nullptr), -1);
  ProfileRatings r;
  char text[64];
  ratingCounts(r, text, sizeof(text));
  EXPECT_STREQ(text, "not rated");
  r.ratings[0] = Rating::CLEAN;
  r.ratings[1] = Rating::CLEAN;
  r.ratings[4] = Rating::UNREADABLE;
  ratingCounts(r, text, sizeof(text));
  EXPECT_STREQ(text, "2 clean, 1 unreadable");
}

TEST(ChoiceJson, TheChoiceFileRecordsTheBytesAndTheRatingsItWasBasedOn) {
  const std::string json = devicetest_samples::choiceJson();
  for (const char* expected :
       {"\"kind\":\"choice\"", "\"version\":2", "\"file\":\"0015-choice.json\"", "\"status\":\"complete\"",
        "\"chosen\":\"trial-c\"", "\"saved\":true", "\"before\":15,\"after\":53",
        "\"unlocked_after\":[\"trial-a\",\"trial-c\"]", "\"kept_before\":[]",
        "\"kept_after\":[\"trial-a\",\"trial-c\"]", "\"locked\":[\"trial-b\",\"trial-d\"]",
        "\"benchmark_file\":\"0012-bench.json\"", "\"key\":\"trial-b\",\"name\":\"Trial B\",\"chosen\":false,\"kept\":false",
        "\"playback-300-1\":\"tolerable\"", "\"delivered_wpm_240\":175.0",
        "\"key\":\"trial-d\",\"name\":\"Trial D\",\"chosen\":false,\"kept\":false,\"ratings\":null,\"residue\":null,"
        "\"delivered_wpm_240\":null"}) {
    EXPECT_NE(json.find(expected), std::string::npos) << expected;
  }
}
