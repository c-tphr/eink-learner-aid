#include "DisplayBackend.h"

namespace displayprofile {

bool DisplayBackend::select(const ProfileId id) {
  if (static_cast<size_t>(id) >= PROFILE_COUNT || !allows(id)) return false;
  current = id;
  onSelect(id);
  return true;
}

uint32_t DisplayBackend::update() {
  const uint32_t ms = presentUpdate();
  statistics.updates.add(ms);
  return ms;
}

uint32_t DisplayBackend::clear() {
  const uint32_t ms = presentClear();
  statistics.clears.add(ms);
  return ms;
}

uint32_t ModelledBackend::run(const RefreshKind kind) {
  const ProfileTiming timing = timingModel.timing(profile());
  ModelledCall call;
  call.kind = kind;
  call.modelMs = kind == RefreshKind::UPDATE ? timing.updateMs : timing.clearMs;

  // Times are wrap-safe uint32_t differences of the clock. With a millisecond clock that truncates,
  // the total is still at least the model: the sleep starts after the truncated "real" end.
  const uint32_t start = clock.nowMs();
  presenter.present(kind);
  call.realMs = clock.nowMs() - start;
  call.sleptMs = remainderToSleepMs(call.modelMs, call.realMs);
  if (call.sleptMs > 0) sleeper.sleepMs(call.sleptMs);
  call.totalMs = clock.nowMs() - start;
  last = call;
  return call.totalMs;
}

}  // namespace displayprofile
