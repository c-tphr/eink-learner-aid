#include <gtest/gtest.h>

#include "activities/devicetest/ChoiceList.h"

using devicetest::ChoiceList;
using Kind = devicetest::ChoiceList::Kind;

namespace {

controls::Event ev(const controls::Ev e, const int16_t x = 0, const int16_t y = 0) {
  controls::Event out;
  out.ev = e;
  out.x = x;
  out.y = y;
  return out;
}

}  // namespace

TEST(ChoiceList, RowsFollowS4Row34) {
  // Rows 448x72 from y=72, 8 px apart (docs/spikes/S4-control-map.md, row 34).
  for (uint8_t i = 0; i < controls::CHOICE_ROWS_MAX; ++i) {
    const controls::Zone& z = controls::CHOICE_ZONES[i];
    EXPECT_EQ(z.x, 16);
    EXPECT_EQ(z.y, 72 + 80 * i);
    EXPECT_EQ(z.w, 448);
    EXPECT_EQ(z.h, 72);
  }
  // The last row ends above the key hint (y 752-800).
  const controls::Zone& last = controls::CHOICE_ZONES[controls::CHOICE_ROWS_MAX - 1];
  EXPECT_LE(last.y + last.h, 752);
  // The Controls check's Finish button is a full-width target above the hint.
  EXPECT_EQ(controls::CONTROLS_CHECK.count, 1);
  EXPECT_GE(controls::CONTROLS_CHECK_ZONES[0].h, 64);
  EXPECT_LE(controls::CONTROLS_CHECK_ZONES[0].y + controls::CONTROLS_CHECK_ZONES[0].h, 752);
}

TEST(ChoiceList, StartsOnTheDefaultRowAndNeverWraps) {
  ChoiceList list;
  list.reset(5, 0);
  EXPECT_EQ(list.focus(), 0);
  auto o = list.handle(ev(controls::Ev::PrevShort));
  EXPECT_EQ(o.kind, Kind::MOVED);
  EXPECT_EQ(o.row, 0) << "no wrap at the top";
  for (int i = 0; i < 4; ++i) list.handle(ev(controls::Ev::NextShort));
  EXPECT_EQ(list.focus(), 4);
  o = list.handle(ev(controls::Ev::NextShort));
  EXPECT_EQ(o.kind, Kind::MOVED);
  EXPECT_EQ(o.row, 4) << "no wrap at the bottom";
  o = list.handle(ev(controls::Ev::PrevShort));
  EXPECT_EQ(o.row, 3);

  list.reset(3, 2);
  EXPECT_EQ(list.focus(), 2);
  EXPECT_EQ(list.defaultRow(), 2);
}

TEST(ChoiceList, HoldOrConfirmChoosesTheHighlightedRow) {
  ChoiceList list;
  list.reset(4, 0);
  list.handle(ev(controls::Ev::NextShort));
  auto o = list.handle(ev(controls::Ev::Hold));
  EXPECT_EQ(o.kind, Kind::CHOSEN);
  EXPECT_EQ(o.row, 1);
  o = list.handle(ev(controls::Ev::Confirm));
  EXPECT_EQ(o.kind, Kind::CHOSEN) << "KEYS.confirmSelects is true";
  EXPECT_EQ(o.row, 1);
}

TEST(ChoiceList, ATapChoosesTheRowUnderTheFingerAndMovesTheHighlight) {
  ChoiceList list;
  list.reset(4, 0);
  auto o = list.handle(ev(controls::Ev::Tap, 240, 72 + 80 * 2 + 36));
  EXPECT_EQ(o.kind, Kind::CHOSEN);
  EXPECT_EQ(o.row, 2);
  EXPECT_EQ(list.focus(), 2);
  // The 8 px gap between rows, a row past the list's end, and the margin choose nothing.
  EXPECT_EQ(list.handle(ev(controls::Ev::Tap, 240, 148)).kind, Kind::NONE);
  EXPECT_EQ(list.handle(ev(controls::Ev::Tap, 240, 72 + 80 * 5 + 10)).kind, Kind::NONE);
  EXPECT_EQ(list.handle(ev(controls::Ev::Tap, 8, 100)).kind, Kind::NONE);
  EXPECT_EQ(list.focus(), 2);
}

TEST(ChoiceList, OnlyRowsInTheRepeatMaskRepeatWhileHeld) {
  ChoiceList list;
  list.reset(5, 0, 0b0110);  // rows 1 and 2 are steppers
  EXPECT_FALSE(list.repeats(0));
  EXPECT_TRUE(list.repeats(1));
  EXPECT_EQ(list.handle(ev(controls::Ev::HoldRepeat)).kind, Kind::NONE);
  list.handle(ev(controls::Ev::NextShort));
  const auto o = list.handle(ev(controls::Ev::HoldRepeat));
  EXPECT_EQ(o.kind, Kind::REPEATED);
  EXPECT_EQ(o.row, 1);
}

TEST(ChoiceList, BackGestureAsksToLeaveAndOtherEventsDoNothing) {
  ChoiceList list;
  list.reset(2, 1);
  EXPECT_EQ(list.handle(ev(controls::Ev::BackGesture)).kind, Kind::BACK);
  EXPECT_EQ(list.handle(ev(controls::Ev::None)).kind, Kind::NONE);
  EXPECT_EQ(list.handle(ev(controls::Ev::KeyDown)).kind, Kind::NONE);
  EXPECT_EQ(list.handle(ev(controls::Ev::TouchDown)).kind, Kind::NONE);
  EXPECT_EQ(list.focus(), 1);
}

TEST(ChoiceList, CountIsClampedToTheRowsThereAre) {
  ChoiceList list;
  list.reset(12, 9);
  EXPECT_EQ(list.count(), controls::CHOICE_ROWS_MAX);
  EXPECT_EQ(list.focus(), 0) << "a default outside the list falls back to the first row";
  list.reset(0);
  EXPECT_EQ(list.count(), 1);
}

TEST(ChoiceList, EveryControlMapItemHasAName) {
  // The binding table is shared by the RSVP, checkpoint and device test screens: every item, whichever
  // task added it, has a [CTL] log name. ContinueOn (after Incorrect) logs as "Continue", like the banner's.
  const int last = static_cast<int>(controls::Item::ContinueOn);
  for (int i = 0; i <= last; ++i) {
    const char* name = controls::itemName(static_cast<controls::Item>(i));
    ASSERT_NE(name, nullptr);
    EXPECT_STRNE(name, "?") << i;
  }
  EXPECT_STREQ(controls::itemName(controls::Item::ChoiceRow0), "Row0");
  EXPECT_STREQ(controls::itemName(controls::Item::ControlsFinish), "ControlsFinish");
}
