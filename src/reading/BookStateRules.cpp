#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <StateRestore.h>

#include <cstring>

#include "BookStateRules.h"

namespace reading {

const char* stateSourceName(const StateSource source) {
  switch (source) {
    case StateSource::Missing:
      return "missing";
    case StateSource::Read:
      return "read";
    case StateSource::Invalid:
      return "invalid";
    case StateSource::Unreadable:
      return "unreadable";
    case StateSource::NoMemory:
      return "no-memory";
  }
  return "?";
}

StateLoad decodeForUse(const uint8_t* data, const size_t size, const char* bookSha256,
                       const checkpoint::CheckpointIndex* index, checkpoint::BookState& out) {
  StateLoad load;
  const checkpoint::DecodeResult result = checkpoint::decodeState(data, size, bookSha256, out);
  load.status = result.status;
  if (result.status != checkpoint::DecodeStatus::Ok) {
    out.reset();  // decodeState() already did; kept explicit for the fresh-state contract
    load.source = StateSource::Invalid;
    return load;
  }
  load.source = StateSource::Read;
  if (index != nullptr && index->isOpen()) {
    // lib/CheckpointCore/README.md, "Opening a book", step 2.
    load.shaMismatch = checkpoint::applyBookChangeRule(out, index).shaMismatch;
    return load;
  }
  if (result.shaMismatch) {
    load.shaMismatch = true;
    std::memcpy(out.bookSha256, bookSha256, checkpoint::SHA256_HEX_CHARS);
    out.bookSha256[checkpoint::SHA256_HEX_CHARS] = '\0';
    out.hasPosition = false;
    out.position = checkpoint::Place{};
    out.hasActive = false;
    out.active = checkpoint::ActiveCheckpoint{};
  }
  return load;
}

}  // namespace reading

#endif  // CROSSPOINT_RSVP_ENABLED
