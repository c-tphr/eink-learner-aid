#pragma once

// Delivered WPM for the current session: words shown divided by active playback time, plus the
// figures a results file records. The scheduler decides what is active (README.md, "Delivered-WPM
// meter"); this class only accumulates. Integer arithmetic throughout.

#include <cstdint>

namespace rsvp {

struct MeterStats {
  uint32_t wordsShown = 0;         // words in every frame drawn
  uint64_t activeMs = 0;           // active playback time
  uint32_t updates = 0;            // word updates (frames drawn)
  uint32_t clearingRefreshes = 0;  // clearing refreshes run by the scheduler during playback
  uint64_t updateMsTotal = 0;      // sum of update times (showFrame() call durations)
  uint32_t updateMsMax = 0;
  uint64_t clearMsTotal = 0;  // sum of clearing refresh times
  uint32_t clearMsMax = 0;
};

class DeliveredWpmMeter {
 public:
  void reset() { totals = MeterStats{}; }

  void addWordUpdate(uint8_t words, uint32_t updateMs);
  void addClearingRefresh(uint32_t clearMs);
  void addActiveMs(uint32_t ms) { totals.activeMs += ms; }

  // Delivered WPM in tenths, rounded to nearest (2405 means 240.5 WPM). 0 before any active time.
  uint32_t deliveredWpmTenths() const;

  // Mean update time in ms, rounded to nearest. 0 before the first update.
  uint32_t meanUpdateMs() const;

  const MeterStats& stats() const { return totals; }

 private:
  MeterStats totals;
};

}  // namespace rsvp
