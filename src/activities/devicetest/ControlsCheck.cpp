#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include "ControlsCheck.h"

#include <cstdio>

namespace devicetest {

namespace {

bool reached(const uint32_t now, const uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

uint8_t logicalOf(const controls::LogicalButton b) {
  switch (b) {
    case controls::LogicalButton::PageBack:
      return LOGICAL_PAGE_BACK;
    case controls::LogicalButton::PageForward:
      return LOGICAL_PAGE_FORWARD;
    case controls::LogicalButton::Up:
      return LOGICAL_UP;
    case controls::LogicalButton::Down:
      return LOGICAL_DOWN;
    case controls::LogicalButton::Confirm:
      return LOGICAL_CONFIRM;
  }
  return LOGICAL_CONFIRM;
}

}  // namespace

const char* rawKeyName(const uint8_t raw) {
  static constexpr const char* NAMES[RAW_KEY_COUNT] = {"BTN_BACK", "BTN_CONFIRM", "BTN_LEFT", "BTN_RIGHT",
                                                       "BTN_UP",   "BTN_DOWN",    "BTN_POWER"};
  return raw < RAW_KEY_COUNT ? NAMES[raw] : "?";
}

const char* logicalName(const uint8_t bit) {
  static constexpr const char* NAMES[LOGICAL_COUNT] = {"Back",     "Confirm",     "Left",    "Right",
                                                       "Up",       "Down",        "Power",   "PageBack",
                                                       "PageForward", "NavNext", "NavPrevious"};
  return bit < LOGICAL_COUNT ? NAMES[bit] : "?";
}

const char* roleName(const Role role) {
  switch (role) {
    case Role::NONE:
      return "none";
    case Role::PREV:
      return "prev";
    case Role::NEXT:
      return "next";
    case Role::CONFIRM:
      return "confirm";
  }
  return "?";
}

Role roleOf(const uint16_t logical, const controls::SideKeys& keys, const bool confirmSelects) {
  if (logical & logicalBit(logicalOf(keys.prev))) return Role::PREV;
  if (logical & logicalBit(logicalOf(keys.next))) return Role::NEXT;
  if (confirmSelects && (logical & logicalBit(LOGICAL_CONFIRM))) return Role::CONFIRM;
  return Role::NONE;
}

const char* eventSourceName(const EventSource source) { return source == EventSource::KEY ? "key" : "touch"; }

const char* eventKindName(const EventKind kind) {
  switch (kind) {
    case EventKind::SHORT:
      return "short";
    case EventKind::HOLD:
      return "hold";
    case EventKind::EXIT_HOLD:
      return "exit-hold";
    case EventKind::TAP:
      return "tap";
    case EventKind::LONG_PRESS:
      return "long-press";
    case EventKind::SWIPE_LEFT:
      return "swipe-left";
    case EventKind::SWIPE_RIGHT:
      return "swipe-right";
    case EventKind::SWIPE_UP:
      return "swipe-up";
    case EventKind::SWIPE_DOWN:
      return "swipe-down";
    case EventKind::BACK_GESTURE:
      return "back-gesture";
    case EventKind::HOME_GESTURE:
      return "home-gesture";
  }
  return "?";
}

void KeyTracker::restart(const uint32_t nowMs) {
  isArmed = false;
  armAtMs = nowMs + controls::ARM_MS;
  exitFired = false;
  for (bool& t : tracking) t = false;
}

uint32_t KeyTracker::heldMs(const uint8_t raw, const uint32_t nowMs) const {
  return timing(raw) ? nowMs - pressedAtMs[raw] : 0;
}

size_t KeyTracker::feed(const KeyFrame& f, const controls::SideKeys& keys, const bool confirmSelects,
                        ControlEvent* out, const size_t capacity) {
  if (!isArmed) {
    // The frame that arms reports nothing: an edge seen here belongs to the press before the screen.
    if (reached(f.nowMs, armAtMs) && f.down == 0) isArmed = true;
    return 0;
  }
  if (exitFired) return 0;

  size_t n = 0;
  for (uint8_t i = 0; i < RAW_KEY_COUNT; ++i) {
    const uint8_t bit = static_cast<uint8_t>(1u << i);
    if (f.pressed & bit) {
      tracking[i] = true;
      pressedAtMs[i] = f.nowMs;
      logicalAtPress[i] = f.logicalPressed;
    }
    if (!tracking[i]) continue;
    const bool ended = (f.released & bit) != 0 || (f.down & bit) == 0;
    const uint32_t held = f.nowMs - pressedAtMs[i];
    if (ended) {
      tracking[i] = false;
      if (n >= capacity) continue;
      ControlEvent& e = out[n++];
      e = ControlEvent{};
      e.atMs = f.nowMs;
      e.source = EventSource::KEY;
      e.raw = i;
      e.logical = static_cast<uint16_t>(logicalAtPress[i] | f.logicalReleased);
      e.role = roleOf(e.logical, keys, confirmSelects);
      e.heldMs = held;
      e.kind = (i != RAW_POWER && held >= controls::HOLD_MS) ? EventKind::HOLD : EventKind::SHORT;
    } else if (i != RAW_POWER && held >= EXIT_HOLD_MS) {
      tracking[i] = false;
      exitFired = true;
      if (n >= capacity) return n;
      ControlEvent& e = out[n++];
      e = ControlEvent{};
      e.atMs = f.nowMs;
      e.source = EventSource::KEY;
      e.kind = EventKind::EXIT_HOLD;
      e.raw = i;
      e.logical = logicalAtPress[i];
      e.role = roleOf(e.logical, keys, confirmSelects);
      e.heldMs = held;
      return n;
    }
  }
  return n;
}

ControlsSummary summarise(const ControlEvent* events, const size_t count) {
  ControlsSummary s;
  for (size_t i = 0; i < count; ++i) {
    const ControlEvent& e = events[i];
    if (e.source == EventSource::TOUCH) {
      s.touched = true;
      continue;
    }
    const bool held = e.kind == EventKind::HOLD || e.kind == EventKind::EXIT_HOLD;
    switch (e.raw) {
      case RAW_UP:
        s.upPressed = true;
        s.upHeld = s.upHeld || held;
        break;
      case RAW_DOWN:
        s.downPressed = true;
        s.downHeld = s.downHeld || held;
        break;
      case RAW_CONFIRM:
        s.confirmPressed = true;
        break;
      case RAW_POWER:
        s.powerClicked = true;
        break;
      default:
        break;
    }
  }
  return s;
}

void describeEvent(const ControlEvent& e, char* out, const size_t size) {
  if (size == 0) return;
  out[0] = '\0';
  if (e.source == EventSource::TOUCH) {
    if (e.x2 != NO_POSITION) {
      snprintf(out, size, "touch %s %d,%d -> %d,%d", eventKindName(e.kind), e.x, e.y, e.x2, e.y2);
    } else if (e.x != NO_POSITION) {
      snprintf(out, size, "touch %s %d,%d", eventKindName(e.kind), e.x, e.y);
    } else {
      snprintf(out, size, "touch %s", eventKindName(e.kind));
    }
    return;
  }
  char names[96] = {};
  size_t used = 0;
  for (uint8_t b = 0; b < LOGICAL_COUNT; ++b) {
    if ((e.logical & logicalBit(b)) == 0) continue;
    const int w = snprintf(names + used, sizeof(names) - used, "%s%s", used > 0 ? " " : "", logicalName(b));
    if (w < 0 || static_cast<size_t>(w) >= sizeof(names) - used) break;
    used += static_cast<size_t>(w);
  }
  if (used == 0) snprintf(names, sizeof(names), "nothing");
  snprintf(out, size, "key %s (%u) -> %s, %s, %lu ms, %s", rawKeyName(e.raw), static_cast<unsigned>(e.raw), names,
           roleName(e.role), static_cast<unsigned long>(e.heldMs), eventKindName(e.kind));
}

}  // namespace devicetest

#endif  // CROSSPOINT_RSVP_ENABLED
