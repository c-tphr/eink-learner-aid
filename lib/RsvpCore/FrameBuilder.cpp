#include "FrameBuilder.h"

#include <cstring>

#include "FocusLetter.h"
#include "RsvpText.h"

namespace rsvp {

void FrameBuilder::setWordsPerFrame(const uint8_t count) {
  maxWords = count < 1 ? 1 : (count > MAX_WORDS_PER_FRAME ? MAX_WORDS_PER_FRAME : count);
}

bool FrameBuilder::next(Frame& out) {
  Token token;
  if (!source.peek(token)) return false;
  out = Frame{};

  while (true) {
    const uint8_t slot = out.wordCount;
    // Words passed over on purpose are not adjacent text: the group ends before them.
    if (slot > 0 && token.skippedBefore != 0) break;
    const size_t keep = text::truncateToCodePoint(token.text, MAX_WORD_BYTES);
    const std::string_view word = token.text.substr(0, keep);
    const size_t offset = slot == 0 ? 0 : out.textLength + 1;
    if (offset + word.size() > FRAME_TEXT_CAPACITY) break;

    // Append tentatively; the committed length only moves if the group still fits.
    if (slot > 0) out.text[out.textLength] = ' ';
    std::memcpy(out.text + offset, word.data(), word.size());
    const auto candidateLength = static_cast<uint16_t>(offset + word.size());
    out.wordOffset[slot] = static_cast<uint16_t>(offset);
    out.wordLength[slot] = static_cast<uint16_t>(word.size());

    const uint8_t focusSlot = slot / 2;  // middle word of slot + 1 words, rounded left
    const FocusLetter focus = focusLetter(out.word(focusSlot));
    const auto focusOffset = static_cast<uint16_t>(out.wordOffset[focusSlot] + focus.byteOffset);
    const bool fit = fits == nullptr || fits(fitsContext, std::string_view(out.text, candidateLength), focusOffset);
    if (!fit && slot > 0) {
      out.text[out.textLength] = '\0';
      break;
    }

    out.textLength = candidateLength;
    out.text[candidateLength] = '\0';
    out.wordCount = static_cast<uint8_t>(slot + 1);
    out.focusWord = focusSlot;
    out.focusByteOffset = focusOffset;
    out.focusByteLength = focus.byteLength;
    if (slot == 0) {
      out.firstWordIndex = token.wordIndex;
      out.skippedBefore = token.skippedBefore;
      out.overWide = !fit;
    }
    out.lastWordIndex = token.wordIndex;
    out.truncated = out.truncated || keep < token.text.size();

    uint8_t flags = token.flags;
    source.advance();
    const bool hasNext = source.peek(token);
    if (!hasNext) flags |= TOKEN_CHAPTER_END;
    out.lastWord = classifyWord(out.word(slot), flags, hasNext ? token.text : std::string_view());
    out.holdMs += pacer.intervalMs(rawWeight(out.lastWord));

    if (!hasNext || out.lastWord.isBoundary() || out.overWide || out.wordCount >= maxWords) break;
  }

  const uint8_t flags = out.lastWord.tokenFlags;
  if (flags & TOKEN_CHECKPOINT) out.stops |= STOP_CHECKPOINT;
  if (flags & TOKEN_NON_PROSE_FOLLOWS) out.stops |= STOP_NON_PROSE;
  if (flags & TOKEN_CHAPTER_END) out.stops |= STOP_CHAPTER_END;
  return true;
}

}  // namespace rsvp
