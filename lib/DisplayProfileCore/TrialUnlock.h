#pragma once

// Which trial profiles the owner has unlocked, and which profile is chosen (plan section 6,
// "Safety limits" and "Outcome"). Trials are off by default and hidden unless unlocked: from the
// device-test menu for the benchmark (task D3a), or rated clean and kept by the owner at the end
// of the device session. With nothing stored only Stock exists. Storing the value in the settings
// file and the screens that change it belong to tasks D3a and D3b; this file only defines the value
// and how it resolves. Portable.

#include <cstdint>

#include "DisplayProfiles.h"

namespace displayprofile {

// One bit per trial: bit 0 TRIAL_A, bit 1 TRIAL_B, bit 2 TRIAL_C, bit 3 TRIAL_D.
using TrialMask = uint8_t;
inline constexpr TrialMask NO_TRIALS = 0;
inline constexpr TrialMask ALL_TRIALS = 0x0F;

// The bit of a trial; 0 for STOCK and for an id outside the table.
constexpr TrialMask trialBit(const ProfileId id) {
  return isTrial(id) ? static_cast<TrialMask>(1u << (static_cast<unsigned>(id) - 1u)) : NO_TRIALS;
}

struct TrialSetting {
  TrialMask unlocked = NO_TRIALS;
  ProfileId chosen = ProfileId::STOCK;
};

// The compact settings value (one byte; 0 is the default: nothing unlocked, Stock chosen):
//
//   bits 0..3  unlocked trials (TrialMask)
//   bits 4..6  chosen profile: 0 Stock, 1..4 Trial A..D
//   bit  7     reserved, always 0
inline constexpr uint8_t TRIAL_SETTING_DEFAULT = 0;
uint8_t encodeTrialSetting(const TrialSetting& setting);

// The inverse of encodeTrialSetting() for every value it produces. Anything else resolves safely:
// a reserved bit set, or a chosen value of 5..7, gives the default (nothing unlocked, Stock): the
// byte is not one this firmware wrote, so none of it is trusted. A chosen trial that is not
// unlocked keeps the unlocked bits and chooses Stock.
TrialSetting decodeTrialSetting(uint8_t value);

// The profile RSVP plays with: the chosen one if it is unlocked and profileAllowed() on this panel
// now, otherwise Stock.
ProfileId resolveProfile(const TrialSetting& setting, PanelId panel, bool inverted);

}  // namespace displayprofile
