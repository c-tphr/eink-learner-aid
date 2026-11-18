#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include "ControlMap.h"

namespace controls {

Act resolve(const Ctx ctx, const Ev ev, const KeyRoles& roles) {
  if (ctx == Ctx::FocusScreen && ev == Ev::Confirm) return roles.confirmSelects ? Act::Select : Act::None;
  for (const Binding& b : BINDINGS) {
    if (b.ctx == ctx && b.ev == ev) return b.act;
  }
  return Act::None;
}

const Zone* hit(const Screen& screen, const int x, const int y) {
  for (uint8_t i = 0; i < screen.count; ++i) {
    const Zone& z = screen.zones[i];
    if (x >= z.x && x < z.x + z.w && y >= z.y && y < z.y + z.h) return &z;
  }
  return nullptr;
}

int indexOf(const Screen& screen, const Item item) {
  for (uint8_t i = 0; i < screen.count; ++i) {
    if (screen.zones[i].item == item) return i;
  }
  return -1;
}

const char* itemName(const Item item) {
  switch (item) {
    case Item::Exit:
      return "Exit";
    case Item::Menu:
      return "Menu";
    case Item::PageView:
      return "PageView";
    case Item::SentenceBack:
      return "SentenceBack";
    case Item::Play:
      return "Play";
    case Item::SentenceForward:
      return "SentenceForward";
    case Item::WpmDown:
      return "WpmDown";
    case Item::WpmUp:
      return "WpmUp";
    case Item::WordsDown:
      return "WordsDown";
    case Item::WordsUp:
      return "WordsUp";
    case Item::SaveDefault:
      return "SaveDefault";
    case Item::SaveBook:
      return "SaveBook";
    case Item::ChoiceRow0:
      return "Row0";
    case Item::ChoiceRow1:
      return "Row1";
    case Item::ChoiceRow2:
      return "Row2";
    case Item::ChoiceRow3:
      return "Row3";
    case Item::ChoiceRow4:
      return "Row4";
    case Item::ChoiceRow5:
      return "Row5";
    case Item::ChoiceRow6:
      return "Row6";
    case Item::ChoiceRow7:
      return "Row7";
    case Item::ControlsFinish:
      return "ControlsFinish";
  }
  return "?";
}

}  // namespace controls

#endif  // CROSSPOINT_RSVP_ENABLED
