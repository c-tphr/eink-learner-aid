#include "DisplayBackend.h"

namespace displayprofile {

bool DisplayBackend::allows(const ProfileId id) const {
  return profileAllowed(id, panel(), inverted(), (unlocked & trialBit(id)) != 0);
}

void DisplayBackend::setUnlockedTrials(const TrialMask mask) {
  unlocked = static_cast<TrialMask>(mask & ALL_TRIALS);
  if (!allows(current)) {
    current = ProfileId::STOCK;
    onSelect(current);
  }
}

bool DisplayBackend::select(const ProfileId id) {
  const bool allowed = static_cast<size_t>(id) < PROFILE_COUNT && allows(id);
  current = allowed ? id : ProfileId::STOCK;
  onSelect(current);
  return allowed;
}

uint32_t DisplayBackend::update(const UpdateKind kind) {
  return run(kind == UpdateKind::WORD ? RefreshRequest::WORD : RefreshRequest::SCREEN);
}

uint32_t DisplayBackend::clear() { return run(RefreshRequest::CLEAR); }

uint32_t DisplayBackend::run(const RefreshRequest request) {
  // The allow rule is checked again on every word update: the screen may have been inverted, or a
  // trial locked, since select().
  const ProfileId inForce = allows(current) ? current : ProfileId::STOCK;
  decision = guard.decide(request, inForce);
  uint32_t ms = 0;
  if (decision.action == RefreshAction::CLEAR) {
    ms = presentClear();
    statistics.clears.add(ms);
  } else {
    ms = presentUpdate(decision.action == RefreshAction::TRIAL_UPDATE ? decision.waveform : ProfileId::STOCK);
    statistics.updates.add(ms);
  }
  guard.record(decision);
  return ms;
}

uint32_t ModelledBackend::run(const RefreshKind kind, const ProfileId timingOf) {
  const ProfileTiming timing = timingModel.timing(timingOf);
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
