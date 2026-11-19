#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <cstdio>
#include <cstring>

#include "ProfileChoice.h"

namespace devicetest {

using displayprofile::decodeTrialSetting;
using displayprofile::encodeTrialSetting;
using displayprofile::isTrial;
using displayprofile::profileAllowed;
using displayprofile::PROFILES;
using displayprofile::trialBit;

bool ProfileList::contains(const ProfileId id) const {
  for (uint8_t i = 0; i < count; ++i) {
    if (ids[i] == id) return true;
  }
  return false;
}

uint8_t ProfileList::indexOf(const ProfileId id, const uint8_t fallback) const {
  for (uint8_t i = 0; i < count; ++i) {
    if (ids[i] == id) return i;
  }
  return fallback;
}

ProfileList choiceCandidates(const TrialSetting& setting, const PanelId panel, const bool inverted) {
  ProfileList list;
  for (const auto& info : PROFILES) {
    const bool unlocked = (setting.unlocked & trialBit(info.id)) != 0;
    if (info.id == ProfileId::STOCK || profileAllowed(info.id, panel, inverted, unlocked)) {
      list.ids[list.count++] = info.id;
    }
  }
  return list;
}

TrialMask keepableTrials(const ProfileList& candidates, const ProfileId chosen) {
  TrialMask mask = displayprofile::NO_TRIALS;
  for (uint8_t i = 0; i < candidates.count; ++i) {
    const ProfileId id = candidates.ids[i];
    if (isTrial(id) && id != chosen) mask = static_cast<TrialMask>(mask | trialBit(id));
  }
  return mask;
}

ChoiceOutcome applyChoice(const uint8_t settingBefore, const ProfileId chosen, const TrialMask keep) {
  const TrialSetting before = decodeTrialSetting(settingBefore);
  TrialSetting after;
  after.chosen = static_cast<size_t>(chosen) < displayprofile::PROFILE_COUNT ? chosen : ProfileId::STOCK;
  after.unlocked =
      static_cast<TrialMask>((keep & static_cast<TrialMask>(~trialBit(after.chosen))) | trialBit(after.chosen));
  after.unlocked = static_cast<TrialMask>(after.unlocked & displayprofile::ALL_TRIALS);
  ChoiceOutcome out;
  out.setting = encodeTrialSetting(after);
  out.kept = after.unlocked;
  out.locked = static_cast<TrialMask>(before.unlocked & static_cast<TrialMask>(~after.unlocked));
  return out;
}

bool speedReadRowVisible(const uint8_t setting, const TrialMask kept) {
  return (decodeTrialSetting(setting).unlocked & kept & displayprofile::ALL_TRIALS) != 0;
}

SpeedReadOptions speedReadOptions(const uint8_t setting, const TrialMask kept, const PanelId panel) {
  const TrialSetting s = decodeTrialSetting(setting);
  const TrialMask selectable = static_cast<TrialMask>(s.unlocked & kept);
  SpeedReadOptions out;
  for (const auto& info : PROFILES) {
    const bool offered = info.id == ProfileId::STOCK ||
                         ((selectable & trialBit(info.id)) != 0 && profileAllowed(info.id, panel, false, true));
    if (offered) out.list.ids[out.list.count++] = info.id;
  }
  out.current = out.list.indexOf(s.chosen, 0);
  return out;
}

uint8_t selectSpeedReadProfile(const uint8_t setting, const TrialMask kept, const PanelId panel, const ProfileId id) {
  const SpeedReadOptions options = speedReadOptions(setting, kept, panel);
  if (!options.list.contains(id)) return setting;
  TrialSetting s = decodeTrialSetting(setting);
  s.chosen = id;
  return encodeTrialSetting(s);
}

int ratedStepIndex(const char* stepKey) {
  if (stepKey == nullptr) return -1;
  int n = 0;
  for (const StepSpec& s : STEPS) {
    if (!s.rated) continue;
    if (strcmp(s.key, stepKey) == 0) return n;
    ++n;
  }
  return -1;
}

void ratingCounts(const ProfileRatings& r, char* out, const size_t size) {
  unsigned counts[4] = {};
  for (const Rating rating : r.ratings) counts[static_cast<unsigned>(rating)]++;
  size_t used = 0;
  out[0] = '\0';
  for (const Rating rating : {Rating::CLEAN, Rating::TOLERABLE, Rating::UNREADABLE}) {
    const unsigned n = counts[static_cast<unsigned>(rating)];
    if (n == 0 || used >= size) continue;
    const int w = snprintf(out + used, size - used, "%s%u %s", used ? ", " : "", n, ratingName(rating));
    if (w > 0) used += static_cast<size_t>(w);
  }
  if (used == 0) snprintf(out, size, "%s", "not rated");
}

}  // namespace devicetest

#endif  // CROSSPOINT_RSVP_ENABLED
