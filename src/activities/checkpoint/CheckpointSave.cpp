#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <cstdio>
#include <cstring>

#include "CheckpointSave.h"

namespace checkpointui {

StateChange changeFor(const checkpoint::BookState& session, const checkpoint::Transition& t, const char* promptId) {
  StateChange change;
  if (t.gradeRecorded && promptId != nullptr) {
    const size_t length = strnlen(promptId, sizeof(change.promptId) - 1);
    std::memcpy(change.promptId, promptId, length);
    change.promptId[length] = '\0';
    change.grade = session.grades.find(change.promptId, &change.at);
    change.gradeRecorded = change.grade != checkpoint::Grade::Ungraded;
  }
  change.hasActive = session.hasActive;
  change.active = session.active;
  if (t.finished) {
    change.positionMoved = true;
    change.position = t.resume;
  }
  return change;
}

void applyChange(checkpoint::BookState& file, const StateChange& change, const char* bookSha256) {
  if (file.bookSha256[0] == '\0' && bookSha256 != nullptr &&
      checkpoint::isSha256Hex(bookSha256, strnlen(bookSha256, checkpoint::SHA256_HEX_CHARS + 1))) {
    std::memcpy(file.bookSha256, bookSha256, checkpoint::SHA256_HEX_CHARS);
    file.bookSha256[checkpoint::SHA256_HEX_CHARS] = '\0';
  }
  if (change.gradeRecorded) {
    // AlreadyGraded keeps the file's first grade; Full cannot drop anything here (the index decides
    // which ids are unknown), so such a grade stays in the session only, logged by the caller.
    file.grades.recordFirst(change.promptId, strlen(change.promptId), change.grade, change.at);
  }
  file.hasActive = change.hasActive;
  file.active = change.hasActive ? change.active : checkpoint::ActiveCheckpoint{};
  if (change.positionMoved) {
    file.hasPosition = true;
    file.position = change.position;
  }
}

bool formatGradeTime(const int tmYear, const int tmMon, const int tmMday, const int tmHour, const int tmMin,
                     const int tmSec, char out[checkpoint::TIMESTAMP_CHARS + 1]) {
  out[0] = '\0';
  const int year = tmYear + 1900;
  if (year < 2020 || year > 2135 || tmMon < 0 || tmMon > 11 || tmMday < 1 || tmMday > 31 || tmHour < 0 || tmHour > 23 ||
      tmMin < 0 || tmMin > 59 || tmSec < 0 || tmSec > 60) {
    return false;
  }
  // A leap second is written as :59, which the state file accepts.
  snprintf(out, checkpoint::TIMESTAMP_CHARS + 1, "%04d-%02d-%02dT%02d:%02d:%02d", year, tmMon + 1, tmMday, tmHour,
           tmMin, tmSec > 59 ? 59 : tmSec);
  checkpoint::Timestamp parsed = 0;
  if (!checkpoint::parseTimestamp(out, checkpoint::TIMESTAMP_CHARS, parsed)) {
    out[0] = '\0';  // an impossible date, such as 31 April
    return false;
  }
  return true;
}

}  // namespace checkpointui

#endif  // CROSSPOINT_RSVP_ENABLED
