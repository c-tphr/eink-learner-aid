#pragma once

// Time source for playback. Milliseconds in 32 bits, as Arduino millis() returns them: the value
// wraps every 2^32 ms (about 49.7 days), and every comparison in RsvpCore is wrap-safe.

#include <cstdint>

namespace rsvp {

class Clock {
 public:
  virtual ~Clock() = default;

  // Monotonic milliseconds since an arbitrary origin. May wrap from 0xFFFFFFFF to 0.
  virtual uint32_t nowMs() = 0;
};

// Wrap-safe time helpers. They are correct while the two times are less than 2^31 ms (24.8 days)
// apart, which holds for every interval the scheduler compares.

// True when `a` is strictly earlier than `b`.
inline constexpr bool timeBefore(const uint32_t a, const uint32_t b) { return static_cast<int32_t>(a - b) < 0; }

// The later of two times.
inline constexpr uint32_t timeLater(const uint32_t a, const uint32_t b) { return timeBefore(a, b) ? b : a; }

// The earlier of two times.
inline constexpr uint32_t timeEarlier(const uint32_t a, const uint32_t b) { return timeBefore(a, b) ? a : b; }

}  // namespace rsvp
