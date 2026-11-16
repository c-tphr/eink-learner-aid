#include "DisplayStats.h"

namespace displayprofile {

void RefreshStats::add(const uint32_t ms) {
  minMs = count == 0 || ms < minMs ? ms : minMs;
  maxMs = count == 0 || ms > maxMs ? ms : maxMs;
  lastMs = ms;
  totalMs += ms;
  ++count;
}

uint32_t RefreshStats::meanMs() const {
  if (count == 0) return 0;
  return static_cast<uint32_t>((totalMs + count / 2) / count);
}

}  // namespace displayprofile
