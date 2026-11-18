#include "DisplayStats.h"

namespace displayprofile {

static_assert(RefreshStats::SAMPLE_WINDOW <= UINT16_MAX, "window indices are 16-bit");

void RefreshStats::add(const uint32_t ms) {
  minMs = count == 0 || ms < minMs ? ms : minMs;
  maxMs = count == 0 || ms > maxMs ? ms : maxMs;
  lastMs = ms;
  totalMs += ms;
  ++count;
  samples[next] = static_cast<uint16_t>(ms > UINT16_MAX ? UINT16_MAX : ms);
  next = static_cast<uint16_t>((next + 1) % SAMPLE_WINDOW);
  if (stored < SAMPLE_WINDOW) ++stored;
}

void RefreshStats::reset() {
  // Field by field: a temporary RefreshStats{} would put the whole window on the stack.
  count = 0;
  minMs = 0;
  maxMs = 0;
  lastMs = 0;
  totalMs = 0;
  timeouts = 0;
  next = 0;
  stored = 0;
}

uint32_t RefreshStats::meanMs() const {
  if (count == 0) return 0;
  return static_cast<uint32_t>((totalMs + count / 2) / count);
}

// Selection without a sorted copy (no stack buffer, no heap): the answer is the smallest sample
// with at least `rank` samples at or below it. At most SAMPLE_WINDOW^2 = 16,384 comparisons, run
// only when a caller asks (pause screen, benchmark, stats log line), never per refresh.
uint32_t RefreshStats::rankedMs(const size_t rank) const {
  uint32_t best = UINT16_MAX;
  for (size_t i = 0; i < stored; ++i) {
    const uint16_t candidate = samples[i];
    if (candidate >= best) continue;
    size_t atOrBelow = 0;
    for (size_t j = 0; j < stored; ++j) {
      if (samples[j] <= candidate) ++atOrBelow;
    }
    if (atOrBelow >= rank) best = candidate;
  }
  return best;
}

uint32_t RefreshStats::medianMs() const {
  if (stored == 0) return 0;
  if (stored % 2 != 0) return rankedMs(stored / 2 + 1);
  const uint32_t low = rankedMs(stored / 2);
  const uint32_t high = rankedMs(stored / 2 + 1);
  return (low + high + 1) / 2;
}

uint32_t RefreshStats::percentileMs(const uint32_t percent) const {
  if (stored == 0 || percent == 0 || percent > 100) return 0;
  // Nearest rank: ceil(percent / 100 * n), at least 1.
  const size_t rank = (static_cast<size_t>(percent) * stored + 99) / 100;
  return rankedMs(rank == 0 ? 1 : rank);
}

}  // namespace displayprofile
