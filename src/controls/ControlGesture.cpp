#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include "ControlGesture.h"

namespace controls {

namespace {

// Wrap-safe "a is at or after b" for millis() values less than 24.8 days apart.
bool reached(const uint32_t now, const uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

}  // namespace

void GestureReader::restartGuard(const uint32_t nowMs) {
  isArmed = false;
  armAtMs = nowMs + ARM_MS;
  active = Key::None;
  holdFired = false;
}

Event GestureReader::pollFocus(const InputFrame& f) {
  Event e;
  if (!isArmed) {
    // The frame that arms reports nothing: a release seen here belongs to the press before the screen.
    if (reached(f.nowMs, armAtMs) && !f.prevDown && !f.nextDown && !f.touchActive) isArmed = true;
    return e;
  }

  if (active == Key::None) {
    if (f.prevPressed || f.nextPressed) {
      active = f.prevPressed ? Key::Prev : Key::Next;
      pressedAtMs = f.nowMs;
      holdFired = false;
    }
  } else {
    const bool down = active == Key::Prev ? f.prevDown : f.nextDown;
    if (!down) {
      const Key released = active;
      active = Key::None;
      if (!holdFired) {
        e.ev = released == Key::Prev ? Ev::PrevShort : Ev::NextShort;
        return e;
      }
    } else if (!holdFired && reached(f.nowMs, pressedAtMs + HOLD_MS)) {
      holdFired = true;
      nextRepeatMs = pressedAtMs + HOLD_MS + REPEAT_MS;
      e.ev = Ev::Hold;
      return e;
    } else if (holdFired && reached(f.nowMs, nextRepeatMs)) {
      nextRepeatMs += REPEAT_MS;
      e.ev = Ev::HoldRepeat;
      return e;
    }
  }

  if (f.confirmReleased) {
    e.ev = Ev::Confirm;
  } else if (f.tapped) {
    e.ev = Ev::Tap;
    e.x = f.x;
    e.y = f.y;
  } else if (f.back) {
    e.ev = Ev::BackGesture;
  } else if (f.swipeUp || f.swipeDown) {
    e.ev = f.swipeUp ? Ev::SwipeUp : Ev::SwipeDown;
    e.x = f.x;
    e.y = f.y;
  }
  return e;
}

Event GestureReader::pollPlaying(const InputFrame& f) {
  Event e;
  if (f.touchDown) {
    e.ev = Ev::TouchDown;
    e.x = f.x;
    e.y = f.y;
  } else if (f.prevPressed || f.nextPressed || f.otherKeyPressed) {
    e.ev = Ev::KeyDown;
  } else if (f.back) {
    e.ev = Ev::BackGesture;
  } else if (f.home) {
    e.ev = Ev::HomeGesture;
  }
  return e;
}

}  // namespace controls

#endif  // CROSSPOINT_RSVP_ENABLED
