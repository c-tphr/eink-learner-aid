#include "FocusLetter.h"

#include "RsvpText.h"
#include "WordClassifier.h"
#include "WordSource.h"

namespace rsvp {

uint8_t focusOrdinal(const uint8_t letters) {
  uint8_t ordinal = 0;
  for (const FocusStep& step : FOCUS_TABLE) {
    if (letters >= step.minLetters) ordinal = step.letter;
  }
  return ordinal;
}

FocusLetter focusLetter(std::string_view word) {
  word = word.substr(0, text::truncateToCodePoint(word, MAX_WORD_BYTES));
  FocusLetter result;
  if (word.empty()) return result;

  const uint8_t target = focusOrdinal(countLetters(word));
  uint8_t seen = 0;
  bool found = false;
  uint16_t codePoint = 0;
  size_t pos = 0;
  while (pos < word.size()) {
    uint8_t length;
    const uint32_t cp = text::decodeAt(word, pos, length);
    if (text::isLetter(cp)) {
      if (seen == target) {
        result.codePoint = codePoint;
        result.byteOffset = static_cast<uint16_t>(pos);
        found = true;
        break;
      }
      ++seen;
    }
    pos += length;
    ++codePoint;
  }

  if (!found) {
    // No letter at all: the first code point.
    pos = 0;
  }
  uint8_t length;
  text::decodeAt(word, pos, length);
  size_t end = pos + length;
  while (end < word.size()) {
    uint8_t markLength;
    if (!text::isCombining(text::decodeAt(word, end, markLength))) break;
    end += markLength;
  }
  result.byteLength = static_cast<uint16_t>(end - pos);
  return result;
}

}  // namespace rsvp
