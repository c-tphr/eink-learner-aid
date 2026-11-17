#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <WordSource.h>

#include <cstdio>

#include "ReadingSessionRules.h"
#include "RsvpPosition.h"

namespace reading {

namespace {

// "rsvp-<id>-<suffix>": the parser has checked that the file names its anchors so.
bool anchorWord(wordindex::WordIndexReader& reader, const char* id, const char* suffix, uint32_t& word) {
  char name[checkpoint::ID_BUFFER_BYTES + 16];
  const int n = snprintf(name, sizeof(name), "rsvp-%s-%s", id, suffix);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(name)) return false;
  wordindex::Anchor anchor;
  if (!reader.findAnchor(std::string_view(name, static_cast<size_t>(n)), anchor)) return false;
  word = anchor.wordIndex;
  return true;
}

}  // namespace

bool ChapterLayout::chapterWordCount(const uint16_t chapter, uint32_t& count) {
  if (chapter != readerChapter) return false;
  count = words.wordCount();
  return true;
}

bool ChapterLayout::sectionRange(const uint16_t section, checkpoint::SectionRange& out) {
  const char* id = checkpoints.sectionId(section);
  if (id == nullptr || *id == '\0') return false;
  uint32_t start = 0;
  uint32_t end = 0;
  if (!anchorWord(words, id, "start", start) || !anchorWord(words, id, "end", end)) return false;
  out.section = section;
  out.chapter = readerChapter;
  out.start = start;
  out.end = end;
  return true;
}

uint8_t sectionEndFlags(const checkpoint::ChapterSections& sections, const uint32_t firstAnchorIndex,
                        const uint32_t lastAnchorIndex) {
  uint8_t flags = 0;
  for (uint16_t i = 0; i < sections.count(); ++i) {
    const checkpoint::SectionSpan& span = sections.span(i);
    if (span.end < firstAnchorIndex) continue;
    if (span.end > lastAnchorIndex) break;  // sorted and disjoint: the ends increase
    flags |= rsvp::TOKEN_SECTION_END;
    if (span.checkpoint != checkpoint::NO_INDEX) flags |= rsvp::TOKEN_CHECKPOINT;
  }
  return flags;
}

checkpoint::Step rsvpCheckpointStep(const checkpoint::ChapterSections& sections, const uint32_t lastShown,
                                    const uint32_t next, const uint16_t rereadSection) {
  const checkpoint::Step step = checkpoint::rsvpStep(sections, lastShown, rereadSection);
  if (step.kind != checkpoint::StepKind::None || next <= lastShown + 1) return step;
  const uint16_t chapter = sections.chapter();
  return checkpoint::evaluateMove(sections, checkpoint::Position{chapter, lastShown},
                                  checkpoint::Position{chapter, next}, checkpoint::NavSource::RsvpPlayback,
                                  rereadSection);
}

PageWords pageWords(wordindex::WordIndexReader& reader, const uint32_t pageStartOffset,
                    const uint32_t nextPageStartOffset) {
  PageWords page;
  const uint32_t count = reader.wordCount();
  page.first = entryWord(reader, pageStartOffset);
  const uint32_t after = nextPageStartOffset == NO_NEXT_PAGE ? count : entryWord(reader, nextPageStartOffset);
  if (page.first >= count || after <= page.first) return page;
  page.any = true;
  page.last = after - 1;
  return page;
}

uint32_t pageFromWord(const PageWords& page, const uint16_t chapter, const PageReading& reading) {
  if (reading.valid && reading.chapter == chapter && reading.word > page.first && reading.word <= page.last) {
    return reading.word;
  }
  return page.first;
}

PageResume pageResumeFor(const uint16_t chapter, const uint32_t chapterWords, const PageWords& page,
                         const checkpoint::Position& resume) {
  if (resume.chapter != chapter) return PageResume::Chapter;
  if (resume.word >= chapterWords) return PageResume::EndOfBook;
  if (page.any && resume.word >= page.first && resume.word <= page.last) return PageResume::ThisPage;
  return PageResume::LaterPage;
}

const char* pageResumeName(const PageResume resume) {
  switch (resume) {
    case PageResume::ThisPage:
      return "this-page";
    case PageResume::LaterPage:
      return "later-page";
    case PageResume::Chapter:
      return "chapter";
    case PageResume::EndOfBook:
      return "end-of-book";
  }
  return "?";
}

uint32_t intoSection(const checkpoint::SectionRange& range, const uint32_t word) {
  if (range.end <= range.start) return range.start;
  if (word < range.start) return range.start;
  if (word >= range.end) return range.end - 1;
  return word;
}

RereadPagePlace rereadPagePlace(const PageWords& page, const uint16_t chapter, const PageReading& reading,
                                const checkpoint::SectionRange& range) {
  RereadPagePlace place;
  place.word = page.first;
  if (chapter != range.chapter || range.end <= range.start) return place;
  const uint32_t last = page.any ? page.last : page.first;
  if (last < range.start || page.first >= range.end) return place;
  place.inside = true;
  place.word = intoSection(range, page.any ? pageFromWord(page, chapter, reading) : page.first);
  return place;
}

bool rereadStepStays(const checkpoint::SectionRange& range, const uint32_t to) {
  return to >= range.start && to < range.end;
}

bool BookIndexLayout::chapterWordCount(const uint16_t chapter, uint32_t& count) {
  if (chapter >= chapters) return false;
  wordindex::WordIndexReader* reader = openFn(fnContext, chapter);
  if (reader == nullptr) return false;
  count = reader->wordCount();
  return true;
}

bool BookIndexLayout::sectionRange(const uint16_t section, checkpoint::SectionRange& out) {
  const int spine = spineFn(fnContext, section);
  if (spine < 0 || spine >= chapters) return false;
  wordindex::WordIndexReader* reader = openFn(fnContext, static_cast<uint16_t>(spine));
  if (reader == nullptr) return false;
  ChapterLayout chapter(checkpoints, chapters, static_cast<uint16_t>(spine), *reader);
  return chapter.sectionRange(section, out);
}

OpenAction openActionFor(const checkpoint::RestorePlan& plan) {
  OpenAction action;
  action.at = plan.place;
  switch (plan.view) {
    case checkpoint::OpenView::Reread:
      // The page under RSVP holds the reread word too, so no page after the section shows first.
      action.show = OpenAction::Show::Reread;
      action.jump = true;
      action.speedRead = plan.place.mode == checkpoint::ReadingMode::Rsvp;
      break;
    case checkpoint::OpenView::Prompt:
      action.show = OpenAction::Show::Prompt;
      break;
    case checkpoint::OpenView::Position:
      // A dropped checkpoint's resume (or the position before its break): page view goes there, RSVP
      // opens there by its own rule (the saved word).
      action.jump = plan.place.mode == checkpoint::ReadingMode::Page;
      action.speedRead = plan.place.mode == checkpoint::ReadingMode::Rsvp;
      break;
    case checkpoint::OpenView::BookStart:
      action.at = checkpoint::Place{};
      break;
  }
  return action;
}

const char* openViewName(const checkpoint::OpenView view) {
  switch (view) {
    case checkpoint::OpenView::Reread:
      return "reread";
    case checkpoint::OpenView::Prompt:
      return "prompt";
    case checkpoint::OpenView::Position:
      return "position";
    case checkpoint::OpenView::BookStart:
      return "book-start";
  }
  return "?";
}

void restoreNoteNames(const uint16_t notes, char* out, const size_t size) {
  static const struct {
    uint16_t bit;
    const char* name;
  } NAMES[] = {
      {checkpoint::NOTE_NO_CHECKPOINT_DATA, "no-checkpoint-data"},
      {checkpoint::NOTE_UNKNOWN_CHECKPOINT, "unknown-checkpoint"},
      {checkpoint::NOTE_PROMPT_OUT_OF_RANGE, "prompt-out-of-range"},
      {checkpoint::NOTE_SECTION_UNRESOLVED, "section-unresolved"},
      {checkpoint::NOTE_RESUME_OUTSIDE_BOOK, "resume-outside-book"},
      {checkpoint::NOTE_REREAD_OUTSIDE_SECTION, "reread-outside-section"},
      {checkpoint::NOTE_POSITION_OUTSIDE_BOOK, "position-outside-book"},
  };
  if (size == 0) return;
  out[0] = '\0';
  size_t used = 0;
  for (const auto& n : NAMES) {
    if ((notes & n.bit) == 0) continue;
    const int w = snprintf(out + used, size - used, "%s%s", used > 0 ? "+" : "", n.name);
    if (w < 0 || static_cast<size_t>(w) >= size - used) return;
    used += static_cast<size_t>(w);
  }
  if (used == 0) snprintf(out, size, "none");
}

}  // namespace reading

#endif  // CROSSPOINT_RSVP_ENABLED
