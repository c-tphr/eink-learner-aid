#pragma once

// PLACEHOLDER, REPLACED BY D1 (#30): the device backend, so RSVP UI work builds for the Picco now.
// It performs the existing fast refresh (update) and HALF refresh (clear) through the renderer and
// measures each call with millis(). It loads no waveform and allows only the stock profile.
//
// D1 makes stock a FAST refresh inside ReaderRefreshScope (the panel's reading waveform), adds the
// async split for the benchmark, and logs the panel name. D2 adds the trial waveforms (loaded before
// each update and released after it), the clearing refresh with the LUT released, and overrides
// allows() with the S2 rule (panel variant, inversion, device-test menu).

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED && !defined(SIMULATOR)

#include <DisplayBackend.h>

class GfxRenderer;

namespace displayprofile {

class DeviceDisplayBackend final : public DisplayBackend {
 public:
  explicit DeviceDisplayBackend(GfxRenderer& renderer) : renderer(renderer) {}

  // BoardConfig::ACTIVE.displayControllerVariant, filled by the SDK's boot probe.
  PanelId panel() const override;
  // Placeholder: no trial waveform exists yet, so only stock, whatever the panel.
  bool allows(ProfileId id) const override { return id == ProfileId::STOCK; }

 protected:
  uint32_t presentUpdate() override;
  uint32_t presentClear() override;

 private:
  GfxRenderer& renderer;
};

}  // namespace displayprofile

#endif  // CROSSPOINT_RSVP_ENABLED && !defined(SIMULATOR)
