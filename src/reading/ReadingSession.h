#pragma once

// ReadingSession (docs/PLAN.md sections 4 and 7; README.md, "ReadingSession"): for the open book, its
// checkpoint data (or none), the section layout of the chapter being read, the mode in use, and the
// navigation source of each move; it decides when a checkpoint fires by calling lib/CheckpointCore's
// trigger rules, and where reading goes on afterwards. The reader owns it for as long as the book is
// open and shares it with RSVP (rsvpentry::Entry::session). It also keeps the checkpoint in progress
// (its flow and state) from the moment it fires until it ends, across a reread of its section (task C4;
// README.md, "Rereading").
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
  // pageShown() only (task C4): where the reader's text area ends (its screen height minus its bottom
  // margin) and one line of its text, for the reread bar (controls::rereadBarLayout()).
  int16_t textBottom = 0;
  int16_t lineHeight = 0;
};

// A checkpoint that fired, kept by the session while it is in progress and by the result handlers.
struct CheckpointRun;

// Where page view goes once a checkpoint's screens have closed (afterPageCheckpoint()).
struct PageResume {
  enum class Go : uint8_t {
    ThisPage,   // stay on the page that was left: it shows the first word after the break
    Offset,     // the page holding `offset` in `spine`
    EndOfBook,  // the section ended the book: the reader's end-of-book screen
    SpeedRead,  // open RSVP: a reread in RSVP, or the checkpoint fired in RSVP (the session says where)
  };
  Go go = Go::ThisPage;
  int spine = 0;
  uint32_t offset = 0;
};

// How page view moved without a checkpoint: the page-view reading word is forgotten, and the moves
// that never fire are logged.
enum class PageMove : uint8_t { NextPage, PreviousPage, ChapterSkip };

// What the reader does after the list a side-key hold opens during a page-view reread.
enum class RereadChoice : uint8_t { KeepReading, BackToQuestion, SpeedRead };

// Where the reader opens a book (task C5; README.md, "Opening a book"). A restored prompt is not here:
// it waits in the session (promptDue()) and the reader shows it before any page.
struct OpenRestore {
  bool speedRead = false;  // open RSVP once the first page is shown (it takes its place from the session)
  bool jump = false;       // page view opens on the page holding `offset` of `spine`, not at its progress
  int spine = 0;
  uint32_t offset = 0;
};

#if CROSSPOINT_RSVP_ENABLED

// The session for a loaded book: its checkpoint data when it is a prepared book (logged). nullptr only
// when out of memory; reading then goes on without checkpoints.
std::shared_ptr<reading::ReadingSession> open(const std::shared_ptr<Epub>& epub);

// The book opens (main task, from loadBook(), after open()). Reads the state file, when there is one,
// and restores in docs/PLAN.md's order: a reread in progress (its mode, its place), else the prompt in
// progress with its answer hidden (promptDue() is then true), else the plain reading position. Without
// a session or checkpoint data this is the saved mode only (rsvpentry::openedInRsvp()). May build a
// chapter's word index, with "Preparing..." on screen.
OpenRestore restoreOnOpen(GfxRenderer& renderer, const Epub& epub, reading::ReadingSession* session);

// Whether a next-page action in chapter `spine` may need the next page's start offset (a prepared
// book's chapter with a checkpoint): the reader then lays it out before asking nextPageCheckpoint().
bool wantsNextPageOffset(const reading::ReadingSession& session, int spine);

// A next-page action is about to leave `page`. When it carries the position past the end of a section
// with a checkpoint (checkpoint::pageAdvance()), returns CheckpointActivity, to be pushed before the
// next page is shown, and fills `run` for afterPageCheckpoint(); during a reread, when it leaves the
// page with the section's last word, the same for the prompt again (task C4). Otherwise nullptr. Call
// with the RenderLock held (it may build the chapter's word index).
std::unique_ptr<Activity> nextPageCheckpoint(GfxRenderer& renderer, MappedInputManager& input,
                                             reading::ReadingSession& session, const PageSpan& page,
                                             std::shared_ptr<CheckpointRun>& run);

// The checkpoint screens of `run` have closed (any outcome): where page view goes on, a reread
// included. Call with the RenderLock held. After Go::ThisPage the reader asks nextPageCheckpoint() again
// for the same page (trigger rule 7: another section end on that page fires before reading continues).
PageResume afterPageCheckpoint(reading::ReadingSession& session, const CheckpointRun& run);

void notePageMove(reading::ReadingSession& session, PageMove move);

// --- Rereading in page view (task C4; S4 row 31). Call with the RenderLock held unless noted.

// The reader is about to draw `page` (render task): during a reread the session records the page as the
// reread place, saved before the page shows (task C5), or, when the page holds no word of the section,
// goes back to the prompt (promptDue()).
void pageShowing(reading::ReadingSession& session, const PageSpan& page);
// The reader drew `page`: while the reread goes on, the bar is drawn over the page and shown with a
// fast refresh.
void pageShown(GfxRenderer& renderer, reading::ReadingSession& session, const PageSpan& page);

// Any task, no lock: a prompt waits to be shown again (rereadPrompt()).
bool promptDue(const reading::ReadingSession& session);

// The prompt is waiting to be shown again (the reread reached its end or was left). Returns the
// activity for it and fills `run` (its outcome reset); with `run` set and a null activity, there was no
// memory for the screens and the outcome is Failed. Neither when no prompt waits.
std::unique_ptr<Activity> rereadPrompt(GfxRenderer& renderer, MappedInputManager& input,
                                       reading::ReadingSession& session, std::shared_ptr<CheckpointRun>& run);

// Main task, no lock needed: a tap on the bar's "Back to the question" during a page-view reread
// (checked before the page's own taps). True: the reread went back to its prompt; call rereadPrompt().
bool rereadBarTapped(const MappedInputManager& input, reading::ReadingSession& session);

// Main task: during a page-view reread, the list a side-key hold opens (S4 row 31): "Back to the
// question" (the default), "Speed read from here", "Keep reading". Null outside a reread (the hold does
// what its setting says) or when out of memory. `chosen` is set when the list finishes.
std::unique_ptr<Activity> rereadChoices(GfxRenderer& renderer, MappedInputManager& input,
                                        reading::ReadingSession& session, std::shared_ptr<RereadChoice> chosen);
// Acts on the list's choice: BackToQuestion ends the reread (then call rereadPrompt()).
RereadChoice afterRereadChoice(reading::ReadingSession& session, RereadChoice chosen);

// The checkpoint in progress, for the reader's RSVP result handler (HandOff::Then::CheckpointDone).
std::shared_ptr<CheckpointRun> checkpointRun(const reading::ReadingSession& session);

#else

}  // namespace readingsession

#include "activities/Activity.h"  // the stubs' unique_ptr<Activity> needs the complete type

namespace readingsession {

inline std::shared_ptr<reading::ReadingSession> open(const std::shared_ptr<Epub>&) { return nullptr; }
inline OpenRestore restoreOnOpen(GfxRenderer&, const Epub&, reading::ReadingSession*) { return {}; }
inline bool wantsNextPageOffset(const reading::ReadingSession&, int) { return false; }
inline std::unique_ptr<Activity> nextPageCheckpoint(GfxRenderer&, MappedInputManager&, reading::ReadingSession&,
                                                    const PageSpan&, std::shared_ptr<CheckpointRun>&) {
  return nullptr;
}
inline PageResume afterPageCheckpoint(reading::ReadingSession&, const CheckpointRun&) { return {}; }
inline void notePageMove(reading::ReadingSession&, PageMove) {}
inline void pageShowing(reading::ReadingSession&, const PageSpan&) {}
inline void pageShown(GfxRenderer&, reading::ReadingSession&, const PageSpan&) {}
inline std::unique_ptr<Activity> rereadPrompt(GfxRenderer&, MappedInputManager&, reading::ReadingSession&,
                                              std::shared_ptr<CheckpointRun>&) {
  return nullptr;
}
inline bool promptDue(const reading::ReadingSession&) { return false; }
inline bool rereadBarTapped(const MappedInputManager&, reading::ReadingSession&) { return false; }
inline std::unique_ptr<Activity> rereadChoices(GfxRenderer&, MappedInputManager&, reading::ReadingSession&,
                                               std::shared_ptr<RereadChoice>) {
  return nullptr;
}
inline RereadChoice afterRereadChoice(reading::ReadingSession&, RereadChoice) { return RereadChoice::KeepReading; }
inline std::shared_ptr<CheckpointRun> checkpointRun(const reading::ReadingSession&) { return nullptr; }

#endif

}  // namespace readingsession

#if CROSSPOINT_RSVP_ENABLED

#include <CheckpointFlow.h>
#include <CheckpointTrigger.h>
#include <CheckpointTypes.h>
#include <WordIndexReader.h>

#include <atomic>
#include <string>

#include "BookStateFile.h"
#include "ChapterWordIndex.h"
#include "ReadingSessionRules.h"
#include "activities/checkpoint/CheckpointLaunch.h"
#include "controls/ControlMap.h"

namespace readingsession {

struct CheckpointRun {
  checkpointentry::Request request;
  std::shared_ptr<checkpointentry::Outcome> outcome;
  reading::PageWords page;  // page view: the words of the page that was left
  bool reread = false;      // the section was reread: the views no longer show where it fired
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

  // The book opened (readingsession::restoreOnOpen()) with checkpoint data.
  readingsession::OpenRestore restoreOnOpen(GfxRenderer& renderer);

  checkpoint::ReadingMode mode() const { return currentMode; }
  // RSVP opened (Rsvp) or returned to page view (Page).
  void setMode(checkpoint::ReadingMode mode);

  // --- RSVP (the render task) ---

  // Loads the sections of chapter `spine` from `reader`, an open word index of that chapter (RSVP's).
  // False for a book without checkpoint data, or when the chapter has no section.
  bool loadSections(int spine, wordindex::WordIndexReader& reader);
  // wordindex::SectionEndFn over the sections of the chapter loaded last (`context`: the session).
  static uint8_t sectionEnds(void* context, uint32_t firstAnchorIndex, uint32_t lastAnchorIndex);
  // The step at an RSVP stop with STOP_CHECKPOINT (rsvpCheckpointStep()), logged when it fires. During a
  // reread no checkpoint fires and the reread section's end gives RereadEnd.
  checkpoint::Step rsvpStop(int spine, uint32_t lastShown, uint32_t next);

  // Where RSVP opens when the session decides it (RsvpActivity::load()).
  enum class RsvpStart : uint8_t {
    None,             // the reader's page, as always
    Reread,           // `at`: the reread place, inside the section being reread
    AfterCheckpoint,  // `at`: the first word after the break of a checkpoint that ended in page view but
                      // fired in RSVP; `fromChapter` is its section's chapter (the place is taken once)
  };
  RsvpStart rsvpStart(checkpoint::Place& at, uint16_t& fromChapter);

  // --- Either mode: the checkpoint in progress ---

  // CheckpointActivity for a Fire step, with `before` (the place before the break) written as the
  // reading position when it starts. Allocates the checkpoint's flow and state (78 KB, kept until it
  // ends) and keeps `run`. Logs the start. nullptr when out of memory (`run` stays empty).
  std::unique_ptr<Activity> startCheckpoint(GfxRenderer& renderer, MappedInputManager& input,
                                            const checkpoint::Step& fire, checkpoint::ReadingMode mode,
                                            const checkpoint::Place& before,
                                            std::shared_ptr<readingsession::CheckpointRun>& run);
  // The checkpoint of `run` ended (Finished, Skipped, Failed): where reading goes on, in the mode it
  // fired in (the flow's resume; after the break when it could not start or show). Logs it and frees
  // the flow and state. Not for a reread request (startReread()).
  checkpoint::Place endCheckpoint(const readingsession::CheckpointRun& run);

  // The screens of `run` closed with a reread request (Outcome RereadRsvp, RereadPage; the flow is in
  // Rereading and saved): logs the start and returns where the reread starts, the section's first word
  // in the chosen mode.
  checkpoint::Place startReread(const readingsession::CheckpointRun& run);
  bool rereading() const;
  // The section being reread, for the trigger rules: checkpoint::NO_INDEX unless rereading.
  uint16_t rereadSection() const;
  // Valid while rereading.
  const checkpoint::SectionRange& rereadRange() const;
  checkpoint::Place rereadPlace() const;

  // The reread moved (RSVP: a pause, a sentence step, leaving or entering RSVP; page view: a page shown).
  // Saves the place when it changed. BackToPrompt: the move left the section, and the prompt waits
  // (promptDue()).
  enum class RereadMove : uint8_t { NotRereading, Stays, BackToPrompt };
  RereadMove rereadMoved(const checkpoint::Place& to, checkpoint::NavSource source, const char* why);
  // Return early (FlowEvent::ReturnEarly) or the reread reached its end (RereadReachedEnd): back to the
  // prompt, saved; the prompt waits. False when not rereading.
  bool endReread(checkpoint::FlowEvent event, const char* why);
  bool promptDue() const { return promptWaiting.load(); }
  // CheckpointActivity for the prompt that waits, over the caller (see readingsession::rereadPrompt()).
  std::unique_ptr<Activity> promptAgain(GfxRenderer& renderer, MappedInputManager& input,
                                        std::shared_ptr<readingsession::CheckpointRun>& run);
  const std::shared_ptr<readingsession::CheckpointRun>& checkpointRun() const { return current; }
  // The checkpoint ended in page view but fired in RSVP: RSVP opens at `resume` (RsvpStart).
  void setRsvpResume(const checkpoint::Place& resume, uint16_t fromChapter);

  // --- Page view (the main task, holding the RenderLock) ---

  // The step of a next-page action leaving the page [startOffset, nextStartOffset) of `spine`. Fills
  // `words` with the page's words and `from` with the word it counts from (pageFromWord()). Opens
  // (and if needed builds) the chapter's word index.
  checkpoint::Step nextPage(const readingsession::PageSpan& page, PageWords& words, uint32_t& from);
  // Reading continues at `word` of `chapter` on the page shown (a checkpoint resumed mid-page, or RSVP
  // left to page view): the next next-page action counts from it.
  void setPageReading(uint16_t chapter, uint32_t word);
  void clearPageReading() { pageReading = PageReading{}; }
  const PageReading& pageReadingWord() const { return pageReading; }
  // Why page view moved last (notePageMove()); a page shown without one was a jump.
  void notePageSource(checkpoint::NavSource source) { pageSource = source; }
  // The reread bar's button as last drawn (RenderLock), for a tap on it; none before the first draw.
  void setRereadButton(const controls::Zone& button) { rereadButton = button; }
  // pageShowing() kept the reread on: pageShown() draws its bar.
  void setRereadBarDue(bool due) { rereadBarDue = due; }
  bool rereadBarWanted() const { return rereadBarDue && rereading(); }
  const controls::Zone& rereadButtonZone() const { return rereadButton; }
  checkpoint::NavSource takePageSource();
  // The visible offset of a word's start (the page holding it), and a chapter's word count.
  bool wordOffset(int spine, uint32_t word, uint32_t& offset);
  bool chapterWords(int spine, uint32_t& words);
  // The words of a page of `spine` (pageWords()) through page view's word index.
  bool pageWordsOf(const readingsession::PageSpan& page, PageWords& words);

 private:
  // The word index page view maps pages through, and the sections of one chapter. One heap block for
  // a prepared book only: ChapterSections is 6 KB (MAX_SECTIONS spans), the reader 1.9 KB.
  struct Layout {
    checkpoint::ChapterSections sections;
    int sectionsSpine = -1;
    bool sectionsLoaded = false;
    ChapterWordIndex pageIndex;
  };

  bool openPageIndex(int spine, ChapterWordIndex::BuildNotice notice = nullptr, void* noticeContext = nullptr);
  // checkpoint::planRestore() for a state with a checkpoint in progress, and what the reader does.
  readingsession::OpenRestore restoreActive(std::shared_ptr<checkpointentry::Progress> made);
  // BookIndexLayout's callbacks (`context`: the session; RestoreNotice for "Preparing...").
  static wordindex::WordIndexReader* restoreChapter(void* context, uint16_t chapter);
  static int restoreSectionSpine(void* context, uint16_t section);
  int hrefChapterOf(int spine) const;
  // The loaded chapter has a section with a checkpoint (layout must exist).
  bool hasCheckpointSection() const;
  bool save(const checkpoint::Transition& t, const char* reason);
  // The id of the section being reread, or "-", for the log lines.
  const char* rereadName() const;

  std::shared_ptr<Epub> epub;
  std::shared_ptr<CheckpointBook> book;
  std::unique_ptr<Layout> layout;
  checkpoint::ReadingMode currentMode = checkpoint::ReadingMode::Page;
  PageReading pageReading;
  checkpoint::NavSource pageSource = checkpoint::NavSource::TableOfContents;

  // The checkpoint in progress (task C4): its run and its flow and state, from the Fire step until it
  // ends; null otherwise.
  std::shared_ptr<readingsession::CheckpointRun> current;
  std::shared_ptr<checkpointentry::Progress> progress;
  // Written under the RenderLock; read without it by the reader's main loop (readingsession::promptDue()).
  std::atomic<bool> promptWaiting{false};
  bool rsvpResumeValid = false;
  checkpoint::Place rsvpResume;
  uint16_t rsvpResumeFrom = 0;
  controls::Zone rereadButton{controls::Item::BackToQuestion, 0, 0, 0, 0, false};
  bool rereadBarDue = false;
  // While restoreOnOpen() runs: the renderer for "Preparing..." when a word index must be built.
  GfxRenderer* restoreRenderer = nullptr;
};

}  // namespace reading

#endif  // CROSSPOINT_RSVP_ENABLED
