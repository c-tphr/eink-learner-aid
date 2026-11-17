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
                                    const uint32_t next) {
  const checkpoint::Step step = checkpoint::rsvpStep(sections, lastShown);
  if (step.kind != checkpoint::StepKind::None || next <= lastShown + 1) return step;
  const uint16_t chapter = sections.chapter();
  return checkpoint::evaluateMove(sections, checkpoint::Position{chapter, lastShown},
                                  checkpoint::Position{chapter, next}, checkpoint::NavSource::RsvpPlayback);
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

}  // namespace reading

#endif  // CROSSPOINT_RSVP_ENABLED
