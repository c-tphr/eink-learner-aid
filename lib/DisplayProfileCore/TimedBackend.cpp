#include "TimedBackend.h"

namespace displayprofile {

uint32_t TimedBackend::run(const RefreshKind kind, const ProfileId waveform) {
  TimedCall call;
  call.kind = kind;
  // The panel decides too: a profile with no table for this panel runs the stock path.
  if (kind == RefreshKind::UPDATE) call.waveform = wordWaveform(waveform, panelId);

  // A deferred refresh started elsewhere would otherwise be waited out inside the driver, after
  // our clock starts (FreeInkDisplay::displayBuffer drains it first).
  const uint32_t before = clock.nowMs();
  port.finishPending();
  const uint32_t start = clock.nowMs();
  call.waitedMs = start - before;

  switch (call.waveform.path) {
    case WordPath::TABLE:
      // Loaded for this one refresh and released right after it returns, even after a timeout
      // (the SDK's BUSY wait gives up after 30 s), so no later refresh can run it.
      port.loadTable(call.waveform.table);
      port.refresh(RefreshKind::UPDATE);
      port.releaseTable();
      break;
    case WordPath::STOCK_UI:
      port.refreshUi();
      break;
    case WordPath::STOCK:
    default:
      // A clearing refresh must run the stock waveform: release first (a no-op flag write unless
      // something left a table loaded).
      if (kind == RefreshKind::CLEAR) port.releaseTable();
      port.refresh(kind);
      break;
  }
  call.ms = clock.nowMs() - start;  // wrap-safe uint32_t difference
  call.timedOut = call.ms >= timeoutMs(kind);
  if (call.timedOut) recordTimeout(kind);
  last = call;
  return call.ms;
}

}  // namespace displayprofile
