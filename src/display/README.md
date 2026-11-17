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
| `DeviceDisplayBackend.h/.cpp` | The stock profile on the panel (task D1): the two hardware calls of `TimedBackend` (`lib/DisplayProfileCore`) over `HalDisplay` and the renderer, and its log lines; stock only until D2 | RSVP flag, not `SIMULATOR` |
| `DisplaySelfTest.h/.cpp` | **Temporary:** with `CROSSPOINT_SIM_DISPLAY_SELFTEST=<n>`, draws a counter band n times through `update()` and runs one `clear()` once Home is up | RSVP flag and `SIMULATOR` |

Filled by task E3 (#8); D1 (#30) gave the device backend the stock profile's real path, its timing
and timeouts; D2 (#31) adds the trial profiles and their safety rules.

## Calling contract

From `DisplayProfile.h`: the caller draws the whole frame into the renderer's frame buffer; the
backend alone refreshes (`update()`: one word update; `clear()`: one clearing refresh); both are
called on the task that holds the RenderLock (for RSVP, the render task inside
`Scheduler::step()`); both block until the refresh has finished and return its duration in ms.

In the simulator, `RendererPresenter` also logs `[DSP] presented <update|clear> n=<count since
start-up>` right after the real draw and before the modelled sleep: the frame is on screen from that
line until the modelled time has passed, so scenarios take screenshots after it (R4a's
`rsvp_slowdown`).

## Device backend

`DeviceDisplayBackend` (task D1) is `TimedBackend` (`lib/DisplayProfileCore/TimedBackend.h`) with two
hardware calls. Everything that decides something (the timing window, timeouts, statistics, which
panel) is portable and host-tested; this directory only makes the calls below and logs.

| Step | Call | Why |
| --- | --- | --- |
| Before the clock starts | `display.waitRefreshComplete()` | Returns at once unless some other code left a deferred refresh running (`D/include/FreeInkDisplay.h:191-194`; `D/src/FreeInkDisplay.cpp:636`). Otherwise `FreeInkDisplay::displayBuffer` would wait it out first (`:691`), inside our timing |
| Word update | `display.setReaderRefresh(true)`, `renderer.displayBuffer(FAST_REFRESH)` | CrossPoint's book-page refresh (`ReaderRefreshScope`, `src/activities/reader/ReaderUtils.h`): the panel's reading waveform. The renderer passes the fading-fix setting as `turnOffScreen` (`lib/GfxRenderer/GfxRenderer.cpp:1779-1784`) |
| Clearing refresh | the same, with `HALF_REFRESH` | An absolute refresh (both RAM planes rewritten, `CTRL1=0x40`), the one CrossPoint's reader runs every N pages. HALF, not FULL: S2 section 8 and plan section 6; on the Picco the stock firmware's only clean primitive is the single-pass HALF (`D/src/driver/Ssd1677Driver.cpp:809-815`) |
| After the refresh | `display.waitRefreshComplete()`, `display.setReaderRefresh(false)` | The first returns at once (nothing is pending after a blocking refresh); it is there so a driver that ever defers a refresh is still timed to its end. The flag is restored, as `ReaderRefreshScope` does |

`D/` is `freeink-sdk/libs/display/FreeInkDisplay/` at the pinned SDK commit `3f3e4c11`.

Log lines (DBG unless noted), stable for the device session's serial log:

| Line | When |
| --- | --- |
| `[DSP] Device backend: panel=<name> variant=<0..4> driver=<SSD1677\|SSD2677> recognised=<0\|1> async_split=<0\|1> update="<waveform>" clear="<waveform>"` | Once, when RSVP first creates the backend |
| `[DSP] <update\|clear> n=<count> profile=<id> ms=<duration>` | After every call (the same line as before D1) |
| `[DSP] <update\|clear> waited <ms> ms for a refresh started elsewhere` | Only when `finishPending()` had to wait |
| `[ERR] [DSP] <update\|clear> timed out: ms=<ms> limit=<ms> panel=<name>; ...` | A timeout (below) |
| `[DSP] stats ...` | `logStats()`; D1 appended `update_median= update_p95= update_timeouts= clear_median= clear_p95= clear_timeouts=` |

### What each call runs, per panel

The probe writes the panel to `BoardConfig::ACTIVE.displayControllerVariant` at boot
(`D/src/driver/Ssd1677Driver.cpp:266-288`); `resolvePanel()` keeps it only when
`display.controllerName()` names the driver that panel runs on (`PanelRecords.h`). The SDK picks
`Ssd2677Driver` only for SE0400NQW47 (`D/src/FreeInkDisplay.cpp:214-221`), otherwise
`Ssd1677Driver` with the panel's configuration (`Ssd1677Driver.cpp:313-318`).

| Panel (variant) | Driver | Word update (FAST, reader flag on) | Clearing refresh (HALF) | Blocks until BUSY ends |
| --- | --- | --- | --- | --- |
| YRD0426 (1), OPM040B3 (3) | SSD1677 | No board table while reading (`readerFastLut` null, `piccoStockGrayscale`: `:290-297`, `:579-580`), so the Picco branch runs sequence `0xDF`: OTP display mode 2, the stock reading waveform (`:599`, `:609-622`) | No board table (`halfLut` null), sequence `0xD7`: OTP mode 1, temperature written as `0x5A` (`:253-264`, `:612-615`) | Yes: `bus.waitRefreshComplete` at `:622` |
| DEPG0397 (2) | SSD1677 | `lut_picco_depg_custom_fast`, 19 frames = 380 ms, loaded before and released after, sequence `0xCF` (`:302-310`, `:584-586`, `:611`) | `lut_picco_depg_full`, 48 frames = 960 ms, sequence `0xC7` (`:303`, `:262`) | Yes: `:622` |
| SE0400NQW47 (4) | SSD2677 | Reading makes every FAST a "redraw": `lut_picco_se0400_fast`, rails powered off after (`D/src/driver/Ssd2677Driver.cpp:196-202`, `:219-221`) | `lut_picco_se0400_full`, rails off after (`:221`) | Yes: `powerAndRefresh` waits at `:272`, then the power-off wait at `:256`; the next refresh first waits for power-on (`:263`) |
| Unrecognised (0) | SSD1677, generic configuration (`Ssd1677Driver.cpp:99-120`, `:317`) | The reader flag changes nothing (no board tables, no Picco waveforms): sequence `0xFF`, OTP (`:599`, `:630-649`) | Sequence `0xF7`, OTP full waveform with temperature (`halfSeqOverride` 0 falls back to `fullSeqOverride`) | Yes: `:649`. Then both RAM planes are rewritten from the frame (`:909-914`), about 38 ms more |

Every row is the blocking path: `FreeInkDisplay::displayBuffer` (single-buffer build,
`platformio.ini:42`) calls `_driver->display(...)` (`D/src/FreeInkDisplay.cpp:683-702`), which on
SSD1677 is `displayImpl(..., async=false)` (`Ssd1677Driver.cpp:757-759`), and every branch of
`refresh()` waits when `async` is false (`:622`, `:649`, `:682`, `:720`); `Ssd2677Driver::display`
has no deferred form at all (`PanelDriver.h:70`). So the measured duration runs from before the
frame transfer to the end of the driver's BUSY wait, plus the driver's bookkeeping after it: a
48,000-byte copy of the frame into its previous-frame buffer (`Ssd1677Driver.cpp:915`;
`Ssd2677Driver.cpp:222`), on SE0400NQW47 the power-off wait, and on an unrecognised panel the RAM
rewrite above.

The SDK changes the requested mode by itself in a few cases, and the call is then timed and counted
under the kind the backend asked for: the first paint after boot or wake becomes FULL on the Picco
SSD1677 path (`Ssd1677Driver.cpp:816-847`) and SE0400NQW47 (`Ssd2677Driver.cpp:168`); FAST after
grayscale content becomes HALF (`:849-854`); FAST right after an inversion change becomes HALF
(`D/src/FreeInkDisplay.cpp:692-694`); and a pending `GfxRenderer::promoteNextRefresh()` (the light
panel sets FULL when it closes) replaces the mode once. RSVP runs `clear()` first when it opens and
before each playback, which absorbs all of these; the benchmark should too.

### Timeouts

`displayBuffer()` cannot be interrupted, so the backend's timeout is a classification after the
call returns: an update of 5,000 ms or more, or a clear of 10,000 ms or more, adds to
`stats().<kind>.timeouts` and logs `[DSP] <kind> timed out: ms=... limit=... panel=...` as an error.
The call does return, because the SDK bounds its own BUSY wait:

- `EpdBus::waitRefreshComplete` sleeps on a BUSY edge interrupt and gives up after 30 s without an
  edge (`D/src/bus/EpdBus.cpp:467`); the polled fallback stops after 30 s too (`:316`, `:343`). The
  firmware installs no busy-wait hooks, so the interrupt path is the one used.
- A BUSY line stuck at the working level therefore ends the call after about 30 s; on SE0400NQW47
  the power-on and power-off waits are capped the same way, so up to about 90 s.
- A BUSY line stuck at the idle level ends the wait after 20 ms (`:452`): the call then looks fast,
  not slow. The benchmark's minimum shows it (an update cannot take less than the transfer plus the
  waveform); no timeout is counted.
- A BUSY line that keeps toggling without settling could keep the interrupt wait going, since each
  edge restarts its 30 s. Not a plausible failure, and not bounded here.

A timed-out call is recorded like any other (it is how long the call took) and playback carries on:
RSVP's scheduler sees a slow update. Pausing playback on a timeout is the activity's decision
(`stats().updates.timeouts` changes).

### What only the device session can confirm

- That BUSY behaves as the SDK assumes on the unit's panel: asserted within 20 ms of activation,
  released at the end of the waveform (a refresh that returns after the transfer plus about 20 ms
  means it did not assert).
- Real update and clear times for the unit's panel: only DEPG0397 has a computable prediction (380
  and 960 ms of waveform, `PanelRecords.h`); YRD0426 and OPM040B3 run OTP waveforms and SE0400NQW47
  undocumented tables.
- How much the fading fix (`turnOffScreen`) adds per update, and on SE0400NQW47 how much the
  power-off and power-on waits add (each waits up to 100 ms for BUSY to assert,
  `D/src/bus/EpdBus.cpp:325`).
- That no timeout is ever logged in normal use.

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
`docs/BASE.md`); E3 and D1 needed none (D1 uses only existing `HalDisplay` calls).
