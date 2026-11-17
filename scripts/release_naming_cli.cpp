// Host command line around the Wi-Fi updater's own naming and version code
// (lib/UpdatePolicy/ReleaseNaming.cpp), so the release tools ask the firmware's logic instead of
// re-implementing it (scripts/make_release_kit.py, docs/RELEASING.md). Build:
//   c++ -std=c++17 -Ilib/UpdatePolicy scripts/release_naming_cli.cpp lib/UpdatePolicy/ReleaseNaming.cpp
//
//   asset-name <tag>             the asset the updater downloads for this tag; exit 1 if it finds none
//   version <text>               MAJOR.MINOR.PATCH as the updater parses it; exit 1 if it does not parse
//   is-newer <tag> <running>     "install" or "no update", the updater's verdict; always exit 0
#include <cstdio>
#include <cstring>

#include "ReleaseNaming.h"

namespace {

// The capacity OtaUpdater::checkForUpdate passes (char assetName[48], src/network/OtaUpdater.cpp).
// scripts/tests/test_make_release_kit.py fails if that buffer changes.
constexpr size_t kAssetNameCapacity = 48;

int usage() {
  std::fprintf(stderr, "usage: release_naming_cli asset-name TAG | version TEXT | is-newer TAG RUNNING\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 3 && std::strcmp(argv[1], "asset-name") == 0) {
    char name[kAssetNameCapacity];
    if (!update_release::formatAssetName(name, sizeof(name), argv[2])) {
      std::printf("none\n");
      return 1;
    }
    std::printf("%s\n", name);
    return 0;
  }
  if (argc == 3 && std::strcmp(argv[1], "version") == 0) {
    update_release::Version v;
    if (!update_release::parseVersion(argv[2], v)) {
      std::printf("unparsed\n");
      return 1;
    }
    std::printf("%lu.%lu.%lu\n", v.major, v.minor, v.patch);
    return 0;
  }
  if (argc == 4 && std::strcmp(argv[1], "is-newer") == 0) {
    std::printf("%s\n", update_release::isNewerRelease(argv[2], argv[3]) ? "install" : "no update");
    return 0;
  }
  return usage();
}
