#pragma once

// The input adapter of the control map: the only control code that reads MappedInputManager. It maps
// the side keys through KEYS and the Side button layout setting, samples one InputFrame per main-loop
// pass and classifies it with GestureReader. Logs nothing; the screens log their [CTL] lines.

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include "ControlGesture.h"

class MappedInputManager;

namespace controls {

class ControlInput {
 public:
  explicit ControlInput(MappedInputManager& input) : input(input) {}

  // A new screen: see GestureReader::restartGuard.
  void restartGuard();
  bool armed() const { return reader.armed(); }

  // Focus screens. On Hold, the held key's release (and any later long-press test of it) is handed
  // to MappedInputManager, so the screen shown next never sees that key as a new gesture.
  Event pollFocus();

  // Playback. On TouchDown the rest of the contact is suppressed: its lift cannot tap a pause-screen
  // button and an edge swipe that started with it cannot complete.
  Event pollPlaying();

 private:
  InputFrame sample() const;

  MappedInputManager& input;
  GestureReader reader;
};

}  // namespace controls

#endif  // CROSSPOINT_RSVP_ENABLED
