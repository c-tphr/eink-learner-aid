#pragma once

// Turns one input frame into at most one control event (S4 section 5.1): a short press of a side key
// moves the highlight on release, a hold of either side key activates at HOLD_MS and repeats every
// REPEAT_MS, Confirm activates on release, a tap carries its position. A new screen ignores input
// until it has been shown for ARM_MS and every key and finger is up. Pure: no clock or input API;
// ControlInput fills the frame from MappedInputManager.

#include <cstdint>

#include "ControlMap.h"

namespace controls {

// One main-loop frame, already mapped to the two side-key roles (KeyRoles::prev / next).
struct InputFrame {
  uint32_t nowMs = 0;
  bool prevDown = false;  // levels
  bool nextDown = false;
  bool prevPressed = false;  // press edges
  bool nextPressed = false;
  bool otherKeyPressed = false;  // press edge of any other key (Confirm, Power click)
  bool confirmReleased = false;
  bool touchActive = false;  // a finger is on the screen
  bool touchDown = false;    // touch-down edge
  bool tapped = false;       // tap released this frame, at (x, y)
  int16_t x = 0;
  int16_t y = 0;
  bool back = false;  // left-edge swipe or header back tap
  bool home = false;  // bottom-edge swipe
  // A vertical swipe ended this frame; (x, y) is where it started (checkpoint answer box, task C3a).
  bool swipeUp = false;
  bool swipeDown = false;
};

struct Event {
  Ev ev = Ev::None;
  int16_t x = 0;
  int16_t y = 0;
};

class GestureReader {
 public:
  // A new screen (or the pause screen after playback): ignore input until `nowMs + ARM_MS` and until a
  // frame with no key or finger down.
  void restartGuard(uint32_t nowMs);
  bool armed() const { return isArmed; }

  // Focus screens: guard, short/hold classification, hold repeat, Confirm, tap, Back, vertical swipe.
  Event pollFocus(const InputFrame& frame);

  // Playback: no guard, press and touch-down edges only.
  static Event pollPlaying(const InputFrame& frame);

 private:
  enum class Key : uint8_t { None, Prev, Next };

  bool isArmed = true;
  uint32_t armAtMs = 0;
  Key active = Key::None;  // the side key being classified
  uint32_t pressedAtMs = 0;
  bool holdFired = false;
  uint32_t nextRepeatMs = 0;
};

}  // namespace controls
