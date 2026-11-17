#pragma once

// The device backend (task D1): the stock profile's real refresh on the panel. The decisions
// (timing window, timeouts, statistics) are in the portable TimedBackend (lib/DisplayProfileCore);
// this file only supplies the two hardware calls and the log lines. What each call does on each
// panel, with the SDK source lines, is in src/display/README.md ("Device backend").
//
//   word update      renderer.displayBuffer(FAST_REFRESH) with HalDisplay::setReaderRefresh(true):
//                    CrossPoint's book-page refresh, the panel's reading waveform
//   clearing refresh renderer.displayBuffer(HALF_REFRESH), likewise
//
// Both block until the driver's BUSY wait has ended. Only the stock profile is allowed here
// (defaultProfileAllowed()); task D2 adds the trial waveforms and their allow rule.

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED && !defined(SIMULATOR)

#include <TimedBackend.h>

#include "DisplayProfile.h"

class GfxRenderer;

namespace displayprofile {

namespace device_detail {

// Base-from-member: the port and clock must exist before TimedBackend stores references to them.
struct Parts {
  class HalRefreshPort final : public RefreshPort {
   public:
    explicit HalRefreshPort(GfxRenderer& renderer) : renderer(renderer) {}
    void finishPending() override;
    void refresh(RefreshKind kind) override;

   private:
    GfxRenderer& renderer;
  };

  explicit Parts(GfxRenderer& renderer) : halPort(renderer) {}
  HalRefreshPort halPort;
  MillisClock deviceClock;
};

}  // namespace device_detail

class DeviceDisplayBackend final : private device_detail::Parts, public TimedBackend {
 public:
  // Reads the panel the SDK probed at boot and logs it, once.
  explicit DeviceDisplayBackend(GfxRenderer& renderer);

 protected:
  uint32_t presentUpdate() override;
  uint32_t presentClear() override;

 private:
  void logCall();
};

}  // namespace displayprofile

#endif  // CROSSPOINT_RSVP_ENABLED && !defined(SIMULATOR)
