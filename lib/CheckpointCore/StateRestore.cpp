#include "StateRestore.h"

#include <cstring>

namespace checkpoint {

BookChange applyBookChangeRule(BookState& state, const CheckpointIndex* index) {
  BookChange change;
  if (index == nullptr || !index->isOpen()) {
    return change;
  }
  const char* bookSha = index->sourceSha256();
  if (state.bookSha256[0] == '\0') {
    memcpy(state.bookSha256, bookSha, SHA256_HEX_CHARS + 1);
    change.adoptedSha = true;
    return change;
  }
  if (strncmp(state.bookSha256, bookSha, SHA256_HEX_CHARS + 1) == 0) {
    return change;
  }
  change.shaMismatch = true;
  const uint16_t dropped = state.grades.retainPromptsOf(*index);
  change.gradesDropped = dropped == 0xFFFF ? 0 : dropped;
  change.positionDropped = state.hasPosition;
  change.activeDropped = state.hasActive;
  state.hasPosition = false;
  state.position = Place{};
  state.hasActive = false;
  state.active = ActiveCheckpoint{};
  memcpy(state.bookSha256, bookSha, SHA256_HEX_CHARS + 1);
  return change;
}

bool positionInBook(const Position& position, BookLayout& layout) {
  uint32_t words = 0;
  return position.chapter < layout.chapterCount() && layout.chapterWordCount(position.chapter, words) &&
         position.word <= words;
}

RestorePlan planRestore(BookState& state, const CheckpointIndex* index, BookLayout& layout) {
  RestorePlan plan;
  if (state.hasActive) {
    ActiveCheckpoint& a = state.active;
    uint16_t problem = 0;  // one RestoreNote bit
    int c = -1;
    SectionRange range;
    if (index == nullptr || !index->isOpen()) {
      problem = NOTE_NO_CHECKPOINT_DATA;
    } else if ((c = index->findCheckpoint(a.id)) < 0) {
      problem = NOTE_UNKNOWN_CHECKPOINT;
    } else if (a.promptIndex >= index->promptCount(static_cast<uint16_t>(c))) {
      problem = NOTE_PROMPT_OUT_OF_RANGE;
    } else {
      const uint16_t section = index->checkpointSection(static_cast<uint16_t>(c));
      if (!layout.sectionRange(section, range) || range.start >= range.end ||
          !positionInBook(Position{range.chapter, range.end - 1}, layout)) {
        problem = NOTE_SECTION_UNRESOLVED;
      }
      range.section = section;
    }
    const bool resumeInBook = positionInBook(a.resume.position, layout);
    if (problem == 0 && !resumeInBook) {
      problem = NOTE_RESUME_OUTSIDE_BOOK;
    }
    if (problem == 0) {
      if (a.rereading && (a.reread.position.chapter != range.chapter || a.reread.position.word < range.start ||
                          a.reread.position.word >= range.end)) {
        plan.notes = static_cast<uint16_t>(plan.notes | NOTE_REREAD_OUTSIDE_SECTION);
        a.rereading = false;
        a.reread = Place{};
      }
      plan.view = a.rereading ? OpenView::Reread : OpenView::Prompt;
      plan.checkpoint = static_cast<uint16_t>(c);
      plan.section = range;
      plan.place = a.rereading ? a.reread : a.resume;
      return plan;
    }
    plan.notes = static_cast<uint16_t>(plan.notes | problem);
    if (resumeInBook) {
      state.hasPosition = true;
      state.position = a.resume;
    } else if (problem != NOTE_RESUME_OUTSIDE_BOOK) {
      plan.notes = static_cast<uint16_t>(plan.notes | NOTE_RESUME_OUTSIDE_BOOK);
    }
    state.hasActive = false;
    state.active = ActiveCheckpoint{};
  }
  if (state.hasPosition && positionInBook(state.position.position, layout)) {
    plan.view = OpenView::Position;
    plan.place = state.position;
    return plan;
  }
  if (state.hasPosition) {
    plan.notes = static_cast<uint16_t>(plan.notes | NOTE_POSITION_OUTSIDE_BOOK);
    state.hasPosition = false;
    state.position = Place{};
  }
  plan.view = OpenView::BookStart;
  return plan;
}

}  // namespace checkpoint
