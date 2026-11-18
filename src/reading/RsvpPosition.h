#pragma once

// Position hand-over between page view and RSVP, and sentence stepping, on a chapter's word index
// (S1 sections 4 and 9; the position is a chapter word index). Pure: host-tested in
// test/rsvp_activity with real indexes.

#include <WordClassifier.h>
#include <WordIndexReader.h>
#include <WordSource.h>

#include <cstdint>

namespace reading {

// The word RSVP starts at when entered from a page: the first word whose end is past the page's start
// offset. wordCount() when the page holds no word (or on a read error).
uint32_t entryWord(wordindex::WordIndexReader& reader, uint32_t pageStartOffset);

// Where RSVP opens (task R4b; src/activities/rsvp/README.md, "Where RSVP opens"). Continued: after
// a non-prose item shown in page view, at the first prose word after it (task R5).
enum class EntryFrom : uint8_t { Page, Saved, Resumed, Continued };
struct EntryChoice {
  uint32_t word;
  EntryFrom from;
};
// The state file's reading position, if any.
struct SavedWord {
  bool valid = false;
  int chapter = -1;
  uint32_t word = 0;
};
// No page after this one in the chapter, or its start is unknown.
inline constexpr uint32_t NO_PAGE_END = UINT32_MAX;

// `resume` (the book opened with its state in RSVP mode, `chapter` being the saved one): the saved
// word, at most the chapter's word count, whatever page the reader shows. Otherwise the saved word
// when it lies on the page the reader shows: the same chapter, not before the page's entry word, and
// starting before `pageEndOffset` (the next page's start offset, or NO_PAGE_END); else the page's
// entry word (entryWord()).
EntryChoice chooseEntry(wordindex::WordIndexReader& reader, int chapter, uint32_t pageStartOffset,
                        uint32_t pageEndOffset, const SavedWord& saved, bool resume);

const char* entryFromName(EntryFrom from);

// The visible offset page view jumps to for `position` (the next word RSVP would show): that word's
// start, or the last word's start when `position` is at the chapter end. False for an empty chapter.
bool pageViewOffset(wordindex::WordIndexReader& reader, uint32_t position, uint32_t& offset);

// The image, table or code block RSVP stopped before (task R5): the first such marker outside the
// notes that starts at or after word `from` and not after word `next` (the first prose word after
// the stop). When several items follow each other with no prose between, this is the first of them.
// False when there is none or on a read error.
bool itemBefore(wordindex::WordIndexReader& reader, uint32_t from, uint32_t next, wordindex::Marker& out);
// "image", "table" or "code" for the log lines.
const char* itemKindName(uint8_t kind);

// Progress through the chapter, 0..100, for the pause screen.
uint8_t chapterPercent(uint32_t position, uint32_t wordCount);

// Classified prose words for sentence stepping: up to CAPACITY consecutive RSVP tokens (non-prose and
// note words are not tokens) with their chapter word indexes. About 2.3 KB: the activity owns one.
struct SentenceWindow {
  static constexpr uint32_t CAPACITY = 256;
  rsvp::WordClass classes[CAPACITY];
  uint32_t wordIndex[CAPACITY];
  uint32_t count = 0;
  bool startsChapter = false;  // the window starts at the chapter's first token
  bool reachesEnd = false;     // the chapter has no token after the window
  char text[2][rsvp::MAX_WORD_BYTES + 1];
};

// Where "back one sentence" lands from `position`: the start of the current sentence when the position
// is past it, otherwise the start of the previous one; the chapter's first token at its first
// sentence. A sentence longer than the window lands on the window's first word. False on a read error.
bool sentenceBack(wordindex::WordIndexReader& reader, uint32_t position, SentenceWindow& window, uint32_t& out);

// Where "forward one sentence" lands: the first token after the current sentence. False when the
// current sentence is the chapter's last (the caller stays put) or on a read error. A sentence longer
// than the window lands on the window's last word.
bool sentenceForward(wordindex::WordIndexReader& reader, uint32_t position, SentenceWindow& window, uint32_t& out);

}  // namespace reading
