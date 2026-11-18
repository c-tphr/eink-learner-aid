#pragma once

// Shared types of the checkpoint flow (docs/PLAN.md section 7): reading
// positions, modes, navigation sources and grades. README.md describes the
// rules that use them.

#include <cstdint>

#include "CheckpointIndex.h"

namespace checkpoint {

// A reader chapter: the spine index the reader uses, not the index of a
// distinct chapter_href in CheckpointIndex. NO_CHAPTER means "none".
inline constexpr uint16_t NO_CHAPTER = 0xFFFF;

// A reading position: chapter plus word index within that chapter (plan
// section 4, "Position model"). word == the chapter's word count means "just
// after the chapter's last word", which is valid only at the end of the book.
struct Position {
  uint16_t chapter = 0;
  uint32_t word = 0;
};

inline bool operator==(const Position& a, const Position& b) { return a.chapter == b.chapter && a.word == b.word; }
inline bool operator!=(const Position& a, const Position& b) { return !(a == b); }

enum class ReadingMode : uint8_t { Rsvp, Page };

// A position together with the mode it is shown in.
struct Place {
  Position position;
  ReadingMode mode = ReadingMode::Page;
};

inline bool operator==(const Place& a, const Place& b) { return a.position == b.position && a.mode == b.mode; }
inline bool operator!=(const Place& a, const Place& b) { return !(a == b); }

// Why the reading position moved. Only RsvpPlayback and NextPage are
// "ordinary forward reading" and can fire a checkpoint.
enum class NavSource : uint8_t {
  RsvpPlayback,     // RSVP showed the next word or group
  NextPage,         // page view next-page action
  PreviousPage,     // page view previous-page action
  TableOfContents,  // table-of-contents or footnote-link jump
  GoToPercent,
  Bookmark,
  ProgressSync,  // position taken from another device or app
  SentenceStep,  // RSVP step back or forward by sentence while paused
  ModeSwitch,    // switch between RSVP and page view at the same position
  OpenRestore,   // book opened, state restored
  RereadStart,   // jump to a section start for a reread
  RereadReturn,  // jump back from a reread
};

inline constexpr uint8_t NAV_SOURCE_COUNT = 12;

inline bool isOrdinaryForward(NavSource source) {
  return source == NavSource::RsvpPlayback || source == NavSource::NextPage;
}

enum class Grade : uint8_t { Ungraded, Correct, Incorrect };

// "rsvp"/"page", "correct"/"incorrect"/"" as written in the state file.
const char* readingModeName(ReadingMode mode);
const char* gradeName(Grade grade);
const char* navSourceName(NavSource source);

// A section's word range [start, end) in a reader chapter, found by resolving
// its start and end anchors (docs/spikes/S1-word-stream.md section 5).
struct SectionRange {
  uint16_t section = NO_INDEX;  // index in CheckpointIndex
  uint16_t chapter = NO_CHAPTER;
  uint32_t start = 0;
  uint32_t end = 0;  // one past the last word
};

// What the caller knows about the open book, used to resolve sections and to
// check saved positions. ReadingSession implements it over WordIndex.
class BookLayout {
 public:
  virtual ~BookLayout() = default;

  // Number of reader chapters (spine items).
  virtual uint16_t chapterCount() const = 0;

  // Words in a chapter. False when it is unknown or cannot be read.
  virtual bool chapterWordCount(uint16_t chapter, uint32_t& words) = 0;

  // The reader chapter and word range of a section of the index. False when
  // its anchors cannot be found.
  virtual bool sectionRange(uint16_t section, SectionRange& out) = 0;
};

}  // namespace checkpoint
