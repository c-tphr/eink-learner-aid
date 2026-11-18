#pragma once

// ReadingSession (docs/PLAN.md sections 4 and 7; README.md, "ReadingSession"): for the open book, its
// checkpoint data (or none), the section layout of the chapter being read, the mode in use, and the
// navigation source of each move; it decides when a checkpoint fires by calling lib/CheckpointCore's
// trigger rules, and where reading goes on afterwards. The reader owns it for as long as the book is
// open and shares it with RSVP (rsvpentry::Entry::session).
//
// Only ordinary forward reading asks it: RSVP playback (the word source's section-end flags, then
// rsvpStop()) and the reader's next-page action (readingsession::nextPageCheckpoint(), called at the
// top of EpubReaderActivity::pageTurn()). Every other route that moves the position never reaches the
// trigger rules (README.md lists them with their navigation source).
//
// The reader includes this header (registration point): without the RSVP flag the readingsession::
// functions below are empty inline functions and the reader's calls compile to nothing.

#include <cstdint>
#include <memory>

#include "RsvpFeature.h"

class Activity;
class Epub;
class GfxRenderer;
class MappedInputManager;

namespace reading {
class ReadingSession;
}

namespace readingsession {

// The page view page a next-page action leaves: chapter (spine index), its start offset and the next
// page's start offset (visible-text offsets, Section::getVisibleTextOffsetForPage()).
struct PageSpan {
  int spine = 0;
  uint32_t startOffset = 0;
  uint32_t nextStartOffset = UINT32_MAX;  // UINT32_MAX on the chapter's last page
};

// A checkpoint that fired, kept by the caller's result handler.
struct CheckpointRun;

// Where page view goes once a checkpoint has ended (afterPageCheckpoint()).
struct PageResume {
  enum class Go : uint8_t {
    ThisPage,   // stay on the page that was left: it shows the first word after the break
    Offset,     // the page holding `offset` in `spine`
    EndOfBook,  // the section ended the book: the reader's end-of-book screen
  };
  Go go = Go::ThisPage;
  int spine = 0;
  uint32_t offset = 0;
};

// How page view moved without a checkpoint: the page-view reading word is forgotten, and the moves
// that never fire are logged.
enum class PageMove : uint8_t { NextPage, PreviousPage, ChapterSkip };

#if CROSSPOINT_RSVP_ENABLED

// The session for a loaded book: its checkpoint data when it is a prepared book (logged). nullptr only
// when out of memory; reading then goes on without checkpoints.
std::shared_ptr<reading::ReadingSession> open(const std::shared_ptr<Epub>& epub);

// Whether a next-page action in chapter `spine` may need the next page's start offset (a prepared
// book's chapter with a checkpoint): the reader then lays it out before asking nextPageCheckpoint().
bool wantsNextPageOffset(const reading::ReadingSession& session, int spine);

// A next-page action is about to leave `page`. When it carries the position past the end of a section
// with a checkpoint (checkpoint::pageAdvance()), returns CheckpointActivity, to be pushed before the
// next page is shown, and fills `run` for afterPageCheckpoint(); otherwise nullptr. Call with the
// RenderLock held (it may build the chapter's word index).
std::unique_ptr<Activity> nextPageCheckpoint(GfxRenderer& renderer, MappedInputManager& input,
                                             reading::ReadingSession& session, const PageSpan& page,
                                             std::shared_ptr<CheckpointRun>& run);

// The checkpoint of `run` has ended (any outcome): where page view goes on. Call with the RenderLock
// held. After Go::ThisPage the reader asks nextPageCheckpoint() again for the same page (trigger rule
// 7: another section end on that page fires before reading continues).
PageResume afterPageCheckpoint(reading::ReadingSession& session, const CheckpointRun& run);

void notePageMove(reading::ReadingSession& session, PageMove move);

#else

}  // namespace readingsession

#include "activities/Activity.h"  // the stubs' unique_ptr<Activity> needs the complete type

namespace readingsession {

inline std::shared_ptr<reading::ReadingSession> open(const std::shared_ptr<Epub>&) { return nullptr; }
inline bool wantsNextPageOffset(const reading::ReadingSession&, int) { return false; }
inline std::unique_ptr<Activity> nextPageCheckpoint(GfxRenderer&, MappedInputManager&, reading::ReadingSession&,
                                                    const PageSpan&, std::shared_ptr<CheckpointRun>&) {
  return nullptr;
}
inline PageResume afterPageCheckpoint(reading::ReadingSession&, const CheckpointRun&) { return {}; }
inline void notePageMove(reading::ReadingSession&, PageMove) {}

#endif

}  // namespace readingsession

#if CROSSPOINT_RSVP_ENABLED

#include <CheckpointTrigger.h>
#include <CheckpointTypes.h>
#include <WordIndexReader.h>

#include <string>

#include "BookStateFile.h"
#include "ChapterWordIndex.h"
#include "ReadingSessionRules.h"
#include "activities/checkpoint/CheckpointLaunch.h"

namespace readingsession {

struct CheckpointRun {
  checkpointentry::Request request;
  std::shared_ptr<checkpointentry::Outcome> outcome;
  reading::PageWords page;  // page view: the words of the page that was left
};

}  // namespace readingsession

namespace reading {

class CheckpointBook;

class ReadingSession {
 public:
  ReadingSession(std::shared_ptr<Epub> epub, std::shared_ptr<CheckpointBook> book);
  ~ReadingSession();
  // Called once by readingsession::open(): allocates the layout of a prepared book and logs the open.
  void attachLayout();
  ReadingSession(const ReadingSession&) = delete;
  ReadingSession& operator=(const ReadingSession&) = delete;

  bool hasCheckpoints() const { return book != nullptr; }
  // A checkpoint can fire in chapter `spine` (as far as is known without its word index).
  bool mayFireIn(int spine) const;
  const std::shared_ptr<CheckpointBook>& checkpointBook() const { return book; }
  // What the state file is read and written with (BookStateFile.h).
  BookIdentity identity() const;
  const std::string& bookPath() const;

  checkpoint::ReadingMode mode() const { return currentMode; }
  // RSVP opened (Rsvp) or returned to page view (Page).
  void setMode(checkpoint::ReadingMode mode);

  // --- RSVP (the render task) ---

  // Loads the sections of chapter `spine` from `reader`, an open word index of that chapter (RSVP's).
  // False for a book without checkpoint data, or when the chapter has no section.
  bool loadSections(int spine, wordindex::WordIndexReader& reader);
  // wordindex::SectionEndFn over the sections of the chapter loaded last (`context`: the session).
  static uint8_t sectionEnds(void* context, uint32_t firstAnchorIndex, uint32_t lastAnchorIndex);
  // The step at an RSVP stop with STOP_CHECKPOINT (rsvpCheckpointStep()), logged when it fires.
  checkpoint::Step rsvpStop(int spine, uint32_t lastShown, uint32_t next);

  // --- Either mode ---

  // CheckpointActivity for a Fire step, with `before` (the place before the break) written as the
  // reading position when it starts. Logs the start. nullptr when out of memory (`run` stays empty).
  std::unique_ptr<Activity> startCheckpoint(GfxRenderer& renderer, MappedInputManager& input,
                                            const checkpoint::Step& fire, checkpoint::ReadingMode mode,
                                            const checkpoint::Place& before,
                                            std::shared_ptr<readingsession::CheckpointRun>& run);
  // Where reading goes on once the checkpoint of `run` ended, in the mode it fired in: the flow's
  // resume (Finished, Skipped), or, for a reread request (task C4) and a checkpoint that could not
  // start, the first word after the break. Logs it; for a reread request it also clears the
  // checkpoint in progress and saves that position, as Finished does.
  checkpoint::Place endCheckpoint(const readingsession::CheckpointRun& run);

  // --- Page view (the main task, holding the RenderLock) ---

  // The step of a next-page action leaving the page [startOffset, nextStartOffset) of `spine`. Fills
  // `words` with the page's words and `from` with the word it counts from (pageFromWord()). Opens
  // (and if needed builds) the chapter's word index.
  checkpoint::Step nextPage(const readingsession::PageSpan& page, PageWords& words, uint32_t& from);
  // Reading continues at `word` of `chapter` on the page shown (a checkpoint resumed mid-page, or RSVP
  // left to page view): the next next-page action counts from it.
  void setPageReading(uint16_t chapter, uint32_t word);
  void clearPageReading() { pageReading = PageReading{}; }
  // The visible offset of a word's start (the page holding it), and a chapter's word count.
  bool wordOffset(int spine, uint32_t word, uint32_t& offset);
  bool chapterWords(int spine, uint32_t& words);

 private:
  // The word index page view maps pages through, and the sections of one chapter. One heap block for
  // a prepared book only: ChapterSections is 6 KB (MAX_SECTIONS spans), the reader 1.9 KB.
  struct Layout {
    checkpoint::ChapterSections sections;
    int sectionsSpine = -1;
    bool sectionsLoaded = false;
    ChapterWordIndex pageIndex;
  };

  bool openPageIndex(int spine);
  int hrefChapterOf(int spine) const;
  // The loaded chapter has a section with a checkpoint (layout must exist).
  bool hasCheckpointSection() const;

  std::shared_ptr<Epub> epub;
  std::shared_ptr<CheckpointBook> book;
  std::unique_ptr<Layout> layout;
  checkpoint::ReadingMode currentMode = checkpoint::ReadingMode::Page;
  PageReading pageReading;
};

}  // namespace reading

#endif  // CROSSPOINT_RSVP_ENABLED
