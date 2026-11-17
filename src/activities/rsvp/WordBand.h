#pragma once

// Where the word band draws a frame (pure arithmetic, host-tested in test/rsvp_activity).
//
// Rule: the frame's focus letter is centred on the fixed focus column, in the band's first font
// (the largest built-in reader font). If the text does not fit between the band's margins there, the
// next smaller font is tried, down to the last one. If it still does not fit on the column, the text
// is shifted into the band (the focus letter leaves the column; the ticks stay) when it fits the
// band's width; otherwise it is split at code-point boundaries into lines of the band's width in the
// last font, left-aligned, and lines that do not fit the band's height are drawn below it. Nothing
// is ever dropped. Grouping uses only the first rule (fitsOnColumn with font 0).

#include <cstdint>
#include <string_view>

namespace rsvpui {

inline constexpr uint8_t BAND_FONT_COUNT = 4;
inline constexpr uint8_t BAND_MAX_LINES = 12;  // 255 bytes in the last font need at most 8

struct BandGeometry {
  int16_t left = 0;    // first usable x
  int16_t right = 0;   // one past the last usable x
  int16_t focusX = 0;  // the focus column
  int16_t top = 0;     // band top y
  int16_t height = 0;  // band height
  int16_t lineHeight[BAND_FONT_COUNT] = {};
};

// Width in pixels of `text` in band font `font` (0 = largest).
using MeasureFn = int (*)(void* context, uint8_t font, std::string_view text);

struct BandLine {
  uint16_t offset = 0;  // byte range of the text
  uint16_t length = 0;
  int16_t x = 0;
  int16_t y = 0;  // top of the line
};

struct BandLayout {
  uint8_t font = 0;
  bool onColumn = false;  // the focus letter sits on the focus column
  uint8_t lineCount = 0;
  BandLine lines[BAND_MAX_LINES];
};

// Left x that centres the focus letter (`prefixWidth` px after the text's start, `letterWidth` wide)
// on the focus column.
inline int focusLeft(const int focusX, const int prefixWidth, const int letterWidth) {
  return focusX - prefixWidth - letterWidth / 2;
}

// Bytes of the UTF-8 code point starting at `text[offset]` (1 for a stray continuation byte).
uint16_t codePointBytes(std::string_view text, uint16_t offset);

// True when `text` fits between the margins with its focus letter on the column, in `font`.
bool fitsOnColumn(const BandGeometry& band, MeasureFn measure, void* context, uint8_t font, std::string_view text,
                  uint16_t focusOffset, uint16_t focusLength);

BandLayout layoutBand(const BandGeometry& band, MeasureFn measure, void* context, std::string_view text,
                      uint16_t focusOffset, uint16_t focusLength);

}  // namespace rsvpui
