#include "DeviceDisplayBackend.h"

#if CROSSPOINT_RSVP_ENABLED && !defined(SIMULATOR)

#include <Arduino.h>
#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

#include "DisplayProfile.h"

namespace displayprofile {

PanelId DeviceDisplayBackend::panel() const { return panelFromVariant(BoardConfig::ACTIVE.displayControllerVariant); }

// PLACEHOLDER (D1): the existing fast refresh, timed. displayBuffer() returns after the BUSY wait.
uint32_t DeviceDisplayBackend::presentUpdate() {
  const uint32_t start = static_cast<uint32_t>(millis());
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  const uint32_t ms = static_cast<uint32_t>(millis()) - start;
  logRefresh(*this, RefreshKind::UPDATE, ms);
  return ms;
}

// PLACEHOLDER (D1/D2): an absolute HALF refresh, timed. D2 releases any trial table first.
uint32_t DeviceDisplayBackend::presentClear() {
  const uint32_t start = static_cast<uint32_t>(millis());
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  const uint32_t ms = static_cast<uint32_t>(millis()) - start;
  logRefresh(*this, RefreshKind::CLEAR, ms);
  return ms;
}

}  // namespace displayprofile

#endif  // CROSSPOINT_RSVP_ENABLED && !defined(SIMULATOR)
