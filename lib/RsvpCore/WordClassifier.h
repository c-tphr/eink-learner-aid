#pragma once

// Per-word classification: the pause a word ends with, structural breaks after it, and its
// letter count. Pacing weights, grouping and sentence stepping all read this one result.
// English rules only; see README.md, "Classification".

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "WordSource.h"

namespace rsvp {

// The pause the word's own punctuation asks for. Structural breaks (paragraph, section,
// non-prose, chapter end) are kept separately in WordClass::tokenFlags.
enum class Pause : uint8_t {
  NONE,
  CLAUSE,    // trailing , ; : or a dash, a dash inside the word, or . ! ? … that does not end the sentence
  SENTENCE,  // terminal . ! ? … that ends the sentence
};

struct WordClass {
  Pause pause = Pause::NONE;
  uint8_t tokenFlags = 0;  // TokenFlag bits copied from the token
  uint8_t letters = 0;     // letters and digits in the word, capped at 255

  bool endsSentence() const { return pause == Pause::SENTENCE; }
  // A paragraph end, or a section end, non-prose marker or chapter end (which also end a block).
  bool endsBlock() const {
    return (tokenFlags & (TOKEN_PARAGRAPH_END | TOKEN_SECTION_END | TOKEN_CHECKPOINT | TOKEN_NON_PROSE_FOLLOWS |
                          TOKEN_CHAPTER_END)) != 0;
  }
  // Nothing after this word may share its frame, and the next word starts a sentence.
  bool isBoundary() const { return endsSentence() || endsBlock(); }
};

// Classifies `word`. `tokenFlags` are the source's TokenFlag bits for this word, and `nextWord`
// is the text of the following word in the chapter (empty when unknown or at the chapter end).
WordClass classifyWord(std::string_view word, uint8_t tokenFlags, std::string_view nextWord);

// Classifies `count` consecutive tokens; `wordAfter` is the text of the token after the last one
// (empty when unknown). Writes one WordClass per token to `out`.
void classifyWords(const Token* tokens, size_t count, std::string_view wordAfter, WordClass* out);

// Letters and digits in `word` (combining marks and punctuation excluded), capped at 255.
uint8_t countLetters(std::string_view word);

// True when `word` (punctuation included, e.g. "(Dr.") is in the abbreviation table or is a
// single letter followed by a full stop. Such a word never ends a sentence.
bool isAbbreviation(std::string_view word);

}  // namespace rsvp
