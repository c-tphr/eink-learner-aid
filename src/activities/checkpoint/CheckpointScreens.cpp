#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include "CheckpointScreens.h"

namespace checkpointui {

using checkpoint::FlowEvent;
using checkpoint::FlowState;
using controls::Item;

Screen screenFor(const FlowState state) {
  switch (state) {
    case FlowState::PromptHidden:
      return Screen::Prompt;
    case FlowState::AnswerShown:
      return Screen::Answer;
    case FlowState::IncorrectChoice:
      return Screen::AfterIncorrect;
    case FlowState::Idle:
    case FlowState::Rereading:
    case FlowState::Finished:
      break;
  }
  return Screen::None;
}

const char* screenName(const Screen screen) {
  switch (screen) {
    case Screen::Prompt:
      return "Prompt";
    case Screen::Answer:
      return "Answer";
    case Screen::AfterIncorrect:
      return "Incorrect";
    case Screen::None:
      break;
  }
  return "None";
}

const controls::Screen& layoutOf(const Screen screen) {
  switch (screen) {
    case Screen::Answer:
      return controls::CHECKPOINT_ANSWER;
    case Screen::AfterIncorrect:
      return controls::CHECKPOINT_INCORRECT;
    case Screen::Prompt:
    case Screen::None:
      break;
  }
  return controls::CHECKPOINT_PROMPT;
}

int defaultFocus(const Screen screen, const checkpoint::ReadingMode modeInUse) {
  const controls::Screen& layout = layoutOf(screen);
  if (screen == Screen::AfterIncorrect) {
    return controls::indexOf(layout, modeInUse == checkpoint::ReadingMode::Rsvp ? Item::RereadRsvp : Item::RereadPage);
  }
  return layout.defaultIndex;
}

bool flowEventFor(const Item item, FlowEvent& event) {
  switch (item) {
    case Item::Reveal:
      event = FlowEvent::Reveal;
      return true;
    case Item::SkipPrompt:
      event = FlowEvent::SkipPrompt;
      return true;
    case Item::SkipCheckpoint:
      event = FlowEvent::SkipCheckpoint;
      return true;
    case Item::Correct:
      event = FlowEvent::Correct;
      return true;
    case Item::Incorrect:
      event = FlowEvent::Incorrect;
      return true;
    case Item::RereadRsvp:
      event = FlowEvent::RereadRsvp;
      return true;
    case Item::RereadPage:
      event = FlowEvent::RereadPage;
      return true;
    case Item::ContinueOn:
      event = FlowEvent::Continue;
      return true;
    default:
      return false;  // the answer box, and the RSVP pause screen's items
  }
}

bool offered(const Item item, const FlowState state) {
  if (item == Item::AnswerBox) return state == FlowState::AnswerShown;
  FlowEvent event = FlowEvent::Reveal;
  return flowEventFor(item, event) && checkpoint::CheckpointFlow::accepts(state, event);
}

uint16_t partCount(const uint32_t lines, const uint16_t linesPerPart) {
  if (linesPerPart == 0 || lines <= linesPerPart) return 1;
  const uint32_t parts = (lines + linesPerPart - 1) / linesPerPart;
  return static_cast<uint16_t>(parts > UINT16_MAX ? UINT16_MAX : parts);
}

uint32_t partFirstLine(const uint16_t part, const uint16_t linesPerPart) {
  return static_cast<uint32_t>(part) * linesPerPart;
}

uint32_t partLines(const uint32_t lines, const uint16_t part, const uint16_t linesPerPart) {
  const uint32_t first = partFirstLine(part, linesPerPart);
  if (first >= lines) return 0;
  const uint32_t rest = lines - first;
  return rest < linesPerPart ? rest : linesPerPart;
}

uint16_t movePart(const uint16_t part, const uint16_t parts, const PageMove move) {
  if (parts <= 1) return 0;
  const uint16_t last = static_cast<uint16_t>(parts - 1);
  const uint16_t at = part > last ? last : part;
  switch (move) {
    case PageMove::Cycle:
      return at == last ? 0 : static_cast<uint16_t>(at + 1);
    case PageMove::Next:
      return at == last ? last : static_cast<uint16_t>(at + 1);
    case PageMove::Previous:
      return at == 0 ? 0 : static_cast<uint16_t>(at - 1);
  }
  return at;
}

const char* endName(const End end) {
  switch (end) {
    case End::None:
      return "none";
    case End::Finished:
      return "finished";
    case End::Skipped:
      return "skipped";
    case End::RereadRsvp:
      return "reread-rsvp";
    case End::RereadPage:
      return "reread-page";
    case End::Failed:
      return "failed";
  }
  return "?";
}

End endFor(const FlowEvent event, const checkpoint::Transition& t) {
  if (!t.accepted) return End::None;
  if (t.finished) return event == FlowEvent::SkipCheckpoint ? End::Skipped : End::Finished;
  if (t.state == FlowState::Rereading) return event == FlowEvent::RereadRsvp ? End::RereadRsvp : End::RereadPage;
  return End::None;
}

}  // namespace checkpointui

#endif  // CROSSPOINT_RSVP_ENABLED
