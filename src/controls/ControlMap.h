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
  // Checkpoint screens (task C3a): a vertical swipe, with its start position (S4 row 20).
  SwipeUp,
  SwipeDown,
};
enum class Act : uint8_t {
  None,
  Pause,
  FocusPrev,
  FocusNext,
  Select,
  SelectRepeat,
  TapItem,
  EnterRsvp,
  Back,
  // Checkpoint screens (task C3a): page the answer box when the swipe starts inside it.
  SwipeNext,
  SwipePrev,
};
enum class Ctx : uint8_t { PageView, RsvpPlaying, FocusScreen };

struct Binding {
  Ctx ctx;
  Ev ev;
  Act act;
};

inline constexpr Binding BINDINGS[] = {
    {Ctx::RsvpPlaying, Ev::KeyDown, Act::Pause},       {Ctx::RsvpPlaying, Ev::TouchDown, Act::Pause},
    {Ctx::RsvpPlaying, Ev::HomeGesture, Act::Pause},   {Ctx::RsvpPlaying, Ev::BackGesture, Act::Pause},
    {Ctx::FocusScreen, Ev::PrevShort, Act::FocusPrev}, {Ctx::FocusScreen, Ev::NextShort, Act::FocusNext},
    {Ctx::FocusScreen, Ev::Hold, Act::Select},         {Ctx::FocusScreen, Ev::HoldRepeat, Act::SelectRepeat},
    {Ctx::FocusScreen, Ev::Tap, Act::TapItem},         {Ctx::FocusScreen, Ev::BackGesture, Act::Back},
    {Ctx::PageView, Ev::Hold, Act::EnterRsvp},         {Ctx::FocusScreen, Ev::SwipeUp, Act::SwipeNext},
    {Ctx::FocusScreen, Ev::SwipeDown, Act::SwipePrev},
};

// First matching row, else Act::None. Ev::Confirm selects on focus screens when KEYS.confirmSelects.
Act resolve(Ctx ctx, Ev ev, const KeyRoles& roles = KEYS);

// 4. Screens: items in focus order with their touch zones (portrait 480x800 logical pixels).
enum class Item : uint8_t {
  // The non-prose banner over page view (task R5; S4 rows 15-16).
  Continue,
  Stay,
  // The RSVP pause screen.
  Exit,
  Menu,
  PageView,
  SentenceBack,
  Play,
  SentenceForward,
  WpmDown,
  WpmUp,
  WordsDown,
  WordsUp,
  SaveDefault,
  SaveBook,
  // Device test screens (task D3a): the rows of a ChoiceList, and the Controls check's Finish button.
  ChoiceRow0,
  ChoiceRow1,
  ChoiceRow2,
  ChoiceRow3,
  ChoiceRow4,
  ChoiceRow5,
  ChoiceRow6,
  ChoiceRow7,
  ControlsFinish,
  // Checkpoint screens (task C3a; S4 rows 17-29).
  Reveal,
  SkipPrompt,
  SkipCheckpoint,
  AnswerBox,
  Correct,
  Incorrect,
  RereadRsvp,
  RereadPage,
  ContinueOn,  // "Continue" after Incorrect
};

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

// The non-prose banner over page view (task R5; S4 rows 15-16 and section 2): Continue, the default,
// then Stay.
inline constexpr Zone BANNER_ZONES[] = {
    {Item::Continue, 16, 688, 264, 72, false},
    {Item::Stay, 304, 688, 160, 72, false},
};
inline constexpr Screen BANNER{BANNER_ZONES, static_cast<uint8_t>(sizeof(BANNER_ZONES) / sizeof(BANNER_ZONES[0])), 0};

// The RSVP pause screen (S4 section 2): R4a's rows, then the WPM and words-per-update steppers and the
// two save buttons (S4 rows 6-11). The four steppers repeat while held.
inline constexpr Zone PAUSE_ZONES[] = {
    {Item::Exit, 16, 52, 136, 64, false},         {Item::Menu, 172, 52, 136, 64, false},
    {Item::PageView, 328, 52, 136, 64, false},    {Item::SentenceBack, 16, 204, 448, 64, true},
    {Item::Play, 16, 280, 448, 120, false},       {Item::SentenceForward, 16, 412, 448, 64, true},
    {Item::WpmDown, 16, 488, 96, 64, true},       {Item::WpmUp, 368, 488, 96, 64, true},
    {Item::WordsDown, 16, 564, 96, 64, true},     {Item::WordsUp, 368, 564, 96, 64, true},
    {Item::SaveDefault, 16, 640, 216, 64, false}, {Item::SaveBook, 248, 640, 216, 64, false},
};
inline constexpr Screen PAUSE{PAUSE_ZONES, static_cast<uint8_t>(sizeof(PAUSE_ZONES) / sizeof(PAUSE_ZONES[0])), 4};

// Device test lists (S4 row 34; task D3a): rows 448x72 from y=72, 8 px apart, in focus order.
// A ChoiceList (src/activities/devicetest/ChoiceList.h) shows the first `count` of them; which rows
// repeat while held is the list's own choice.
inline constexpr uint8_t CHOICE_ROWS_MAX = 8;
inline constexpr Zone CHOICE_ZONES[CHOICE_ROWS_MAX] = {
    {Item::ChoiceRow0, 16, 72, 448, 72, false},  {Item::ChoiceRow1, 16, 152, 448, 72, false},
    {Item::ChoiceRow2, 16, 232, 448, 72, false}, {Item::ChoiceRow3, 16, 312, 448, 72, false},
    {Item::ChoiceRow4, 16, 392, 448, 72, false}, {Item::ChoiceRow5, 16, 472, 448, 72, false},
    {Item::ChoiceRow6, 16, 552, 448, 72, false}, {Item::ChoiceRow7, 16, 632, 448, 72, false},
};
constexpr Screen choiceScreen(const uint8_t count, const uint8_t defaultIndex) {
  return Screen{CHOICE_ZONES, count < CHOICE_ROWS_MAX ? count : CHOICE_ROWS_MAX, defaultIndex};
}

// The Controls check (task D3a; S4 section 7): every input is recorded, and a tap on Finish (or a
// 3 s hold of any key, src/activities/devicetest/ControlsCheck.h) leaves.
inline constexpr Zone CONTROLS_CHECK_ZONES[] = {{Item::ControlsFinish, 16, 672, 448, 72, false}};
inline constexpr Screen CONTROLS_CHECK{CONTROLS_CHECK_ZONES, 1, 0};

// Checkpoint screens (task C3a; S4 section 2, rows 17-29). Focus order follows reading order, except
// that Skip checkpoint is always last although it sits at the bottom left. Skip checkpoint and Skip
// question have the same zone on all three screens.
// Prompt, answer hidden: Reveal (default), Skip question, Skip checkpoint.
inline constexpr Zone CHECKPOINT_PROMPT_ZONES[] = {
    {Item::Reveal, 16, 416, 448, 120, false},
    {Item::SkipPrompt, 264, 680, 200, 64, false},
    {Item::SkipCheckpoint, 16, 680, 200, 64, false},
};
inline constexpr Screen CHECKPOINT_PROMPT{
    CHECKPOINT_PROMPT_ZONES, static_cast<uint8_t>(sizeof(CHECKPOINT_PROMPT_ZONES) / sizeof(CHECKPOINT_PROMPT_ZONES[0])),
    0};
// Answer revealed: the answer box (default; covers where Reveal was), Correct, Incorrect, Skip question,
// Skip checkpoint. A grade is never the default.
inline constexpr Zone CHECKPOINT_ANSWER_ZONES[] = {
    {Item::AnswerBox, 16, 120, 448, 440, false},     {Item::Correct, 16, 576, 208, 88, false},
    {Item::Incorrect, 256, 576, 208, 88, false},     {Item::SkipPrompt, 264, 680, 200, 64, false},
    {Item::SkipCheckpoint, 16, 680, 200, 64, false},
};
inline constexpr Screen CHECKPOINT_ANSWER{
    CHECKPOINT_ANSWER_ZONES, static_cast<uint8_t>(sizeof(CHECKPOINT_ANSWER_ZONES) / sizeof(CHECKPOINT_ANSWER_ZONES[0])),
    0};
// After Incorrect: Reread in RSVP, Reread in page view, Continue, Skip question, Skip checkpoint. The
// default is the reread in the mode in use when the checkpoint appeared, so the screen's own
// defaultIndex is that of RSVP and the activity picks page view when the checkpoint came from it.
inline constexpr Zone CHECKPOINT_INCORRECT_ZONES[] = {
    {Item::RereadRsvp, 16, 216, 448, 80, false},     {Item::RereadPage, 16, 308, 448, 80, false},
    {Item::ContinueOn, 16, 400, 448, 80, false},     {Item::SkipPrompt, 264, 680, 200, 64, false},
    {Item::SkipCheckpoint, 16, 680, 200, 64, false},
};
inline constexpr Screen CHECKPOINT_INCORRECT{
    CHECKPOINT_INCORRECT_ZONES,
    static_cast<uint8_t>(sizeof(CHECKPOINT_INCORRECT_ZONES) / sizeof(CHECKPOINT_INCORRECT_ZONES[0])), 0};

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
