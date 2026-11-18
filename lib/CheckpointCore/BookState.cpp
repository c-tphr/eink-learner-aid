#include "BookState.h"

#include <cstring>

namespace checkpoint {

namespace {

bool placeValid(const Place& p) {
  return p.position.chapter != NO_CHAPTER && (p.mode == ReadingMode::Rsvp || p.mode == ReadingMode::Page);
}

size_t textLength(const char* text, size_t limit) {
  size_t n = 0;
  while (n < limit && text[n] != '\0') {
    ++n;
  }
  return n;
}

}  // namespace

void BookState::reset() {
  bookSha256[0] = '\0';
  overrides = Overrides{};
  hasPosition = false;
  position = Place{};
  grades.clear();
  hasActive = false;
  active = ActiveCheckpoint{};
}

bool BookState::sameAs(const BookState& other) const {
  if (strcmp(bookSha256, other.bookSha256) != 0 || overrides.hasWpm != other.overrides.hasWpm ||
      overrides.hasWordsPerUpdate != other.overrides.hasWordsPerUpdate || hasPosition != other.hasPosition ||
      hasActive != other.hasActive || !grades.sameAs(other.grades)) {
    return false;
  }
  if ((overrides.hasWpm && overrides.wpm != other.overrides.wpm) ||
      (overrides.hasWordsPerUpdate && overrides.wordsPerUpdate != other.overrides.wordsPerUpdate) ||
      (hasPosition && position != other.position)) {
    return false;
  }
  if (!hasActive) {
    return true;
  }
  return strcmp(active.id, other.active.id) == 0 && active.promptIndex == other.active.promptIndex &&
         active.resume == other.active.resume && active.rereading == other.active.rereading &&
         (!active.rereading || active.reread == other.active.reread);
}

bool isSha256Hex(const char* text, size_t length) {
  if (text == nullptr || length != SHA256_HEX_CHARS) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    const char c = text[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

bool stateInvariantsHold(const BookState& state) {
  const size_t shaLength = textLength(state.bookSha256, sizeof(state.bookSha256));
  if (shaLength == sizeof(state.bookSha256) || (shaLength != 0 && !isSha256Hex(state.bookSha256, shaLength))) {
    return false;
  }
  if ((state.overrides.hasWpm && state.overrides.wpm == 0) ||
      (state.overrides.hasWordsPerUpdate && state.overrides.wordsPerUpdate == 0)) {
    return false;
  }
  if (state.hasPosition && !placeValid(state.position)) {
    return false;
  }
  const GradeTable& g = state.grades;
  if (g.count() > GRADE_CAPACITY) {
    return false;
  }
  for (uint16_t i = 0; i < g.count(); ++i) {
    if (!isValidId(g.idData(i), g.idLength(i)) || (g.grade(i) != Grade::Correct && g.grade(i) != Grade::Incorrect)) {
      return false;
    }
    char text[TIMESTAMP_CHARS + 1];
    if (g.at(i) != NO_TIMESTAMP && !formatTimestamp(g.at(i), text)) {
      return false;
    }
    for (uint16_t j = 0; j < i; ++j) {
      if (g.idLength(i) == g.idLength(j) && memcmp(g.idData(i), g.idData(j), g.idLength(i)) == 0) {
        return false;
      }
    }
  }
  if (state.hasActive) {
    const ActiveCheckpoint& a = state.active;
    if (!isValidId(a.id, textLength(a.id, sizeof(a.id))) || a.promptIndex >= MAX_PROMPTS_PER_CHECKPOINT ||
        !placeValid(a.resume) || (a.rereading && !placeValid(a.reread))) {
      return false;
    }
  }
  return true;
}

}  // namespace checkpoint
