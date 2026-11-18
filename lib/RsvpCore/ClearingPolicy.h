#pragma once

// When to run a clearing refresh during playback (plan section 6, "Clearing"): at a paragraph or
// section end once at least `softLimit` word updates have passed since the last clearing refresh,
// and before the next update in any case once `hardLimit` have passed. Pure counting, no clock.

#include <cstdint>

namespace rsvp {

inline constexpr uint16_t DEFAULT_CLEAR_SOFT_LIMIT = 60;
inline constexpr uint16_t DEFAULT_CLEAR_HARD_LIMIT = 120;

// A limit of 0 turns that rule off. Trial display profiles pass lower limits (45/90, 30/60).
struct ClearingLimits {
  uint16_t softLimit = DEFAULT_CLEAR_SOFT_LIMIT;
  uint16_t hardLimit = DEFAULT_CLEAR_HARD_LIMIT;
};

class ClearingPolicy {
 public:
  explicit ClearingPolicy(ClearingLimits limits = {}) : limits(limits) {}

  void setLimits(ClearingLimits newLimits) { limits = newLimits; }
  ClearingLimits currentLimits() const { return limits; }

  // After each word update. `endsBlock` is the shown frame's lastWord.endsBlock(): a paragraph,
  // section, non-prose or chapter end follows it.
  void noteWordUpdate(bool endsBlock);

  // After any clearing refresh, whoever ran it (the scheduler, or the activity on pause).
  void noteClearingRefresh();

  // True when a clearing refresh must run before the next word update.
  bool clearDue() const;

  uint32_t updatesSinceClear() const { return updates; }

 private:
  ClearingLimits limits;
  uint32_t updates = 0;
  bool atBlockEnd = false;  // the last word update ended a paragraph or section
};

}  // namespace rsvp
