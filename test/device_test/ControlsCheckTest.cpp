#include <gtest/gtest.h>

#include <string>

#include "activities/devicetest/ControlsCheck.h"

using namespace devicetest;

namespace {

constexpr controls::SideKeys DEFAULT_KEYS{controls::LogicalButton::PageBack, controls::LogicalButton::PageForward};
constexpr uint16_t UP_LOGICAL =
    logicalBit(LOGICAL_UP) | logicalBit(LOGICAL_PAGE_BACK) | logicalBit(10 /* NavPrevious */);
constexpr uint16_t DOWN_LOGICAL =
    logicalBit(LOGICAL_DOWN) | logicalBit(LOGICAL_PAGE_FORWARD) | logicalBit(9 /* NavNext */);

struct Driver {
  KeyTracker tracker;
  uint32_t now = 0;
  uint8_t down = 0;
  std::vector<ControlEvent> events;

  void frame(const uint8_t pressed, const uint8_t released, const uint16_t logicalPressed = 0,
             const uint16_t logicalReleased = 0) {
    down = static_cast<uint8_t>((down | pressed) & ~released);
    KeyFrame f;
    f.nowMs = now;
    f.pressed = pressed;
    f.released = released;
    f.down = down;
    f.logicalPressed = logicalPressed;
    f.logicalReleased = logicalReleased;
    ControlEvent out[RAW_KEY_COUNT];
    const size_t n = tracker.feed(f, DEFAULT_KEYS, true, out, RAW_KEY_COUNT);
    for (size_t i = 0; i < n; ++i) events.push_back(out[i]);
  }
  void idle(const uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) {
      now += 10;
      frame(0, 0);
    }
  }
  void press(const uint8_t raw, const uint32_t holdMs, const uint16_t logical) {
    now += 10;
    frame(static_cast<uint8_t>(1u << raw), 0, logical);
    idle(holdMs - 10);
    now += 10;
    frame(0, static_cast<uint8_t>(1u << raw), 0, raw == RAW_POWER ? logicalBit(LOGICAL_CONFIRM) : 0);
  }
};

}  // namespace

TEST(ControlsCheck, AKeyHeldWhenTheScreenOpensIsIgnoredUntilReleased) {
  Driver d;
  d.down = 1u << RAW_DOWN;  // the hold that chose "Controls check"
  d.tracker.restart(0);
  d.idle(500);
  EXPECT_FALSE(d.tracker.armed());
  d.now += 10;
  d.frame(0, 1u << RAW_DOWN);  // its release arms the tracker, and reports nothing
  EXPECT_TRUE(d.tracker.armed());
  EXPECT_TRUE(d.events.empty());
  d.press(RAW_DOWN, 100, DOWN_LOGICAL);
  ASSERT_EQ(d.events.size(), 1u) << "the next press is the first event";
  EXPECT_EQ(d.events[0].heldMs, 100u);
}

TEST(ControlsCheck, ShortPressesAndHoldsOfTheSideKeysAreTimedAndMapped) {
  Driver d;
  d.tracker.restart(0);
  d.idle(400);
  ASSERT_TRUE(d.tracker.armed());
  d.press(RAW_UP, 120, UP_LOGICAL);
  d.press(RAW_DOWN, 1000, DOWN_LOGICAL);
  d.press(RAW_CONFIRM, 300, logicalBit(LOGICAL_CONFIRM));
  ASSERT_EQ(d.events.size(), 3u);

  EXPECT_EQ(d.events[0].raw, RAW_UP);
  EXPECT_EQ(d.events[0].kind, EventKind::SHORT);
  EXPECT_EQ(d.events[0].heldMs, 120u);
  EXPECT_EQ(d.events[0].role, Role::PREV);
  EXPECT_EQ(d.events[0].logical, UP_LOGICAL);

  EXPECT_EQ(d.events[1].raw, RAW_DOWN);
  EXPECT_EQ(d.events[1].kind, EventKind::HOLD) << "held past controls::HOLD_MS";
  EXPECT_EQ(d.events[1].heldMs, 1000u);
  EXPECT_EQ(d.events[1].role, Role::NEXT);

  EXPECT_EQ(d.events[2].role, Role::CONFIRM);
  EXPECT_FALSE(d.tracker.exitRequested());

  char line[160];
  describeEvent(d.events[1], line, sizeof(line));
  EXPECT_STREQ(line, "key BTN_DOWN (5) -> Down PageForward NavNext, next, 1000 ms, hold");
}

TEST(ControlsCheck, HoldThresholdIsTheControlMapsOwn) {
  Driver d;
  d.tracker.restart(0);
  d.idle(400);
  d.press(RAW_UP, controls::HOLD_MS - 10, UP_LOGICAL);
  d.press(RAW_UP, controls::HOLD_MS, UP_LOGICAL);
  ASSERT_EQ(d.events.size(), 2u);
  EXPECT_EQ(d.events[0].kind, EventKind::SHORT);
  EXPECT_EQ(d.events[1].kind, EventKind::HOLD);
}

TEST(ControlsCheck, APowerClickIsAlwaysShortAndNeverLeaves) {
  Driver d;
  d.tracker.restart(0);
  d.idle(400);
  d.press(RAW_POWER, 3500, logicalBit(6 /* Power */));
  ASSERT_EQ(d.events.size(), 1u);
  EXPECT_EQ(d.events[0].kind, EventKind::SHORT);
  EXPECT_FALSE(d.tracker.exitRequested());
  // "Short power button: Confirm" turns the release into Confirm: both are recorded.
  EXPECT_EQ(d.events[0].logical, logicalBit(6) | logicalBit(LOGICAL_CONFIRM));
}

TEST(ControlsCheck, AThreeSecondHoldOfEitherSideKeyAloneLeaves) {
  for (const uint8_t raw : {RAW_UP, RAW_DOWN, RAW_CONFIRM}) {
    SCOPED_TRACE(raw);
    Driver d;
    d.tracker.restart(0);
    d.idle(400);
    d.now += 10;
    d.frame(static_cast<uint8_t>(1u << raw), 0, raw == RAW_UP ? UP_LOGICAL : DOWN_LOGICAL);
    d.idle(EXIT_HOLD_MS - 10);
    EXPECT_FALSE(d.tracker.exitRequested());
    d.idle(10);
    ASSERT_TRUE(d.tracker.exitRequested());
    ASSERT_EQ(d.events.size(), 1u);
    EXPECT_EQ(d.events[0].kind, EventKind::EXIT_HOLD);
    EXPECT_EQ(d.events[0].heldMs, EXIT_HOLD_MS);
    // Nothing more is reported once the owner has asked to leave.
    d.now += 10;
    d.frame(0, static_cast<uint8_t>(1u << raw));
    EXPECT_EQ(d.events.size(), 1u);
  }
}

TEST(ControlsCheck, RolesFollowASwappedLayout) {
  const controls::SideKeys swapped{controls::LogicalButton::PageForward, controls::LogicalButton::PageBack};
  EXPECT_EQ(roleOf(UP_LOGICAL, swapped, true), Role::NEXT);
  EXPECT_EQ(roleOf(DOWN_LOGICAL, swapped, true), Role::PREV);
  EXPECT_EQ(roleOf(logicalBit(LOGICAL_CONFIRM), swapped, false), Role::NONE) << "KEYS.confirmSelects false";
  // The fallbacks when the layout gives no pair: Up and Down themselves.
  const controls::SideKeys fallback = controls::sideKeys(controls::KEYS, false);
  EXPECT_EQ(roleOf(logicalBit(LOGICAL_UP), fallback, true), Role::PREV);
}

TEST(ControlsCheck, SummaryTicksOffWhatWasTried) {
  ControlEvent e[4];
  e[0].raw = RAW_UP;
  e[0].kind = EventKind::SHORT;
  e[1].raw = RAW_DOWN;
  e[1].kind = EventKind::HOLD;
  e[2].raw = RAW_POWER;
  e[3].source = EventSource::TOUCH;
  e[3].kind = EventKind::TAP;
  const ControlsSummary s = summarise(e, 4);
  EXPECT_TRUE(s.upPressed);
  EXPECT_FALSE(s.upHeld);
  EXPECT_TRUE(s.downPressed);
  EXPECT_TRUE(s.downHeld);
  EXPECT_FALSE(s.confirmPressed);
  EXPECT_TRUE(s.powerClicked);
  EXPECT_TRUE(s.touched);

  char line[96];
  e[3].x = 120;
  e[3].y = 300;
  describeEvent(e[3], line, sizeof(line));
  EXPECT_STREQ(line, "touch tap 120,300");
  e[3].kind = EventKind::SWIPE_LEFT;
  e[3].x2 = 20;
  e[3].y2 = 310;
  describeEvent(e[3], line, sizeof(line));
  EXPECT_STREQ(line, "touch swipe-left 120,300 -> 20,310");
}

TEST(ControlsCheck, NamesMatchHalGpioAndMappedInputManager) {
  EXPECT_STREQ(rawKeyName(0), "BTN_BACK");
  EXPECT_STREQ(rawKeyName(RAW_CONFIRM), "BTN_CONFIRM");
  EXPECT_STREQ(rawKeyName(RAW_UP), "BTN_UP");
  EXPECT_STREQ(rawKeyName(RAW_DOWN), "BTN_DOWN");
  EXPECT_STREQ(rawKeyName(RAW_POWER), "BTN_POWER");
  EXPECT_STREQ(rawKeyName(7), "?");
  EXPECT_STREQ(logicalName(0), "Back");
  EXPECT_STREQ(logicalName(LOGICAL_PAGE_BACK), "PageBack");
  EXPECT_STREQ(logicalName(10), "NavPrevious");
}
