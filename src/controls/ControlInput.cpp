#include "ControlInput.h"

#if CROSSPOINT_RSVP_ENABLED

#include <Arduino.h>
#include <HalGPIO.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/reader/ReaderUtils.h"

namespace controls {

static_assert(HOLD_MS == ReaderUtils::SKIP_HOLD_MS, "page view and the new screens share one hold time");
static_assert(LONG_PRESS_SPEED_READ == CrossPointSettings::SPEED_READ, "KEYS.longPressDefault");

namespace {

using Btn = MappedInputManager::Button;

Btn toButton(const LogicalButton b) {
  switch (b) {
    case LogicalButton::PageBack:
      return Btn::PageBack;
    case LogicalButton::PageForward:
      return Btn::PageForward;
    case LogicalButton::Up:
      return Btn::Up;
    case LogicalButton::Down:
      return Btn::Down;
    case LogicalButton::Confirm:
      return Btn::Confirm;
  }
  return Btn::Confirm;
}

// PREV_NEXT and NEXT_PREV give PageBack and PageForward one key each; the other layouts do not.
bool sideLayoutUsable() {
  return SETTINGS.sideButtonLayout == CrossPointSettings::PREV_NEXT ||
         SETTINGS.sideButtonLayout == CrossPointSettings::NEXT_PREV;
}

int16_t clampCoordinate(const int v) { return static_cast<int16_t>(v < -32768 ? -32768 : (v > 32767 ? 32767 : v)); }

}  // namespace

void ControlInput::restartGuard() { reader.restartGuard(static_cast<uint32_t>(millis())); }

InputFrame ControlInput::sample() const {
  const SideKeys keys = sideKeys(KEYS, sideLayoutUsable());
  const Btn prev = toButton(keys.prev);
  const Btn next = toButton(keys.next);

  InputFrame f;
  f.nowMs = static_cast<uint32_t>(millis());
  f.prevDown = input.isPressed(prev);
  f.nextDown = input.isPressed(next);
  f.prevPressed = input.wasPressed(prev);
  f.nextPressed = input.wasPressed(next);
  f.otherKeyPressed =
      input.wasPressed(Btn::Up) || input.wasPressed(Btn::Down) || input.wasPressed(Btn::Confirm);
  f.confirmReleased = input.wasReleased(Btn::Confirm);

  int x = 0;
  int y = 0;
  f.touchActive = input.isScreenTouchHeld(x, y);
  if (input.wasScreenTouchPressed(x, y)) {
    f.touchDown = true;
    f.x = clampCoordinate(x);
    f.y = clampCoordinate(y);
  }
  if (input.wasScreenTapped(x, y)) {
    f.tapped = true;
    f.x = clampCoordinate(x);
    f.y = clampCoordinate(y);
  }
  f.back = input.wasPressed(Btn::Back);
  f.home = input.wasHomeGesture();
  return f;
}

Event ControlInput::pollFocus() {
  const Event e = reader.pollFocus(sample());
  if (e.ev == Ev::Hold) {
    // Threshold 0: the gesture reader already timed the hold. Marks the long press as fired for the
    // keys that are down and suppresses their release for every later owner.
    for (const Btn b : {Btn::PageBack, Btn::PageForward, Btn::Up, Btn::Down}) input.wasLongPressed(b, 0);
  }
  return e;
}

Event ControlInput::pollPlaying() {
  const Event e = GestureReader::pollPlaying(sample());
  if (e.ev == Ev::TouchDown) gpio.suppressTouchContact();
  return e;
}

}  // namespace controls

#endif  // CROSSPOINT_RSVP_ENABLED
