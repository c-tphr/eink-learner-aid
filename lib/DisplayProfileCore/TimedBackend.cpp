#include "TimedBackend.h"

namespace displayprofile {

uint32_t TimedBackend::run(const RefreshKind kind) {
  TimedCall call;
  call.kind = kind;

  // A deferred refresh started elsewhere would otherwise be waited out inside the driver, after
  // our clock starts (FreeInkDisplay::displayBuffer drains it first).
  const uint32_t before = clock.nowMs();
  port.finishPending();
  const uint32_t start = clock.nowMs();
  call.waitedMs = start - before;

  port.refresh(kind);
  call.ms = clock.nowMs() - start;  // wrap-safe uint32_t difference
  call.timedOut = call.ms >= timeoutMs(kind);
  if (call.timedOut) recordTimeout(kind);
  last = call;
  return call.ms;
}

}  // namespace displayprofile
