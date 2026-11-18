#include "ClearingPolicy.h"

namespace rsvp {

void ClearingPolicy::noteWordUpdate(const bool endsBlock) {
  if (updates < UINT32_MAX) ++updates;
  atBlockEnd = endsBlock;
}

void ClearingPolicy::noteClearingRefresh() {
  updates = 0;
  atBlockEnd = false;
}

bool ClearingPolicy::clearDue() const {
  if (limits.hardLimit != 0 && updates >= limits.hardLimit) return true;
  return limits.softLimit != 0 && atBlockEnd && updates >= limits.softLimit;
}

}  // namespace rsvp
