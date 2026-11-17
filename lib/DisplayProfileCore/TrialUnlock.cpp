#include "TrialUnlock.h"

namespace displayprofile {

namespace {

constexpr uint8_t UNLOCKED_MASK = ALL_TRIALS;
constexpr unsigned CHOSEN_SHIFT = 4;
constexpr uint8_t CHOSEN_FIELD = 0x07;  // after the shift
constexpr uint8_t RESERVED_MASK = 0x80;

static_assert(trialBit(ProfileId::TRIAL_A) == 0x01 && trialBit(ProfileId::TRIAL_D) == 0x08, "one bit per trial");
static_assert((ALL_TRIALS & RESERVED_MASK) == 0 && ((CHOSEN_FIELD << CHOSEN_SHIFT) & (ALL_TRIALS | RESERVED_MASK)) == 0,
              "fields do not overlap");
static_assert(PROFILE_COUNT - 1 <= CHOSEN_FIELD, "every profile fits the chosen field");

}  // namespace

uint8_t encodeTrialSetting(const TrialSetting& setting) {
  const auto chosen = static_cast<uint8_t>(setting.chosen);
  const uint8_t field = chosen < PROFILE_COUNT ? chosen : 0;
  return static_cast<uint8_t>((setting.unlocked & UNLOCKED_MASK) | (field << CHOSEN_SHIFT));
}

TrialSetting decodeTrialSetting(const uint8_t value) {
  const auto chosen = static_cast<uint8_t>((value >> CHOSEN_SHIFT) & CHOSEN_FIELD);
  if ((value & RESERVED_MASK) != 0 || chosen >= PROFILE_COUNT) return TrialSetting{};
  TrialSetting setting;
  setting.unlocked = static_cast<TrialMask>(value & UNLOCKED_MASK);
  const auto id = static_cast<ProfileId>(chosen);
  setting.chosen = (setting.unlocked & trialBit(id)) != 0 ? id : ProfileId::STOCK;
  return setting;
}

ProfileId resolveProfile(const TrialSetting& setting, const PanelId panel, const bool inverted) {
  const bool unlocked = (setting.unlocked & trialBit(setting.chosen)) != 0;
  return profileAllowed(setting.chosen, panel, inverted, unlocked) ? setting.chosen : ProfileId::STOCK;
}

}  // namespace displayprofile
