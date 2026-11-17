#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include "WordBand.h"

namespace rsvpui {

namespace {

int16_t clamp16(const int v) { return static_cast<int16_t>(v < -32768 ? -32768 : (v > 32767 ? 32767 : v)); }

// One line, vertically centred in the band.
void singleLine(const BandGeometry& band, const uint8_t font, const std::string_view text, const int x,
                BandLayout& out) {
  out.font = font;
  out.lineCount = 1;
  out.lines[0].offset = 0;
  out.lines[0].length = static_cast<uint16_t>(text.size());
  out.lines[0].x = clamp16(x);
  out.lines[0].y = clamp16(band.top + (band.height - band.lineHeight[font]) / 2);
}

}  // namespace

uint16_t codePointBytes(const std::string_view text, const uint16_t offset) {
  if (offset >= text.size()) return 0;
  const auto lead = static_cast<uint8_t>(text[offset]);
  uint16_t n = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
  if (offset + n > text.size()) n = static_cast<uint16_t>(text.size() - offset);
  return n;
}

bool fitsOnColumn(const BandGeometry& band, const MeasureFn measure, void* context, const uint8_t font,
                  const std::string_view text, const uint16_t focusOffset, const uint16_t focusLength) {
  const int prefix = measure(context, font, text.substr(0, focusOffset));
  const int letter = measure(context, font, text.substr(focusOffset, focusLength));
  const int left = focusLeft(band.focusX, prefix, letter);
  const int width = measure(context, font, text);
  return left >= band.left && left + width <= band.right;
}

BandLayout layoutBand(const BandGeometry& band, const MeasureFn measure, void* context, const std::string_view text,
                      const uint16_t focusOffset, const uint16_t focusLength) {
  BandLayout out;
  for (uint8_t font = 0; font < BAND_FONT_COUNT; ++font) {
    if (fitsOnColumn(band, measure, context, font, text, focusOffset, focusLength)) {
      const int prefix = measure(context, font, text.substr(0, focusOffset));
      const int letter = measure(context, font, text.substr(focusOffset, focusLength));
      singleLine(band, font, text, focusLeft(band.focusX, prefix, letter), out);
      out.onColumn = true;
      return out;
    }
  }

  const uint8_t last = BAND_FONT_COUNT - 1;
  const int bandWidth = band.right - band.left;
  const int width = measure(context, last, text);
  if (width <= bandWidth) {
    // As close to the column as the band allows.
    const int prefix = measure(context, last, text.substr(0, focusOffset));
    const int letter = measure(context, last, text.substr(focusOffset, focusLength));
    int x = focusLeft(band.focusX, prefix, letter);
    if (x < band.left) x = band.left;
    if (x + width > band.right) x = band.right - width;
    singleLine(band, last, text, x, out);
    return out;
  }

  // Split into lines of the band's width, each at least one code point.
  out.font = last;
  const int lineHeight = band.lineHeight[last];
  uint16_t start = 0;
  while (start < text.size() && out.lineCount < BAND_MAX_LINES) {
    uint16_t end = start + codePointBytes(text, start);
    while (end < text.size()) {
      const uint16_t next = end + codePointBytes(text, end);
      if (measure(context, last, text.substr(start, next - start)) > bandWidth) break;
      end = next;
    }
    // The last line takes whatever is left, so nothing is dropped.
    if (out.lineCount == BAND_MAX_LINES - 1) end = static_cast<uint16_t>(text.size());
    BandLine& line = out.lines[out.lineCount++];
    line.offset = start;
    line.length = end - start;
    line.x = band.left;
    start = end;
  }
  int y = band.top + (band.height - out.lineCount * lineHeight) / 2;
  if (y < band.top) y = band.top;
  for (uint8_t i = 0; i < out.lineCount; ++i) out.lines[i].y = clamp16(y + i * lineHeight);
  return out;
}

}  // namespace rsvpui

#endif  // CROSSPOINT_RSVP_ENABLED
