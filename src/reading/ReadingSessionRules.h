#pragma once

// The pure decisions of ReadingSession (task C2; README.md, "ReadingSession"): a chapter's section
// layout from its word index, the section-end flags RSVP's word source carries, the step at an RSVP
// checkpoint stop, the words of a page in page view, and where reading goes on after a checkpoint.
// The trigger rules themselves are lib/CheckpointCore's (CheckpointTrigger.h): these functions only
// gather their inputs and call them. Host-tested in test/rsvp_activity (ReadingSessionTest).

#include <CheckpointIndex.h>
#include <CheckpointTrigger.h>
#include <CheckpointTypes.h>
#include <StateRestore.h>
#include <WordIndexReader.h>

#include <cstddef>
#include <cstdint>

namespace reading {

// checkpoint::BookLayout over the open word index of one reader chapter: enough for
// ChapterSections::load() of that chapter. A section's range comes from its anchors in `reader`
// ("rsvp-<id>-start", "rsvp-<id>-end"; docs/spikes/S1-word-stream.md section 5); every other chapter
// is unknown.
class ChapterLayout final : public checkpoint::BookLayout {
 public:
  ChapterLayout(const checkpoint::CheckpointIndex& index, uint16_t chapterCount, uint16_t chapter,
                wordindex::WordIndexReader& reader)
      : checkpoints(index), chapters(chapterCount), readerChapter(chapter), words(reader) {}

  uint16_t chapterCount() const override { return chapters; }
  bool chapterWordCount(uint16_t chapter, uint32_t& count) override;
  bool sectionRange(uint16_t section, checkpoint::SectionRange& out) override;

 private:
  const checkpoint::CheckpointIndex& checkpoints;
  uint16_t chapters;
  uint16_t readerChapter;
  wordindex::WordIndexReader& words;
};

// TOKEN_SECTION_END, plus TOKEN_CHECKPOINT when the section has a checkpoint, if the end anchor of a
// section of `sections` has a word index in [firstAnchorIndex, lastAnchorIndex]; otherwise 0. This is
// wordindex::SectionEndFn's contract: IndexWordSource asks it for the anchors between a word and the
// next prose word, so the word that carries the flag is the section's last prose word, the word
// checkpoint::stopAfterWord() names.
uint8_t sectionEndFlags(const checkpoint::ChapterSections& sections, uint32_t firstAnchorIndex,
                        uint32_t lastAnchorIndex);

// RSVP stopped at a checkpoint (STOP_CHECKPOINT): `lastShown` is the last word on screen and `next` the
// next prose word (the chapter's word count at its end). checkpoint::rsvpStep() on the word on screen;
// when note words lie between it and `next` (the section's last word is one of them), the same move
// through checkpoint::evaluateMove() as RSVP playback. During a reread of `rereadSection` nothing fires
// and that section's end gives RereadEnd (task C4).
checkpoint::Step rsvpCheckpointStep(const checkpoint::ChapterSections& sections, uint32_t lastShown, uint32_t next,
                                    uint16_t rereadSection = checkpoint::NO_INDEX);

// No page after this one in the chapter.
inline constexpr uint32_t NO_NEXT_PAGE = UINT32_MAX;

// The words of a page in page view: those whose last character it shows (S1 section 4.1). `first`
// is the first word ending after the page's start offset; `last` the word before the first one ending
// after the next page's start (the chapter's last word on its last page). `any` is false for a page
// with no word of its own (an image page) or on a read error.
struct PageWords {
  bool any = false;
  uint32_t first = 0;
  uint32_t last = 0;
};
PageWords pageWords(wordindex::WordIndexReader& reader, uint32_t pageStartOffset, uint32_t nextPageStartOffset);

// The reading position in page view: normally the page's first word; after a checkpoint resumed
// mid-page, or after RSVP left to page view, the word reading continues at.
struct PageReading {
  bool valid = false;
  uint16_t chapter = checkpoint::NO_CHAPTER;
  uint32_t word = 0;
};

// The word a next-page action counts from (checkpoint::pageAdvance()'s `fromWord`): the reading word
// when it lies on the page being left, else the page's first word.
uint32_t pageFromWord(const PageWords& page, uint16_t chapter, const PageReading& reading);

// Where page view goes on after a checkpoint that resumes at `resume`, leaving `page` of `chapter`.
enum class PageResume : uint8_t {
  ThisPage,   // the resume word is on the page being left: show it again (and apply trigger rule 7)
  LaterPage,  // a later page of the same chapter
  Chapter,    // another chapter (the section ended its chapter): the page holding its word
  EndOfBook,  // the section ended the book
};
PageResume pageResumeFor(uint16_t chapter, uint32_t chapterWords, const PageWords& page,
                         const checkpoint::Position& resume);

const char* pageResumeName(PageResume resume);

// --- Rereading (task C4; README.md, "Rereading").

// `word` moved into the section `range`: its first word when before it, its last word when after it.
uint32_t intoSection(const checkpoint::SectionRange& range, uint32_t word);

// A page of `chapter` shown in page view during a reread of `range`. The reread goes on while the page
// shows a word of the section (one whose last character it shows; on a page with no word of its own, an
// image page, the first word after it); the reread place is then the page's reading word
// (pageFromWord()) moved into the section, so the section's first page counts from its first word even
// when the page starts before it. Otherwise the page left the section, and the reread goes back to its
// prompt (CheckpointFlow::rereadMoved()).
struct RereadPagePlace {
  bool inside = false;
  uint32_t word = 0;  // inside: the reread place; otherwise the page's first word
};
RereadPagePlace rereadPagePlace(const PageWords& page, uint16_t chapter, const PageReading& reading,
                                const checkpoint::SectionRange& range);

// RSVP's sentence step during a reread goes to `to` only when it stays inside the section: a step
// across the section's start or end does not move (README.md, "Rereading").
bool rereadStepStays(const checkpoint::SectionRange& range, uint32_t to);

// --- Opening a book with a checkpoint in progress (task C5; README.md, "Opening a book").

// checkpoint::BookLayout over the word indexes of every reader chapter, for checkpoint::planRestore().
// `openChapter` gives an open word index of a chapter (built if needed), or null; one index may be open
// at a time, so a reader it returned is used before the next call. `sectionSpine` gives the reader
// chapter of a section of the index (its chapter_href through the spine), or -1.
class BookIndexLayout final : public checkpoint::BookLayout {
 public:
  using OpenFn = wordindex::WordIndexReader* (*)(void* context, uint16_t chapter);
  using SpineFn = int (*)(void* context, uint16_t section);
  BookIndexLayout(const checkpoint::CheckpointIndex& index, uint16_t chapterCount, OpenFn openChapter,
                  SpineFn sectionSpine, void* context)
      : checkpoints(index), chapters(chapterCount), openFn(openChapter), spineFn(sectionSpine), fnContext(context) {}

  uint16_t chapterCount() const override { return chapters; }
  bool chapterWordCount(uint16_t chapter, uint32_t& count) override;
  bool sectionRange(uint16_t section, checkpoint::SectionRange& out) override;

 private:
  const checkpoint::CheckpointIndex& checkpoints;
  uint16_t chapters;
  OpenFn openFn;
  SpineFn spineFn;
  void* fnContext;
};

// What the reader does with checkpoint::planRestore()'s plan for a state file that held a checkpoint in
// progress (docs/PLAN.md section 7, "State file": an active reread, then an active prompt with its
// answer hidden, then the plain reading position).
struct OpenAction {
  enum class Show : uint8_t {
    Reading,  // ordinary reading (`jump`: at `at`; otherwise the reader's own progress)
    Prompt,   // the prompt, answer hidden, before any page of the book is drawn
    Reread,   // the reread at `at`, in its mode
  };
  Show show = Show::Reading;
  checkpoint::Place at;
  bool jump = false;       // page view opens on the page holding `at` (in RSVP: the page under it)
  bool speedRead = false;  // RSVP opens once the first page is shown
};
OpenAction openActionFor(const checkpoint::RestorePlan& plan);

const char* openViewName(checkpoint::OpenView view);
// The names of the checkpoint::RestoreNote bits in `notes`, joined by '+' ("none" for 0), for the log.
void restoreNoteNames(uint16_t notes, char* out, size_t size);

}  // namespace reading
