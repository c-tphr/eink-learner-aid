# src/display

**DisplayProfile** (`docs/PLAN.md` sections 4, 6 and 9): named refresh strategies (stock and trial
candidates), per-update timing and the backend that RSVP draws through. The portable parts (profile
table, clearing limits, backend interface, statistics, the `rsvp::DisplaySink` adapter and the
simulator timing model) are in `lib/DisplayProfileCore` (its README has the details and the timing
model). This directory holds what needs Arduino and the renderer.

| File | What it holds | Built for |
| --- | --- | --- |
| `DisplayProfile.h/.cpp` | The calling contract, `MillisClock`, `firmwareBackend()`, `logStats()`, the log lines, a temporary link check | RSVP flag |
| `SimulatorDisplayBackend.h/.cpp` | The real draw through `renderer.displayBuffer()`, then a sleep for the rest of the modelled time; reads the compiled-in timings file and `CROSSPOINT_SIM_UPDATE_MS` / `CROSSPOINT_SIM_CLEAR_MS` | RSVP flag and `SIMULATOR` |
| `DeviceDisplayBackend.h/.cpp` | **Placeholder, replaced by D1:** the existing FAST refresh (update) and HALF refresh (clear), timed with `millis()`; stock only | RSVP flag, not `SIMULATOR` |
| `DisplaySelfTest.h/.cpp` | **Temporary:** with `CROSSPOINT_SIM_DISPLAY_SELFTEST=<n>`, draws a counter band n times through `update()` and runs one `clear()` once Home is up | RSVP flag and `SIMULATOR` |

Filled by task E3 (#8); D1 (#30) replaces the device backend's refresh with the stock profile's
real path, D2 (#31) adds the trial profiles and their safety rules.

## Calling contract

From `DisplayProfile.h`: the caller draws the whole frame into the renderer's frame buffer; the
backend alone refreshes (`update()`: one word update; `clear()`: one clearing refresh); both are
called on the task that holds the RenderLock (for RSVP, the render task inside
`Scheduler::step()`); both block until the refresh has finished and return its duration in ms.

## Environment variables (simulator only)

| Variable | Meaning |
| --- | --- |
| `CROSSPOINT_SIM_UPDATE_MS` | Modelled word-update time for every profile, 0 to 60,000 ms; `0` adds no delay. Beats the timings file and the defaults |
| `CROSSPOINT_SIM_CLEAR_MS` | The same for clearing refreshes |
| `CROSSPOINT_SIM_DISPLAY_SELFTEST` | Run the self-test with this many updates (1 to 100) |

A malformed value stops the simulator with an error line. The scenarios `display_timing_model` and
`display_timing_model_zero` (`tools/simtest/scenarios/`) use all three.

## The self-test hook

`runDisplaySelfTestIfRequested(renderer)` is called from `loop()` in `src/main.cpp`, after
`activityManager.loop()` (an upstream edit listed in `docs/BASE.md`). On the device and on every
build without the RSVP flag it is an empty inline function: the guard is
`#if CROSSPOINT_RSVP_ENABLED && defined(SIMULATOR)` in `DisplaySelfTest.h`, and the `.cpp` is empty
otherwise. In the simulator it reads its variable once; when unset it does nothing more. When set,
it waits until Home has been on screen for 500 ms, takes the RenderLock (the render task waits),
draws and presents the band, runs the clear, logs the statistics and `[DSP] Self-test done`, and
returns; the band stays until Home next redraws. Remove it once the RSVP activity's scenario covers
the timing model.

Everything here is compiled only with the RSVP feature flag: every file includes `RsvpFeature.h` and
wraps its code in `#if CROSSPOINT_RSVP_ENABLED` (see `docs/ARCHITECTURE-RSVP.md`). A `HalDisplay`
method added for a profile also needs a stub in the simulator (`scripts/simulator_patches/`, see
`docs/BASE.md`); E3 needed none.
