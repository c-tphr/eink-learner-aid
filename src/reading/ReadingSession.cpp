#include "ReadingSession.h"

#if CROSSPOINT_RSVP_ENABLED

#include <Epub.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include "CheckpointBook.h"
#include "RsvpPosition.h"
#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "activities/RenderLock.h"
#include "controls/ControlMap.h"

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
    if (!layout)
      LOG_ERR("CPT", "OOM: section layout (%u bytes): checkpoints cannot fire", static_cast<unsigned>(sizeof(Layout)));
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

bool ReadingSession::mayFireIn(const int spine) const {
  if (!layout) return false;
  if (layout->sectionsLoaded && layout->sectionsSpine == spine) return hasCheckpointSection();
  return hrefChapterOf(spine) >= 0;
}

bool ReadingSession::hasCheckpointSection() const {
  for (uint16_t i = 0; i < layout->sections.count(); ++i) {
    if (layout->sections.span(i).checkpoint != checkpoint::NO_INDEX) return true;
  }
  return false;
}

uint8_t ReadingSession::sectionEnds(void* context, const uint32_t firstAnchorIndex, const uint32_t lastAnchorIndex) {
  const auto* self = static_cast<const ReadingSession*>(context);
  if (!self->layout) return 0;
  return sectionEndFlags(self->layout->sections, firstAnchorIndex, lastAnchorIndex);
}

checkpoint::Step ReadingSession::rsvpStop(const int spine, const uint32_t lastShown, const uint32_t next) {
  if (!layout || layout->sectionsSpine != spine) return checkpoint::Step{};
  const checkpoint::Step step = rsvpCheckpointStep(layout->sections, lastShown, next, rereadSection());
  LOG_DBG("CPT", "rsvp stop ch=%d last=%lu next=%lu step=%s reread=%s", spine, static_cast<unsigned long>(lastShown),
          static_cast<unsigned long>(next), stepName(step.kind), rereadName());
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
  if (!layout) return checkpoint::Step{};
  if (!mayFireIn(page.spine)) return checkpoint::Step{};  // no word index needed
  if (!openPageIndex(page.spine)) return checkpoint::Step{};
  wordindex::WordIndexReader& reader = layout->pageIndex.reader();
  if (!loadSections(page.spine, reader) || !hasCheckpointSection()) return checkpoint::Step{};
  words = pageWords(reader, page.startOffset, page.nextStartOffset);
  if (!words.any) return checkpoint::Step{};
  from = pageFromWord(words, static_cast<uint16_t>(page.spine), pageReading);
  const checkpoint::Step step = checkpoint::pageAdvance(layout->sections, from, words.last, rereadSection());
  LOG_DBG("CPT", "page advance ch=%d first=%lu from=%lu last=%lu step=%s reread=%s", page.spine,
          static_cast<unsigned long>(words.first), static_cast<unsigned long>(from),
          static_cast<unsigned long>(words.last), stepName(step.kind), rereadName());
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

bool ReadingSession::pageWordsOf(const readingsession::PageSpan& page, PageWords& words) {
  words = PageWords{};
  if (!openPageIndex(page.spine)) return false;
  words = pageWords(layout->pageIndex.reader(), page.startOffset, page.nextStartOffset);
  return true;
}

checkpoint::NavSource ReadingSession::takePageSource() {
  const checkpoint::NavSource source = pageSource;
  pageSource = checkpoint::NavSource::TableOfContents;  // a page shown without a page turn was a jump
  return source;
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
  // Kept by the session and the caller's result handler for the checkpoint's life (about 170 bytes, once
  // per checkpoint).
  auto made = std::make_shared<readingsession::CheckpointRun>();
  made->request.fire = fire;
  made->request.mode = mode;
  made->request.hasBefore = true;
  made->request.before = before;
  made->outcome = std::make_shared<checkpointentry::Outcome>();
  // The flow and the state it changes, from now until the checkpoint ends, a reread included (78 KB,
  // nearly all of it the grade table; PSRAM on the Picco). Before task C4 CheckpointActivity allocated
  // the same block while it showed. Without it the activity ends as Failed and reading goes on.
  made->request.progress = std::shared_ptr<checkpointentry::Progress>(
      makeUniqueNoThrow<checkpointentry::Progress>(book->index()).release());
  if (!made->request.progress) {
    LOG_ERR("CPT", "OOM: checkpoint flow and state (%u bytes)",
            static_cast<unsigned>(sizeof(checkpointentry::Progress)));
  }
  auto activity = checkpointentry::create(renderer, input, epub->getPath(), book, made->request, made->outcome);
  if (activity) {
    current = made;
    progress = made->request.progress;
    promptWaiting = false;
    run = std::move(made);
  }
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
    case checkpointui::End::RereadPage:
      // startReread() handles these; a caller that ends the checkpoint instead still lets reading go on.
      LOG_ERR("CPT", "checkpoint ended with a reread request: reading resumes after the break");
      break;
    case checkpointui::End::Failed:
    case checkpointui::End::None:
      // Nothing was saved (or the activity was left without an ending): reading must go on.
      LOG_ERR("CPT", "checkpoint %s: reading resumes after the break",
              outcome.end == checkpointui::End::Failed ? "failed to start" : "left without an ending");
      break;
  }
  LOG_DBG("CPT", "resume end=%s mode=%s ch=%u word=%lu reread=%d", checkpointui::endName(outcome.end),
          modeName(place.mode), place.position.chapter, static_cast<unsigned long>(place.position.word),
          run.reread ? 1 : 0);
  if (current.get() == &run) {
    // The checkpoint is over: its flow and state (78 KB) go.
    current.reset();
    progress.reset();
    promptWaiting = false;
  }
  return place;
}

bool ReadingSession::save(const checkpoint::Transition& t, const char* reason) {
  if (!progress || !book) return false;
  return checkpointentry::saveTransition(epub->getPath(), *book, progress->state, t, nullptr, reason);
}

checkpoint::Place ReadingSession::startReread(const readingsession::CheckpointRun& run) {
  if (current.get() == &run) current->reread = true;
  const checkpoint::SectionRange& r = run.request.fire.section;
  checkpoint::Place at;
  at.position = checkpoint::Position{r.chapter, r.start};
  at.mode = run.outcome->end == checkpointui::End::RereadRsvp ? checkpoint::ReadingMode::Rsvp
                                                               : checkpoint::ReadingMode::Page;
  if (rereading()) {
    at = progress->state.active.reread;
  } else {
    LOG_ERR("CPT", "reread start: the flow is not rereading");
  }
  LOG_DBG("CPT", "reread start mode=%s checkpoint=%s section=%s prompt=%u ch=%u start=%lu last_word=%lu",
          modeName(at.mode), book->index().checkpointId(run.request.fire.checkpoint),
          book->index().sectionId(r.section), run.outcome->promptIndex, r.chapter, static_cast<unsigned long>(r.start),
          static_cast<unsigned long>(r.end - 1));
  return at;
}

bool ReadingSession::rereading() const {
  return progress && progress->flow.state() == checkpoint::FlowState::Rereading;
}

uint16_t ReadingSession::rereadSection() const {
  return progress ? progress->flow.rereadSection() : checkpoint::NO_INDEX;
}

const checkpoint::SectionRange& ReadingSession::rereadRange() const {
  static const checkpoint::SectionRange none;
  return progress ? progress->flow.section() : none;
}

checkpoint::Place ReadingSession::rereadPlace() const {
  return rereading() ? progress->state.active.reread : checkpoint::Place{};
}

const char* ReadingSession::rereadName() const {
  const uint16_t section = rereadSection();
  return section == checkpoint::NO_INDEX || !book ? "-" : book->index().sectionId(section);
}

ReadingSession::RereadMove ReadingSession::rereadMoved(const checkpoint::Place& to, const checkpoint::NavSource source,
                                                       const char* why) {
  if (!rereading()) return RereadMove::NotRereading;
  const checkpoint::Place before = progress->state.active.reread;
  const checkpoint::Transition t = progress->flow.rereadMoved(to, source);
  LOG_DBG("CPT", "reread moved by=%s source=%s mode=%s ch=%u word=%lu state=%s left_section=%d", why,
          checkpoint::navSourceName(source), modeName(to.mode), to.position.chapter,
          static_cast<unsigned long>(to.position.word), checkpoint::flowStateName(t.state), t.leftSection ? 1 : 0);
  if (!t.accepted) return RereadMove::Stays;
  if (t.state != checkpoint::FlowState::Rereading) {
    save(t, t.leftSection ? "reread-left-section" : "reread-reached-end");
    promptWaiting = true;
    return RereadMove::BackToPrompt;
  }
  // The callers are the reader's normal save points (RSVP's pauses, page view's page turns): the place is
  // written there, and not at all when it did not change.
  if (progress->state.active.reread != before) save(t, "reread-moved");
  return RereadMove::Stays;
}

bool ReadingSession::endReread(const checkpoint::FlowEvent event, const char* why) {
  if (!rereading()) return false;
  const checkpoint::Transition t = progress->flow.handle(event);
  LOG_DBG("CPT", "reread end by=%s event=%s accepted=%d state=%s", why, checkpoint::flowEventName(event),
          t.accepted ? 1 : 0, checkpoint::flowStateName(t.state));
  if (!t.accepted) return false;
  save(t, checkpoint::flowEventName(event));
  promptWaiting = true;
  return true;
}

std::unique_ptr<Activity> ReadingSession::promptAgain(GfxRenderer& renderer, MappedInputManager& input,
                                                      std::shared_ptr<readingsession::CheckpointRun>& run) {
  run.reset();
  if (!promptWaiting || !current) return nullptr;
  promptWaiting = false;
  run = current;
  // A fresh outcome for these screens; the request (the Fire step, the mode, the flow) stays the
  // checkpoint's. One small block per return to the prompt.
  run->outcome = std::make_shared<checkpointentry::Outcome>();
  auto activity = checkpointentry::create(renderer, input, epub->getPath(), book, run->request, run->outcome);
  if (!activity) run->outcome->end = checkpointui::End::Failed;
  return activity;
}

void ReadingSession::setRsvpResume(const checkpoint::Place& resume, const uint16_t fromChapter) {
  rsvpResumeValid = true;
  rsvpResume = resume;
  rsvpResumeFrom = fromChapter;
}

ReadingSession::RsvpStart ReadingSession::rsvpStart(checkpoint::Place& at, uint16_t& fromChapter) {
  if (rsvpResumeValid) {
    rsvpResumeValid = false;
    at = rsvpResume;
    fromChapter = rsvpResumeFrom;
    return RsvpStart::AfterCheckpoint;
  }
  if (!rereading()) return RsvpStart::None;
  at = progress->state.active.reread;
  at.position.word = intoSection(rereadRange(), at.position.word);
  fromChapter = at.position.chapter;
  return RsvpStart::Reread;
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

bool wantsNextPageOffset(const reading::ReadingSession& session, const int spine) { return session.mayFireIn(spine); }

std::unique_ptr<Activity> nextPageCheckpoint(GfxRenderer& renderer, MappedInputManager& input,
                                             reading::ReadingSession& session, const PageSpan& page,
                                             std::shared_ptr<CheckpointRun>& run) {
  run.reset();
  if (!session.hasCheckpoints()) return nullptr;
  reading::PageWords words;
  uint32_t from = 0;
  const checkpoint::Step step = session.nextPage(page, words, from);
  if (step.kind == checkpoint::StepKind::RereadEnd) {
    // The reread reached its section's end (S4 row 31): the prompt again, before the next page shows.
    if (!session.endReread(checkpoint::FlowEvent::RereadReachedEnd, "next-page")) return nullptr;
    return session.promptAgain(renderer, input, run);
  }
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
  PageResume next;
  const checkpointui::End end = run.outcome->end;
  if (end == checkpointui::End::RereadRsvp || end == checkpointui::End::RereadPage) {
    // Task C4: the reread starts at the section's first word, in the mode chosen.
    const checkpoint::Place start = session.startReread(run);
    next.spine = start.position.chapter;
    if (start.mode == checkpoint::ReadingMode::Rsvp) {
      next.go = PageResume::Go::SpeedRead;  // RSVP opens at the reread place (ReadingSession::rsvpStart())
    } else {
      next.go = PageResume::Go::Offset;
      if (!session.wordOffset(start.position.chapter, start.position.word, next.offset)) next.offset = 0;
      session.setPageReading(start.position.chapter, start.position.word);
    }
    LOG_DBG("CPT", "page resume reread-%s ch=%u word=%lu offset=%lu",
            start.mode == checkpoint::ReadingMode::Rsvp ? "rsvp" : "page", start.position.chapter,
            static_cast<unsigned long>(start.position.word), static_cast<unsigned long>(next.offset));
    return next;
  }
  const checkpoint::Place place = session.endCheckpoint(run);
  const checkpoint::Position& at = place.position;
  next.spine = at.chapter;
  if (place.mode == checkpoint::ReadingMode::Rsvp) {
    // It fired in RSVP and ended here after a reread in page view: RSVP opens after the break, waiting.
    session.setRsvpResume(place, run.request.fire.section.chapter);
    next.go = PageResume::Go::SpeedRead;
    LOG_DBG("CPT", "page resume rsvp ch=%u word=%lu", at.chapter, static_cast<unsigned long>(at.word));
    return next;
  }
  const uint16_t left = run.request.fire.section.chapter;
  uint32_t words = 0;
  if (!session.chapterWords(left, words)) words = run.request.fire.section.end;
  // After a reread the reader shows another page than the one the checkpoint fired on: it goes to the
  // page holding the resume word, never "this page".
  const reading::PageResume kind =
      reading::pageResumeFor(left, words, run.reread ? reading::PageWords{} : run.page, at);
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
  // A chapter skip counts as a table-of-contents jump, as for the trigger rules.
  session.notePageSource(move == PageMove::NextPage       ? checkpoint::NavSource::NextPage
                         : move == PageMove::PreviousPage ? checkpoint::NavSource::PreviousPage
                                                          : checkpoint::NavSource::TableOfContents);
  if (move == PageMove::NextPage) return;  // ordinary forward reading, asked through nextPageCheckpoint()
  // These never fire, whatever section ends they pass (trigger rule 2).
  LOG_DBG(
      "CPT", "move source=%s",
      move == PageMove::PreviousPage ? checkpoint::navSourceName(checkpoint::NavSource::PreviousPage) : "chapter-skip");
}

void pageShown(GfxRenderer& renderer, reading::ReadingSession& session, const PageSpan& page) {
  if (!session.rereading()) return;
  const checkpoint::NavSource source = session.takePageSource();
  const checkpoint::SectionRange& range = session.rereadRange();
  reading::PageWords words;
  reading::RereadPagePlace place;
  if (page.spine == range.chapter) {
    if (!session.pageWordsOf(page, words)) {
      // No word index for the section's own chapter: the reread stays where it was.
      LOG_ERR("CPT", "reread page ch=%d: no word index, the reread place stays", page.spine);
      checkpointentry::drawRereadBar(renderer);
      return;
    }
    place = reading::rereadPagePlace(words, range.chapter, session.pageReadingWord(), range);
  }
  checkpoint::Place to;
  to.position.chapter = static_cast<uint16_t>(page.spine);
  to.position.word = place.word;
  to.mode = checkpoint::ReadingMode::Page;
  LOG_DBG("CPT", "reread page ch=%d first=%lu last=%lu any=%d inside=%d word=%lu", page.spine,
          static_cast<unsigned long>(words.first), static_cast<unsigned long>(words.last), words.any ? 1 : 0,
          place.inside ? 1 : 0, static_cast<unsigned long>(place.word));
  if (session.rereadMoved(to, source, "page") != reading::ReadingSession::RereadMove::Stays) return;
  checkpointentry::drawRereadBar(renderer);
}

bool promptDue(const reading::ReadingSession& session) { return session.promptDue(); }

std::unique_ptr<Activity> rereadPrompt(GfxRenderer& renderer, MappedInputManager& input,
                                       reading::ReadingSession& session, std::shared_ptr<CheckpointRun>& run) {
  run.reset();
  if (!session.promptDue()) return nullptr;
  return session.promptAgain(renderer, input, run);
}

bool rereadBarTapped(const MappedInputManager& input, reading::ReadingSession& session) {
  int x = 0;
  int y = 0;
  if (!session.rereading() || session.rereadPlace().mode != checkpoint::ReadingMode::Page ||
      !input.wasScreenTapped(x, y)) {
    return false;
  }
  const controls::Zone* z = controls::hit(controls::REREAD_BAR, x, y);
  if (z == nullptr) return false;
  LOG_DBG("CTL", "RereadBar select=%s", controls::itemName(z->item));
  RenderLock lock;  // the flow belongs to the render task too
  return session.endReread(checkpoint::FlowEvent::ReturnEarly, "bar");
}

std::unique_ptr<Activity> rereadChoices(GfxRenderer& renderer, MappedInputManager& input,
                                        reading::ReadingSession& session, std::shared_ptr<RereadChoice> chosen) {
  if (!session.rereading() || session.rereadPlace().mode != checkpoint::ReadingMode::Page) return nullptr;
  LOG_DBG("CTL", "RereadBar hold: the list");
  return checkpointentry::createRereadChoices(renderer, input, std::move(chosen));
}

RereadChoice afterRereadChoice(reading::ReadingSession& session, const RereadChoice chosen) {
  if (chosen != RereadChoice::BackToQuestion) return chosen;
  RenderLock lock;
  return session.endReread(checkpoint::FlowEvent::ReturnEarly, "list") ? chosen : RereadChoice::KeepReading;
}

std::shared_ptr<CheckpointRun> checkpointRun(const reading::ReadingSession& session) { return session.checkpointRun(); }

}  // namespace readingsession

#endif  // CROSSPOINT_RSVP_ENABLED
