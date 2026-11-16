#include "RsvpText.h"

namespace rsvp::text {

namespace {

struct Range {
  uint32_t first;
  uint32_t last;
};

// Code points at or above U+00C0 that are not letters. Sorted, non-overlapping.
constexpr Range NON_LETTER_RANGES[] = {
    {0x00D7, 0x00D7},    // multiplication sign
    {0x00F7, 0x00F7},    // division sign
    {0x02B0, 0x036F},    // spacing modifier letters, combining diacritical marks
    {0x0483, 0x0489},    // Cyrillic combining marks
    {0x1AB0, 0x1AFF},    // combining diacritical marks extended
    {0x1DC0, 0x1DFF},    // combining diacritical marks supplement
    {0x2000, 0x2BFF},    // general punctuation, super/subscripts, currency, symbols, arrows, shapes
    {0x2E00, 0x2E7F},    // supplemental punctuation
    {0x3000, 0x303F},    // CJK symbols and punctuation
    {0xD800, 0xF8FF},    // surrogates, private use
    {0xFE00, 0xFE6F},    // variation selectors, vertical forms, combining half marks, small forms
    {0xFEFF, 0xFEFF},    // zero width no-break space
    {0xFF00, 0xFF0F},    // fullwidth punctuation
    {0xFF1A, 0xFF20},    // fullwidth punctuation
    {0xFF3B, 0xFF40},    // fullwidth punctuation
    {0xFF5B, 0xFF65},    // fullwidth punctuation
    {0xFFF0, 0xFFFF},    // specials, including U+FFFD
    {0x1F000, 0x1FAFF},  // emoji and pictographs
    {0xE0000, 0xE007F},  // tags
};

constexpr Range COMBINING_RANGES[] = {
    {0x0300, 0x036F}, {0x0483, 0x0489}, {0x1AB0, 0x1AFF}, {0x1DC0, 0x1DFF}, {0x20D0, 0x20FF}, {0xFE20, 0xFE2F},
};

bool inRanges(const uint32_t cp, const Range* ranges, const size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (cp < ranges[i].first) return false;
    if (cp <= ranges[i].last) return true;
  }
  return false;
}

// Zero-width and invisible characters that may sit next to punctuation (Standard Ebooks puts
// U+2060 WORD JOINER before every em dash, for example).
bool isInvisibleFormat(const uint32_t cp) {
  return cp == 0x00AD || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x2060 && cp <= 0x2064) || cp == 0xFEFF;
}

}  // namespace

uint32_t decodeAt(const std::string_view s, const size_t pos, uint8_t& length) {
  const auto b0 = static_cast<uint8_t>(s[pos]);
  length = 1;
  if (b0 < 0x80) return b0;

  uint8_t extra;
  uint32_t cp;
  uint32_t minimum;
  if ((b0 & 0xE0) == 0xC0) {
    extra = 1;
    cp = b0 & 0x1F;
    minimum = 0x80;
  } else if ((b0 & 0xF0) == 0xE0) {
    extra = 2;
    cp = b0 & 0x0F;
    minimum = 0x800;
  } else if ((b0 & 0xF8) == 0xF0) {
    extra = 3;
    cp = b0 & 0x07;
    minimum = 0x10000;
  } else {
    return REPLACEMENT;
  }
  if (pos + extra >= s.size()) return REPLACEMENT;
  for (uint8_t i = 1; i <= extra; ++i) {
    const auto b = static_cast<uint8_t>(s[pos + i]);
    if ((b & 0xC0) != 0x80) return REPLACEMENT;
    cp = (cp << 6) | (b & 0x3F);
  }
  if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return REPLACEMENT;
  length = static_cast<uint8_t>(extra + 1);
  return cp;
}

size_t previousCodePointStart(const std::string_view s, const size_t end) {
  size_t start = end - 1;
  // A UTF-8 sequence has at most 3 continuation bytes.
  for (int i = 0; i < 3 && start > 0 && (static_cast<uint8_t>(s[start]) & 0xC0) == 0x80; ++i) --start;
  uint8_t length;
  decodeAt(s, start, length);
  // If the bytes do not form one sequence ending at `end`, step back a single byte.
  return start + length == end ? start : end - 1;
}

size_t truncateToCodePoint(const std::string_view s, const size_t maxBytes) {
  if (s.size() <= maxBytes) return s.size();
  size_t cut = maxBytes;
  while (cut > 0 && (static_cast<uint8_t>(s[cut]) & 0xC0) == 0x80) --cut;
  return cut;
}

bool isLetter(const uint32_t cp) {
  if (cp < 0x80) return (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
  if (cp < 0xC0) return false;
  return !inRanges(cp, NON_LETTER_RANGES, sizeof(NON_LETTER_RANGES) / sizeof(NON_LETTER_RANGES[0]));
}

bool isCombining(const uint32_t cp) {
  return inRanges(cp, COMBINING_RANGES, sizeof(COMBINING_RANGES) / sizeof(COMBINING_RANGES[0]));
}

bool isClosingPunct(const uint32_t cp) {
  switch (cp) {
    case '"':
    case '\'':
    case ')':
    case ']':
    case '}':
    case 0x2019:  // ’
    case 0x201D:  // ”
    case 0x00BB:  // »
    case 0x203A:  // ›
      return true;
    default:
      return isInvisibleFormat(cp);
  }
}

bool isOpeningPunct(const uint32_t cp) {
  switch (cp) {
    case '"':
    case '\'':
    case '(':
    case '[':
    case '{':
    case 0x2018:  // ‘
    case 0x201C:  // “
    case 0x00AB:  // «
    case 0x2039:  // ‹
      return true;
    default:
      return isInvisibleFormat(cp);
  }
}

bool startsSentence(const uint32_t cp) {
  return (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9') || (cp >= 0x00C0 && cp <= 0x00DE && cp != 0x00D7);
}

}  // namespace rsvp::text
