// The simulator timing model: environment parsing, the compiled-in timings file check, and the
// precedence defaults < timings file < environment.

#include <gtest/gtest.h>

#include "DisplayTiming.h"

using namespace displayprofile;

namespace {

constexpr FileProfileTiming STOCK_ONLY[] = {{ProfileId::STOCK, 420, 1000}};
constexpr FileProfileTiming STOCK_AND_C[] = {{ProfileId::STOCK, 420, 1000}, {ProfileId::TRIAL_C, 210, 990}};

TimingFile fileOf(const FileProfileTiming* entries, size_t count) {
  TimingFile file;
  file.profiles = entries;
  file.count = count;
  file.panel = "DEPG0397";
  file.source = "test";
  file.date = "2026-12-01";
  return file;
}

}  // namespace

TEST(OverrideParsing, AcceptsPlainDecimalMilliseconds) {
  uint32_t ms = 7;
  EXPECT_TRUE(parseOverrideMs("0", ms));
  EXPECT_EQ(ms, 0u);
  EXPECT_TRUE(parseOverrideMs("200", ms));
  EXPECT_EQ(ms, 200u);
  EXPECT_TRUE(parseOverrideMs("00540", ms));
  EXPECT_EQ(ms, 540u);
  EXPECT_TRUE(parseOverrideMs("60000", ms));
  EXPECT_EQ(ms, MAX_OVERRIDE_MS);
}

TEST(OverrideParsing, RejectsAnythingElseAndLeavesTheValue) {
  for (const char* bad : {"", "-1", "+5", " 200", "200 ", "200ms", "2.5", "1e3", "0x10", "60001", "99999", "100000",
                          "4294967296", "abc"}) {
    uint32_t ms = 7;
    EXPECT_FALSE(parseOverrideMs(bad, ms)) << '"' << bad << '"';
    EXPECT_EQ(ms, 7u) << bad;
  }
  uint32_t ms = 7;
  EXPECT_FALSE(parseOverrideMs(nullptr, ms));
}

TEST(OverrideParsing, BothOrNothing) {
  TimingOverrides o;
  EXPECT_EQ(parseOverrides(nullptr, nullptr, o), OverrideError::NONE);
  EXPECT_FALSE(o.hasUpdate);
  EXPECT_FALSE(o.hasClear);

  EXPECT_EQ(parseOverrides("200", "", o), OverrideError::NONE);
  EXPECT_TRUE(o.hasUpdate);
  EXPECT_EQ(o.updateMs, 200u);
  EXPECT_FALSE(o.hasClear);

  // A malformed value is an error and nothing is applied, not even the good one.
  TimingOverrides kept;
  kept.hasUpdate = true;
  kept.updateMs = 11;
  EXPECT_EQ(parseOverrides("200", "fast", kept), OverrideError::BAD_CLEAR);
  EXPECT_EQ(kept.updateMs, 11u);
  EXPECT_FALSE(kept.hasClear);
  EXPECT_EQ(parseOverrides("-5", "400", kept), OverrideError::BAD_UPDATE);
  EXPECT_EQ(kept.updateMs, 11u);
}

TEST(TimingFileCheck, AcceptsAValidFile) {
  EXPECT_EQ(checkTimingFile(TimingFile{}), FileError::NONE);  // no file
  EXPECT_EQ(checkTimingFile(fileOf(STOCK_ONLY, 1)), FileError::NONE);
  EXPECT_EQ(checkTimingFile(fileOf(STOCK_AND_C, 2)), FileError::NONE);
}

TEST(TimingFileCheck, RejectsMalformedEntries) {
  const FileProfileTiming noStock[] = {{ProfileId::TRIAL_A, 300, 1000}};
  EXPECT_EQ(checkTimingFile(fileOf(noStock, 1)), FileError::NO_STOCK);
  const FileProfileTiming twice[] = {{ProfileId::STOCK, 420, 1000}, {ProfileId::STOCK, 430, 1000}};
  EXPECT_EQ(checkTimingFile(fileOf(twice, 2)), FileError::DUPLICATE_PROFILE);
  const FileProfileTiming unknown[] = {{ProfileId::STOCK, 420, 1000}, {static_cast<ProfileId>(9), 300, 1000}};
  EXPECT_EQ(checkTimingFile(fileOf(unknown, 2)), FileError::UNKNOWN_PROFILE);
  const FileProfileTiming zeroUpdate[] = {{ProfileId::STOCK, 0, 1000}};
  EXPECT_EQ(checkTimingFile(fileOf(zeroUpdate, 1)), FileError::UPDATE_OUT_OF_RANGE);
  const FileProfileTiming absurdUpdate[] = {{ProfileId::STOCK, MAX_FILE_UPDATE_MS + 1, 1000}};
  EXPECT_EQ(checkTimingFile(fileOf(absurdUpdate, 1)), FileError::UPDATE_OUT_OF_RANGE);
  const FileProfileTiming zeroClear[] = {{ProfileId::STOCK, 420, 0}};
  EXPECT_EQ(checkTimingFile(fileOf(zeroClear, 1)), FileError::CLEAR_OUT_OF_RANGE);
  const FileProfileTiming absurdClear[] = {{ProfileId::STOCK, 420, MAX_FILE_CLEAR_MS + 1}};
  EXPECT_EQ(checkTimingFile(fileOf(absurdClear, 1)), FileError::CLEAR_OUT_OF_RANGE);
  // A bad file is never half-applied: resolve() refuses it and keeps every default.
  TimingModel model;
  const FileProfileTiming halfBad[] = {{ProfileId::STOCK, 420, 1000}, {ProfileId::TRIAL_A, 0, 1000}};
  EXPECT_FALSE(model.resolve(fileOf(halfBad, 2), {}));
  for (const ProfileInfo& info : PROFILES) {
    EXPECT_EQ(model.timing(info.id).updateMs, info.simulatorDefault.updateMs) << info.key;
    EXPECT_EQ(model.timing(info.id).clearMs, info.simulatorDefault.clearMs) << info.key;
    EXPECT_EQ(model.forProfile(info.id).updateSource, TimingSource::DEFAULTS);
  }
}

TEST(TimingPrecedence, DefaultsWhenNothingElse) {
  TimingModel model;
  ASSERT_TRUE(model.resolve(TimingFile{}, TimingOverrides{}));
  EXPECT_EQ(model.timing(ProfileId::STOCK).updateMs, 540u);
  EXPECT_EQ(model.timing(ProfileId::STOCK).clearMs, 1960u);
  EXPECT_EQ(model.timing(ProfileId::TRIAL_C).updateMs, 198u);
  EXPECT_EQ(model.timing(ProfileId::TRIAL_C).clearMs, 1960u);
  EXPECT_EQ(model.forProfile(ProfileId::STOCK).updateSource, TimingSource::DEFAULTS);
  EXPECT_EQ(model.forProfile(ProfileId::STOCK).clearSource, TimingSource::DEFAULTS);
}

TEST(TimingPrecedence, FileBeatsDefaults) {
  TimingModel model;
  ASSERT_TRUE(model.resolve(fileOf(STOCK_AND_C, 2), {}));
  EXPECT_EQ(model.timing(ProfileId::STOCK).updateMs, 420u);
  EXPECT_EQ(model.timing(ProfileId::STOCK).clearMs, 1000u);
  EXPECT_EQ(model.forProfile(ProfileId::STOCK).updateSource, TimingSource::TIMINGS_FILE);
  EXPECT_EQ(model.timing(ProfileId::TRIAL_C).updateMs, 210u);
  EXPECT_EQ(model.timing(ProfileId::TRIAL_C).clearMs, 990u);
  // A profile the file does not list: default update time, the file's stock clear time.
  EXPECT_EQ(model.timing(ProfileId::TRIAL_A).updateMs, 338u);
  EXPECT_EQ(model.forProfile(ProfileId::TRIAL_A).updateSource, TimingSource::DEFAULTS);
  EXPECT_EQ(model.timing(ProfileId::TRIAL_A).clearMs, 1000u);
  EXPECT_EQ(model.forProfile(ProfileId::TRIAL_A).clearSource, TimingSource::TIMINGS_FILE);
}

TEST(TimingPrecedence, EnvironmentBeatsFileAndDefaultsForEveryProfile) {
  TimingOverrides env;
  ASSERT_EQ(parseOverrides("200", "400", env), OverrideError::NONE);
  TimingModel model;
  ASSERT_TRUE(model.resolve(fileOf(STOCK_AND_C, 2), env));
  for (const ProfileInfo& info : PROFILES) {
    EXPECT_EQ(model.timing(info.id).updateMs, 200u) << info.key;
    EXPECT_EQ(model.timing(info.id).clearMs, 400u) << info.key;
    EXPECT_EQ(model.forProfile(info.id).updateSource, TimingSource::ENVIRONMENT);
    EXPECT_EQ(model.forProfile(info.id).clearSource, TimingSource::ENVIRONMENT);
  }
}

TEST(TimingPrecedence, EachOverrideIsIndependent) {
  TimingOverrides env;
  ASSERT_EQ(parseOverrides(nullptr, "0", env), OverrideError::NONE);
  TimingModel model;
  ASSERT_TRUE(model.resolve(fileOf(STOCK_ONLY, 1), env));
  EXPECT_EQ(model.timing(ProfileId::STOCK).updateMs, 420u);    // file
  EXPECT_EQ(model.timing(ProfileId::STOCK).clearMs, 0u);       // env: no added delay
  EXPECT_EQ(model.timing(ProfileId::TRIAL_B).updateMs, 258u);  // default
  EXPECT_EQ(model.timing(ProfileId::TRIAL_B).clearMs, 0u);
}

TEST(TimingPrecedence, ZeroMeansNoAddedDelay) {
  TimingOverrides env;
  ASSERT_EQ(parseOverrides("0", "0", env), OverrideError::NONE);
  TimingModel model;
  ASSERT_TRUE(model.resolve(TimingFile{}, env));
  EXPECT_EQ(model.timing(ProfileId::STOCK).updateMs, 0u);
  EXPECT_EQ(model.timing(ProfileId::STOCK).clearMs, 0u);
}

TEST(TimingSourceNames, AreStable) {
  EXPECT_STREQ(timingSourceName(TimingSource::DEFAULTS), "defaults");
  EXPECT_STREQ(timingSourceName(TimingSource::TIMINGS_FILE), "file");
  EXPECT_STREQ(timingSourceName(TimingSource::ENVIRONMENT), "env");
}

TEST(SleepTheRemainder, Arithmetic) {
  EXPECT_EQ(remainderToSleepMs(540, 0), 540u);
  EXPECT_EQ(remainderToSleepMs(540, 12), 528u);
  EXPECT_EQ(remainderToSleepMs(540, 539), 1u);
  EXPECT_EQ(remainderToSleepMs(540, 540), 0u);
  EXPECT_EQ(remainderToSleepMs(540, 541), 0u);    // the real call was slower: never wait
  EXPECT_EQ(remainderToSleepMs(540, 90000), 0u);  // much slower: still never wait (no underflow)
  EXPECT_EQ(remainderToSleepMs(0, 0), 0u);
  EXPECT_EQ(remainderToSleepMs(0, 15), 0u);
}
