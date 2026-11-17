#pragma once

// The device backend's logic, kept free of hardware so host tests drive it (task D1): every word
// update and clearing refresh is timed from just before the frame transfer starts to the moment
// the driver's BUSY wait has ended, a refresh that runs far past any real waveform is classed as
// a timeout, and a refresh started elsewhere and still running is waited out before the clock
// starts, so it is never counted as ours.
//
// The firmware's DeviceDisplayBackend (src/display/) is this with a RefreshPort over HalDisplay
// and the renderer and MillisClock; tests use FakeRefreshPort (FakeDisplayBackend.h) and FakeClock.

#include <Clock.h>

#include <cstdint>

#include "DisplayBackend.h"
#include "DisplayTiming.h"
#include "TrialWaveforms.h"

namespace displayprofile {

// A call that lasts this long or longer is a timeout: the panel's BUSY line never reported the end
// of a waveform, and the SDK's own wait gave up (it stops waiting after 30 s; see
// src/display/README.md, "Timeouts"). Stock waveforms take well under 2 s; the limits are the
// largest medians the timings file accepts, so a measured median can never count as a timeout.
inline constexpr uint32_t UPDATE_TIMEOUT_MS = MAX_FILE_UPDATE_MS;  // 5,000 ms
inline constexpr uint32_t CLEAR_TIMEOUT_MS = MAX_FILE_CLEAR_MS;    // 10,000 ms

inline constexpr uint32_t timeoutMs(const RefreshKind kind) {
  return kind == RefreshKind::UPDATE ? UPDATE_TIMEOUT_MS : CLEAR_TIMEOUT_MS;
}

// The hardware operations the device backend needs. None draws; the refreshes block.
class RefreshPort {
 public:
  virtual ~RefreshPort() = default;
  // Returns once no refresh started earlier (an asynchronous one elsewhere in the firmware) is
  // still running. Device: HalDisplay::waitRefreshComplete(), a no-op when none is pending.
  virtual void finishPending() = 0;
  // Sends the frame buffer and runs one refresh of `kind`; returns once the driver's wait for BUSY
  // has ended. Device: a FAST (UPDATE) or HALF (CLEAR) refresh with the reader flag on, as for
  // book pages: with no table loaded, the panel's stock reading waveform (task D1); with a table
  // loaded, a FAST runs that table instead (the driver's custom-LUT path).
  virtual void refresh(RefreshKind kind) = 0;
  // A FAST refresh with the reader flag off: the SDK's own UI waveform (Trial D on SE0400NQW47).
  virtual void refreshUi() = 0;
  // Loads a 110-byte SSD1677 table for the next FAST refresh (device: HalDisplay::setCustomLut(true,
  // table)), and releases it (setCustomLut(false): a flag in the driver, no bus traffic). The
  // backend releases after every table refresh, whatever happened during it, and before every clear.
  virtual void loadTable(const uint8_t* table) = 0;
  virtual void releaseTable() = 0;
  // Whether the screen is inverted now (device: HalDisplay::isInverted()).
  virtual bool inverted() const = 0;
};

// What one timed call did, for the log line and for tests.
struct TimedCall {
  RefreshKind kind = RefreshKind::UPDATE;
  uint32_t ms = 0;        // transfer start to BUSY end: what update()/clear() returned
  uint32_t waitedMs = 0;  // spent before the clock started, finishing a refresh started elsewhere
  bool timedOut = false;  // ms >= timeoutMs(kind)
  WordWaveform waveform;  // what an UPDATE ran (CLEAR: the default, stock)
};

class TimedBackend : public DisplayBackend {
 public:
  // `panel` is resolvePanel()'s result (PanelRecords.h).
  TimedBackend(RefreshPort& port, rsvp::Clock& clock, PanelId panel) : port(port), clock(clock), panelId(panel) {}

  PanelId panel() const override { return panelId; }
  const TimedCall& lastCall() const { return last; }

 protected:
  bool inverted() const override { return port.inverted(); }
  uint32_t presentUpdate(const ProfileId waveform) override { return run(RefreshKind::UPDATE, waveform); }
  uint32_t presentClear() override { return run(RefreshKind::CLEAR, ProfileId::STOCK); }

 private:
  uint32_t run(RefreshKind kind, ProfileId waveform);

  RefreshPort& port;
  rsvp::Clock& clock;
  PanelId panelId;
  TimedCall last;
};

}  // namespace displayprofile
