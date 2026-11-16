#include "DeliveredWpmMeter.h"

namespace rsvp {

namespace {
constexpr uint64_t TENTHS_MS_PER_MINUTE = 600000;  // 60,000 ms per minute, times 10 for tenths
}

void DeliveredWpmMeter::addWordUpdate(const uint8_t words, const uint32_t updateMs) {
  totals.wordsShown += words;
  ++totals.updates;
  totals.updateMsTotal += updateMs;
  if (updateMs > totals.updateMsMax) totals.updateMsMax = updateMs;
}

void DeliveredWpmMeter::addClearingRefresh(const uint32_t clearMs) {
  ++totals.clearingRefreshes;
  totals.clearMsTotal += clearMs;
  if (clearMs > totals.clearMsMax) totals.clearMsMax = clearMs;
}

uint32_t DeliveredWpmMeter::deliveredWpmTenths() const {
  if (totals.activeMs == 0) return 0;
  const uint64_t numerator = static_cast<uint64_t>(totals.wordsShown) * TENTHS_MS_PER_MINUTE;
  return static_cast<uint32_t>((numerator + totals.activeMs / 2) / totals.activeMs);
}

uint32_t DeliveredWpmMeter::meanUpdateMs() const {
  if (totals.updates == 0) return 0;
  return static_cast<uint32_t>((totals.updateMsTotal + totals.updates / 2) / totals.updates);
}

}  // namespace rsvp
