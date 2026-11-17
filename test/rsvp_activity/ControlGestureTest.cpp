// The gesture reader of src/controls/ControlGesture.h: arming guard, short press on release, hold at
// HOLD_MS, repeat every REPEAT_MS, Confirm, taps, and playback edges.

#include <gtest/gtest.h>

#include <vector>

#include "controls/ControlGesture.h"

using namespace controls;

namespace {

// Drives a reader frame by frame, 10 ms apart like the main loop, with key levels kept between frames.
class Driver {
 public:
  explicit Driver(uint32_t start = 10000) : now(start) {}

  GestureReader reader;
  uint32_t now;
  bool prev = false;
  bool next = false;
  bool touch = false;

  Event frame(InputFrame f = {}) {
    f.nowMs = now;
    f.prevDown = prev;
    f.nextDown = next;
    f.touchActive = touch;
    now += 10;
    return reader.pollFocus(f);
  }
  Event press(bool isPrev) {
    InputFrame f;
    (isPrev ? prev : next) = true;
    (isPrev ? f.prevPressed : f.nextPressed) = true;
    return frame(f);
  }
  Event release(bool isPrev) {
    (isPrev ? prev : next) = false;
    return frame();
  }
  // Frames until `ms` have passed; collects every event.
  std::vector<Ev> idle(uint32_t ms) {
    std::vector<Ev> events;
    const uint32_t end = now + ms;
    while (static_cast<int32_t>(now - end) < 0) {
      const Event e = frame();
      if (e.ev != Ev::None) events.push_back(e.ev);
    }
    return events;
  }
};

}  // namespace

TEST(ControlGesture, ShortPressMovesOnReleaseNotOnPress) {
  Driver d;
  EXPECT_EQ(d.press(false).ev, Ev::None);
  EXPECT_TRUE(d.idle(300).empty());
  EXPECT_EQ(d.release(false).ev, Ev::NextShort);
  EXPECT_EQ(d.press(true).ev, Ev::None);
  EXPECT_EQ(d.release(true).ev, Ev::PrevShort);
}

TEST(ControlGesture, HoldFiresOnceAtHoldTimeAndTheReleaseIsSilent) {
  Driver d;
  const uint32_t pressedAt = d.now;
  d.press(false);
  std::vector<Ev> events;
  uint32_t holdAt = 0;
  while (d.now - pressedAt < 900) {
    const uint32_t t = d.now;
    const Event e = d.frame();
    if (e.ev == Ev::Hold) holdAt = t;
    if (e.ev != Ev::None) events.push_back(e.ev);
  }
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0], Ev::Hold);
  EXPECT_EQ(holdAt - pressedAt, HOLD_MS);
  EXPECT_EQ(d.release(false).ev, Ev::None) << "a hold never also moves the highlight";
}

TEST(ControlGesture, ReleaseJustBeforeHoldTimeIsShort) {
  Driver d;
  d.press(true);
  EXPECT_TRUE(d.idle(HOLD_MS - 20).empty());
  EXPECT_EQ(d.release(true).ev, Ev::PrevShort);
}

TEST(ControlGesture, HeldKeyRepeatsEveryRepeatTimeAfterTheHold) {
  Driver d;
  const uint32_t pressedAt = d.now;
  d.press(false);
  std::vector<uint32_t> repeats;
  while (d.now - pressedAt < HOLD_MS + 3 * REPEAT_MS + 50) {
    const uint32_t t = d.now;
    if (d.frame().ev == Ev::HoldRepeat) repeats.push_back(t - pressedAt);
  }
  const std::vector<uint32_t> expected = {HOLD_MS + REPEAT_MS, HOLD_MS + 2 * REPEAT_MS, HOLD_MS + 3 * REPEAT_MS};
  EXPECT_EQ(repeats, expected);
  EXPECT_EQ(d.release(false).ev, Ev::None);
}

TEST(ControlGesture, SecondKeyWhileTheFirstIsDownIsIgnored) {
  Driver d;
  d.press(false);
  EXPECT_EQ(d.press(true).ev, Ev::None);
  EXPECT_EQ(d.release(true).ev, Ev::None);
  EXPECT_EQ(d.release(false).ev, Ev::NextShort);
}

TEST(ControlGesture, GuardIgnoresInputForArmTimeAndUntilEverythingIsUp) {
  Driver d;
  d.reader.restartGuard(d.now);
  EXPECT_FALSE(d.reader.armed());
  // A tap inside the 300 ms is ignored.
  InputFrame tap;
  tap.tapped = true;
  tap.x = 240;
  tap.y = 340;
  EXPECT_EQ(d.frame(tap).ev, Ev::None);
  d.idle(ARM_MS - 20);
  EXPECT_FALSE(d.reader.armed());
  d.idle(30);
  EXPECT_TRUE(d.reader.armed());
  EXPECT_EQ(d.frame(tap).ev, Ev::Tap);
}

TEST(ControlGesture, GuardWaitsForTheKeyThatPausedToBeReleased) {
  Driver d;
  d.next = true;  // the key that paused playback is still down
  d.reader.restartGuard(d.now);
  EXPECT_TRUE(d.idle(1000).empty());
  EXPECT_FALSE(d.reader.armed());
  // Its release does not move the highlight; the next press does.
  EXPECT_EQ(d.release(false).ev, Ev::None);
  EXPECT_TRUE(d.reader.armed());
  d.press(false);
  EXPECT_EQ(d.release(false).ev, Ev::NextShort);
}

TEST(ControlGesture, GuardWaitsForTheFingerToLift) {
  Driver d;
  d.touch = true;
  d.reader.restartGuard(d.now);
  d.idle(500);
  EXPECT_FALSE(d.reader.armed());
  d.touch = false;
  d.frame();
  EXPECT_TRUE(d.reader.armed());
}

TEST(ControlGesture, PressDuringGuardAndReleaseAfterArmingIsIgnored) {
  Driver d;
  d.reader.restartGuard(d.now);
  d.idle(100);
  d.press(false);  // ignored: not armed
  d.idle(400);
  EXPECT_EQ(d.release(false).ev, Ev::None) << "the arming frame reports nothing";
  d.press(false);
  EXPECT_EQ(d.release(false).ev, Ev::NextShort);
}

TEST(ControlGesture, GuardSurvivesMillisWrap) {
  Driver d(0xFFFFFF00u);
  d.reader.restartGuard(d.now);
  d.idle(ARM_MS + 20);  // crosses 2^32
  EXPECT_TRUE(d.reader.armed());
}

TEST(ControlGesture, ConfirmTapAndBack) {
  Driver d;
  InputFrame f;
  f.confirmReleased = true;
  EXPECT_EQ(d.frame(f).ev, Ev::Confirm);
  InputFrame t;
  t.tapped = true;
  t.x = 396;
  t.y = 84;
  const Event e = d.frame(t);
  EXPECT_EQ(e.ev, Ev::Tap);
  EXPECT_EQ(e.x, 396);
  EXPECT_EQ(e.y, 84);
  InputFrame b;
  b.back = true;
  EXPECT_EQ(d.frame(b).ev, Ev::BackGesture);
}

TEST(ControlGesture, PlaybackReportsPressAndTouchDownEdgesWithoutGuard) {
  InputFrame f;
  EXPECT_EQ(GestureReader::pollPlaying(f).ev, Ev::None);
  f.nextPressed = true;
  EXPECT_EQ(GestureReader::pollPlaying(f).ev, Ev::KeyDown);
  InputFrame other;
  other.otherKeyPressed = true;
  EXPECT_EQ(GestureReader::pollPlaying(other).ev, Ev::KeyDown);
  InputFrame touch;
  touch.touchDown = true;
  touch.x = 10;
  touch.y = 700;
  const Event e = GestureReader::pollPlaying(touch);
  EXPECT_EQ(e.ev, Ev::TouchDown);
  EXPECT_EQ(e.y, 700);
  // A held key (the hold that started playback) is not a new press.
  InputFrame held;
  held.nextDown = true;
  EXPECT_EQ(GestureReader::pollPlaying(held).ev, Ev::None);
  // Releases and taps never pause.
  InputFrame tap;
  tap.tapped = true;
  tap.confirmReleased = true;
  EXPECT_EQ(GestureReader::pollPlaying(tap).ev, Ev::None);
  InputFrame home;
  home.home = true;
  EXPECT_EQ(GestureReader::pollPlaying(home).ev, Ev::HomeGesture);
}
