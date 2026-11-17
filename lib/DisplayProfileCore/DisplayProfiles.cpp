#include "DisplayProfiles.h"

namespace displayprofile {

static_assert(static_cast<size_t>(ProfileId::TRIAL_D) + 1 == PROFILE_COUNT, "PROFILES must list every ProfileId");

const ProfileInfo& profileInfo(const ProfileId id) {
  const auto index = static_cast<size_t>(id);
  return PROFILES[index < PROFILE_COUNT ? index : 0];
}

const ProfileInfo* findProfile(const std::string_view key) {
  for (const ProfileInfo& info : PROFILES) {
    if (key == info.key) return &info;
  }
  return nullptr;
}

const char* panelName(const PanelId panel) {
  switch (panel) {
    case PanelId::YRD0426:
      return "YRD0426";
    case PanelId::DEPG0397:
      return "DEPG0397";
    case PanelId::OPM040B3:
      return "OPM040B3";
    case PanelId::SE0400NQW47:
      return "SE0400NQW47";
    case PanelId::SIMULATED:
      return "simulator";
    case PanelId::UNKNOWN:
    default:
      return "unknown";
  }
}

PanelId panelFromVariant(const uint8_t variant) {
  return variant <= static_cast<uint8_t>(PanelId::SE0400NQW47) ? static_cast<PanelId>(variant) : PanelId::UNKNOWN;
}

bool profileAllowed(const ProfileId id, const PanelId panel, const bool inverted, const bool unlocked) {
  if (id == ProfileId::STOCK) return true;
  if (!isTrial(id) || !unlocked || inverted) return false;
  switch (panel) {
    case PanelId::YRD0426:
    case PanelId::DEPG0397:
    case PanelId::OPM040B3:
    case PanelId::SIMULATED:
      return true;
    case PanelId::SE0400NQW47:
      return id == ProfileId::TRIAL_D;
    case PanelId::UNKNOWN:
    default:
      return false;
  }
}

}  // namespace displayprofile
