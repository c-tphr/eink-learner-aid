// Host tests for the Wi-Fi updater's release source, asset name and version rule
// (lib/UpdatePolicy/ReleaseNaming.h). No network access.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "ReleaseNaming.h"

using update_release::formatAssetName;
using update_release::isNewerRelease;
using update_release::parseVersion;
using update_release::Version;

TEST(ReleaseNaming, PointsAtThisRepository) {
  EXPECT_STREQ(update_release::LATEST_RELEASE_URL,
               "https://api.github.com/repos/c-tphr/eink-learner-aid/releases/latest");
}

TEST(ReleaseNaming, AssetNameFromTag) {
  char out[64];
  ASSERT_TRUE(formatAssetName(out, sizeof(out), "2.1.0"));
  EXPECT_STREQ(out, "eink-learner-aid-2.1.0-picco2.bin");
  ASSERT_TRUE(formatAssetName(out, sizeof(out), "v2.1.0-rc1"));
  EXPECT_STREQ(out, "eink-learner-aid-v2.1.0-rc1-picco2.bin");
}

// OtaUpdater keeps upstream's 48-byte asset-name buffer.
TEST(ReleaseNaming, FitsTheUpdaterBufferForUsualTags) {
  char out[48];
  ASSERT_TRUE(formatAssetName(out, sizeof(out), "12.34.56-rc78"));
  EXPECT_STREQ(out, "eink-learner-aid-12.34.56-rc78-picco2.bin");
  // 17 + 19 + 11 + 1 = 48 bytes: the longest tag that fits.
  EXPECT_TRUE(formatAssetName(out, sizeof(out), "1234567890123456789"));
  EXPECT_FALSE(formatAssetName(out, sizeof(out), "12345678901234567890"));
  EXPECT_STREQ(out, "");
}

TEST(ReleaseNaming, AssetNameRejectsBadTags) {
  char out[96];
  strcpy(out, "junk");
  EXPECT_FALSE(formatAssetName(out, sizeof(out), ""));
  EXPECT_STREQ(out, "");
  EXPECT_FALSE(formatAssetName(out, sizeof(out), nullptr));
  EXPECT_FALSE(formatAssetName(out, sizeof(out), "2.1.0/../x"));
  EXPECT_FALSE(formatAssetName(out, sizeof(out), "2.1.0 beta"));
  EXPECT_FALSE(formatAssetName(out, sizeof(out), "2.1.0\"x"));
  EXPECT_FALSE(formatAssetName(out, sizeof(out), std::string(33, '1').c_str()));  // > MAX_TAG_LEN
  EXPECT_TRUE(formatAssetName(out, sizeof(out), std::string(32, '1').c_str()));
  EXPECT_FALSE(formatAssetName(out, 0, "2.1.0"));
  EXPECT_FALSE(formatAssetName(nullptr, 10, "2.1.0"));
}

TEST(ReleaseNaming, ParseVersion) {
  Version v;
  ASSERT_TRUE(parseVersion("2.0.0-picco2", v));
  EXPECT_EQ(v.major, 2u);
  EXPECT_EQ(v.minor, 0u);
  EXPECT_EQ(v.patch, 0u);
  ASSERT_TRUE(parseVersion("v10.20.30", v));
  EXPECT_EQ(v.major, 10u);
  EXPECT_EQ(v.minor, 20u);
  EXPECT_EQ(v.patch, 30u);
  ASSERT_TRUE(parseVersion("2.1.0-rc+abc", v));
  EXPECT_EQ(v.minor, 1u);
  for (const char* bad : {"", "2", "2.0", "2.0.", "2.0.0.1", "x2.0.0", "V2.0.0", " 2.0.0", "2..0", "2.0.a",
                          "dev-simulator", "1234567890.0.0"}) {
    EXPECT_FALSE(parseVersion(bad, v)) << bad;
  }
  EXPECT_FALSE(parseVersion(nullptr, v));
}

TEST(ReleaseNaming, NewerRelease) {
  EXPECT_TRUE(isNewerRelease("2.0.1", "2.0.0-picco2"));
  EXPECT_TRUE(isNewerRelease("2.1.0", "2.0.9-picco2"));
  EXPECT_TRUE(isNewerRelease("3.0.0", "2.9.9-picco2"));
  EXPECT_TRUE(isNewerRelease("v2.0.1", "2.0.0-picco2"));
  EXPECT_TRUE(isNewerRelease("2.0.10", "2.0.9-picco2"));  // numeric, not lexical
}

TEST(ReleaseNaming, NotNewerRelease) {
  EXPECT_FALSE(isNewerRelease("2.0.0", "2.0.0-picco2"));  // same version
  EXPECT_FALSE(isNewerRelease("1.9.9", "2.0.0-picco2"));
  EXPECT_FALSE(isNewerRelease("2.0.0", "2.0.1-picco2"));
  EXPECT_FALSE(isNewerRelease("2.0.9", "2.0.10-picco2"));
}

TEST(ReleaseNaming, ReleaseCandidateTakesTheSameRelease) {
  EXPECT_TRUE(isNewerRelease("2.1.0", "2.1.0-rc1-picco2"));
  EXPECT_FALSE(isNewerRelease("2.0.9", "2.1.0-rc1-picco2"));
}

TEST(ReleaseNaming, UnparsableVersionsNeverInstall) {
  EXPECT_FALSE(isNewerRelease("latest", "2.0.0-picco2"));
  EXPECT_FALSE(isNewerRelease("9.9", "2.0.0-picco2"));
  EXPECT_FALSE(isNewerRelease("2.1.0", "dev-simulator"));
  EXPECT_FALSE(isNewerRelease(nullptr, "2.0.0-picco2"));
  EXPECT_FALSE(isNewerRelease("2.1.0", nullptr));
}
