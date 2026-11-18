#include "CheckpointFlow.h"

#include <cstring>

namespace checkpoint {

const char* flowStateName(FlowState state) {
  switch (state) {
    case FlowState::Idle:
      return "idle";
    case FlowState::PromptHidden:
      return "prompt-hidden";
    case FlowState::AnswerShown:
      return "answer-shown";
    case FlowState::IncorrectChoice:
      return "incorrect-choice";
    case FlowState::Rereading:
      return "rereading";
    case FlowState::Finished:
      return "finished";
  }
  return "";
}

const char* flowEventName(FlowEvent event) {
  switch (event) {
    case FlowEvent::Reveal:
      return "reveal";
    case FlowEvent::Correct:
      return "correct";
    case FlowEvent::Incorrect:
      return "incorrect";
    case FlowEvent::SkipPrompt:
      return "skip-prompt";
    case FlowEvent::SkipCheckpoint:
      return "skip-checkpoint";
    case FlowEvent::RereadRsvp:
      return "reread-rsvp";
    case FlowEvent::RereadPage:
      return "reread-page";
    case FlowEvent::Continue:
      return "continue";
    case FlowEvent::ReturnEarly:
      return "return-early";
    case FlowEvent::RereadReachedEnd:
      return "reread-reached-end";
  }
  return "";
}

bool CheckpointFlow::accepts(FlowState state, FlowEvent event) {
  switch (event) {
    case FlowEvent::Reveal:
      return state == FlowState::PromptHidden;
    case FlowEvent::Correct:
    case FlowEvent::Incorrect:
      return state == FlowState::AnswerShown;
    case FlowEvent::SkipPrompt:
      return state == FlowState::PromptHidden || state == FlowState::AnswerShown || state == FlowState::IncorrectChoice;
    case FlowEvent::SkipCheckpoint:
      return state == FlowState::PromptHidden || state == FlowState::AnswerShown ||
             state == FlowState::IncorrectChoice || state == FlowState::Rereading;
    case FlowEvent::RereadRsvp:
    case FlowEvent::RereadPage:
    case FlowEvent::Continue:
      return state == FlowState::IncorrectChoice;
    case FlowEvent::ReturnEarly:
    case FlowEvent::RereadReachedEnd:
      return state == FlowState::Rereading;
  }
  return false;
}

uint16_t CheckpointFlow::promptCount() const {
  return checkpointNumber == NO_INDEX ? 0 : index.promptCount(checkpointNumber);
}

Grade CheckpointFlow::storedGrade(Timestamp* at) const {
  if (at != nullptr) {
    *at = NO_TIMESTAMP;
  }
  char id[ID_BUFFER_BYTES];
  if (checkpointNumber == NO_INDEX || !book.hasActive ||
      !index.loadPromptId(checkpointNumber, book.active.promptIndex, id, sizeof(id))) {
    return Grade::Ungraded;
  }
  return book.grades.find(id, at);
}

Transition CheckpointFlow::refuse(Refusal refusal) const {
  Transition t;
  t.refusal = refusal;
  t.state = current;
  return t;
}

Transition CheckpointFlow::moved(FlowState to, Save save) {
  current = to;
  Transition t;
  t.accepted = true;
  t.save = save;
  t.state = to;
  return t;
}

Transition CheckpointFlow::start(const Step& fire, ReadingMode mode) {
  if (current != FlowState::Idle && current != FlowState::Finished) {
    return refuse(Refusal::InvalidInState);
  }
  if (fire.kind != StepKind::Fire || fire.checkpoint >= index.checkpointCount() ||
      index.promptCount(fire.checkpoint) == 0 || index.checkpointSection(fire.checkpoint) != fire.section.section) {
    return refuse(Refusal::BadStart);
  }
  checkpointNumber = fire.checkpoint;
  range = fire.section;
  ActiveCheckpoint& a = book.active;
  a = ActiveCheckpoint{};
  const char* id = index.checkpointId(fire.checkpoint);
  const size_t idLength = strlen(id);  // ids in the index are at most MAX_ID_CHARS
  memcpy(a.id, id, idLength < sizeof(a.id) ? idLength : sizeof(a.id) - 1);
  a.promptIndex = 0;
  a.resume = Place{fire.resume, mode};
  book.hasActive = true;
  return moved(FlowState::PromptHidden, Save::Now);
}

Transition CheckpointFlow::restore(const RestorePlan& plan) {
  if (current != FlowState::Idle && current != FlowState::Finished) {
    return refuse(Refusal::InvalidInState);
  }
  const bool reread = plan.view == OpenView::Reread;
  if ((!reread && plan.view != OpenView::Prompt) || !book.hasActive || book.active.rereading != reread ||
      plan.checkpoint >= index.checkpointCount() || book.active.promptIndex >= index.promptCount(plan.checkpoint) ||
      index.checkpointSection(plan.checkpoint) != plan.section.section ||
      strcmp(index.checkpointId(plan.checkpoint), book.active.id) != 0) {
    return refuse(Refusal::BadRestore);
  }
  checkpointNumber = plan.checkpoint;
  range = plan.section;
  return moved(reread ? FlowState::Rereading : FlowState::PromptHidden, Save::No);
}

Transition CheckpointFlow::finish() {
  const Place resume = book.active.resume;
  book.position = resume;
  book.hasPosition = true;
  book.hasActive = false;
  book.active = ActiveCheckpoint{};
  Transition t = moved(FlowState::Finished, Save::Now);
  t.finished = true;
  t.resume = resume;
  return t;
}

Transition CheckpointFlow::advance() {
  if (book.active.promptIndex + 1u >= promptCount()) {
    return finish();
  }
  ++book.active.promptIndex;
  return moved(FlowState::PromptHidden, Save::Now);
}

Transition CheckpointFlow::backToPrompt(bool leftSection) {
  book.active.rereading = false;
  book.active.reread = Place{};
  Transition t = moved(FlowState::PromptHidden, Save::Now);
  t.leftSection = leftSection;
  return t;
}

Refusal CheckpointFlow::grade(Grade g, const char* at, bool& recorded) {
  recorded = false;
  char id[ID_BUFFER_BYTES];
  if (!index.loadPromptId(checkpointNumber, book.active.promptIndex, id, sizeof(id))) {
    return Refusal::PromptIdUnreadable;
  }
  GradeTable::RecordResult r = book.grades.recordFirst(id, g, at);
  if (r == GradeTable::RecordResult::Full) {
    book.grades.retainPromptsOf(index);
    r = book.grades.recordFirst(id, g, at);
  }
  switch (r) {
    case GradeTable::RecordResult::Recorded:
      recorded = true;
      return Refusal::None;
    case GradeTable::RecordResult::AlreadyGraded:
      return Refusal::None;
    case GradeTable::RecordResult::Full:
      return Refusal::GradeTableFull;
    case GradeTable::RecordResult::BadId:
    case GradeTable::RecordResult::BadGrade:
      break;
  }
  return Refusal::PromptIdUnreadable;
}

Transition CheckpointFlow::handle(FlowEvent event, const char* at) {
  if (!accepts(current, event)) {
    return refuse(Refusal::InvalidInState);
  }
  switch (event) {
    case FlowEvent::Reveal:
      return moved(FlowState::AnswerShown, Save::No);
    case FlowEvent::Correct:
    case FlowEvent::Incorrect: {
      const bool correct = event == FlowEvent::Correct;
      bool recorded = false;
      const Refusal refusal = grade(correct ? Grade::Correct : Grade::Incorrect, at, recorded);
      if (refusal != Refusal::None) {
        return refuse(refusal);
      }
      Transition t = correct ? advance() : moved(FlowState::IncorrectChoice, recorded ? Save::Now : Save::No);
      t.gradeRecorded = recorded;
      return t;
    }
    case FlowEvent::SkipPrompt:
    case FlowEvent::Continue:
      return advance();
    case FlowEvent::SkipCheckpoint:
      return finish();
    case FlowEvent::RereadRsvp:
    case FlowEvent::RereadPage:
      book.active.rereading = true;
      book.active.reread.position = Position{range.chapter, range.start};
      book.active.reread.mode = event == FlowEvent::RereadRsvp ? ReadingMode::Rsvp : ReadingMode::Page;
      return moved(FlowState::Rereading, Save::Now);
    case FlowEvent::ReturnEarly:
    case FlowEvent::RereadReachedEnd:
      return backToPrompt(false);
  }
  return refuse(Refusal::InvalidInState);
}

Transition CheckpointFlow::rereadMoved(const Place& to, NavSource source) {
  if (current != FlowState::Rereading) {
    return refuse(Refusal::InvalidInState);
  }
  const Position& from = book.active.reread.position;
  const uint32_t lastWord = range.end - 1;
  const bool forwardPastEnd =
      to.position.chapter > range.chapter || (to.position.chapter == range.chapter && to.position.word > lastWord);
  if (isOrdinaryForward(source) && from.chapter == range.chapter && from.word <= lastWord && forwardPastEnd) {
    return backToPrompt(false);
  }
  if (to.position.chapter != range.chapter || to.position.word < range.start || to.position.word >= range.end) {
    return backToPrompt(true);
  }
  const bool modeChanged = to.mode != book.active.reread.mode;
  book.active.reread = to;
  return moved(FlowState::Rereading, modeChanged ? Save::Now : Save::Position);
}

}  // namespace checkpoint
