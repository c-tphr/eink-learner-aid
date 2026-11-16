#pragma once

// Groups words into frames: what one display update shows, how long it should ideally be held,
// and what the scheduler must do after it. No clock, display or font code here.

#include <cstdint>
#include <string_view>

#include "Pacing.h"
#include "WordClassifier.h"
#include "WordSource.h"

namespace rsvp {

inline constexpr uint8_t MAX_WORDS_PER_FRAME = 4;
// Joined frame text capacity in bytes (words separated by one ASCII space). A word that would
// overflow it ends the group like a width limit; a single word always fits.
inline constexpr uint16_t FRAME_TEXT_CAPACITY = 256;

// What the scheduler must act on after showing a frame (bits).
enum StopReason : uint8_t {
  STOP_CHECKPOINT = 1 << 0,   // a section end that carries a checkpoint follows the frame
  STOP_NON_PROSE = 1 << 1,    // an image, table or code block follows the frame
  STOP_CHAPTER_END = 1 << 2,  // the frame holds the chapter's last word
};

struct Frame {
  char text[FRAME_TEXT_CAPACITY + 1] = {};  // NUL-terminated UTF-8, words joined by one space
  uint16_t textLength = 0;
  uint8_t wordCount = 0;
  uint16_t wordOffset[MAX_WORDS_PER_FRAME] = {};  // byte offset of each word in `text`
  uint16_t wordLength[MAX_WORDS_PER_FRAME] = {};  // byte length of each word
  uint32_t firstWordIndex = 0;                    // chapter word index of the first word
  uint32_t lastWordIndex = 0;                     // chapter word index of the last word
  uint32_t skippedBefore = 0;                     // the first word's Token::skippedBefore

  // Focus: the focus letter of the middle word, word (wordCount - 1) / 2, i.e. the first word of
  // a pair. Offsets are into `text`.
  uint8_t focusWord = 0;
  uint16_t focusByteOffset = 0;
  uint16_t focusByteLength = 0;

  uint32_t holdMs = 0;     // ideal hold: the sum of the member words' paced intervals
  WordClass lastWord;      // classification of the last word (sentence, paragraph, section ends)
  uint8_t stops = 0;       // StopReason bits
  bool overWide = false;   // a single word that did not fit the width; shown alone anyway
  bool truncated = false;  // a word was longer than MAX_WORD_BYTES and was cut

  std::string_view view() const { return {text, textLength}; }
  std::string_view word(uint8_t slot) const { return {text + wordOffset[slot], wordLength[slot]}; }
};

// Width check supplied by the UI: true when `text` fits the word band with the focus letter at
// byte `focusByteOffset` placed on the focus column. Called once per candidate group.
using FitsFn = bool (*)(void* context, std::string_view text, uint16_t focusByteOffset);

class FrameBuilder {
 public:
  // `fits` may be null, meaning every group fits. The source must outlive the builder.
  explicit FrameBuilder(WordSource& wordSource, FitsFn fitsFn = nullptr, void* context = nullptr)
      : source(wordSource), fits(fitsFn), fitsContext(context) {}

  // Words per frame, clamped to 1..MAX_WORDS_PER_FRAME. Takes effect from the next frame.
  void setWordsPerFrame(uint8_t count);
  uint8_t wordsPerFrame() const { return maxWords; }

  // Target WPM; clears the pacing remainder.
  void setWpm(uint16_t wpm) { pacer.setWpm(wpm); }
  uint16_t wpm() const { return pacer.wpm(); }

  // Call after repositioning the source: clears the pacing remainder.
  void reset() { pacer.reset(); }

  // Builds the next frame from the source. Returns false when the chapter has no more words.
  bool next(Frame& out);

 private:
  WordSource& source;
  FitsFn fits;
  void* fitsContext;
  Pacer pacer;
  uint8_t maxWords = 1;
};

}  // namespace rsvp
