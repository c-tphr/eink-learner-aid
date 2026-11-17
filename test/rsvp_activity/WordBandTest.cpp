// The word band's layout rule (src/activities/rsvp/WordBand.h) with synthetic glyph advances.

#include <gtest/gtest.h>

#include <string>

#include "activities/rsvp/WordBand.h"

using namespace rsvpui;

namespace {

// Font f advances 12 - 2f pixels per byte: 12, 10, 8, 6.
int fixedAdvance(void*, const uint8_t font, const std::string_view text) {
  return static_cast<int>(text.size()) * (12 - 2 * font);
}

// As fixedAdvance, but the letter 'W' is three times as wide.
int wideW(void*, const uint8_t font, const std::string_view text) {
  int width = 0;
  for (const char c : text) width += (c == 'W' ? 3 : 1) * (12 - 2 * font);
  return width;
}

BandGeometry band() {
  BandGeometry b;
  b.left = 16;
  b.right = 464;
  b.focusX = 192;
  b.top = 352;
  b.height = 96;
  b.lineHeight[0] = 40;
  b.lineHeight[1] = 34;
  b.lineHeight[2] = 28;
  b.lineHeight[3] = 22;
  return b;
}

std::string joined(const std::string_view text, const BandLayout& layout) {
  std::string out;
  for (uint8_t i = 0; i < layout.lineCount; ++i) out += text.substr(layout.lines[i].offset, layout.lines[i].length);
  return out;
}

}  // namespace

TEST(WordBand, FocusLetterCentredOnTheColumn) {
  const BandGeometry b = band();
  // "harbour": focus letter 'a' (byte 1). Prefix 12 px, letter 12 px: left = 192 - 12 - 6.
  const BandLayout l = layoutBand(b, fixedAdvance, nullptr, "harbour", 1, 1);
  EXPECT_TRUE(l.onColumn);
  EXPECT_EQ(l.font, 0);
  ASSERT_EQ(l.lineCount, 1);
  EXPECT_EQ(l.lines[0].x, 174);
  EXPECT_EQ(l.lines[0].y, 352 + (96 - 40) / 2);
  EXPECT_EQ(focusLeft(192, 12, 12) + 12 + 6, 192);
}

TEST(WordBand, FocusColumnStaysFixedAcrossWords) {
  const BandGeometry b = band();
  for (const auto& [word, focus] : {std::pair<std::string, uint16_t>{"a", 0}, {"the", 1}, {"lighthouse", 3},
                                    {"extraordinarily", 4}}) {
    const BandLayout l = layoutBand(b, fixedAdvance, nullptr, word, focus, 1);
    ASSERT_TRUE(l.onColumn) << word;
    const int letterCentre = l.lines[0].x + focus * 12 + 6;
    EXPECT_EQ(letterCentre, 192) << word;
  }
}

TEST(WordBand, FitsChecksBothSidesOfTheColumn) {
  const BandGeometry b = band();
  // Left side: 176 px before the column -> at most 14 bytes of 12 px before the focus letter's centre.
  EXPECT_TRUE(fitsOnColumn(b, fixedAdvance, nullptr, 0, std::string(14, 'x') + "y", 14, 1));
  EXPECT_FALSE(fitsOnColumn(b, fixedAdvance, nullptr, 0, std::string(15, 'x') + "y", 15, 1));
  // Right side: 272 px after the column.
  EXPECT_TRUE(fitsOnColumn(b, fixedAdvance, nullptr, 0, "y" + std::string(22, 'x'), 0, 1));
  EXPECT_FALSE(fitsOnColumn(b, fixedAdvance, nullptr, 0, "y" + std::string(23, 'x'), 0, 1));
}

TEST(WordBand, TooWideForTheLargestFontUsesASmallerOne) {
  const BandGeometry b = band();
  const std::string word = "y" + std::string(30, 'x');  // 372 px at 12 px; 310 at 10 px: still too wide; 248 at 8
  const BandLayout l = layoutBand(b, fixedAdvance, nullptr, word, 0, 1);
  EXPECT_TRUE(l.onColumn);
  EXPECT_EQ(l.font, 2);
  EXPECT_EQ(l.lines[0].x + 4, 192);
}

TEST(WordBand, OverWideWordIsShiftedIntoTheBandInTheSmallestFont) {
  const BandGeometry b = band();
  // 60 bytes: 360 px in the smallest font fits the 448 px band, but not with byte 4 on the column.
  const std::string word(60, 'x');
  const BandLayout l = layoutBand(b, fixedAdvance, nullptr, word, 4, 1);
  EXPECT_FALSE(l.onColumn);
  EXPECT_EQ(l.font, BAND_FONT_COUNT - 1);
  ASSERT_EQ(l.lineCount, 1);
  EXPECT_EQ(l.lines[0].x + 360, 464) << "right-aligned at the margin, as close to the column as possible";
}

TEST(WordBand, WordWiderThanTheBandIsSplitAndNothingIsDropped) {
  const BandGeometry b = band();
  // 255 bytes: 1530 px in the smallest font: four lines of at most 74 bytes (444 px).
  const std::string word(255, 'x');
  const BandLayout l = layoutBand(b, fixedAdvance, nullptr, word, 4, 1);
  EXPECT_FALSE(l.onColumn);
  ASSERT_EQ(l.lineCount, 4);
  EXPECT_EQ(joined(word, l), word);
  for (uint8_t i = 0; i < l.lineCount; ++i) {
    EXPECT_LE(l.lines[i].length * 6, 448);
    EXPECT_EQ(l.lines[i].x, 16);
    if (i > 0) {
      EXPECT_EQ(l.lines[i].y - l.lines[i - 1].y, 22);
    }
  }
}

TEST(WordBand, SplitNeverCutsACodePoint) {
  const BandGeometry b = band();
  std::string word;
  for (int i = 0; i < 100; ++i) word += "\xC3\xA9";  // é, two bytes each
  const BandLayout l = layoutBand(b, fixedAdvance, nullptr, word, 0, 2);
  EXPECT_EQ(joined(word, l), word);
  for (uint8_t i = 0; i < l.lineCount; ++i) {
    EXPECT_EQ(l.lines[i].offset % 2, 0) << "line " << int(i) << " starts inside a code point";
  }
}

TEST(WordBand, VeryWideGlyphsStillKeepEveryByte) {
  const BandGeometry b = band();
  const std::string word(255, 'W');  // 18 px each in the smallest font: 24 per line, 11 lines
  const BandLayout l = layoutBand(b, wideW, nullptr, word, 0, 1);
  EXPECT_EQ(joined(word, l), word);
  EXPECT_LE(l.lineCount, BAND_MAX_LINES);
}

TEST(WordBand, CodePointBytes) {
  EXPECT_EQ(codePointBytes("a", 0), 1);
  EXPECT_EQ(codePointBytes("\xC3\xA9", 0), 2);
  EXPECT_EQ(codePointBytes("\xE2\x80\x94", 0), 3);
  EXPECT_EQ(codePointBytes("\xF0\x9F\x98\x80", 0), 4);
  EXPECT_EQ(codePointBytes("\xE2\x80", 0), 2) << "truncated sequence";
  EXPECT_EQ(codePointBytes("ab", 2), 0);
}
