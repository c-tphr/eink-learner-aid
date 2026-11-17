#pragma once

// The focus letter ("optimal recognition point") of a word: the letter the UI places on the
// fixed focus column. Slightly left of the word's centre, chosen by letter count.

#include <cstdint>
#include <string_view>

namespace rsvp {

struct FocusStep {
  uint8_t minLetters;  // applies to words with at least this many letters
  uint8_t letter;      // 0-based ordinal of the focus letter among the word's letters
};

// The one focus table. Letters are letters and digits (see countLetters); punctuation, quotes
// and combining marks are not counted and are never chosen.
inline constexpr FocusStep FOCUS_TABLE[] = {
    {1, 0},   // 1 letter
    {2, 1},   // 2 to 5 letters
    {6, 2},   // 6 to 9 letters
    {10, 3},  // 10 to 13 letters
    {14, 4},  // 14 or more letters
};

// Focus letter ordinal for a word with `letters` letters (0 for a word with none).
uint8_t focusOrdinal(uint8_t letters);

struct FocusLetter {
  uint16_t codePoint = 0;   // index of the focus code point within the word, counted in code points
  uint16_t byteOffset = 0;  // byte offset of that code point within the word
  uint16_t byteLength = 0;  // bytes of the letter plus any combining marks that follow it
};

// Finds the focus letter of `word`. A word with no letter or digit at all ("—", "&") focuses on
// its first code point. Only the first MAX_WORD_BYTES bytes are considered. Empty word: all 0.
FocusLetter focusLetter(std::string_view word);

}  // namespace rsvp
