#pragma once

// Grade rules (docs/PLAN.md section 7, "Grade rules"): the first Correct or
// Incorrect given to a prompt id is stored once and never overwritten. The
// table keeps ids as text, so grades for prompts the current checkpoint file
// no longer has survive until retainPromptsOf() drops them.

#include <cstddef>
#include <cstdint>

#include "CheckpointIndex.h"
#include "CheckpointLimits.h"
#include "CheckpointTypes.h"

namespace checkpoint {

// A grade time: seconds since 2000-01-01T00:00:00, written as
// "YYYY-MM-DDTHH:MM:SS" (local time, no zone, years 2000 to 2135).
using Timestamp = uint32_t;
inline constexpr Timestamp NO_TIMESTAMP = 0xFFFFFFFFu;
inline constexpr size_t TIMESTAMP_CHARS = 19;

// Parses exactly TIMESTAMP_CHARS characters. False for any other text or an
// impossible date or time.
bool parseTimestamp(const char* text, size_t length, Timestamp& out);
// Writes TIMESTAMP_CHARS characters and a NUL. False (and "") for NO_TIMESTAMP.
bool formatTimestamp(Timestamp at, char out[TIMESTAMP_CHARS + 1]);

// True for an id the checkpoint file accepts: 1 to MAX_ID_CHARS of
// [A-Za-z0-9_-], not starting with '_' or '-'.
bool isValidId(const char* text, size_t length);

inline constexpr uint16_t GRADE_CAPACITY = MAX_PROMPTS;
inline constexpr uint32_t GRADE_ID_POOL_BYTES = static_cast<uint32_t>(MAX_PROMPTS) * MAX_ID_CHARS;

class GradeTable {
 public:
  enum class RecordResult : uint8_t { Recorded, AlreadyGraded, Full, BadId, BadGrade };

  GradeTable() = default;
  GradeTable(const GradeTable&) = delete;
  GradeTable& operator=(const GradeTable&) = delete;

  void clear();

  // Records `grade` for `promptId` unless it already has one. `at` is the
  // caller's clock as "YYYY-MM-DDTHH:MM:SS"; unparsable or null text records
  // the grade without a time rather than lose it.
  RecordResult recordFirst(const char* promptId, Grade grade, const char* at);
  RecordResult recordFirst(const char* promptId, size_t idLength, Grade grade, Timestamp at);

  // The stored first grade, or Ungraded.
  Grade find(const char* promptId, Timestamp* at = nullptr) const;
  Grade find(const char* promptId, size_t idLength, Timestamp* at = nullptr) const;

  // Entries in the order they were recorded.
  uint16_t count() const { return total; }
  const char* idData(uint16_t i) const { return pool + entries[i].idOffset; }  // not NUL-terminated
  uint8_t idLength(uint16_t i) const { return entries[i].idLength; }
  Grade grade(uint16_t i) const { return static_cast<Grade>(entries[i].grade); }
  Timestamp at(uint16_t i) const { return entries[i].at; }

  // Drops the grades of prompt ids the index does not have; returns how many.
  // Reads every prompt id of the index once. Nothing is dropped, and 0xFFFF is
  // returned, when the index is closed or a prompt id cannot be read.
  uint16_t retainPromptsOf(const CheckpointIndex& index);

  bool sameAs(const GradeTable& other) const;

 private:
  struct Entry {
    uint32_t hash;
    Timestamp at;
    uint16_t idOffset;
    uint8_t idLength;
    uint8_t grade;
  };

  int findEntry(const char* id, size_t length, uint32_t hash) const;

  Entry entries[GRADE_CAPACITY] = {};
  char pool[GRADE_ID_POOL_BYTES] = {};
  uint32_t poolUsed = 0;
  uint16_t total = 0;
};

}  // namespace checkpoint
