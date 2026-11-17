#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include "RsvpValues.h"

namespace reading {

uint16_t clampWpm(const uint32_t wpm) {
  if (wpm < RSVP_WPM_MIN) return RSVP_WPM_MIN;
  if (wpm > RSVP_WPM_MAX) return RSVP_WPM_MAX;
  return static_cast<uint16_t>(wpm);
}

uint8_t clampWordsPerUpdate(const uint32_t words) {
  if (words < RSVP_WORDS_MIN) return RSVP_WORDS_MIN;
  if (words > RSVP_WORDS_MAX) return RSVP_WORDS_MAX;
  return static_cast<uint8_t>(words);
}

uint16_t stepWpm(const uint16_t wpm, const bool up) {
  const uint32_t from = clampWpm(wpm);
  if (up) return clampWpm(from + RSVP_WPM_STEP);
  return from > RSVP_WPM_STEP ? clampWpm(from - RSVP_WPM_STEP) : RSVP_WPM_MIN;
}

uint8_t stepWordsPerUpdate(const uint8_t words, const bool up) {
  const uint32_t from = clampWordsPerUpdate(words);
  return clampWordsPerUpdate(up ? from + 1 : from - 1);
}

uint8_t wpmToTens(const uint16_t wpm) { return static_cast<uint8_t>((clampWpm(wpm) + 5u) / 10u); }

uint16_t wpmFromTens(const uint8_t tens) { return clampWpm(static_cast<uint32_t>(tens) * 10u); }

RsvpValues resolveValues(const checkpoint::Overrides* book, const RsvpValues global) {
  RsvpValues values{clampWpm(global.wpm), clampWordsPerUpdate(global.wordsPerUpdate)};
  if (book != nullptr && book->hasWpm) values.wpm = clampWpm(book->wpm);
  if (book != nullptr && book->hasWordsPerUpdate) values.wordsPerUpdate = clampWordsPerUpdate(book->wordsPerUpdate);
  return values;
}

checkpoint::Overrides overridesFor(const RsvpValues values) {
  checkpoint::Overrides o;
  o.hasWpm = true;
  o.wpm = clampWpm(values.wpm);
  o.hasWordsPerUpdate = true;
  o.wordsPerUpdate = clampWordsPerUpdate(values.wordsPerUpdate);
  return o;
}

}  // namespace reading

#endif  // CROSSPOINT_RSVP_ENABLED
