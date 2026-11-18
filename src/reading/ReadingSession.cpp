#include "ReadingSession.h"

#if CROSSPOINT_RSVP_ENABLED

#include <Epub.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include "CheckpointBook.h"
#include "RsvpPosition.h"
#include "activities/Activity.h"

namespace reading {

namespace {

const char* modeName(const checkpoint::ReadingMode mode) {
  return mode == checkpoint::ReadingMode::Rsvp ? "rsvp" : "page";
}

const char* stepName(const checkpoint::StepKind kind) {
  switch (kind) {
    case checkpoint::StepKind::None:
      return "none";
    case checkpoint::StepKind::Fire:
      return "fire";
    case checkpoint::StepKind::RereadEnd:
      return "reread-end";
  }
  return "?";
}

}  // namespace

ReadingSession::ReadingSession(std::shared_ptr<Epub> epub, std::shared_ptr<CheckpointBook> book)
    : epub(std::move(epub)), book(std::move(book)) {}

ReadingSession::~ReadingSession() = default;

void ReadingSession::attachLayout() {
  if (book) {
    // 8 KB while a prepared book is open: the section layout and page view's word index reader.
    layout = makeUniqueNoThrow<Layout>();
    if (!layout) LOG_ERR("CPT", "OOM: section layout (%u bytes): checkpoints cannot fire",
                          static_cast<unsigned>(sizeof(Layout)));
  }
  LOG_DBG("CPT", "session open checkpoints=%d sha=%s session_bytes=%u layout_bytes=%u", layout ? 1 : 0,
          book ? book->sourceSha256() : "none", static_cast<unsigned>(sizeof(ReadingSession)),
          layout ? static_cast<unsigned>(sizeof(Layout)) : 0u);
}

BookIdentity ReadingSession::identity() const {
  BookIdentity id;
  if (book) {
    id.sha256 = book->sourceSha256();
    id.index = &book->index();
  }
  return id;
}

const std::string& ReadingSession::bookPath() const { return epub->getPath(); }

void ReadingSession::setMode(const checkpoint::ReadingMode mode) {
  if (mode == currentMode) return;
  currentMode = mode;
  LOG_DBG("CPT", "mode %s source=%s", modeName(mode), checkpoint::navSourceName(checkpoint::NavSource::ModeSwitch));
}

int ReadingSession::hrefChapterOf(const int spine) const {
  // The index numbers the distinct chapter_href values; each names the spine item of its sections.
  const checkpoint::CheckpointIndex& index = book->index();
  for (uint16_t c = 0; c < index.chapterCount(); ++c) {
    const int first = index.nextSectionInChapter(c, -1);
    if (first >= 0 && book->sectionSpine(*epub, static_cast<uint16_t>(first)) == spine) return c;
  }
  return -1;
}

bool ReadingSession::loadSections(const int spine, wordindex::WordIndexReader& reader) {
  if (!layout) return false;
  if (layout->sectionsLoaded && layout->sectionsSpine == spine) return layout->sections.count() > 0;
  layout->sectionsSpine = spine;
  layout->sectionsLoaded = true;
  const int hrefChapter = hrefChapterOf(spine);
  const uint16_t chapter = static_cast<uint16_t>(spine);
  const uint16_t next =
      spine + 1 < epub->getSpineItemsCount() ? static_cast<uint16_t>(spine + 1) : checkpoint::NO_CHAPTER;
  if (hrefChapter < 0) {
    layout->sections.reset(chapter, reader.wordCount(), next);
    return false;
  }
  ChapterLayout chapterLayout(book->index(), static_cast<uint16_t>(epub->getSpineItemsCount()), chapter, reader);
  const checkpoint::ChapterSections::LoadResult r =
      layout->sections.load(book->index(), static_cast<uint16_t>(hrefChapter), chapterLayout, chapter, next);
  uint16_t checkpoints = 0;
  for (uint16_t i = 0; i < layout->sections.count(); ++i) {
    if (layout->sections.span(i).checkpoint != checkpoint::NO_INDEX) ++checkpoints;
  }
  LOG_DBG("CPT", "sections ch=%d words=%lu sections=%u checkpoints=%u failed=%u", spine,
          static_cast<unsigned long>(layout->sections.wordCount()), layout->sections.count(), checkpoints, r.failed);
  if (r.failed > 0) LOG_ERR("CPT", "sections ch=%d: %u section(s) without usable anchors cannot fire", spine, r.failed);
  return layout->sections.count() > 0;
}

uint8_t ReadingSession::sectionEnds(void* context, const uint32_t firstAnchorIndex, const uint32_t lastAnchorIndex) {
  const auto* self = static_cast<const ReadingSession*>(context);
  if (!self->layout) return 0;
  return sectionEndFlags(self->layout->sections, firstAnchorIndex, lastAnchorIndex);
}

checkpoint::Step ReadingSession::rsvpStop(const int spine, const uint32_t lastShown, const uint32_t next) {
  if (!layout || layout->sectionsSpine != spine) return checkpoint::Step{};
  const checkpoint::Step step = rsvpCheckpointStep(layout->sections, lastShown, next);
  LOG_DBG("CPT", "rsvp stop ch=%d last=%lu next=%lu step=%s", spine, static_cast<unsigned long>(lastShown),
          static_cast<unsigned long>(next), stepName(step.kind));
  return step;
}

bool ReadingSession::openPageIndex(const int spine) {
  if (!layout) return false;
  if (layout->pageIndex.isOpen() && layout->pageIndex.spineIndex() == spine) return true;
  layout->pageIndex.close();
  return layout->pageIndex.open(*epub, spine);
}

checkpoint::Step ReadingSession::nextPage(const readingsession::PageSpan& page, PageWords& words, uint32_t& from) {
  words = PageWords{};
  from = 0;
  if (!layout || !openPageIndex(page.spine)) return checkpoint::Step{};
  wordindex::WordIndexReader& reader = layout->pageIndex.reader();
  if (!loadSections(page.spine, reader)) return checkpoint::Step{};
  words = pageWords(reader, page.startOffset, page.nextStartOffset);
  if (!words.any) return checkpoint::Step{};
  from = pageFromWord(words, static_cast<uint16_t>(page.spine), pageReading);
  const checkpoint::Step step = checkpoint::pageAdvance(layout->sections, from, words.last);
  LOG_DBG("CPT", "page advance ch=%d first=%lu from=%lu last=%lu step=%s", page.spine,
          static_cast<unsigned long>(words.first), static_cast<unsigned long>(from),
          static_cast<unsigned long>(words.last), stepName(step.kind));
  return step;
}

void ReadingSession::setPageReading(const uint16_t chapter, const uint32_t word) {
  pageReading.valid = true;
  pageReading.chapter = chapter;
  pageReading.word = word;
}

bool ReadingSession::wordOffset(const int spine, const uint32_t word, uint32_t& offset) {
  return openPageIndex(spine) && pageViewOffset(layout->pageIndex.reader(), word, offset);
}

bool ReadingSession::chapterWords(const int spine, uint32_t& words) {
  if (!openPageIndex(spine)) return false;
  words = layout->pageIndex.reader().wordCount();
  return true;
}

std::unique_ptr<Activity> ReadingSession::startCheckpoint(GfxRenderer& renderer, MappedInputManager& input,
                                                          const checkpoint::Step& fire,
                                                          const checkpoint::ReadingMode mode,
                                                          const checkpoint::Place& before,
                                                          std::shared_ptr<readingsession::CheckpointRun>& run) {
  run.reset();
  if (!book || fire.kind != checkpoint::StepKind::Fire) return nullptr;
  const checkpoint::CheckpointIndex& index = book->index();
  LOG_DBG("CPT", "fire source=%s checkpoint=%s section=%s ch=%u last_word=%lu resume_ch=%u resume_word=%lu crossed=%u",
          checkpoint::navSourceName(mode == checkpoint::ReadingMode::Rsvp ? checkpoint::NavSource::RsvpPlayback
                                                                          : checkpoint::NavSource::NextPage),
          index.checkpointId(fire.checkpoint), index.sectionId(fire.section.section), fire.section.chapter,
          static_cast<unsigned long>(fire.section.end - 1), fire.resume.chapter,
          static_cast<unsigned long>(fire.resume.word), fire.crossed);
  // Kept by the caller's result handler for the checkpoint's life (about 150 bytes, once per checkpoint).
  auto made = std::make_shared<readingsession::CheckpointRun>();
  made->request.fire = fire;
  made->request.mode = mode;
  made->request.hasBefore = true;
  made->request.before = before;
  made->outcome = std::make_shared<checkpointentry::Outcome>();
  auto activity = checkpointentry::create(renderer, input, epub->getPath(), book, made->request, made->outcome);
  if (activity) run = std::move(made);
  return activity;
}

checkpoint::Place ReadingSession::endCheckpoint(const readingsession::CheckpointRun& run) {
  const checkpointentry::Outcome& outcome = *run.outcome;
  // The first word after the break, in the mode the checkpoint appeared in.
  checkpoint::Place place;
  place.position = run.request.fire.resume;
  place.mode = run.request.mode;
  switch (outcome.end) {
    case checkpointui::End::Finished:
    case checkpointui::End::Skipped:
      place = outcome.resume;
      break;
    case checkpointui::End::RereadRsvp:
    case checkpointui::End::RereadPage: {
      // TASK C4 replaces this: reread the section in the chosen mode, then return to the prompt. Until
      // then reading resumes after the break, as for Finished: the grade is saved, and the checkpoint
      // in progress is closed so that nothing restores a reread that does not exist yet.
      LOG_INF("CPT", "reread in %s not implemented yet (task C4): reading resumes after the break",
              outcome.end == checkpointui::End::RereadRsvp ? "rsvp" : "page");
      const bool ok = editBookState(
          epub->getPath(), identity(),
          [](checkpoint::BookState& state, const void* context) {
            state.hasActive = false;
            state.active = checkpoint::ActiveCheckpoint{};
            state.hasPosition = true;
            state.position = *static_cast<const checkpoint::Place*>(context);
          },
          &place);
      if (!ok) LOG_ERR("CPT", "reread stub: state file not saved");
      break;
    }
    case checkpointui::End::Failed:
    case checkpointui::End::None:
      // Nothing was saved (or the activity was left without an ending): reading must go on.
      LOG_ERR("CPT", "checkpoint %s: reading resumes after the break",
              outcome.end == checkpointui::End::Failed ? "failed to start" : "left without an ending");
      break;
  }
  LOG_DBG("CPT", "resume end=%s mode=%s ch=%u word=%lu", checkpointui::endName(outcome.end), modeName(place.mode),
          place.position.chapter, static_cast<unsigned long>(place.position.word));
  return place;
}

}  // namespace reading

namespace readingsession {

std::shared_ptr<reading::ReadingSession> open(const std::shared_ptr<Epub>& epub) {
  if (!epub) return nullptr;
  reading::CheckpointData data = reading::CheckpointData::None;
  // About 72 KB while the book is open, for a prepared book only (CheckpointBook.h).
  std::shared_ptr<reading::CheckpointBook> book = reading::CheckpointBook::open(*epub, &data);
  // The session itself is small; the layout below is allocated for a prepared book only.
  auto made = makeUniqueNoThrow<reading::ReadingSession>(epub, book);
  if (!made) {
    LOG_ERR("CPT", "OOM: reading session (%u bytes)", static_cast<unsigned>(sizeof(reading::ReadingSession)));
    return nullptr;
  }
  std::shared_ptr<reading::ReadingSession> session(std::move(made));
  session->attachLayout();
  return session;
}

bool wantsNextPageOffset(const reading::ReadingSession& session) { return session.hasCheckpoints(); }

std::unique_ptr<Activity> nextPageCheckpoint(GfxRenderer& renderer, MappedInputManager& input,
                                             reading::ReadingSession& session, const PageSpan& page,
                                             std::shared_ptr<CheckpointRun>& run) {
  run.reset();
  if (!session.hasCheckpoints()) return nullptr;
  reading::PageWords words;
  uint32_t from = 0;
  const checkpoint::Step step = session.nextPage(page, words, from);
  if (step.kind != checkpoint::StepKind::Fire) return nullptr;
  checkpoint::Place before;
  before.position.chapter = static_cast<uint16_t>(page.spine);
  before.position.word = from;
  before.mode = checkpoint::ReadingMode::Page;
  auto activity = session.startCheckpoint(renderer, input, step, checkpoint::ReadingMode::Page, before, run);
  if (run) run->page = words;
  return activity;
}

PageResume afterPageCheckpoint(reading::ReadingSession& session, const CheckpointRun& run) {
  const checkpoint::Place place = session.endCheckpoint(run);
  const checkpoint::Position& at = place.position;
  const uint16_t left = run.request.fire.section.chapter;
  uint32_t words = 0;
  if (!session.chapterWords(left, words)) words = run.request.fire.section.end;
  const reading::PageResume kind = reading::pageResumeFor(left, words, run.page, at);
  PageResume next;
  next.spine = at.chapter;
  switch (kind) {
    case reading::PageResume::ThisPage:
      next.go = PageResume::Go::ThisPage;
      session.setPageReading(at.chapter, at.word);
      break;
    case reading::PageResume::LaterPage:
    case reading::PageResume::Chapter:
      next.go = PageResume::Go::Offset;
      if (!session.wordOffset(at.chapter, at.word, next.offset)) next.offset = 0;
      session.setPageReading(at.chapter, at.word);
      break;
    case reading::PageResume::EndOfBook:
      next.go = PageResume::Go::EndOfBook;
      session.clearPageReading();
      break;
  }
  LOG_DBG("CPT", "page resume %s ch=%u word=%lu offset=%lu", reading::pageResumeName(kind), at.chapter,
          static_cast<unsigned long>(at.word), static_cast<unsigned long>(next.offset));
  return next;
}

void notePageMove(reading::ReadingSession& session, const PageMove move) {
  if (!session.hasCheckpoints()) return;
  session.clearPageReading();
  if (move == PageMove::NextPage) return;  // ordinary forward reading, asked through nextPageCheckpoint()
  // These never fire, whatever section ends they pass (trigger rule 2).
  LOG_DBG("CPT", "move source=%s",
          move == PageMove::PreviousPage ? checkpoint::navSourceName(checkpoint::NavSource::PreviousPage)
                                         : "chapter-skip");
}

}  // namespace readingsession

#endif  // CROSSPOINT_RSVP_ENABLED
