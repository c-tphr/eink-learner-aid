#pragma once

// Small UTF-8 and character-class helpers shared by the RsvpCore modules.
// Locale-free and allocation-free, so results are the same on the host and the device.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rsvp::text {

inline constexpr uint32_t REPLACEMENT = 0xFFFD;

// Decodes the code point that starts at byte `pos`. Malformed or truncated sequences decode as
// U+FFFD with a length of 1 byte, so a scan always moves forward. `pos` must be < s.size().
uint32_t decodeAt(std::string_view s, size_t pos, uint8_t& length);

// Byte offset of the code point that ends just before byte `end` (end > 0).
size_t previousCodePointStart(std::string_view s, size_t end);

// Largest prefix length <= maxBytes that does not split a UTF-8 sequence.
size_t truncateToCodePoint(std::string_view s, size_t maxBytes);

// A "letter" for RSVP purposes: a letter or digit of any script. ASCII punctuation, Latin-1
// symbols, general punctuation (quotes, dashes, ellipsis, invisible format characters such as
// U+2060), symbols, emoji and combining marks are not letters.
bool isLetter(uint32_t cp);

// Combining diacritical marks that attach to the preceding letter.
bool isCombining(uint32_t cp);

// Punctuation stripped from the end of a word before its terminal punctuation is read:
// " ' ) ] } ’ ” » › and invisible format characters.
bool isClosingPunct(uint32_t cp);

// Punctuation skipped at the start of a word before its first letter is read:
// " ' ( [ { ‘ “ « ‹ and invisible format characters.
bool isOpeningPunct(uint32_t cp);

// True when a word starting with `cp` can start a new sentence: A-Z, 0-9 or a Latin-1 capital.
bool startsSentence(uint32_t cp);

}  // namespace rsvp::text
