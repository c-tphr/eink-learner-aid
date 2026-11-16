#pragma once

// Cumulative timing of display calls, for the pause screen and the device benchmark.

#include <cstdint>

namespace displayprofile {

struct RefreshStats {
  uint32_t count = 0;
  uint32_t minMs = 0;  // 0 while count is 0
  uint32_t maxMs = 0;
  uint32_t lastMs = 0;
  uint64_t totalMs = 0;

  void add(uint32_t ms);
  void reset() { *this = RefreshStats{}; }
  // Rounded half up; 0 while count is 0.
  uint32_t meanMs() const;
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
