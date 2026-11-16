#include "WordClassifier.h"

#include "RsvpText.h"

namespace rsvp {

namespace {

// Abbreviations that never end a sentence: lower case, without their final full stop, matched
// case-insensitively after leading and trailing quotes and brackets are removed. Same list as the
// WordIndex word rules (docs/spikes/S1-word-stream.md, section 5). "etc." is deliberately absent:
// it ends a sentence when the next word starts with a capital, like any other full stop.
constexpr std::string_view ABBREVIATIONS[] = {
    "mr", "mrs", "ms", "dr", "st", "jr",  "sr",  "prof", "rev", "gen", "col", "capt",
    "lt", "sgt", "mt", "no", "vs", "e.g", "i.e", "cf",   "fig", "vol", "pp",  "ch",
};

constexpr size_t MAX_ABBREVIATION_BYTES = 4;

bool isTerminal(const uint32_t cp) { return cp == '.' || cp == '!' || cp == '?' || cp == 0x2026; }

bool isDash(const uint32_t cp) { return cp == 0x2013 || cp == 0x2014 || cp == 0x2015; }

bool isClauseMark(const uint32_t cp) { return cp == ',' || cp == ';' || cp == ':' || cp == '-' || isDash(cp); }

// Byte range of `word` without leading opening and trailing closing punctuation.
void trimQuotes(const std::string_view word, size_t& begin, size_t& end) {
  begin = 0;
  end = word.size();
  while (begin < end) {
    uint8_t length;
    if (!text::isOpeningPunct(text::decodeAt(word, begin, length))) break;
    begin += length;
  }
  while (end > begin) {
    const size_t start = text::previousCodePointStart(word, end);
    uint8_t length;
    if (start < begin || !text::isClosingPunct(text::decodeAt(word, start, length))) break;
    end = start;
  }
}

bool nextStartsSentence(const std::string_view next) {
  size_t pos = 0;
  while (pos < next.size()) {
    uint8_t length;
    const uint32_t cp = text::decodeAt(next, pos, length);
    if (!text::isOpeningPunct(cp)) return text::startsSentence(cp);
    pos += length;
  }
  return false;
}

// A dash inside the word ("alone—two", "now--then") marks a pause even though the word goes on.
bool hasInnerDash(const std::string_view word) {
  size_t pos = 0;
  while (pos < word.size()) {
    uint8_t length;
    const uint32_t cp = text::decodeAt(word, pos, length);
    if (isDash(cp)) return true;
    if (cp == '-' && pos + 1 < word.size() && word[pos + 1] == '-') return true;
    pos += length;
  }
  return false;
}

}  // namespace

uint8_t countLetters(const std::string_view word) {
  unsigned count = 0;
  size_t pos = 0;
  while (pos < word.size()) {
    uint8_t length;
    if (text::isLetter(text::decodeAt(word, pos, length)) && count < 255) ++count;
    pos += length;
  }
  return static_cast<uint8_t>(count);
}

bool isAbbreviation(const std::string_view word) {
  size_t begin;
  size_t end;
  trimQuotes(word, begin, end);
  if (end <= begin || word[end - 1] != '.') return false;
  const std::string_view core = word.substr(begin, end - 1 - begin);
  if (core.empty()) return false;

  uint8_t length;
  const uint32_t first = text::decodeAt(core, 0, length);
  if (length == core.size() && text::isLetter(first) && !(first >= '0' && first <= '9')) return true;

  if (core.size() > MAX_ABBREVIATION_BYTES) return false;
  char lower[MAX_ABBREVIATION_BYTES];
  for (size_t i = 0; i < core.size(); ++i) {
    const char ch = core[i];
    lower[i] = (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
  }
  const std::string_view folded(lower, core.size());
  for (const std::string_view abbreviation : ABBREVIATIONS) {
    if (folded == abbreviation) return true;
  }
  return false;
}

WordClass classifyWord(const std::string_view word, const uint8_t tokenFlags, const std::string_view nextWord) {
  WordClass result;
  result.tokenFlags = tokenFlags;
  if (tokenFlags & TOKEN_CHECKPOINT) result.tokenFlags |= TOKEN_SECTION_END;
  result.letters = countLetters(word);

  // Trailing quotes and brackets do not hide the punctuation inside them: `weather."` ends in '.'.
  size_t end = word.size();
  while (end > 0) {
    const size_t start = text::previousCodePointStart(word, end);
    uint8_t length;
    if (!text::isClosingPunct(text::decodeAt(word, start, length))) break;
    end = start;
  }
  if (end == 0) return result;

  uint8_t length;
  const uint32_t last = text::decodeAt(word, text::previousCodePointStart(word, end), length);
  if (isTerminal(last)) {
    if (last == '.' && isAbbreviation(word)) {
      result.pause = Pause::NONE;
    } else if (result.endsBlock() || nextStartsSentence(nextWord)) {
      result.pause = Pause::SENTENCE;
    } else {
      result.pause = Pause::CLAUSE;
    }
  } else if (isClauseMark(last) || hasInnerDash(word)) {
    result.pause = Pause::CLAUSE;
  }
  return result;
}

void classifyWords(const Token* tokens, const size_t count, const std::string_view wordAfter, WordClass* out) {
  for (size_t i = 0; i < count; ++i) {
    const std::string_view next = (i + 1 < count) ? tokens[i + 1].text : wordAfter;
    out[i] = classifyWord(tokens[i].text, tokens[i].flags, next);
  }
}

}  // namespace rsvp
