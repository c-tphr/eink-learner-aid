#pragma once

// When RSVP pauses because the panel stopped answering (task R5, carried over from D1): the display
// backend classes a word update of UPDATE_TIMEOUT_MS (5 s) or more as a timeout and counts it in
// stats().updates.timeouts (lib/DisplayProfileCore/TimedBackend.h). A stuck panel would otherwise
// cost that long, up to the SDK's 30 s BUSY limit, for every word. Pure: host-tested in
// test/rsvp_activity with the device backend's timing logic.

#include <cstdint>

namespace reading {

class UpdateTimeoutWatch {
 public:
  // At Play: the backend's timeout count so far. Earlier timeouts do not pause again.
  void arm(const uint32_t timeouts) { seen = timeouts; }

  // After each word update: true when the count rose since arm() or the last call, so playback
  // pauses before the next word.
  bool rose(const uint32_t timeouts) {
    const bool more = timeouts > seen;
    seen = timeouts;
    return more;
  }

 private:
  uint32_t seen = 0;
};

}  // namespace reading
