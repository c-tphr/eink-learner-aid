#include <gtest/gtest.h>

#include <string>

#include "lib/RsvpCore/FocusLetter.h"
#include "lib/RsvpCore/RsvpText.h"

using rsvp::focusLetter;
using rsvp::FocusLetter;

namespace {

void expectFocus(const std::string& word, uint16_t codePoint, uint16_t byteOffset, uint16_t byteLength) {
  const FocusLetter f = focusLetter(word);
  EXPECT_EQ(f.codePoint, codePoint) << word;
  EXPECT_EQ(f.byteOffset, byteOffset) << word;
  EXPECT_EQ(f.byteLength, byteLength) << word;
}

}  // namespace

TEST(FocusLetter, TableForLengthsOneToTwenty) {
  const std::string letters = "abcdefghijklmnopqrstuvwxyz";
  for (uint8_t n = 1; n <= 20; ++n) {
    const std::string word = letters.substr(0, n);
    uint16_t expected;
    if (n == 1) {
      expected = 0;
    } else if (n <= 5) {
      expected = 1;
    } else if (n <= 9) {
      expected = 2;
    } else if (n <= 13) {
      expected = 3;
    } else {
      expected = 4;
    }
    EXPECT_EQ(rsvp::focusOrdinal(n), expected) << "letters " << int(n);
    expectFocus(word, expected, expected, 1);
  }
  EXPECT_EQ(rsvp::focusOrdinal(0), 0);
  EXPECT_EQ(rsvp::focusOrdinal(255), 4);
}

TEST(FocusLetter, LeadingPunctuationIsSkipped) {
  expectFocus("\"The", 2, 2, 1);              // 'h'
  expectFocus("(I", 1, 1, 1);                 // 'I'
  expectFocus("...and", 4, 4, 1);             // 'n'
  expectFocus("\xE2\x80\x9CHello", 2, 4, 1);  // “Hello -> 'e'
  expectFocus(
      "(\xE2\x80\x9C"
      "Fog",
      3, 5, 1);  // (“Fog -> 'o'
}

TEST(FocusLetter, TrailingPunctuationIsIgnored) {
  expectFocus("weather.\"", 2, 2, 1);  // 7 letters -> 'a'
  expectFocus("winter?", 2, 2, 1);     // 6 letters -> 'n'
  expectFocus("it:", 1, 1, 1);         // 't'
  expectFocus("a,", 0, 0, 1);
  expectFocus("Hello,\xE2\x80\x9D)", 1, 1, 1);
  // 15 letters plus a comma: ordinal 4, not shifted by the comma.
  expectFocus("phosphorescence,", 4, 4, 1);
}

TEST(FocusLetter, InnerPunctuationIsNeverChosen) {
  expectFocus("3.5", 2, 2, 1);          // the '5', not the '.'
  expectFocus("doctor's", 2, 2, 1);     // 7 letters -> 'c'
  expectFocus("a-b", 2, 2, 1);          // 2 letters -> 'b'
  expectFocus("half-frozen", 3, 3, 1);  // 10 letters -> ordinal 3, the first 'f'
}

TEST(FocusLetter, MultiByteWords) {
  expectFocus("caf\xC3\xA9", 1, 1, 1);                                       // café -> 'a'
  expectFocus("r\xC3\xA9sum\xC3\xA9", 2, 3, 1);                              // résumé, 6 letters -> 's' (ordinal 2)
  expectFocus("na\xC3\xAFvet\xC3\xA9", 2, 2, 2);                             // naïveté, 7 letters -> 'ï' (2 bytes)
  expectFocus("\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82", 2, 4, 2);  // привет -> 'и'
  expectFocus("alone\xE2\x80\x94two", 2, 2, 1);                              // alone—two, 8 letters -> 'o'
  // Standard Ebooks style: word joiner U+2060 before an em dash.
  expectFocus("us\xE2\x81\xA0\xE2\x80\x94", 1, 1, 1);
}

TEST(FocusLetter, CombiningMarksStayWithTheirLetter) {
  // NFD "née": n, e, U+0301, e. Three letters -> ordinal 1, the first 'e' plus its accent.
  expectFocus(
      "ne\xCC\x81"
      "e",
      1, 1, 3);
  // NFD "naïve": the combining diaeresis is not a letter and not counted.
  expectFocus("nai\xCC\x88ve", 1, 1, 1);
  // A word starting with a stray combining mark still focuses on a letter.
  expectFocus(
      "\xCC\x81"
      "ab",
      2, 3, 1);
}

TEST(FocusLetter, NoLetterFallsBackToFirstCodePoint) {
  expectFocus("\xE2\x80\x94", 0, 0, 3);  // —
  expectFocus("&", 0, 0, 1);
  expectFocus("", 0, 0, 0);
}

TEST(FocusLetter, LongWordIsReadUpToTheByteLimit) {
  const std::string word(300, 'a');
  expectFocus(word, 4, 4, 1);
}

TEST(FocusLetter, AlwaysALetterWhenOneExists) {
  const char* words[] = {"\"The", "weather.\"", "Dr.", "3.5", "seconds;", "turns!",        "doctor's",
                         "it:",   "never",      "(a)", "--x", "...y...",  "\xE2\x80\x9CZ", "x\xE2\x80\x94"};
  for (const char* w : words) {
    const std::string word(w);
    const FocusLetter f = focusLetter(word);
    uint8_t length;
    const uint32_t cp = rsvp::text::decodeAt(word, f.byteOffset, length);
    EXPECT_TRUE(rsvp::text::isLetter(cp)) << word;
  }
}
