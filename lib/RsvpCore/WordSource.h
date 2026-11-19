#pragma once

// Input side of RsvpCore: one word of a chapter and the pull interface that supplies it.
// The source owns the text and decides what a word is (WordIndex, or a test vector).

#include <cstdint>
#include <string_view>

namespace rsvp {

// Longest word RsvpCore keeps, in UTF-8 bytes. Matches WordIndex, which cuts longer words at a
// code point boundary below 255 bytes and continues them as the next word. A longer token is
// truncated at a code point boundary and its frame is marked `truncated`.
inline constexpr size_t MAX_WORD_BYTES = 255;

// Context flags the source sets on a token. Each describes what comes right after the word.
enum TokenFlag : uint8_t {
  TOKEN_PARAGRAPH_END = 1 << 0,      // last word of a paragraph, heading or other text block
  TOKEN_SECTION_END = 1 << 1,        // a section end anchor follows this word
  TOKEN_CHECKPOINT = 1 << 2,         // that section end carries a checkpoint (implies TOKEN_SECTION_END)
  TOKEN_NON_PROSE_FOLLOWS = 1 << 3,  // an image, table or code block follows this word
  TOKEN_CHAPTER_END = 1 << 4,        // last word of the chapter (optional: peek() returning false also says so)
};

struct Token {
  // Whitespace-free UTF-8 word with its punctuation attached ("weather.\"", "Dr.", "3.5").
  // Footnote reference marks are already removed. Valid until the next call to the source.
  std::string_view text;
  // Index of the word within its chapter (0-based), as used by the position model.
  uint32_t wordIndex = 0;
  uint8_t flags = 0;  // TokenFlag bits
  // Word indexes right before this one that the source passed over on purpose without a stop
  // (a footnote body): wordIndex - skippedBefore follows the previous token. 0 for most sources.
  uint32_t skippedBefore = 0;
};

// Pull-style word supply for one chapter. RsvpCore needs one token of lookahead: it peeks the
// next word to classify the current one (sentence ends depend on the next word's first letter)
// and to decide whether that word joins the current group before consuming it.
class WordSource {
 public:
  virtual ~WordSource() = default;

  // Fills `out` with the next unconsumed token without consuming it. Repeated calls without
  // advance() return the same token. Returns false at the end of the chapter (or when the source
  // cannot read further; the source reports its own errors). `out.text` stays valid until the
  // next call on this source.
  virtual bool peek(Token& out) = 0;

  // Consumes the token returned by the last successful peek().
  virtual void advance() = 0;
};

}  // namespace rsvp
