#include "GradeTable.h"

#include <cstring>

#include "JsonText.h"

namespace checkpoint {

namespace {

constexpr uint32_t MIN_YEAR = 2000;
constexpr uint32_t MAX_YEAR = 2135;  // the last full year that fits in 32 bits of seconds
constexpr int32_t DAYS_1970_TO_2000 = 10957;

bool isLeap(uint32_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

uint32_t daysInMonth(uint32_t y, uint32_t m) {
  static constexpr uint8_t DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return m == 2 && isLeap(y) ? 29u : DAYS[m - 1];
}

// Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant's algorithm).
int32_t daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
  y -= m <= 2 ? 1 : 0;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = static_cast<uint32_t>(y - era * 400);
  const uint32_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

void civilFromDays(int32_t z, uint32_t& y, uint32_t& m, uint32_t& d) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t doe = static_cast<uint32_t>(z - era * 146097);
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = static_cast<uint32_t>(static_cast<int32_t>(yoe) + era * 400) + (m <= 2 ? 1u : 0u);
}

bool digits(const char* text, size_t at, size_t count, uint32_t& out) {
  out = 0;
  for (size_t i = at; i < at + count; ++i) {
    if (text[i] < '0' || text[i] > '9') {
      return false;
    }
    out = out * 10 + static_cast<uint32_t>(text[i] - '0');
  }
  return true;
}

void putDigits(char* out, uint32_t value, size_t count) {
  for (size_t i = count; i > 0; --i) {
    out[i - 1] = static_cast<char>('0' + value % 10);
    value /= 10;
  }
}

size_t boundedLength(const char* text, size_t limit) {
  size_t n = 0;
  while (n < limit && text[n] != '\0') {
    ++n;
  }
  return n;
}

bool isIdChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

}  // namespace

bool parseTimestamp(const char* text, size_t length, Timestamp& out) {
  out = NO_TIMESTAMP;
  if (text == nullptr || length != TIMESTAMP_CHARS || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
      text[13] != ':' || text[16] != ':') {
    return false;
  }
  uint32_t y, mo, d, h, mi, s;
  if (!digits(text, 0, 4, y) || !digits(text, 5, 2, mo) || !digits(text, 8, 2, d) || !digits(text, 11, 2, h) ||
      !digits(text, 14, 2, mi) || !digits(text, 17, 2, s)) {
    return false;
  }
  if (y < MIN_YEAR || y > MAX_YEAR || mo < 1 || mo > 12 || d < 1 || d > daysInMonth(y, mo) || h > 23 || mi > 59 ||
      s > 59) {
    return false;
  }
  const int32_t days = daysFromCivil(static_cast<int32_t>(y), mo, d) - DAYS_1970_TO_2000;
  out = static_cast<uint32_t>(days) * 86400u + h * 3600u + mi * 60u + s;
  return true;
}

bool formatTimestamp(Timestamp at, char out[TIMESTAMP_CHARS + 1]) {
  out[0] = '\0';
  if (at == NO_TIMESTAMP) {
    return false;
  }
  const uint32_t secondsOfDay = at % 86400u;
  uint32_t y, m, d;
  civilFromDays(static_cast<int32_t>(at / 86400u) + DAYS_1970_TO_2000, y, m, d);
  if (y > MAX_YEAR) {
    return false;
  }
  putDigits(out, y, 4);
  out[4] = '-';
  putDigits(out + 5, m, 2);
  out[7] = '-';
  putDigits(out + 8, d, 2);
  out[10] = 'T';
  putDigits(out + 11, secondsOfDay / 3600u, 2);
  out[13] = ':';
  putDigits(out + 14, secondsOfDay / 60u % 60u, 2);
  out[16] = ':';
  putDigits(out + 17, secondsOfDay % 60u, 2);
  out[19] = '\0';
  return true;
}

bool isValidId(const char* text, size_t length) {
  if (text == nullptr || length == 0 || length > MAX_ID_CHARS || text[0] == '_' || text[0] == '-') {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    if (!isIdChar(text[i])) {
      return false;
    }
  }
  return true;
}

void GradeTable::clear() {
  total = 0;
  poolUsed = 0;
}

int GradeTable::findEntry(const char* id, size_t length, uint32_t hash) const {
  for (uint16_t i = 0; i < total; ++i) {
    const Entry& e = entries[i];
    if (e.hash == hash && e.idLength == length && memcmp(pool + e.idOffset, id, length) == 0) {
      return i;
    }
  }
  return -1;
}

GradeTable::RecordResult GradeTable::recordFirst(const char* promptId, Grade grade, const char* at) {
  Timestamp when = NO_TIMESTAMP;
  if (at != nullptr) {
    parseTimestamp(at, boundedLength(at, TIMESTAMP_CHARS + 1), when);
  }
  return recordFirst(promptId, promptId == nullptr ? 0 : boundedLength(promptId, MAX_ID_CHARS + 1), grade, when);
}

GradeTable::RecordResult GradeTable::recordFirst(const char* promptId, size_t idLength, Grade grade, Timestamp at) {
  if (grade != Grade::Correct && grade != Grade::Incorrect) {
    return RecordResult::BadGrade;
  }
  if (!isValidId(promptId, idLength)) {
    return RecordResult::BadId;
  }
  const uint32_t hash = detail::fnvBytes(promptId, idLength);
  if (findEntry(promptId, idLength, hash) >= 0) {
    return RecordResult::AlreadyGraded;
  }
  if (total >= GRADE_CAPACITY || poolUsed + idLength > GRADE_ID_POOL_BYTES) {
    return RecordResult::Full;
  }
  Entry& e = entries[total++];
  e.hash = hash;
  e.at = at;
  e.idOffset = static_cast<uint16_t>(poolUsed);
  e.idLength = static_cast<uint8_t>(idLength);
  e.grade = static_cast<uint8_t>(grade);
  memcpy(pool + poolUsed, promptId, idLength);
  poolUsed += static_cast<uint32_t>(idLength);
  return RecordResult::Recorded;
}

Grade GradeTable::find(const char* promptId, Timestamp* at) const {
  return find(promptId, promptId == nullptr ? 0 : boundedLength(promptId, MAX_ID_CHARS + 1), at);
}

Grade GradeTable::find(const char* promptId, size_t idLength, Timestamp* at) const {
  if (at != nullptr) {
    *at = NO_TIMESTAMP;
  }
  if (promptId == nullptr || idLength == 0 || idLength > MAX_ID_CHARS) {
    return Grade::Ungraded;
  }
  const int i = findEntry(promptId, idLength, detail::fnvBytes(promptId, idLength));
  if (i < 0) {
    return Grade::Ungraded;
  }
  if (at != nullptr) {
    *at = entries[i].at;
  }
  return static_cast<Grade>(entries[i].grade);
}

uint16_t GradeTable::retainPromptsOf(const CheckpointIndex& index) {
  if (!index.isOpen()) {
    return 0xFFFF;
  }
  uint8_t keep[(GRADE_CAPACITY + 7) / 8] = {};
  char id[ID_BUFFER_BYTES];
  for (uint16_t c = 0; c < index.checkpointCount(); ++c) {
    for (uint16_t p = 0; p < index.promptCount(c); ++p) {
      if (!index.loadPromptId(c, p, id, sizeof(id))) {
        return 0xFFFF;
      }
      const size_t length = strlen(id);
      const int i = findEntry(id, length, detail::fnvBytes(id, length));
      if (i >= 0) {
        keep[i / 8] = static_cast<uint8_t>(keep[i / 8] | (1u << (i % 8)));
      }
    }
  }
  uint16_t kept = 0;
  uint32_t used = 0;
  for (uint16_t i = 0; i < total; ++i) {
    if ((keep[i / 8] & (1u << (i % 8))) == 0) {
      continue;
    }
    Entry e = entries[i];
    memmove(pool + used, pool + e.idOffset, e.idLength);
    e.idOffset = static_cast<uint16_t>(used);
    used += e.idLength;
    entries[kept++] = e;
  }
  const uint16_t dropped = static_cast<uint16_t>(total - kept);
  total = kept;
  poolUsed = used;
  return dropped;
}

bool GradeTable::sameAs(const GradeTable& other) const {
  if (total != other.total) {
    return false;
  }
  for (uint16_t i = 0; i < total; ++i) {
    const Entry& a = entries[i];
    const Entry& b = other.entries[i];
    if (a.idLength != b.idLength || a.grade != b.grade || a.at != b.at ||
        memcmp(pool + a.idOffset, other.pool + b.idOffset, a.idLength) != 0) {
      return false;
    }
  }
  return true;
}

}  // namespace checkpoint
