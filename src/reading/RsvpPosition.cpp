#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <IndexWordSource.h>
#include <SentenceStep.h>

#include <cstring>
#include <string_view>

#include "RsvpPosition.h"

namespace reading {

namespace {

// Raw word indexes looked back for a back step; doubled while the window cannot decide.
constexpr uint32_t BACK_SPAN = 96;
constexpr int BACK_SPAN_DOUBLINGS = 2;

// Loads the RSVP tokens from the first prose word at or after `from`, up to `last` (inclusive) or the
// window's capacity, and classifies each with the text of the token after it, as FrameBuilder does.
bool loadWindow(wordindex::WordIndexReader& reader, const uint32_t from, const uint32_t last, SentenceWindow& w) {
  w.count = 0;
  w.startsChapter = from == 0;
  w.reachesEnd = false;
  wordindex::IndexWordSource source(reader);
  if (!source.seek(from)) return false;

  rsvp::Token token;
  bool have = source.peek(token);
  uint8_t flags = 0;
  size_t length = 0;
  int slot = 0;
  while (have && w.count < SentenceWindow::CAPACITY && token.wordIndex <= last) {
    length = token.text.size() < rsvp::MAX_WORD_BYTES ? token.text.size() : rsvp::MAX_WORD_BYTES;
    std::memcpy(w.text[slot], token.text.data(), length);
    w.wordIndex[w.count] = token.wordIndex;
    flags = token.flags;
    source.advance();
    have = source.peek(token);
    const std::string_view next = have ? token.text : std::string_view{};
    w.classes[w.count] = rsvp::classifyWord(std::string_view(w.text[slot], length), flags, next);
    ++w.count;
    slot ^= 1;
  }
  if (source.failed()) return false;
  w.reachesEnd = !have;
  return true;
}

// Window index of `position`, or `w.count` when it lies after the window.
uint32_t localIndex(const SentenceWindow& w, const uint32_t position) {
  for (uint32_t k = 0; k < w.count; ++k) {
    if (w.wordIndex[k] >= position) return k;
  }
  return w.count;
}

// SentenceStep reads word 0 of a window as the chapter's first word; a window that starts later is
// numbered from 1, so reaching its start means "load more" rather than "chapter start".
rsvp::ClassWindow classWindow(const SentenceWindow& w) { return {w.classes, w.count, w.startsChapter ? 0u : 1u}; }

}  // namespace

uint32_t entryWord(wordindex::WordIndexReader& reader, const uint32_t pageStartOffset) {
  uint32_t word = reader.wordCount();
  if (!reader.firstWordEndingAfter(pageStartOffset, word)) return reader.wordCount();
  return word;
}

EntryChoice chooseEntry(wordindex::WordIndexReader& reader, const int chapter, const uint32_t pageStartOffset,
                        const uint32_t pageEndOffset, const SavedWord& saved, const bool resume) {
  const uint32_t count = reader.wordCount();
  const bool sameChapter = saved.valid && saved.chapter == chapter;
  if (resume && sameChapter) return {saved.word < count ? saved.word : count, EntryFrom::Resumed};

  const uint32_t pageWord = entryWord(reader, pageStartOffset);
  if (sameChapter && saved.word >= pageWord && saved.word < count) {
    uint32_t start = 0;
    if (pageEndOffset == NO_PAGE_END || (reader.wordStart(saved.word, start) && start < pageEndOffset)) {
      return {saved.word, EntryFrom::Saved};
    }
  }
  return {pageWord, EntryFrom::Page};
}

const char* entryFromName(const EntryFrom from) {
  switch (from) {
    case EntryFrom::Page:
      return "page";
    case EntryFrom::Saved:
      return "saved";
    case EntryFrom::Resumed:
      return "resumed";
    case EntryFrom::Continued:
      return "continue";
  }
  return "?";
}

bool pageViewOffset(wordindex::WordIndexReader& reader, const uint32_t position, uint32_t& offset) {
  const uint32_t count = reader.wordCount();
  if (count == 0) return false;
  return reader.wordStart(position < count ? position : count - 1, offset);
}

bool itemBefore(wordindex::WordIndexReader& reader, const uint32_t from, const uint32_t next, wordindex::Marker& out) {
  constexpr uint8_t ITEMS = wordindex::kindBit(wordindex::MARKER_IMAGE) | wordindex::kindBit(wordindex::MARKER_TABLE) |
                            wordindex::kindBit(wordindex::MARKER_CODE);
  uint32_t position = 0;
  if (!reader.nextMarker(from, ITEMS, out, &position)) return false;
  // Markers are in order of their start; an item inside a note never stops playback.
  for (uint32_t i = position; i < reader.markerCount(); ++i) {
    if (!reader.marker(i, out) || out.wordStart > next) return false;
    if ((wordindex::kindBit(out.kind) & ITEMS) != 0 && (out.flags & wordindex::MARKER_IN_NOTE) == 0) return true;
  }
  return false;
}

const char* itemKindName(const uint8_t kind) {
  switch (kind) {
    case wordindex::MARKER_IMAGE:
      return "image";
    case wordindex::MARKER_TABLE:
      return "table";
    case wordindex::MARKER_CODE:
      return "code";
    default:
      return "?";
  }
}

uint8_t chapterPercent(const uint32_t position, const uint32_t wordCount) {
  if (wordCount == 0 || position >= wordCount) return 100;
  return static_cast<uint8_t>(static_cast<uint64_t>(position) * 100u / wordCount);
}

bool sentenceBack(wordindex::WordIndexReader& reader, const uint32_t position, SentenceWindow& w, uint32_t& out) {
  uint32_t span = BACK_SPAN;
  for (int attempt = 0; attempt <= BACK_SPAN_DOUBLINGS; ++attempt, span *= 2) {
    const uint32_t from = position > span ? position - span : 0;
    if (!loadWindow(reader, from, position, w)) return false;
    if (w.count == 0) {
      out = position;
      return true;
    }
    const rsvp::ClassWindow window = classWindow(w);
    // At the chapter end, the position is one past the last token: step back into the last sentence.
    uint32_t local = localIndex(w, position);
    const bool atEnd = local == w.count;
    if (atEnd) local = w.count - 1;
    const uint32_t current = window.firstWordIndex + local;

    uint32_t start = 0;
    if (rsvp::sentenceStart(window, current, start) && (start < current || atEnd)) {
      out = w.wordIndex[start - window.firstWordIndex];
      return true;
    }
    if (rsvp::previousSentenceStart(window, current, start)) {
      out = w.wordIndex[start - window.firstWordIndex];
      return true;
    }
    if (w.startsChapter) {
      out = w.wordIndex[0];  // already in the chapter's first sentence
      return true;
    }
  }
  out = w.wordIndex[0];  // a sentence longer than the window
  return true;
}

bool sentenceForward(wordindex::WordIndexReader& reader, const uint32_t position, SentenceWindow& w, uint32_t& out) {
  if (!loadWindow(reader, position, UINT32_MAX, w)) return false;
  if (w.count == 0) return false;
  const rsvp::ClassWindow window = classWindow(w);
  uint32_t next = 0;
  if (rsvp::nextSentenceStart(window, window.firstWordIndex, next)) {
    const uint32_t local = next - window.firstWordIndex;
    if (local < w.count) {
      out = w.wordIndex[local];
      return true;
    }
    if (w.reachesEnd) return false;  // the current sentence ends the chapter
    // The window ended exactly at the boundary: the next token is the first prose word after it.
    wordindex::IndexWordSource source(reader);
    if (!source.seek(w.wordIndex[w.count - 1] + 1)) return false;
    out = source.position();
    return out < reader.wordCount();
  }
  if (w.reachesEnd) return false;
  out = w.wordIndex[w.count - 1];  // a sentence longer than the window
  return true;
}

}  // namespace reading

#endif  // CROSSPOINT_RSVP_ENABLED
