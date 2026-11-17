#pragma once

// Where the Wi-Fi updater of the feature builds looks for firmware, and when it installs it
// (task D4). Pure: no network, no ESP-IDF. src/network/OtaUpdater.cpp uses these under the feature
// flag; the release workflow must publish under exactly this name (docs/TWO_SLOT.md).
//
//   Release source: the latest release of c-tphr/eink-learner-aid (GitHub API "releases/latest",
//                   which skips drafts and pre-releases).
//   Asset name:     "eink-learner-aid-" + <tag_name> + "-picco2.bin", e.g.
//                   tag 2.1.0 -> eink-learner-aid-2.1.0-picco2.bin. The asset is the plain app image
//                   (firmware.bin of the picco_2slot build), the same file as the SD-card update.bin.
//   Newer:          see isNewerRelease().

#include <cstddef>

namespace update_release {

inline constexpr char LATEST_RELEASE_URL[] = "https://api.github.com/repos/c-tphr/eink-learner-aid/releases/latest";
inline constexpr char ASSET_PREFIX[] = "eink-learner-aid-";
// "picco2": the two-slot Picco build (picco_2slot). The image's board tag stays "picco"
// (src/network/FirmwareBoardTag.cpp), so the wrong-board check is unchanged.
inline constexpr char ASSET_SUFFIX[] = "-picco2.bin";
// Longest tag_name accepted in an asset name.
inline constexpr size_t MAX_TAG_LEN = 32;

// Writes ASSET_PREFIX + tagName + ASSET_SUFFIX into out. Returns false and leaves out empty (when
// capacity > 0) if the tag is empty, longer than MAX_TAG_LEN, contains a character other than
// A-Z a-z 0-9 . _ + -, or the name does not fit in capacity bytes including the terminator.
bool formatAssetName(char* out, size_t capacity, const char* tagName);

struct Version {
  unsigned long major = 0;
  unsigned long minor = 0;
  unsigned long patch = 0;
};

// Parses "MAJOR.MINOR.PATCH" at the start of text, with an optional leading 'v'. Each part is 1 to
// 9 decimal digits. What follows the patch number must be the end of the text or a character that
// is neither a digit nor '.' (so "2.0.0-picco2" parses as 2.0.0; "2.0", "2.0.0.1", "x2.0.0" fail).
bool parseVersion(const char* text, Version& out);

// The install rule. True only if both strings parse and the release's MAJOR.MINOR.PATCH is greater
// than the running firmware's, or equal while the running firmware is a release candidate (its
// version contains "-rc"), as upstream's OtaUpdater::isUpdateNewer. A tag that does not parse is
// never installed (upstream would compare uninitialised numbers).
bool isNewerRelease(const char* latestTag, const char* currentVersion);

}  // namespace update_release
