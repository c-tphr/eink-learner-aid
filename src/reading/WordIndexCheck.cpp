#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include "WordIndexCheck.h"

namespace reading {

IndexState checkIndex(const bool fileExists, const wordindex::ReadStatus openStatus, const bool sourceKnown,
                      const bool sourceMatches) {
  if (!fileExists) return IndexState::Missing;
  if (openStatus != wordindex::ReadStatus::Ok) return IndexState::Unreadable;
  if (!sourceKnown) return IndexState::Unverifiable;
  return sourceMatches ? IndexState::Valid : IndexState::Stale;
}

const char* indexStateName(const IndexState state) {
  switch (state) {
    case IndexState::Valid:
      return "valid";
    case IndexState::Missing:
      return "missing";
    case IndexState::Unreadable:
      return "unreadable";
    case IndexState::Stale:
      return "stale";
    case IndexState::Unverifiable:
      return "unverifiable";
  }
  return "?";
}

}  // namespace reading

#endif  // CROSSPOINT_RSVP_ENABLED
