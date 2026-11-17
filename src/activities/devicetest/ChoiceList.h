#pragma once

// A device-test list (docs/spikes/S4-control-map.md, row 34 and section 13): up to eight rows of
// 448x72 from y=72, 8 px apart (controls::CHOICE_ZONES). A short press of a side key moves the
// highlight on release (no wrap), a 0.7 s hold of either side key or Confirm chooses the highlighted
// row, a tap chooses the row under the finger, and the Back gesture asks to leave. Rows named in the
// repeat mask are chosen again every 0.4 s while the hold continues (steppers). The default row is
// the list's own choice; on these screens it never commits anything ("Repeat step", "Continue").
//
// Pure: no drawing, no input API. The activity feeds it controls::Event values from
// controls::ControlInput::pollFocus() and draws the rows (DeviceTestActivity.cpp). Task D3b reuses it
// for its pass/fail steps and the profile choice. Host-tested in test/device_test.

#include <cstdint>

#include "controls/ControlGesture.h"
#include "controls/ControlMap.h"

namespace devicetest {

class ChoiceList {
 public:
  enum class Kind : uint8_t {
    NONE,      // nothing for this list
    MOVED,     // the highlight moved, or stayed at an end (no wrap); `row` is the highlight
    CHOSEN,    // `row` was chosen (hold, Confirm, or a tap on it)
    REPEATED,  // a held row in the repeat mask, chosen again
    BACK,      // the Back gesture
  };
  struct Outcome {
    Kind kind = Kind::NONE;
    uint8_t row = 0;
  };

  // A new list of `count` rows (1 to controls::CHOICE_ROWS_MAX) with the highlight on
  // `defaultRow`. Bit i of `repeatMask` makes row i repeat while held.
  void reset(uint8_t count, uint8_t defaultRow = 0, uint8_t repeatMask = 0);

  uint8_t count() const { return rows; }
  uint8_t focus() const { return highlighted; }
  uint8_t defaultRow() const { return initial; }
  bool repeats(uint8_t row) const { return row < rows && (repeatMask & (1u << row)) != 0; }
  controls::Screen screen() const { return controls::choiceScreen(rows, initial); }

  Outcome handle(const controls::Event& event);

 private:
  uint8_t rows = 1;
  uint8_t initial = 0;
  uint8_t highlighted = 0;
  uint8_t repeatMask = 0;
};

}  // namespace devicetest
