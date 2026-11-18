#pragma once

// The Controls check (docs/spikes/S4-control-map.md, sections 7 and 12; plan section 10, step 7):
// every key press, power-button click and touch is shown, logged and written to a results file with
// the raw input (the HalGPIO button index, or the touch event), the logical buttons the firmware
// mapped it to, the control-map role, and how long it was held. Portable part: the event record,
// the names, and the key tracker that times presses and decides when the owner has asked to leave.
// Host-tested in test/device_test; DeviceTestActivity feeds it from HalGPIO and MappedInputManager.
//
// Leaving does not depend on any one input working: a tap on Finish, a 3 s hold of any key that is
// not the power button (either side key alone is enough), the bottom-edge Home swipe, or a power
// hold (which sleeps the device, as everywhere).

#include <cstddef>
#include <cstdint>

#include "controls/ControlMap.h"

namespace devicetest {

// HalGPIO's button indices (lib/hal/HalGPIO.h: BTN_BACK 0 ... BTN_POWER 6).
inline constexpr uint8_t RAW_KEY_COUNT = 7;
inline constexpr uint8_t RAW_CONFIRM = 1;
inline constexpr uint8_t RAW_UP = 4;
inline constexpr uint8_t RAW_DOWN = 5;
inline constexpr uint8_t RAW_POWER = 6;
// "BTN_BACK", "BTN_CONFIRM", "BTN_LEFT", "BTN_RIGHT", "BTN_UP", "BTN_DOWN", "BTN_POWER"; "?" otherwise.
const char* rawKeyName(uint8_t raw);

// Logical buttons, one bit each, in MappedInputManager::Button order (DeviceTestActivity.cpp checks
// the order): Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward, NavNext,
// NavPrevious.
inline constexpr uint8_t LOGICAL_COUNT = 11;
const char* logicalName(uint8_t bit);
inline constexpr uint16_t logicalBit(const uint8_t bit) { return static_cast<uint16_t>(1u << bit); }
inline constexpr uint8_t LOGICAL_CONFIRM = 1;
inline constexpr uint8_t LOGICAL_UP = 4;
inline constexpr uint8_t LOGICAL_DOWN = 5;
inline constexpr uint8_t LOGICAL_PAGE_BACK = 7;
inline constexpr uint8_t LOGICAL_PAGE_FORWARD = 8;

// What the new screens do with an input (controls::KEYS through the Side button layout setting).
enum class Role : uint8_t { NONE, PREV, NEXT, CONFIRM };
const char* roleName(Role role);  // "none", "prev", "next", "confirm"
// The role of a press whose logical buttons are `logical`, with the side keys in use now.
Role roleOf(uint16_t logical, const controls::SideKeys& keys, bool confirmSelects);

enum class EventSource : uint8_t { KEY, TOUCH };
enum class EventKind : uint8_t {
  SHORT,         // a key released before controls::HOLD_MS (a power click is always this)
  HOLD,          // a key held at least controls::HOLD_MS, then released
  EXIT_HOLD,     // a key held EXIT_HOLD_MS: the owner leaves the check
  TAP,           // touch: a tap at (x, y)
  LONG_PRESS,    // touch: held still past the SDK's long-press time, at (x, y)
  SWIPE_LEFT,    // touch: a swipe from (x, y) to (x2, y2)
  SWIPE_RIGHT,
  SWIPE_UP,
  SWIPE_DOWN,
  BACK_GESTURE,  // the left-edge swipe (MappedInputManager Back)
  HOME_GESTURE,  // the bottom-edge swipe: the system leaves the screen for Home
};
const char* eventSourceName(EventSource source);  // "key", "touch"
const char* eventKindName(EventKind kind);        // "short", "hold", "exit-hold", "tap", ...

inline constexpr uint16_t EXIT_HOLD_MS = 3000;
inline constexpr int16_t NO_POSITION = -1;

struct ControlEvent {
  uint32_t atMs = 0;  // millis() when the event ended
  EventSource source = EventSource::KEY;
  EventKind kind = EventKind::SHORT;
  uint8_t raw = 0;       // keys: HalGPIO index
  uint16_t logical = 0;  // keys: logical bits seen at the press and at the release
  Role role = Role::NONE;
  uint32_t heldMs = 0;  // keys: press to release (EXIT_HOLD: EXIT_HOLD_MS)
  int16_t x = NO_POSITION, y = NO_POSITION, x2 = NO_POSITION, y2 = NO_POSITION;  // touch, logical pixels
};

// One main-loop frame of raw key state, bit i = HalGPIO index i.
struct KeyFrame {
  uint32_t nowMs = 0;
  uint8_t pressed = 0;   // press edges
  uint8_t released = 0;  // release edges
  uint8_t down = 0;      // levels
  uint16_t logicalPressed = 0;   // logical buttons with a press edge this frame
  uint16_t logicalReleased = 0;  // logical buttons with a release edge this frame
};

// Times each raw key from its press edge to its release. A key already down when the screen opened
// (the hold that chose "Controls check") is ignored until it is released: the tracker arms after
// controls::ARM_MS once no key is down.
class KeyTracker {
 public:
  void restart(uint32_t nowMs);
  bool armed() const { return isArmed; }
  // Feeds one frame; writes finished presses to `out` (at most `capacity`) and returns how many.
  // A key other than power held EXIT_HOLD_MS gives one EXIT_HOLD event while still down; from then
  // on exitRequested() is true and the tracker reports nothing more.
  size_t feed(const KeyFrame& frame, const controls::SideKeys& keys, bool confirmSelects, ControlEvent* out,
              size_t capacity);
  bool exitRequested() const { return exitFired; }
  // Raw keys down and being timed (for the screen's "held ... ms" line).
  bool timing(uint8_t raw) const { return raw < RAW_KEY_COUNT && tracking[raw]; }
  uint32_t heldMs(uint8_t raw, uint32_t nowMs) const;

 private:
  bool isArmed = false;
  uint32_t armAtMs = 0;
  bool exitFired = false;
  bool tracking[RAW_KEY_COUNT] = {};
  uint32_t pressedAtMs[RAW_KEY_COUNT] = {};
  uint16_t logicalAtPress[RAW_KEY_COUNT] = {};
};

// Which inputs the owner has tried so far (the screen's checklist line and the file's summary).
struct ControlsSummary {
  bool upPressed = false;
  bool upHeld = false;
  bool downPressed = false;
  bool downHeld = false;
  bool confirmPressed = false;
  bool powerClicked = false;
  bool touched = false;
};
ControlsSummary summarise(const ControlEvent* events, size_t count);

// One line for the screen and the serial log, e.g.
// "key BTN_UP (4) -> Up PageBack NavPrevious, role prev, 812 ms, hold".
void describeEvent(const ControlEvent& event, char* out, size_t size);

}  // namespace devicetest
