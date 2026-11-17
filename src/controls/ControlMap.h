#pragma once

// The single binding table for the RSVP and checkpoint screens (docs/spikes/S4-control-map.md,
// section 7): key roles, timings, event-to-action bindings and touch zones. Pure data and lookups,
// host-tested in test/rsvp_activity; ControlInput.h turns MappedInputManager frames into events.
// This is the file a device-session finding changes.

#include <cstdint>

namespace controls {

// Logical buttons as MappedInputManager names them (ControlInput.cpp maps them one to one).
enum class LogicalButton : uint8_t { PageBack, PageForward, Up, Down, Confirm };

// 1. Physical roles. `prev`/`next` follow Settings > Controls > Side button layout, so the owner can
// fix a swapped pair on the device; the fallbacks apply when that layout gives no usable pair
// (Disabled, Next/Next, Prev/Prev). A durable fix for a fresh settings file swaps the fallbacks here.
struct KeyRoles {
  uint8_t longPressDefault;  // seeds "Page turn long press" before settings load (feature builds)
  LogicalButton prev;
  LogicalButton next;
  LogicalButton prevFallback;
  LogicalButton nextFallback;
  bool confirmSelects;  // GPIO5 "Confirm" (role unconfirmed) selects; false if it misfires
};

// CrossPointSettings::SPEED_READ (checked in ControlInput.cpp).
inline constexpr uint8_t LONG_PRESS_SPEED_READ = 3;

inline constexpr KeyRoles KEYS{LONG_PRESS_SPEED_READ, LogicalButton::PageBack, LogicalButton::PageForward,
                               LogicalButton::Up,     LogicalButton::Down,     true};

// The logical buttons the screens read as prev and next. `layoutUsable` is false when the Side button
// layout setting gives no prev/next pair.
struct SideKeys {
  LogicalButton prev;
  LogicalButton next;
};
inline constexpr SideKeys sideKeys(const KeyRoles& roles, const bool layoutUsable) {
  return layoutUsable ? SideKeys{roles.prev, roles.next} : SideKeys{roles.prevFallback, roles.nextFallback};
}

// 2. Timing.
inline constexpr uint16_t HOLD_MS = 700;    // hold to activate; equal to ReaderUtils::SKIP_HOLD_MS
inline constexpr uint16_t REPEAT_MS = 400;  // repeat while a repeating item stays held
inline constexpr uint16_t ARM_MS = 300;     // a new screen ignores input until shown + this and released

// 3. Events -> actions, per context.
enum class Ev : uint8_t {
  None,
  KeyDown,      // press edge of any key (playback)
  PrevShort,    // prev key released before HOLD_MS
  NextShort,    // next key released before HOLD_MS
  Hold,         // either side key held HOLD_MS
  HoldRepeat,   // every REPEAT_MS after Hold while still held
  Confirm,      // Confirm released
  TouchDown,    // a finger touched the screen (playback)
  Tap,          // a tap, with its position
  BackGesture,  // left-edge swipe or header back tap
  HomeGesture,  // bottom-edge swipe
};
enum class Act : uint8_t { None, Pause, FocusPrev, FocusNext, Select, SelectRepeat, TapItem, EnterRsvp, Back };
enum class Ctx : uint8_t { PageView, RsvpPlaying, FocusScreen };

struct Binding {
  Ctx ctx;
  Ev ev;
  Act act;
};

inline constexpr Binding BINDINGS[] = {
    {Ctx::RsvpPlaying, Ev::KeyDown, Act::Pause},          {Ctx::RsvpPlaying, Ev::TouchDown, Act::Pause},
    {Ctx::RsvpPlaying, Ev::HomeGesture, Act::Pause},      {Ctx::RsvpPlaying, Ev::BackGesture, Act::Pause},
    {Ctx::FocusScreen, Ev::PrevShort, Act::FocusPrev},    {Ctx::FocusScreen, Ev::NextShort, Act::FocusNext},
    {Ctx::FocusScreen, Ev::Hold, Act::Select},            {Ctx::FocusScreen, Ev::HoldRepeat, Act::SelectRepeat},
    {Ctx::FocusScreen, Ev::Tap, Act::TapItem},            {Ctx::FocusScreen, Ev::BackGesture, Act::Back},
    {Ctx::PageView, Ev::Hold, Act::EnterRsvp},
};

// First matching row, else Act::None. Ev::Confirm selects on focus screens when KEYS.confirmSelects.
Act resolve(Ctx ctx, Ev ev, const KeyRoles& roles = KEYS);

// 4. Screens: items in focus order with their touch zones (portrait 480x800 logical pixels).
enum class Item : uint8_t { Exit, Menu, PageView, SentenceBack, Play, SentenceForward };

struct Zone {
  Item item;
  int16_t x, y, w, h;
  bool repeats;  // a held activation repeats every REPEAT_MS
};

struct Screen {
  const Zone* zones;
  uint8_t count;
  uint8_t defaultIndex;
};

// The RSVP pause screen (S4 section 2). R4b appends the WPM and words-per-update steppers and the
// two save buttons after SentenceForward, in the rows S4 gives them (488-704).
inline constexpr Zone PAUSE_ZONES[] = {
    {Item::Exit, 16, 52, 136, 64, false},          {Item::Menu, 172, 52, 136, 64, false},
    {Item::PageView, 328, 52, 136, 64, false},     {Item::SentenceBack, 16, 204, 448, 64, true},
    {Item::Play, 16, 280, 448, 120, false},        {Item::SentenceForward, 16, 412, 448, 64, true},
};
inline constexpr Screen PAUSE{PAUSE_ZONES, static_cast<uint8_t>(sizeof(PAUSE_ZONES) / sizeof(PAUSE_ZONES[0])), 4};

// Zone under a tap, or nullptr.
const Zone* hit(const Screen& screen, int x, int y);
// Index of `item` on `screen`, or -1.
int indexOf(const Screen& screen, Item item);

// Highlight moves never wrap.
inline int focusPrev(int focus) { return focus > 0 ? focus - 1 : 0; }
inline int focusNext(const Screen& screen, int focus) { return focus + 1 < screen.count ? focus + 1 : focus; }

// Stable names for the [CTL] log lines.
const char* itemName(Item item);

}  // namespace controls
