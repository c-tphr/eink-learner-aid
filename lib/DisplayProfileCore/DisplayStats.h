#pragma once

// Cumulative timing of display calls, for the pause screen and the device benchmark (task D3a).
// Fixed size: no heap, no growth however many refreshes are recorded.

#include <cstddef>
#include <cstdint>

namespace displayprofile {

class RefreshStats {
 public:
  // How many of the most recent durations are kept for the median and percentiles. The device
  // benchmark's largest series is 100 word updates (plan section 10), so a benchmark that calls
  // resetStats() before each series gets exact figures over the whole series.
  static constexpr size_t SAMPLE_WINDOW = 128;

  uint32_t count = 0;
  uint32_t minMs = 0;  // 0 while count is 0
  uint32_t maxMs = 0;
  uint32_t lastMs = 0;
  uint64_t totalMs = 0;
  uint32_t timeouts = 0;  // calls the device backend classed as timed out (TimedBackend.h)

  // Records one duration. Durations above 65,535 ms are kept exactly in min, max, last and the
  // total, and as 65,535 in the sample window.
  void add(uint32_t ms);
  void addTimeout() { ++timeouts; }
  void reset();

  // Rounded half up; 0 while count is 0.
  uint32_t meanMs() const;

  // Number of durations in the sample window: min(count, SAMPLE_WINDOW).
  size_t windowCount() const { return stored; }

  // Over the sample window; 0 while it is empty.
  // Median: the middle duration, or the mean of the two middle ones rounded half up.
  uint32_t medianMs() const;
  // Nearest-rank percentile, 1 to 100: the smallest duration that at least `percent` % of the
  // window's durations do not exceed. percentileMs(95) is the 95th percentile, 100 the window's
  // maximum. 0 for a percent outside 1..100.
  uint32_t percentileMs(uint32_t percent) const;

 private:
  // The `rank`-th smallest duration in the window, 1-based (1 <= rank <= stored).
  uint32_t rankedMs(size_t rank) const;

  uint16_t samples[SAMPLE_WINDOW] = {};
  uint16_t next = 0;    // where the next duration goes
  uint16_t stored = 0;  // how many slots hold a duration
};

struct DisplayStats {
  RefreshStats updates;  // word updates
  RefreshStats clears;   // clearing refreshes

  void reset() {
    updates.reset();
    clears.reset();
  }
};

}  // namespace displayprofile
