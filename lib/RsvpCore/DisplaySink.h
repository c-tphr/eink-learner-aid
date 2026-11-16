#pragma once

// What the scheduler needs from the display. The firmware implements it on top of DisplayProfile
// (device and simulator backends); host tests implement it with a fake clock.
//
// Threading: the scheduler calls these only from inside Scheduler::step(), so on the task that
// drives step(). On the device that is the RSVP activity's render task, because the SDK's
// displayBuffer() must run there (docs/spikes/S2-waveforms.md, section 5). Both calls block.

#include "FrameBuilder.h"

namespace rsvp {

class DisplaySink {
 public:
  virtual ~DisplaySink() = default;

  // Draws `frame` in the word band with the profile's word-update waveform and returns when the
  // panel reports the refresh finished (the BUSY wait in displayBuffer() has ended). The whole
  // frame may be transferred; only the word band changes. `frame` is valid only during the call.
  // The scheduler reads its clock just before and just after this call: the difference is the
  // update time, and the return is the "refresh finished" moment of plan section 6.
  virtual void showFrame(const Frame& frame) = 0;

  // Runs a clearing refresh (an absolute HALF or FULL refresh with any trial LUT released) with
  // the word band empty, and returns when the panel reports it finished. Guides and progress
  // marks stay as drawn. The next showFrame() follows without a dwell.
  virtual void clearingRefresh() = 0;
};

}  // namespace rsvp
