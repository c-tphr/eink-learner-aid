#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <cinttypes>
#include <cstdio>

#include "CheckpointBookRules.h"

namespace reading {

const char* checkpointDataName(const CheckpointData data) {
  switch (data) {
    case CheckpointData::None:
      return "none";
    case CheckpointData::Valid:
      return "valid";
    case CheckpointData::UnsupportedVersion:
      return "unsupported-version";
    case CheckpointData::Invalid:
      return "invalid";
    case CheckpointData::TooLarge:
      return "too-large";
    case CheckpointData::ReadFailed:
      return "read-failed";
    case CheckpointData::NoMemory:
      return "no-memory";
  }
  return "?";
}

CheckpointData checkpointDataFor(const checkpoint::OpenResult& result) {
  switch (result.verdict) {
    case checkpoint::Verdict::Valid:
      return CheckpointData::Valid;
    case checkpoint::Verdict::UnsupportedVersion:
      return CheckpointData::UnsupportedVersion;
    case checkpoint::Verdict::Invalid:
      break;
  }
  return result.code == checkpoint::IssueCode::Io ? CheckpointData::ReadFailed : CheckpointData::Invalid;
}

bool checkpointFileSizeAllowed(const size_t bytes) { return bytes <= checkpoint::MAX_FILE_BYTES; }

std::string checkpointCopyName(const uint32_t crc, const uint32_t bytes) {
  char name[48];
  snprintf(name, sizeof(name), "checkpoints-%08" PRIx32 "-%" PRIu32 ".json", crc, bytes);
  return name;
}

}  // namespace reading

#endif  // CROSSPOINT_RSVP_ENABLED
