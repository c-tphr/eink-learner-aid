#pragma once

// RSVP pace settings (docs/PLAN.md sections 4 and 6; S4 rows 6-11): the stepper arithmetic, how the
// global default is kept in CrossPoint's settings file, and which value applies when RSVP opens. Pure:
// host-tested in test/rsvp_activity.

#include <BookState.h>
#include <FrameBuilder.h>
#include <Pacing.h>

#include <cstdint>

namespace reading {

struct RsvpValues {
  uint16_t wpm;
  uint8_t wordsPerUpdate;
};

inline bool operator==(const RsvpValues& a, const RsvpValues& b) {
  return a.wpm == b.wpm && a.wordsPerUpdate == b.wordsPerUpdate;
}

// The stepper ranges (S4 rows 6-9). Words per update is FrameBuilder's limit.
inline constexpr uint16_t RSVP_WPM_MIN = 60;
inline constexpr uint16_t RSVP_WPM_MAX = 1000;
inline constexpr uint16_t RSVP_WPM_STEP = 20;
inline constexpr uint8_t RSVP_WORDS_MIN = 1;
inline constexpr uint8_t RSVP_WORDS_MAX = rsvp::MAX_WORDS_PER_FRAME;

// The values when neither the book nor the settings file says otherwise (PRD: 240 WPM, one word).
inline constexpr RsvpValues RSVP_DEFAULTS{rsvp::DEFAULT_WPM, 1};

uint16_t clampWpm(uint32_t wpm);
uint8_t clampWordsPerUpdate(uint32_t words);

// One stepper press: +-RSVP_WPM_STEP or +-1, clamped to the range (a press at a limit changes nothing).
uint16_t stepWpm(uint16_t wpm, bool up);
uint8_t stepWordsPerUpdate(uint8_t words, bool up);

// The settings file keeps the default WPM in tens, because upstream's settings are one byte each
// (`rsvpWpmTens`, 6 to 100). The steps are 20 WPM, so no stepper value is lost.
inline constexpr uint8_t RSVP_WPM_TENS_MIN = RSVP_WPM_MIN / 10;
inline constexpr uint8_t RSVP_WPM_TENS_MAX = RSVP_WPM_MAX / 10;
inline constexpr uint8_t RSVP_WPM_TENS_DEFAULT = rsvp::DEFAULT_WPM / 10;
uint8_t wpmToTens(uint16_t wpm);
uint16_t wpmFromTens(uint8_t tens);

// The values RSVP opens with, each chosen on its own: the book's override when the state file has
// one, else the global default (`global`, from the settings file, which always holds a value: 240 WPM
// and one word until the reader saves another). Out-of-range values are clamped.
RsvpValues resolveValues(const checkpoint::Overrides* book, RsvpValues global);

// Both values as this book's override.
checkpoint::Overrides overridesFor(RsvpValues values);

}  // namespace reading
