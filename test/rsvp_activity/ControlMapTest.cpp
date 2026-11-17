// The binding table of src/controls/ControlMap.h: lookups, touch zones, focus order, and the
// "swap Up/Down by editing one line" property (docs/spikes/S4-control-map.md, section 7).

#include <gtest/gtest.h>

#include "controls/ControlGesture.h"
#include "controls/ControlMap.h"

using namespace controls;

TEST(ControlMap, PlaybackTurnsEveryInputIntoPause) {
  EXPECT_EQ(resolve(Ctx::RsvpPlaying, Ev::KeyDown), Act::Pause);
  EXPECT_EQ(resolve(Ctx::RsvpPlaying, Ev::TouchDown), Act::Pause);
  EXPECT_EQ(resolve(Ctx::RsvpPlaying, Ev::HomeGesture), Act::Pause);
  EXPECT_EQ(resolve(Ctx::RsvpPlaying, Ev::BackGesture), Act::Pause);
  // Nothing in playback selects, moves or taps an item.
  EXPECT_EQ(resolve(Ctx::RsvpPlaying, Ev::Hold), Act::None);
  EXPECT_EQ(resolve(Ctx::RsvpPlaying, Ev::Tap), Act::None);
  EXPECT_EQ(resolve(Ctx::RsvpPlaying, Ev::NextShort), Act::None);
  EXPECT_EQ(resolve(Ctx::RsvpPlaying, Ev::Confirm), Act::None);
}

TEST(ControlMap, FocusScreenBindings) {
  EXPECT_EQ(resolve(Ctx::FocusScreen, Ev::PrevShort), Act::FocusPrev);
  EXPECT_EQ(resolve(Ctx::FocusScreen, Ev::NextShort), Act::FocusNext);
  EXPECT_EQ(resolve(Ctx::FocusScreen, Ev::Hold), Act::Select);
  EXPECT_EQ(resolve(Ctx::FocusScreen, Ev::HoldRepeat), Act::SelectRepeat);
  EXPECT_EQ(resolve(Ctx::FocusScreen, Ev::Tap), Act::TapItem);
  EXPECT_EQ(resolve(Ctx::FocusScreen, Ev::BackGesture), Act::Back);
  EXPECT_EQ(resolve(Ctx::FocusScreen, Ev::Confirm), Act::Select);
  // Key press and touch-down edges mean nothing on a focus screen: release and tap do.
  EXPECT_EQ(resolve(Ctx::FocusScreen, Ev::KeyDown), Act::None);
  EXPECT_EQ(resolve(Ctx::FocusScreen, Ev::TouchDown), Act::None);
  EXPECT_EQ(resolve(Ctx::PageView, Ev::Hold), Act::EnterRsvp);
  EXPECT_EQ(resolve(Ctx::FocusScreen, Ev::None), Act::None);
}

TEST(ControlMap, ConfirmSelectsOnlyWhenTheTableSaysSo) {
  KeyRoles noConfirm = KEYS;
  noConfirm.confirmSelects = false;
  EXPECT_EQ(resolve(Ctx::FocusScreen, Ev::Confirm, noConfirm), Act::None);
  EXPECT_EQ(resolve(Ctx::FocusScreen, Ev::Hold, noConfirm), Act::Select);
}

TEST(ControlMap, PauseScreenOrderAndDefault) {
  // S4 section 2 focus order: Exit, Menu, Page view, << sentence, PLAY, sentence >>, WPM -, WPM +,
  // words -, words +, Save default, Save book.
  const Item order[] = {
      Item::Exit,    Item::Menu,  Item::PageView,  Item::SentenceBack, Item::Play,        Item::SentenceForward,
      Item::WpmDown, Item::WpmUp, Item::WordsDown, Item::WordsUp,      Item::SaveDefault, Item::SaveBook};
  ASSERT_EQ(PAUSE.count, 12);
  for (int i = 0; i < PAUSE.count; ++i) EXPECT_EQ(PAUSE.zones[i].item, order[i]) << i;
  EXPECT_EQ(PAUSE.zones[PAUSE.defaultIndex].item, Item::Play);
  // Key paths from the default (S4 section 1): Up x1 back, Up x2 page view, Up x3 menu, Up x4 exit.
  int f = PAUSE.defaultIndex;
  f = focusPrev(f);
  EXPECT_EQ(PAUSE.zones[f].item, Item::SentenceBack);
  f = focusPrev(f);
  EXPECT_EQ(PAUSE.zones[f].item, Item::PageView);
  f = focusPrev(f);
  EXPECT_EQ(PAUSE.zones[f].item, Item::Menu);
  f = focusPrev(f);
  EXPECT_EQ(PAUSE.zones[f].item, Item::Exit);
  EXPECT_EQ(focusPrev(f), f) << "no wrap at the top";
  // Down x1 forward, x2 WPM -, x3 WPM +, x4 words -, x5 words +, x6 save default, x7 save book (S4 rows 5-11).
  const Item down[] = {Item::SentenceForward, Item::WpmDown,     Item::WpmUp,   Item::WordsDown,
                       Item::WordsUp,         Item::SaveDefault, Item::SaveBook};
  f = PAUSE.defaultIndex;
  for (const Item expected : down) {
    f = focusNext(PAUSE, f);
    EXPECT_EQ(PAUSE.zones[f].item, expected) << itemName(expected);
  }
  EXPECT_EQ(focusNext(PAUSE, f), f) << "no wrap at the bottom";
}

TEST(ControlMap, PauseZonesMatchS4AndRespectTargetSizes) {
  struct Expected {
    Item item;
    int x, y, w, h;
  };
  const Expected s4[] = {{Item::Exit, 16, 52, 136, 64},         {Item::Menu, 172, 52, 136, 64},
                         {Item::PageView, 328, 52, 136, 64},    {Item::SentenceBack, 16, 204, 448, 64},
                         {Item::Play, 16, 280, 448, 120},       {Item::SentenceForward, 16, 412, 448, 64},
                         {Item::WpmDown, 16, 488, 96, 64},      {Item::WpmUp, 368, 488, 96, 64},
                         {Item::WordsDown, 16, 564, 96, 64},    {Item::WordsUp, 368, 564, 96, 64},
                         {Item::SaveDefault, 16, 640, 216, 64}, {Item::SaveBook, 248, 640, 216, 64}};
  for (const auto& e : s4) {
    const int i = indexOf(PAUSE, e.item);
    ASSERT_GE(i, 0);
    const Zone& z = PAUSE.zones[i];
    EXPECT_EQ(z.x, e.x);
    EXPECT_EQ(z.y, e.y);
    EXPECT_EQ(z.w, e.w);
    EXPECT_EQ(z.h, e.h);
    EXPECT_GE(z.h, 64);
    EXPECT_GE(z.w, 96);
    EXPECT_LE(z.x + z.w, 480);
    EXPECT_LE(z.y + z.h, 752) << "the bottom hint row stays free";
  }
  EXPECT_GE(PAUSE.zones[indexOf(PAUSE, Item::Play)].h, 120) << "the default primary action is 120 px high";
  // The sentence steps and the four steppers repeat when held; nothing else does.
  for (int i = 0; i < PAUSE.count; ++i) {
    const Item item = PAUSE.zones[i].item;
    const bool repeats = item == Item::SentenceBack || item == Item::SentenceForward || item == Item::WpmDown ||
                         item == Item::WpmUp || item == Item::WordsDown || item == Item::WordsUp;
    EXPECT_EQ(PAUSE.zones[i].repeats, repeats) << itemName(item);
  }
}

TEST(ControlMap, ZonesDoNotOverlapAndHitTestingFindsThem) {
  for (int i = 0; i < PAUSE.count; ++i) {
    for (int j = i + 1; j < PAUSE.count; ++j) {
      const Zone& a = PAUSE.zones[i];
      const Zone& b = PAUSE.zones[j];
      const bool overlap = a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
      EXPECT_FALSE(overlap) << itemName(a.item) << " / " << itemName(b.item);
    }
  }
  // The simulator tokens of S4 section 9.
  EXPECT_EQ(hit(PAUSE, 240, 340)->item, Item::Play);
  EXPECT_EQ(hit(PAUSE, 240, 236)->item, Item::SentenceBack);
  EXPECT_EQ(hit(PAUSE, 240, 444)->item, Item::SentenceForward);
  EXPECT_EQ(hit(PAUSE, 396, 84)->item, Item::PageView);
  EXPECT_EQ(hit(PAUSE, 240, 84)->item, Item::Menu);
  EXPECT_EQ(hit(PAUSE, 84, 84)->item, Item::Exit);
  EXPECT_EQ(hit(PAUSE, 64, 520)->item, Item::WpmDown);
  EXPECT_EQ(hit(PAUSE, 416, 520)->item, Item::WpmUp);
  EXPECT_EQ(hit(PAUSE, 64, 596)->item, Item::WordsDown);
  EXPECT_EQ(hit(PAUSE, 416, 596)->item, Item::WordsUp);
  EXPECT_EQ(hit(PAUSE, 124, 672)->item, Item::SaveDefault);
  EXPECT_EQ(hit(PAUSE, 356, 672)->item, Item::SaveBook);
  // Edges: x + w and y + h are outside.
  EXPECT_EQ(hit(PAUSE, 16, 280)->item, Item::Play);
  EXPECT_EQ(hit(PAUSE, 463, 399)->item, Item::Play);
  EXPECT_EQ(hit(PAUSE, 464, 340), nullptr);
  EXPECT_EQ(hit(PAUSE, 240, 400), nullptr) << "the gap between Play and the next row";
  EXPECT_EQ(hit(PAUSE, 240, 160), nullptr) << "the next-words row is not a target";
  EXPECT_EQ(hit(PAUSE, 240, 520), nullptr) << "the WPM value between its steppers is not a target";
  EXPECT_EQ(hit(PAUSE, 240, 672), nullptr) << "the gap between the save buttons";
  EXPECT_EQ(hit(PAUSE, 240, 776), nullptr);
}

TEST(ControlMap, SideKeysFollowTheLayoutSettingWithTableFallbacks) {
  const SideKeys usable = sideKeys(KEYS, true);
  EXPECT_EQ(usable.prev, LogicalButton::PageBack);
  EXPECT_EQ(usable.next, LogicalButton::PageForward);
  const SideKeys fallback = sideKeys(KEYS, false);
  EXPECT_EQ(fallback.prev, LogicalButton::Up);
  EXPECT_EQ(fallback.next, LogicalButton::Down);
  EXPECT_EQ(KEYS.longPressDefault, LONG_PRESS_SPEED_READ);
}

// The one-line fix of S4 section 7, applied to a copy of the table: Up becomes "next".
TEST(ControlMap, SwappingTheSideKeysIsOneLineOfTheTable) {
  constexpr KeyRoles SWAPPED{LONG_PRESS_SPEED_READ, LogicalButton::PageBack, LogicalButton::PageForward,
                             LogicalButton::Down,   LogicalButton::Up,       true};
  const SideKeys keys = sideKeys(SWAPPED, false);
  EXPECT_EQ(keys.prev, LogicalButton::Down);
  EXPECT_EQ(keys.next, LogicalButton::Up);

  // Pressing the physical Up key (logical Up) briefly now moves the highlight forward.
  const auto upKeyShortPress = [](const SideKeys& k) {
    GestureReader reader;
    InputFrame f;
    f.nowMs = 1000;
    const bool upIsPrev = k.prev == LogicalButton::Up;
    (upIsPrev ? f.prevPressed : f.nextPressed) = true;
    (upIsPrev ? f.prevDown : f.nextDown) = true;
    reader.pollFocus(f);
    InputFrame up;
    up.nowMs = 1100;
    return reader.pollFocus(up).ev;
  };
  EXPECT_EQ(upKeyShortPress(sideKeys(KEYS, false)), Ev::PrevShort);
  EXPECT_EQ(upKeyShortPress(keys), Ev::NextShort);
  EXPECT_EQ(resolve(Ctx::FocusScreen, upKeyShortPress(keys), SWAPPED), Act::FocusNext);
}

TEST(ControlMap, TimingsAreS4s) {
  EXPECT_EQ(HOLD_MS, 700);
  EXPECT_EQ(REPEAT_MS, 400);
  EXPECT_EQ(ARM_MS, 300);
}
