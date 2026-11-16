#pragma once

// rsvp::DisplaySink on top of a DisplayBackend, so the scheduler stays unaware of profiles.
// The RSVP activity supplies a FramePainter that draws into the frame buffer; this adapter then
// has the backend present it. Calling contract: DisplayBackend.h (the painter runs inside
// showFrame()/clearingRefresh(), on the task that drives Scheduler::step(), holding the RenderLock).

#include <DisplaySink.h>

#include "DisplayBackend.h"

namespace displayprofile {

// Draws into the renderer's frame buffer only: no display or refresh call.
class FramePainter {
 public:
  virtual ~FramePainter() = default;
  // The word band showing `frame` (valid only during the call), on top of whatever else the frame
  // buffer holds (guides, progress marks).
  virtual void paintFrame(const rsvp::Frame& frame) = 0;
  // The word band empty, guides kept: what a clearing refresh during playback leaves on screen.
  virtual void paintClearedBand() = 0;
};

class ProfileDisplaySink final : public rsvp::DisplaySink {
 public:
  ProfileDisplaySink(DisplayBackend& backend, FramePainter& painter) : backend(backend), painter(painter) {}

  // paintFrame(frame), then backend.update(): blocks until the update has finished.
  void showFrame(const rsvp::Frame& frame) override;
  // paintClearedBand(), then backend.clear(): blocks until the clearing refresh has finished.
  void clearingRefresh() override;

 private:
  DisplayBackend& backend;
  FramePainter& painter;
};

}  // namespace displayprofile
