#include "TrialExposure.h"

namespace displayprofile {

namespace {

// Rule 3 needs a real bound for every trial (a hard limit of 0 turns the scheduler's rule off).
constexpr bool trialsHaveHardLimits() {
  for (const ProfileInfo& info : PROFILES) {
    if (isTrial(info.id) &&
        (info.clearing.hardLimit == 0 || info.clearing.hardLimit > PROFILES[0].clearing.hardLimit)) {
      return false;
    }
  }
  return true;
}
static_assert(trialsHaveHardLimits(), "every trial has a hard clearing limit, no higher than stock's");

RefreshDecision clearFor(const RefreshRequest request, const Promotion promotion) {
  RefreshDecision d;
  d.request = request;
  d.action = RefreshAction::CLEAR;
  d.promotion = promotion;
  return d;
}

}  // namespace

const char* promotionName(const Promotion promotion) {
  switch (promotion) {
    case Promotion::NOT_CLEARED:
      return "not-cleared";
    case Promotion::HARD_LIMIT:
      return "hard-limit";
    case Promotion::RESIDUE:
      return "residue";
    case Promotion::PROFILE_CHANGED:
      return "profile-changed";
    case Promotion::NONE:
    default:
      return "none";
  }
}

RefreshDecision TrialExposure::decide(const RefreshRequest request, const ProfileId profile) const {
  if (request == RefreshRequest::CLEAR) return clearFor(request, Promotion::NONE);

  const bool trialWord = request == RefreshRequest::WORD && isTrial(profile);
  if (!trialWord) {
    // Rules 1 and 4: a screen update or a stock word update.
    if (base == Base::TRIAL) return clearFor(request, Promotion::RESIDUE);
    RefreshDecision d;
    d.request = request;
    return d;
  }

  switch (base) {
    case Base::CLEARED:
      break;
    case Base::TRIAL:
      if (runProfile != profile) return clearFor(request, Promotion::PROFILE_CHANGED);
      if (run >= profileInfo(profile).clearing.hardLimit) return clearFor(request, Promotion::HARD_LIMIT);
      break;
    case Base::UNKNOWN:
    case Base::STOCK:
    default:
      return clearFor(request, Promotion::NOT_CLEARED);
  }
  RefreshDecision d;
  d.request = request;
  d.action = RefreshAction::TRIAL_UPDATE;
  d.waveform = profile;
  return d;
}

void TrialExposure::record(const RefreshDecision& decision) {
  switch (decision.action) {
    case RefreshAction::CLEAR:
      base = Base::CLEARED;
      run = 0;
      if (decision.promotion != Promotion::NONE) ++promoted;
      return;
    case RefreshAction::TRIAL_UPDATE:
      if (base != Base::TRIAL || runProfile != decision.waveform) run = 0;
      base = Base::TRIAL;
      runProfile = decision.waveform;
      if (run < UINT16_MAX) ++run;
      return;
    case RefreshAction::STOCK_UPDATE:
    default:
      base = Base::STOCK;
      run = 0;
      return;
  }
}

}  // namespace displayprofile
