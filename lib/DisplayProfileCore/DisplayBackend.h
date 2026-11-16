#pragma once

// The display backend: what RSVP needs from the panel, and what the device tasks D1 and D2
// implement on hardware. Portable: the firmware's backends (src/display/) and the host-test fake
// (FakeDisplayBackend.h) derive from it.
//
// Calling contract (the firmware header src/display/DisplayProfile.h repeats it):
//
// - Who draws: the caller. Before update() it has drawn the complete frame into the renderer's
//   frame buffer (on the device every update sends the whole frame; only the word band differs).
//   The backend never draws. Before clear() it has drawn the frame the clearing refresh should
//   leave on screen (for RSVP: the word band empty, guides kept, or the pause screen).
// - Who refreshes: the backend, and only it, while a profile is in use. update() presents the frame
//   buffer as one word update with the selected profile's waveform; clear() runs one clearing
//   refresh (an absolute refresh, any trial waveform released). The caller makes no other refresh
//   call in between, so the backend's timing and safety rules see every refresh.
// - Which task: the one task that owns the display at that moment. On the device and in the
//   simulator that is the task holding the RenderLock: the activity's render task inside render(),
//   for RSVP inside Scheduler::step(). Never two tasks at once, never from an interrupt.
// - Blocking: update() and clear() return only when the refresh has finished (device: the panel's
//   BUSY wait inside displayBuffer() has ended; simulator: the modelled time has passed). Both
//   return the call's duration in milliseconds: measured on the device, at least the modelled
//   time in the simulator. A caller timing them with its own clock sees the same duration.
// - select(), stats() and resetStats() belong to the same task (or run under the RenderLock).

#include <Clock.h>

#include <cstdint>

#include "DisplayProfiles.h"
#include "DisplayStats.h"
#include "DisplayTiming.h"

namespace displayprofile {

enum class RefreshKind : uint8_t { UPDATE, CLEAR };

class DisplayBackend {
 public:
  virtual ~DisplayBackend() = default;

  // The panel the display driver found (PanelId::SIMULATED in the simulator).
  virtual PanelId panel() const = 0;

  // Whether `id` may be used on this panel now. Stock is always allowed; see
  // defaultProfileAllowed(). D2 overrides this on the device (panel, inversion, device-test menu).
  virtual bool allows(ProfileId id) const { return defaultProfileAllowed(id, panel()); }

  // Makes `id` the profile of later update() and clear() calls. Returns false, and keeps the
  // current profile, when allows(id) is false. Refreshes nothing: the caller runs clear() before
  // the first update() of a playback (S2, section 8). Initially STOCK.
  bool select(ProfileId id);
  ProfileId profile() const { return current; }
  const ProfileInfo& info() const { return profileInfo(current); }

  // Presents the frame buffer as one word update and blocks until it has finished. Returns the
  // call's duration (ms) and adds it to stats().updates.
  uint32_t update();

  // Runs one clearing refresh of the frame buffer and blocks until it has finished. Returns the
  // call's duration (ms) and adds it to stats().clears.
  uint32_t clear();

  // Every update() and clear() since construction or the last resetStats().
  const DisplayStats& stats() const { return statistics; }
  void resetStats() { statistics.reset(); }

 protected:
  // Called by select() after the profile changed hands (device: prepare or release a waveform).
  virtual void onSelect(ProfileId /*id*/) {}
  // Perform the refresh for the current profile, block until it has finished, return its
  // duration (ms). Called only from update() and clear().
  virtual uint32_t presentUpdate() = 0;
  virtual uint32_t presentClear() = 0;

 private:
  ProfileId current = ProfileId::STOCK;
  DisplayStats statistics;
};

// The real refresh behind a modelled backend: the simulator's renderer call, or a test fake.
class Presenter {
 public:
  virtual ~Presenter() = default;
  // Presents the frame buffer (UPDATE: a fast refresh; CLEAR: a clearing refresh). Blocks until done.
  virtual void present(RefreshKind kind) = 0;
};

// Sleeps the calling task. The simulator uses delay() (a thread sleep); tests advance a fake clock.
class Sleeper {
 public:
  virtual ~Sleeper() = default;
  virtual void sleepMs(uint32_t ms) = 0;
};

// What one modelled call did, for the log line and for tests.
struct ModelledCall {
  RefreshKind kind = RefreshKind::UPDATE;
  uint32_t totalMs = 0;  // the call's duration, what update()/clear() returned
  uint32_t modelMs = 0;  // the modelled time for this profile and kind
  uint32_t realMs = 0;   // the real refresh (Presenter::present)
  uint32_t sleptMs = 0;  // the remainder slept: remainderToSleepMs(modelMs, realMs)
};

// A backend that performs the real refresh through a Presenter, then sleeps the remainder so the
// whole call lasts the selected profile's modelled time. It never busy-waits, and never sleeps
// when the real refresh already took as long as the model or longer. A model of 0 adds nothing.
// The simulator backend is this with the renderer, millis() and delay(); host tests use it with a
// fake clock (FakeDisplayBackend.h).
class ModelledBackend : public DisplayBackend {
 public:
  ModelledBackend(Presenter& presenter, rsvp::Clock& clock, Sleeper& sleeper, const TimingModel& model,
                  PanelId panel = PanelId::SIMULATED)
      : presenter(presenter), clock(clock), sleeper(sleeper), timingModel(model), panelId(panel) {}

  PanelId panel() const override { return panelId; }
  const TimingModel& model() const { return timingModel; }
  const ModelledCall& lastCall() const { return last; }

 protected:
  uint32_t presentUpdate() override { return run(RefreshKind::UPDATE); }
  uint32_t presentClear() override { return run(RefreshKind::CLEAR); }

 private:
  uint32_t run(RefreshKind kind);

  Presenter& presenter;
  rsvp::Clock& clock;
  Sleeper& sleeper;
  TimingModel timingModel;
  PanelId panelId;
  ModelledCall last;
};

}  // namespace displayprofile
