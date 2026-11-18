#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include "Passage.h"

namespace devicetest {

// Written for this project (no copied text). 52 + 58 + 40 words.
const char PASSAGE[] =
    "The ferry left the harbour at seven, just as the rain began to thin. On the upper deck, a few passengers "
    "stood by the rail and watched the town shrink into a grey line of roofs, cranes and chimneys. Nobody spoke "
    "much; the wind took most words away before they could land.\n"
    "\n"
    "Halfway across, the engines slowed. A small boat with a red sail had drifted into the channel, and its "
    "owner, an old man in a yellow coat, was rowing hard against the tide. The captain sounded the horn twice: "
    "once as a warning, and once, it seemed, as a greeting. The old man raised one oar and laughed.\n"
    "\n"
    "Would he reach the far shore before dark? Nobody on deck could say. By the time the ferry docked, the sail "
    "was only a bright speck behind us, steady and unhurried, as if the whole sea belonged to him alone.";

namespace {

constexpr size_t PASSAGE_LENGTH = sizeof(PASSAGE) - 1;

bool isSpace(const char c) { return c == ' ' || c == '\n'; }

}  // namespace

uint32_t countPassageWords() {
  uint32_t words = 0;
  bool inWord = false;
  for (size_t i = 0; i < PASSAGE_LENGTH; ++i) {
    const bool space = isSpace(PASSAGE[i]);
    if (!space && !inWord) ++words;
    inWord = !space;
  }
  return words;
}

void PassageSource::reset(const uint32_t limit) {
  wordLimit = limit;
  index = 0;
  offset = 0;
  located = false;
}

bool PassageSource::locate() {
  if (located) return true;
  if (index >= wordLimit) return false;
  size_t start = offset;
  while (start < PASSAGE_LENGTH && isSpace(PASSAGE[start])) ++start;
  if (start >= PASSAGE_LENGTH) {
    // Past the last word: the passage starts again, word indexes count on.
    start = 0;
    while (start < PASSAGE_LENGTH && isSpace(PASSAGE[start])) ++start;
  }
  size_t end = start;
  while (end < PASSAGE_LENGTH && !isSpace(PASSAGE[end])) ++end;

  // A paragraph ends at a blank line or at the end of the passage.
  size_t newlines = 0;
  size_t after = end;
  while (after < PASSAGE_LENGTH && isSpace(PASSAGE[after])) {
    if (PASSAGE[after] == '\n') ++newlines;
    ++after;
  }
  uint8_t flags = 0;
  if (newlines >= 2 || after >= PASSAGE_LENGTH) flags |= rsvp::TOKEN_PARAGRAPH_END;
  if (index + 1 == wordLimit) flags |= rsvp::TOKEN_PARAGRAPH_END | rsvp::TOKEN_CHAPTER_END;

  next.text = std::string_view(PASSAGE + start, end - start);
  next.wordIndex = index;
  next.flags = flags;
  next.skippedBefore = 0;
  nextEnd = end;
  located = true;
  return true;
}

bool PassageSource::peek(rsvp::Token& out) {
  if (!locate()) return false;
  out = next;
  return true;
}

void PassageSource::advance() {
  if (!locate()) return;
  offset = nextEnd;
  ++index;
  located = false;
}

}  // namespace devicetest

#endif  // CROSSPOINT_RSVP_ENABLED
