#pragma once

#include <cstddef>
#include <cstdint>

#include "RsvpFeature.h"

// Self-update gate for the feature builds (task D4; spike S3, sections 6 and 7). Upstream hides
// "Check for updates", "SD Card Firmware Update" and the boot recovery picker on the Picco because
// its stock table has one app slot. In feature builds the running partition table decides instead
// (lib/UpdatePolicy/SlotPolicy.h), every update write is checked against it, and the Wi-Fi updater
// asks this repository's releases (lib/UpdatePolicy/ReleaseNaming.h).
//
// Without the flag every function returns upstream's own value, so upstream call sites behave
// exactly as before on every other device.
namespace update_gate {

#if CROSSPOINT_RSVP_ENABLED

// True when the running table has exactly two OTA slots and an update would go to the other one.
// Always false in the simulator (no partition table there).
bool selfUpdateAllowed();

// Whether to offer the update entries; the upstream default is ignored in feature builds.
inline bool updateMenusVisible(bool /*upstreamDefault*/) { return selfUpdateAllowed(); }

// Called right before an update erases or writes the partition at targetAddress. False (and an
// error log naming the reason) unless the policy allows self-update, the target is the next update
// slot, and it is not the running slot. `what` names the caller in the log.
bool mayWriteUpdateSlot(uint32_t targetAddress, const char* what);

inline constexpr bool PROJECT_RELEASES = true;
// This repository's "latest release" API URL instead of upstream's.
const char* releaseUrl(const char* upstreamUrl);
// Always true in feature builds: writes the asset name for tagName (ReleaseNaming.h), or "/" (which
// no GitHub asset is called) if the tag is unusable, so nothing matches and no update is offered.
bool formatAssetName(char* out, size_t capacity, const char* tagName);
bool isNewerRelease(const char* latestTag, const char* currentVersion);

#else

inline bool selfUpdateAllowed() { return true; }  // upstream does not gate on the table
inline bool updateMenusVisible(bool upstreamDefault) { return upstreamDefault; }
inline bool mayWriteUpdateSlot(uint32_t /*targetAddress*/, const char* /*what*/) { return true; }
inline constexpr bool PROJECT_RELEASES = false;
inline const char* releaseUrl(const char* upstreamUrl) { return upstreamUrl; }
inline bool formatAssetName(char* /*out*/, size_t /*capacity*/, const char* /*tagName*/) { return false; }
inline bool isNewerRelease(const char* /*latestTag*/, const char* /*currentVersion*/) { return false; }

#endif

}  // namespace update_gate
