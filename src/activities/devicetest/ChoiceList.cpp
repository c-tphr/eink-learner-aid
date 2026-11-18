#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include "ChoiceList.h"

namespace devicetest {

void ChoiceList::reset(const uint8_t count, const uint8_t defaultRow, const uint8_t mask) {
  rows = count == 0 ? 1 : (count > controls::CHOICE_ROWS_MAX ? controls::CHOICE_ROWS_MAX : count);
  initial = defaultRow < rows ? defaultRow : 0;
  highlighted = initial;
  repeatMask = mask;
}

ChoiceList::Outcome ChoiceList::handle(const controls::Event& event) {
  Outcome out;
  const controls::Screen s = screen();
  switch (controls::resolve(controls::Ctx::FocusScreen, event.ev)) {
    case controls::Act::FocusPrev:
      highlighted = static_cast<uint8_t>(controls::focusPrev(highlighted));
      out.kind = Kind::MOVED;
      break;
    case controls::Act::FocusNext:
      highlighted = static_cast<uint8_t>(controls::focusNext(s, highlighted));
      out.kind = Kind::MOVED;
      break;
    case controls::Act::Select:
      out.kind = Kind::CHOSEN;
      break;
    case controls::Act::SelectRepeat:
      if (repeats(highlighted)) out.kind = Kind::REPEATED;
      break;
    case controls::Act::TapItem:
      if (const controls::Zone* z = controls::hit(s, event.x, event.y)) {
        highlighted = static_cast<uint8_t>(controls::indexOf(s, z->item));
        out.kind = Kind::CHOSEN;
      }
      break;
    case controls::Act::Back:
      out.kind = Kind::BACK;
      break;
    default:
      break;
  }
  out.row = highlighted;
  return out;
}

}  // namespace devicetest

#endif  // CROSSPOINT_RSVP_ENABLED
