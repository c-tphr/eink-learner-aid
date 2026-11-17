#pragma once

// The device backend (tasks D1 and D2): the stock and trial profiles' real refreshes on the panel.
// The decisions (allow rule, exposure limits, which table, timing window, timeouts, statistics)
// are in the portable DisplayBackend and TimedBackend (lib/DisplayProfileCore); this file only
// supplies the hardware calls and the log lines. What each call does on each panel, with the SDK
// source lines, is in src/display/README.md ("Device backend").
//
//   word update      renderer.displayBuffer(FAST_REFRESH) with HalDisplay::setReaderRefresh(true):
//                    CrossPoint's book-page refresh, the panel's reading waveform
//   trial table      HalDisplay::setCustomLut(true, table), the same FAST refresh, then
//                    setCustomLut(false)
//   trial UI path    renderer.displayBuffer(FAST_REFRESH) with the reader flag off (SE0400NQW47)
//   clearing refresh setCustomLut(false), renderer.displayBuffer(HALF_REFRESH) with the reader flag
//
// All block until the driver's BUSY wait has ended.

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
    void refreshUi() override;
    void loadTable(const uint8_t* table) override;
    void releaseTable() override;
    bool inverted() const override;

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
  uint32_t presentUpdate(ProfileId waveform) override;
  uint32_t presentClear() override;

 private:
  void logCall();
};

}  // namespace displayprofile

#endif  // CROSSPOINT_RSVP_ENABLED && !defined(SIMULATOR)
