#pragma once

// Trigger rules (docs/PLAN.md section 7, "Trigger rules"): when a move of the
// reading position fires a checkpoint. Pure functions over the section layout
// of one reader chapter; README.md states the rules and the calling contract.

#include <cstdint>

#include "CheckpointIndex.h"
#include "CheckpointTypes.h"

namespace checkpoint {

// One section of a chapter: words [start, end).
struct SectionSpan {
  uint32_t start;
  uint32_t end;
  uint16_t section;     // index in CheckpointIndex
  uint16_t checkpoint;  // NO_INDEX when the section has none
};

// The sections of one reader chapter, sorted by start word, with no overlap.
// Fixed size: MAX_SECTIONS spans of 12 bytes (6 KiB), because every section of
// a book may lie in one chapter.
class ChapterSections {
 public:
  enum class AddResult : uint8_t { Added, EmptyRange, OutOfChapter, Overlap, Full };

  struct LoadResult {
    bool chapterKnown = false;  // the chapter's word count was read
    uint16_t added = 0;
    uint16_t failed = 0;  // sections of the chapter left out (anchors not found, bad range)
  };

  // Empties the layout. `nextChapter` is the reader chapter that follows, or
  // NO_CHAPTER at the end of the book.
  void reset(uint16_t chapter, uint32_t wordCount, uint16_t nextChapter);

  // Adds a section. An empty range, a range past the chapter's end or one that
  // overlaps another section is refused: such a checkpoint can never fire.
  AddResult add(uint16_t section, uint32_t start, uint32_t end, uint16_t checkpoint);

  // Resets to `chapter` and adds every section of the index's chapter
  // `hrefChapter` (CheckpointIndex::findChapter) through layout.sectionRange().
  LoadResult load(const CheckpointIndex& index, uint16_t hrefChapter, BookLayout& layout, uint16_t chapter,
                  uint16_t nextChapter);

  uint16_t chapter() const { return chapterNumber; }
  uint32_t wordCount() const { return words; }
  uint16_t nextChapter() const { return following; }
  uint16_t count() const { return total; }
  const SectionSpan& span(uint16_t i) const { return spans[i]; }
  SectionRange range(uint16_t i) const;

  // Span index of a section, or -1.
  int findSection(uint16_t section) const;
  // Span index of the section that contains `word`, or -1.
  int spanContaining(uint32_t word) const;

  // The first word after the break that follows `lastWord`: the next word, the
  // next chapter's first word when `lastWord` ends the chapter, or
  // (chapter, wordCount) when it ends the book.
  Position after(uint32_t lastWord) const;

 private:
  SectionSpan spans[MAX_SECTIONS] = {};
  uint16_t total = 0;
  uint16_t chapterNumber = NO_CHAPTER;
  uint16_t following = NO_CHAPTER;
  uint32_t words = 0;
};

enum class StepKind : uint8_t {
  None,       // keep reading
  Fire,       // present `checkpoint`
  RereadEnd,  // a reread reached its section's end: back to the prompt
};

struct Step {
  StepKind kind = StepKind::None;
  uint16_t checkpoint = NO_INDEX;  // Fire: the first checkpoint crossed
  SectionRange section;            // Fire, RereadEnd: the section whose end was crossed
  Position resume;                 // Fire: the first word after that section's break
  uint16_t crossed = 0;            // Fire: armed section ends the move crosses, this one included
};

// General rule. The move from `from` to `to` fires when `source` is ordinary
// forward reading and the words it leaves behind, [from.word, to.word) or to
// the chapter's end when `to` is in a later chapter, include the last word of
// a section with a checkpoint. The first such section fires; `crossed` counts
// them all. `layout` is the chapter of `from`. While a reread of
// `rereadSection` is under way, no checkpoint fires and crossing that
// section's end gives RereadEnd.
Step evaluateMove(const ChapterSections& layout, const Position& from, const Position& to, NavSource source,
                  uint16_t rereadSection = NO_INDEX);

// RSVP: `shownWord` is on screen (the last word of a group). The result is
// that of moving on to the next word: Fire or RereadEnd means playback stops
// here and draws nothing beyond.
Step rsvpStep(const ChapterSections& layout, uint32_t shownWord, uint16_t rereadSection = NO_INDEX);

// The scheduler's stop-at-checkpoint flag for `shownWord`.
bool stopAfterWord(const ChapterSections& layout, uint32_t shownWord, uint16_t rereadSection = NO_INDEX);

// Page view: a next-page action leaves the page whose words run up to
// `pageLastWord`. `fromWord` is the page's first word, or the reading position
// when it lies later on that page (after a checkpoint resumed mid-page). A
// page's words are those whose last character is on it, so a word hyphenated
// across two pages belongs to the second.
Step pageAdvance(const ChapterSections& layout, uint32_t fromWord, uint32_t pageLastWord,
                 uint16_t rereadSection = NO_INDEX);

// Checkpoint armed at `word`: that of the section containing it, or -1.
int armedCheckpoint(const ChapterSections& layout, uint32_t word);

}  // namespace checkpoint
