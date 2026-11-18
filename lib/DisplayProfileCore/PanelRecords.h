#pragma once

// What the device backend does on each panel the SDK's Picco probe can report, in one table, so the
// trial profiles (task D2) and the benchmark (task D3a) read "which controller, which panel, is it
// recognised" from here. Facts come from the pinned SDK (docs/spikes/S2-waveforms.md, sections 1,
// 2, 5 and 8; src/display/README.md has the per-call table with source lines). Portable.

#include <cstdint>

#include "DisplayProfiles.h"

namespace displayprofile {

// The SDK driver that runs the panel (HalDisplay::controllerName(), from
// PanelDriver::controllerName()).
enum class Controller : uint8_t {
  NONE,     // no SDK driver (the simulator)
  SSD1677,  // freeink::Ssd1677Driver
  SSD2677,  // freeink::Ssd2677Driver, the Picco's SE0400NQW47 path
};

// "SSD1677", "SSD2677" or "none".
const char* controllerName(Controller controller);

// The controller a driver name stands for; NONE for null or any other name.
Controller controllerFromName(const char* name);

struct PanelRecord {
  PanelId panel;
  Controller controller;  // the driver the SDK selects for this panel
  // One of the four panels the SDK has stock Picco waveforms for. Unrecognised panels run the
  // stock profile only (D2 keeps every trial off there).
  bool recognised;
  // HalDisplay::displayBufferAsync() really returns before the waveform ends, so a benchmark can
  // time the transfer and the waveform apart (S2 section 5), as long as the screen is not inverted
  // and the fading fix is off (GfxRenderer::supportsAsyncRefresh() checks both at run time). False
  // where it falls back to a blocking refresh (SSD2677), or where the deferred path would skip the
  // controller's RAM resync (an unrecognised panel: the generic SSD1677 configuration keeps no
  // previous frame of its own). The stock profile itself never uses the async path.
  bool asyncSplit;
  // Predicted waveform time of a stock word update and of a clearing refresh, without the frame
  // transfer (S2 section 4); 0 when it cannot be computed from the SDK (OTP or undocumented
  // tables). Device session results replace these.
  uint16_t predictedUpdateMs;
  uint16_t predictedClearMs;
  const char* updateWaveform;  // what a stock word update runs
  const char* clearWaveform;   // what a clearing refresh runs
};

inline constexpr PanelRecord PANEL_RECORDS[] = {
    {PanelId::UNKNOWN, Controller::SSD1677, false, false, 0, 0, "generic SSD1677 OTP sequence 0xFF",
     "generic SSD1677 OTP sequence 0xF7"},
    {PanelId::YRD0426, Controller::SSD1677, true, true, 0, 0, "OTP display mode 2 (sequence 0xDF)",
     "OTP display mode 1 (sequence 0xD7), temperature 0x5A"},
    {PanelId::DEPG0397, Controller::SSD1677, true, true, 380, 960, "lut_picco_depg_custom_fast (sequence 0xCF)",
     "lut_picco_depg_full (sequence 0xC7)"},
    {PanelId::OPM040B3, Controller::SSD1677, true, true, 0, 0, "OTP display mode 2 (sequence 0xDF)",
     "OTP display mode 1 (sequence 0xD7), temperature 0x5A"},
    {PanelId::SE0400NQW47, Controller::SSD2677, true, false, 0, 0, "lut_picco_se0400_fast, power off after",
     "lut_picco_se0400_full, power off after"},
    {PanelId::SIMULATED, Controller::NONE, false, false, 0, 0, "modelled", "modelled"},
};

// The record for `panel`; UNKNOWN's record for a value not in the table.
const PanelRecord& panelRecord(PanelId panel);

// The panel the device backend acts on: BoardConfig::ACTIVE.displayControllerVariant (the SDK
// probe's result), checked against the driver the SDK actually selected. A variant outside 0..4,
// or a driver that does not match the variant's record, gives UNKNOWN: the plainest stock path,
// no trial waveform.
PanelId resolvePanel(uint8_t variant, const char* driverName);

}  // namespace displayprofile
