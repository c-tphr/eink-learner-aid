#pragma once

// DisplayProfile: the firmware side of RSVP's display (plan sections 4, 6 and 9; S2 section 9).
// The portable parts are in lib/DisplayProfileCore (README there): the profile table and clearing
// limits (DisplayProfiles.h), the backend interface and statistics (DisplayBackend.h), the
// rsvp::DisplaySink adapter (ProfileDisplaySink.h) and the simulator timing model (DisplayTiming.h).
// This header adds what needs Arduino and the renderer: the millisecond clock and this build's
// backend.
//
// Use from the RSVP activity:
//
//   displayprofile::DisplayBackend& backend = displayprofile::firmwareBackend(renderer);
//   backend.setUnlockedTrials(setting.unlocked);    // TrialUnlock.h; nothing unlocked by default
//   backend.select(setting.chosen);                 // false if not allowed: Stock is selected
//   scheduler.setClearingLimits(backend.info().clearing);
//   displayprofile::ProfileDisplaySink sink(backend, painter);   // painter: the word band
//   displayprofile::MillisClock clock;
//   rsvp::Scheduler scheduler(source, clock, sink, fits, context);
//
// Calling contract (also in DisplayBackend.h):
//
// - Who draws: the caller (the RSVP activity, through its FramePainter). It draws the whole frame
//   into the renderer's frame buffer before update() or clear(). The backend never draws.
// - Who refreshes: the backend only. update() is one update (on the device: the existing
//   renderer.displayBuffer(FAST_REFRESH) path with the panel's reading waveform); only an update
//   marked UpdateKind::WORD (ProfileDisplaySink's word frames) may run a trial waveform, under the
//   allow rule and exposure limits of lib/DisplayProfileCore (README, "Trial profiles"). clear() is
//   one clearing refresh (HALF_REFRESH, trial waveform released). While a profile is in use the
//   caller makes no displayBuffer() call of its own, so every refresh is timed and counted.
// - Which task: the task that owns the display, holding the RenderLock. For RSVP that is the
//   ActivityManager render task inside render(), where Scheduler::step() runs. One task at a time;
//   never from an interrupt or a second task.
// - Blocking: both calls return when the refresh has finished and return its duration in ms.
//   Device: measured with millis() from just before the frame transfer to the end of the driver's
//   wait for the panel's BUSY line (TimedBackend.h); a call of 5 s or more (10 s for a clear) is
//   counted as a timeout and logged as an error.
//   Simulator: the real draw, then a sleep for the rest of the modelled time (a thread sleep:
//   the simulator's main loop keeps presenting frames and reading input meanwhile).
//
// Every update and clear logs one DBG line, "[DSP] update n=<count> profile=<id> ms=<duration> ..."
// (the simulator adds model=, real= and slept=); logStats() logs a summary line.
//
// Compiled only with the RSVP feature flag (RsvpFeature.h).

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <Clock.h>
#include <DisplayBackend.h>
#include <DisplayProfiles.h>
#include <ProfileDisplaySink.h>

#include <cstdint>

class GfxRenderer;

namespace displayprofile {

// rsvp::Clock on Arduino millis(): what the scheduler reads on the device and in the simulator.
class MillisClock final : public rsvp::Clock {
 public:
  uint32_t nowMs() override;
};

// This build's backend, created on the first call and kept for the program's life (one panel).
// Simulator: SimulatorDisplayBackend (modelled timing; reads and checks its configuration here and
// stops the program with an error line if an environment override is malformed). Device:
// DeviceDisplayBackend (the stock profile's refresh, timed; logs the panel on creation). Later
// calls ignore `renderer`.
DisplayBackend& firmwareBackend(GfxRenderer& renderer);

// One summary line: "[DSP] stats profile=<id> panel=<name> updates=<n> min=.. max=.. mean=.. last=..
// clears=<n> min=.. max=.. mean=.. last=.. update_median=.. update_p95=.. update_timeouts=..
// clear_median=.. clear_p95=.. clear_timeouts=..". Median and p95 cover the last
// RefreshStats::SAMPLE_WINDOW calls of each kind.
void logStats(const DisplayBackend& backend);

// TEMPORARY, removed with RsvpLinkCheck (task C2): links the profiles, the adapter, the modelled
// backend, the statistics and this build's backend into the firmware before the RSVP activity
// uses them, and logs one line. Refreshes nothing; firmwareBackend() is referenced, not called.
void runDisplayProfileLinkCheck();

// The per-call log lines the backends print (not for callers).
void logRefresh(const DisplayBackend& backend, RefreshKind kind, uint32_t ms);
void logModelledRefresh(const DisplayBackend& backend, const ModelledCall& call);

}  // namespace displayprofile

#endif  // CROSSPOINT_RSVP_ENABLED
