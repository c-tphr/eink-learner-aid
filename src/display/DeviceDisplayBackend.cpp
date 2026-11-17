#include "DeviceDisplayBackend.h"

#if CROSSPOINT_RSVP_ENABLED && !defined(SIMULATOR)

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <PanelRecords.h>

namespace displayprofile {

namespace device_detail {

// Nothing is pending unless some other code started a deferred refresh and has not finished it;
// then FreeInkDisplay::displayBuffer would wait it out after our clock started.
void Parts::HalRefreshPort::finishPending() { display.waitRefreshComplete(); }

void Parts::HalRefreshPort::refresh(const RefreshKind kind) {
  // As ReaderRefreshScope does for book pages (src/activities/reader/ReaderUtils.h): FAST then
  // runs the panel's reading waveform. HALF ignores the flag.
  display.setReaderRefresh(true);
  // renderer.displayBuffer passes the fading-fix setting as turnOffScreen, as for book pages.
  renderer.displayBuffer(kind == RefreshKind::UPDATE ? HalDisplay::FAST_REFRESH : HalDisplay::HALF_REFRESH);
  // displayBuffer blocks until BUSY ends on every Picco path (README, "Device backend"); this
  // returns at once then, and finishes the refresh inside our timing if a driver ever defers it.
  display.waitRefreshComplete();
  display.setReaderRefresh(false);
}

}  // namespace device_detail

DeviceDisplayBackend::DeviceDisplayBackend(GfxRenderer& renderer)
    : Parts(renderer),
      TimedBackend(halPort, deviceClock,
                   resolvePanel(BoardConfig::ACTIVE.displayControllerVariant, display.controllerName())) {
  const PanelRecord& record = panelRecord(panel());
  const char* driver = display.controllerName();
  LOG_DBG("DSP",
          "Device backend: panel=%s variant=%u driver=%s recognised=%d async_split=%d update=\"%s\" clear=\"%s\"",
          panelName(panel()), static_cast<unsigned>(BoardConfig::ACTIVE.displayControllerVariant),
          driver != nullptr ? driver : "none", record.recognised ? 1 : 0, record.asyncSplit ? 1 : 0,
          record.updateWaveform, record.clearWaveform);
}

uint32_t DeviceDisplayBackend::presentUpdate() {
  const uint32_t ms = TimedBackend::presentUpdate();
  logCall();
  return ms;
}

uint32_t DeviceDisplayBackend::presentClear() {
  const uint32_t ms = TimedBackend::presentClear();
  logCall();
  return ms;
}

void DeviceDisplayBackend::logCall() {
  const TimedCall& call = lastCall();
  const char* kind = call.kind == RefreshKind::UPDATE ? "update" : "clear";
  if (call.waitedMs > 0) {
    LOG_DBG("DSP", "%s waited %lu ms for a refresh started elsewhere", kind, static_cast<unsigned long>(call.waitedMs));
  }
  logRefresh(*this, call.kind, call.ms);
  if (call.timedOut) {
    LOG_ERR("DSP", "%s timed out: ms=%lu limit=%lu panel=%s; the panel's BUSY line did not report the end", kind,
            static_cast<unsigned long>(call.ms), static_cast<unsigned long>(timeoutMs(call.kind)), panelName(panel()));
  }
}

}  // namespace displayprofile

#endif  // CROSSPOINT_RSVP_ENABLED && !defined(SIMULATOR)
