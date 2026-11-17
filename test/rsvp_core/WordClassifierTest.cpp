#include <gtest/gtest.h>

#include <string>

#include "lib/RsvpCore/WordClassifier.h"

using rsvp::classifyWord;
using rsvp::Pause;
using rsvp::WordClass;

namespace {

Pause pauseOf(const std::string& word, const std::string& next = "The", uint8_t flags = 0) {
  return classifyWord(word, flags, next).pause;
}

}  // namespace

TEST(WordClassifier, PlainWordHasNoPause) {
  EXPECT_EQ(pauseOf("lamp", "is"), Pause::NONE);
  EXPECT_EQ(pauseOf("lamp", "The"), Pause::NONE);
  EXPECT_EQ(pauseOf("sea-worn", "boat"), Pause::NONE);
  EXPECT_EQ(pauseOf("doctor's", "arms"), Pause::NONE);
  EXPECT_EQ(pauseOf("3.5", "knots"), Pause::NONE);
}

TEST(WordClassifier, ClausePunctuation) {
  EXPECT_EQ(pauseOf("Hartwell,", "the"), Pause::CLAUSE);
  EXPECT_EQ(pauseOf("seconds;", "no"), Pause::CLAUSE);
  EXPECT_EQ(pauseOf("it:", "phosphorescence,"), Pause::CLAUSE);
  EXPECT_EQ(pauseOf("forgotten\xE2\x80\x94", "which"), Pause::CLAUSE);  // trailing em dash
  EXPECT_EQ(pauseOf("1914\xE2\x80\x93", "18"), Pause::CLAUSE);          // trailing en dash
  EXPECT_EQ(pauseOf("\xE2\x80\x94", "and"), Pause::CLAUSE);             // a lone em dash token
  EXPECT_EQ(pauseOf("well--", "you"), Pause::CLAUSE);
  EXPECT_EQ(pauseOf("now-", "then"), Pause::CLAUSE);
  // A comma inside closing quotes still pauses.
  EXPECT_EQ(pauseOf("you,\xE2\x80\x9D", "they"), Pause::CLAUSE);
  EXPECT_EQ(pauseOf("said,\"", "and"), Pause::CLAUSE);
}

TEST(WordClassifier, DashInsideAWordIsAClausePause) {
  EXPECT_EQ(pauseOf("alone\xE2\x80\x94two", "blasts,"), Pause::CLAUSE);
  EXPECT_EQ(pauseOf("again\xE2\x80\x94until", "his"), Pause::CLAUSE);
  EXPECT_EQ(pauseOf("us\xE2\x81\xA0\xE2\x80\x94George,", "and"), Pause::CLAUSE);
  EXPECT_EQ(pauseOf("now--then", "we"), Pause::CLAUSE);
}

TEST(WordClassifier, TerminalPunctuationBeforeACapitalEndsTheSentence) {
  EXPECT_EQ(pauseOf("noon.", "It"), Pause::SENTENCE);
  EXPECT_EQ(pauseOf("winter?", "Ione"), Pause::SENTENCE);
  EXPECT_EQ(pauseOf("turns!", "By"), Pause::SENTENCE);
  EXPECT_EQ(pauseOf("1999.", "In"), Pause::SENTENCE);
  EXPECT_EQ(pauseOf("end.", "42"), Pause::SENTENCE);            // a digit starts a sentence
  EXPECT_EQ(pauseOf("end.", "\xC3\x89mile"), Pause::SENTENCE);  // Latin-1 capital
  EXPECT_EQ(pauseOf("end.", "\"The"), Pause::SENTENCE);         // opening quote skipped
  EXPECT_EQ(pauseOf("end.",
                    "\xE2\x80\x9C"
                    "Why"),
            Pause::SENTENCE);  // “Why
  EXPECT_EQ(pauseOf("end.", "(See"), Pause::SENTENCE);
}

TEST(WordClassifier, TerminalPunctuationBeforeLowerCaseIsOnlyAClausePause) {
  // "Seaside!" said my brother-in-law: the sentence goes on.
  EXPECT_EQ(pauseOf("\xE2\x80\x9CSeaside!\xE2\x80\x9D", "said"), Pause::CLAUSE);
  EXPECT_EQ(pauseOf("Why?", "he"), Pause::CLAUSE);
  EXPECT_EQ(pauseOf("end.", "and"), Pause::CLAUSE);
  // No next word known and no block end: not a sentence end.
  EXPECT_EQ(pauseOf("end.", ""), Pause::CLAUSE);
}

TEST(WordClassifier, ClosingQuotesAndBracketsAfterTerminalPunctuation) {
  EXPECT_EQ(pauseOf("weather.\"", "Then"), Pause::SENTENCE);
  EXPECT_EQ(pauseOf("weather.'", "Then"), Pause::SENTENCE);
  EXPECT_EQ(pauseOf("weather.\xE2\x80\x9D", "Then"), Pause::SENTENCE);  // .”
  EXPECT_EQ(pauseOf("weather.\xE2\x80\x99", "Then"), Pause::SENTENCE);  // .’
  EXPECT_EQ(pauseOf("above.)", "Then"), Pause::SENTENCE);
  EXPECT_EQ(pauseOf("above.]", "Then"), Pause::SENTENCE);
  EXPECT_EQ(pauseOf("now?\")", "Then"), Pause::SENTENCE);
  EXPECT_EQ(pauseOf("now!\xC2\xBB", "Then"), Pause::SENTENCE);                      // !»
  EXPECT_EQ(pauseOf("pickles.\xE2\x80\x9D\xE2\x81\xA0", "Then"), Pause::SENTENCE);  // word joiner after the quote
}

TEST(WordClassifier, AbbreviationsNeverEndASentence) {
  const char* abbreviations[] = {"Dr.",  "Mr.",   "Mrs.", "Ms.",   "St.",  "vs.", "e.g.", "i.e.", "Prof.",
                                 "Rev.", "Capt.", "No.",  "(e.g.", "Fig.", "DR.", "MRS.", "cf.",  "Jr."};
  for (const char* word : abbreviations) {
    EXPECT_EQ(pauseOf(word, "Vane"), Pause::NONE) << word;
    EXPECT_TRUE(rsvp::isAbbreviation(word)) << word;
  }
  EXPECT_FALSE(rsvp::isAbbreviation("noon."));
  EXPECT_FALSE(rsvp::isAbbreviation("Dr"));
  EXPECT_FALSE(rsvp::isAbbreviation("etc."));
  EXPECT_FALSE(rsvp::isAbbreviation("..."));
  EXPECT_FALSE(rsvp::isAbbreviation("5."));
}

TEST(WordClassifier, SingleInitialsNeverEndASentence) {
  EXPECT_EQ(pauseOf("J.", "R."), Pause::NONE);
  EXPECT_EQ(pauseOf("R.", "Tolkien"), Pause::NONE);
  EXPECT_EQ(pauseOf("(H.", "G."), Pause::NONE);
  EXPECT_EQ(pauseOf("\xC3\x89.", "Zola"), Pause::NONE);  // É.
}

TEST(WordClassifier, EtceteraEndsASentenceOnlyBeforeACapital) {
  EXPECT_EQ(pauseOf("etc.", "and"), Pause::CLAUSE);
  EXPECT_EQ(pauseOf("etc.", "The"), Pause::SENTENCE);
  EXPECT_EQ(pauseOf("etc.,", "and"), Pause::CLAUSE);
}

TEST(WordClassifier, DecimalNumbersDoNotEndASentence) {
  EXPECT_EQ(pauseOf("3.5", "Knots"), Pause::NONE);
  EXPECT_EQ(pauseOf("0.25", "The"), Pause::NONE);
  EXPECT_EQ(pauseOf("$1.50", "Next"), Pause::NONE);
  EXPECT_EQ(pauseOf("3.5,", "and"), Pause::CLAUSE);
  EXPECT_EQ(pauseOf("3.5.", "It"), Pause::SENTENCE);  // a number at the end of a sentence
}

TEST(WordClassifier, Ellipsis) {
  EXPECT_EQ(pauseOf("then\xE2\x80\xA6", "nothing"), Pause::CLAUSE);  // trailing off mid-sentence
  EXPECT_EQ(pauseOf("then\xE2\x80\xA6", "Nothing"), Pause::SENTENCE);
  EXPECT_EQ(pauseOf("then...", "nothing"), Pause::CLAUSE);
  EXPECT_EQ(pauseOf("then...", "Nothing"), Pause::SENTENCE);
  EXPECT_EQ(pauseOf("...", "and"), Pause::CLAUSE);
  EXPECT_EQ(pauseOf("then\xE2\x80\xA6", "nothing", rsvp::TOKEN_PARAGRAPH_END), Pause::SENTENCE);
}

TEST(WordClassifier, BlockEndsCountAsSentenceEndsForTerminalPunctuation) {
  for (const uint8_t flag : {rsvp::TOKEN_PARAGRAPH_END, rsvp::TOKEN_SECTION_END, rsvp::TOKEN_CHECKPOINT,
                             rsvp::TOKEN_NON_PROSE_FOLLOWS, rsvp::TOKEN_CHAPTER_END}) {
    const WordClass c = classifyWord("lamp.", flag, "then");
    EXPECT_EQ(c.pause, Pause::SENTENCE) << int(flag);
    EXPECT_TRUE(c.endsBlock());
    EXPECT_TRUE(c.isBoundary());
  }
  // A heading has no punctuation but still ends its block.
  const WordClass heading = classifyWord("Room", rsvp::TOKEN_PARAGRAPH_END, "The");
  EXPECT_EQ(heading.pause, Pause::NONE);
  EXPECT_TRUE(heading.endsBlock());
  EXPECT_TRUE(heading.isBoundary());
  // An abbreviation at a paragraph end: no punctuation pause, but still a block end.
  const WordClass abbreviation = classifyWord("Dr.", rsvp::TOKEN_PARAGRAPH_END, "");
  EXPECT_EQ(abbreviation.pause, Pause::NONE);
  EXPECT_TRUE(abbreviation.isBoundary());
}

TEST(WordClassifier, CheckpointImpliesSectionEnd) {
  const WordClass c = classifyWord("same.", rsvp::TOKEN_CHECKPOINT, "Winding");
  EXPECT_NE(c.tokenFlags & rsvp::TOKEN_SECTION_END, 0);
}

TEST(WordClassifier, LetterCounts) {
  EXPECT_EQ(rsvp::countLetters("phosphorescence,"), 15);
  EXPECT_EQ(rsvp::countLetters("caf\xC3\xA9"), 4);
  EXPECT_EQ(rsvp::countLetters("ne\xCC\x81"
                               "e"),
            3);
  EXPECT_EQ(rsvp::countLetters("\"The"), 3);
  EXPECT_EQ(rsvp::countLetters("3.5"), 2);
  EXPECT_EQ(rsvp::countLetters("alone\xE2\x80\x94two"), 8);
  EXPECT_EQ(rsvp::countLetters("\xE2\x80\x94"), 0);
  EXPECT_EQ(rsvp::countLetters(std::string(300, 'a')), 255);
  EXPECT_EQ(classifyWord("extraordinary", 0, "fits").letters, 13);
}

TEST(WordClassifier, ClassifyWordsUsesTheFollowingToken) {
  const rsvp::Token tokens[] = {{"noon.", 0, 0}, {"It", 1, 0}, {"Dr.", 2, 0}, {"Vane", 3, 0}, {"end.", 4, 0}};
  WordClass out[5];
  rsvp::classifyWords(tokens, 5, "and", out);
  EXPECT_EQ(out[0].pause, Pause::SENTENCE);
  EXPECT_EQ(out[1].pause, Pause::NONE);
  EXPECT_EQ(out[2].pause, Pause::NONE);
  EXPECT_EQ(out[3].pause, Pause::NONE);
  EXPECT_EQ(out[4].pause, Pause::CLAUSE);  // the word after the window is "and"
}
