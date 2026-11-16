#pragma once

// Display profiles (plan section 6, "Display profiles"; docs/spikes/S2-waveforms.md): the named
// refresh strategies RSVP can play with, their clearing limits and, for the simulator, the time
// each one is modelled to take. Also the panel identity. Portable: no Arduino, HAL or SDK code.

#include <ClearingPolicy.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace displayprofile {

enum class ProfileId : uint8_t {
  STOCK,    // CrossPoint's fast refresh with the panel's reading waveform (default)
  TRIAL_A,  // shortened waveform, about 300 ms drive (D2)
  TRIAL_B,  // shortened waveform, about 220 ms drive (D2)
  TRIAL_C,  // shortened waveform, about 160 ms drive (D2)
  TRIAL_D,  // the panel's stock non-flashing UI waveform, BOOX mode 3 (D2)
};
inline constexpr size_t PROFILE_COUNT = 5;

// Time one blocking call is modelled to take, frame transfer included.
struct ProfileTiming {
  uint32_t updateMs = 0;  // one word update
  uint32_t clearMs = 0;   // one clearing refresh
};

// At least 38.4 ms to send two 48,000-byte planes at 20 MHz (S2, section 4). Every Picco update
// sends the full frame, so it is added to each trial's waveform time.
inline constexpr uint32_t FRAME_TRANSFER_MS = 38;

struct ProfileInfo {
  ProfileId id;
  const char* key;    // stable id in settings and results files
  const char* name;   // display name
  bool experimental;  // trials: off by default, chosen in the device session (plan section 6)
  rsvp::ClearingLimits clearing;
  ProfileTiming simulatorDefault;  // the simulator's built-in model (README.md, "Timing model")
};

// The profile table. Clearing limits: S2 section 8. Simulator defaults: stock uses the Xteink X4
// figures of plan section 9 (540 and 1,960 ms); trials use S2's predicted drive time on DEPG0397
// plus FRAME_TRANSFER_MS, and clear like stock (the clearing refresh is the same HALF refresh with
// any trial table released).
inline constexpr ProfileInfo PROFILES[PROFILE_COUNT] = {
    {ProfileId::STOCK, "stock", "Stock", false, {60, 120}, {540, 1960}},
    {ProfileId::TRIAL_A, "trial-a", "Trial A", true, {60, 120}, {300 + FRAME_TRANSFER_MS, 1960}},
    {ProfileId::TRIAL_B, "trial-b", "Trial B", true, {45, 90}, {220 + FRAME_TRANSFER_MS, 1960}},
    {ProfileId::TRIAL_C, "trial-c", "Trial C", true, {30, 60}, {160 + FRAME_TRANSFER_MS, 1960}},
    {ProfileId::TRIAL_D, "trial-d", "Trial D", true, {30, 60}, {160 + FRAME_TRANSFER_MS, 1960}},
};

const ProfileInfo& profileInfo(ProfileId id);

// The profile with this key ("stock", "trial-a" ...), or nullptr. Exact, case-sensitive match.
const ProfileInfo* findProfile(std::string_view key);

// Which panel the display driver found at boot. Values 0..4 are the SDK's PiccoPanel enum
// (freeink-sdk/libs/display/FreeInkDisplay/src/driver/Ssd1677Driver.h, PiccoUnknown..PiccoSe0400nqw47),
// which is in a private SDK header, so it is copied here (S2, section 1).
enum class PanelId : uint8_t {
  UNKNOWN = 0,      // no Picco panel recognised (generic SSD1677 driver)
  YRD0426 = 1,      // SSD1677
  DEPG0397 = 2,     // SSD1677
  OPM040B3 = 3,     // SSD1677
  SE0400NQW47 = 4,  // the stock "SSD2677" driver
  SIMULATOR = 100,  // the desktop simulator: no glass to protect
};

// "unknown", "YRD0426", "DEPG0397", "OPM040B3", "SE0400NQW47" or "simulator".
const char* panelName(PanelId panel);

// The panel for BoardConfig::ACTIVE.displayControllerVariant (0..4); anything else is UNKNOWN.
PanelId panelFromVariant(uint8_t variant);

// The default allow rule, used by backends that do not override DisplayBackend::allows().
// Stock is always allowed. The simulator allows every profile, so their modelled timings can be
// tried there. On a real panel the trials stay disallowed until task D2 supplies the rule of
// S2, section 9 (A to C on panels 1 to 3, D on 1 to 4, never while the screen is inverted).
bool defaultProfileAllowed(ProfileId id, PanelId panel);

}  // namespace displayprofile
