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
| `DeviceDisplayBackend.h/.cpp` | The stock and trial profiles on the panel (tasks D1, D2): the hardware calls of `TimedBackend`'s `RefreshPort` (`lib/DisplayProfileCore`) over `HalDisplay` and the renderer, and its log lines | RSVP flag, not `SIMULATOR` |
| `DisplaySelfTest.h/.cpp` | **Temporary:** with `CROSSPOINT_SIM_DISPLAY_SELFTEST=<n>`, draws a counter band n times through `update()` and runs one `clear()` once Home is up | RSVP flag and `SIMULATOR` |

Filled by task E3 (#8); D1 (#30) gave the device backend the stock profile's real path, its timing
and timeouts; D2 (#31) added the trial profiles A to D and their safety rules ("Trial profiles"
below; the portable rules are in `lib/DisplayProfileCore/README.md`, "Trial profiles").

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

`DeviceDisplayBackend` (task D1) is `TimedBackend` (`lib/DisplayProfileCore/TimedBackend.h`) with a
few hardware calls. Everything that decides something (the timing window, timeouts, statistics, which
panel, and since D2 the allow rule, the exposure limits and which table) is portable and host-tested;
this directory only makes the calls below and logs. The stock profile's calls are these; the trial
profiles add the ones in "Trial profiles" below.

| Step | Call | Why |
| --- | --- | --- |
| Before the clock starts | `display.waitRefreshComplete()` | Returns at once unless some other code left a deferred refresh running (`D/include/FreeInkDisplay.h:191-194`; `D/src/FreeInkDisplay.cpp:636`). Otherwise `FreeInkDisplay::displayBuffer` would wait it out first (`:691`), inside our timing |
| Word update | `display.setReaderRefresh(true)`, `renderer.displayBuffer(FAST_REFRESH)` | CrossPoint's book-page refresh (`ReaderRefreshScope`, `src/activities/reader/ReaderUtils.h`): the panel's reading waveform. The renderer passes the fading-fix setting as `turnOffScreen` (`lib/GfxRenderer/GfxRenderer.cpp:1779-1784`) |
| Clearing refresh | `display.setCustomLut(false)` (task D2: any trial table released; a flag write, a no-op when none is loaded), then the same, with `HALF_REFRESH` | An absolute refresh (both RAM planes rewritten, `CTRL1=0x40`), the one CrossPoint's reader runs every N pages. HALF, not FULL: S2 section 8 and plan section 6; on the Picco the stock firmware's only clean primitive is the single-pass HALF (`D/src/driver/Ssd1677Driver.cpp:809-815`) |
| After the refresh | `display.waitRefreshComplete()`, `display.setReaderRefresh(false)` | The first returns at once (nothing is pending after a blocking refresh); it is there so a driver that ever defers a refresh is still timed to its end. The flag is restored, as `ReaderRefreshScope` does |

`D/` is `freeink-sdk/libs/display/FreeInkDisplay/` at the pinned SDK commit `3f3e4c11`.

Log lines (DBG unless noted), stable for the device session's serial log:

| Line | When |
| --- | --- |
| `[DSP] Device backend: panel=<name> variant=<0..4> driver=<SSD1677\|SSD2677> recognised=<0\|1> async_split=<0\|1> update="<waveform>" clear="<waveform>"` | Once, when RSVP first creates the backend |
| `[DSP] <update\|clear> n=<count> profile=<id> ms=<duration>` | After every call (the same line as before D1). Task D2 appends ` ran=<id>` when a word update ran another profile's waveform than the selected one (the allow rule refused it at that moment), and ` for=<word\|screen> promoted=<not-cleared\|hard-limit\|residue\|profile-changed>` to a `clear` line that an `update()` ran as a clearing refresh; neither appears while only Stock is used |
| `[DSP] update waveform="<table>" predicted=<ms>` | Task D2: after a trial word update, which table (or SDK path) ran and S2's waveform-only prediction (0 when it cannot be computed) |
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

## Trial profiles (task D2)

Plan section 6 and `docs/spikes/S2-waveforms.md` sections 6 to 8. The rules (allow rule, unlock value,
exposure limits) and the tables are portable: `lib/DisplayProfileCore/README.md`, "Trial profiles".
The device side adds three `RefreshPort` calls and one `HalDisplay` pass-through.

### What a trial word update runs

| Panel | Trial A, B, C | Trial D |
| --- | --- | --- |
| DEPG0397 | `LUT_TRIAL_{A,B,C}_DEPG0397`: 300, 220, 160 ms | `LUT_TRIAL_D_DEPG0397` (= `lut_picco_depg_fast`, BOOX mode 3): 160 ms |
| YRD0426, OPM040B3 | `LUT_TRIAL_{A,B,C}_YRD0426_OPM040B3`: 300, 220, 160 ms | `LUT_TRIAL_D_YRD0426_OPM040B3` (= `lut_picco_fast`, the SDK's own UI table): about 80 ms if FR code 1 is 40 ms |
| SE0400NQW47 | refused | the SDK's UI path: FAST with the reader flag off, `lut_picco_se0400_mode3` when at most 1/8 of the frame bytes changed, otherwise `lut_picco_se0400_fast` (`D/src/driver/Ssd2677Driver.cpp:196-221`) |
| Unrecognised | refused | refused |

Times are waveform only (S2 section 4); the frame transfer (at least 38.4 ms) and the table upload
(about 115 bytes of SPI) come on top.

| Step | Call | SDK lines (`D/` = `freeink-sdk/libs/display/FreeInkDisplay/`, pinned `3f3e4c11`) |
| --- | --- | --- |
| Load | `display.setCustomLut(true, table)` (new pass-through in `lib/hal/HalDisplay.h` to `FreeInkDisplay::setCustomLUT`) | `D/src/FreeInkDisplay.cpp:1225-1229` forwards to `Ssd1677Driver::setCustomLut` (`D/src/driver/Ssd1677Driver.cpp:1245-1271`): 105 bytes to CMD 0x32, then VGH (0x03), VSH1/VSH2/VSL (0x04) and VCOM (0x2C), then `_customLutActive = true` (`:1270`). It does not drain a pending refresh, so `finishPending()` runs first |
| Refresh | the stock update call: reader flag on, `renderer.displayBuffer(FAST_REFRESH)`, `waitRefreshComplete()`, reader flag off | With `_customLutActive` set, `boardLut` is false (`:584`), the Picco sequence branch is skipped (`:609`), the per-board sequence too (`:630`), and a FAST fires CTRL2 `0xCC` (`0xCF` with the fading fix) with the loaded table and no OTP reload (`:703-712`). The reader flag only picks a board table (`:577-584`), which is skipped, so it changes nothing here; it is kept so the driver's `_afterReader` state is the same as after a stock word |
| Release | `display.setCustomLut(false)`, right after the refresh returns | `:1246-1249`: clears the flag only; no bus traffic |
| Before every clear | `display.setCustomLut(false)` | as above; then HALF runs the panel's stock clearing waveform |
| SE0400NQW47 Trial D | reader flag off, `renderer.displayBuffer(FAST_REFRESH)`, `waitRefreshComplete()` | no table: `Ssd2677Driver` does not override `setCustomLut` (`D/src/driver/PanelDriver.h:290-294`, a no-op) |

### Why nothing of a trial can reach a later refresh

- **The table is loaded for one refresh only.** `TimedBackend::run()` loads, refreshes and releases
  in one call on the task holding the RenderLock; nothing else can refresh in between (the main
  loop's `controllerIdle()` takes the RenderLock first, `src/main.cpp:150-155`). The host tests
  check over a 2,000-step mixed session that no clear, stock update or UI refresh ever starts with a
  table loaded, and that the port is never left loaded (`TrialExposureTest.cpp`).
- **Timeout.** `displayBuffer()` returns even when BUSY never ends: the SDK's wait gives up after
  30 s (`D/src/bus/EpdBus.cpp:467`; "Timeouts" above). The release runs after it returns, whatever
  happened (`TrialExposureDevice.ReleasedAfterATimeout`).
- **The next stock refresh rewrites what the table set.** With the flag clear, DEPG0397 loads its
  board table and voltages before every FAST and HALF (`:584-586`); YRD0426 and OPM040B3 reload OTP
  (sequences `0xDF` and `0xD7` carry the `0x10` load bit, `:599`, `:609-622`) or load
  `lut_picco_fast` for UI updates. The voltage registers a trial leaves are the panel's own: DEPG
  trials A to C write exactly what every stock DEPG word writes, YRD/OPM trials and Trial D write
  `lut_picco_fast`'s, which every stock YRD/OPM UI update writes, and DEPG Trial D writes the BOOX
  mode-3 table's own tail.
- **Driver promotions under a loaded table run stock waveforms.** If the driver promoted the trial
  FAST (first paint after `begin()`, after grayscale, after an inversion change, a pending
  `promoteNextRefresh()`), the flag makes FULL `0x34` and HALF `0xD4` (`:698`, `:702`): both carry
  the OTP load bit, so the panel's OTP waveform runs, not the trial table. The backend also starts
  every trial run with its own clearing refresh (rule 2), which absorbs these promotions first.
- **Sleep and wake.** Deep sleep is a chip reset on wake (`src/main.cpp:139-144`): the driver's
  flag, the backend and its exposure state start fresh, and the first trial word after a wake runs
  as a clearing refresh (rule 2; `TrialExposureDevice.AFreshBackendAfterWakeStartsWithAClear`).
  Before sleeping, the sleep screen is drawn with a FULL or HALF refresh
  (`src/activities/boot_sleep/SleepActivity.cpp:39-48`), which wipes any trial residue.
- **Inversion.** Trials are refused while `display.isInverted()` (the DEPG dark-background path
  drives every pixel, S2 section 6, item 6), checked on every word update, not only at `select()`.

### What only the device session can confirm, per trial

| Trial | Open questions |
| --- | --- |
| All | That `setCustomLut` behaves as read above on the unit's panel (the 0xCC sequence runs the loaded table; the next stock refresh is unaffected); real update times against the predictions (A 300, B 220, C 160 ms of waveform plus transfer; whether FR code 2 is really 20 ms on this glass); readability of a single word; residue after 30, 60 and 120 chained updates and recovery after one clearing refresh; contrast at the room temperature recorded (the tables are not temperature-compensated: CTRL2 `0xCC` has no load-temperature bit) |
| A, B, C on YRD0426 / OPM040B3 | These panels read with an OTP waveform the repository cannot see; the trials are their UI table lengthened at its own voltages, so whether they look like the reading waveform at all is unknown |
| D on DEPG0397 | The BOOX mode-3 table keeps its one-frame black top-up and its own VCOM (`0x04`, stock reading uses `0x30`); residue and DC on static black (the focus ticks) over the 300-update endurance run |
| D on YRD0426 / OPM040B3 | The time (80 ms only if FR code 1 is 40 ms per frame); a 2-frame waveform is expected to ghost the most |
| D on SE0400NQW47 | Which table ran (mode 3 or the reading table; the first FAST after a clear always runs the reading table because of the driver's `_afterReader` rule, `Ssd2677Driver.cpp:196-197`, and a large word may exceed 1/8 of the frame bytes); every SE0400 duration |

## Who selects a profile (tasks D3a and D3b)

The stored byte `rsvpDisplayProfile` (CrossPoint's settings file; `TrialUnlock.h` encoding) says which
trials are unlocked and which profile is chosen. `RsvpActivity::onEnter()` applies it before playback:
`setUnlockedTrials(unlocked)`, `select(chosen)`, then the scheduler takes `info().clearing`. With
nothing stored that is Stock, as before. The device test's benchmark (`src/activities/devicetest/`)
unlocks the trials it runs, selects each profile in turn, and on leaving re-selects the chosen one; it
never changes `chosen`. It uses the backend as RSVP does: `update(UpdateKind::WORD)` for word frames
and its full-screen series, `update()` for prompts, `clear()` for clearing refreshes, `resetStats()`
before each series and `exposure().promotions()` to count promotions. No accessor was added.

Task D3b adds who changes `chosen` and keeps a trial for reading (`src/activities/devicetest/README.md`,
"Choose a profile" and "Settings: Speed read display"; the rules in `ProfileChoice.h`):

| Who | What it writes |
| --- | --- |
| The benchmark (D3a) | Unlocks the trials it runs; `chosen` unchanged |
| The device test's "Choose a profile" | `chosen`; clears every unlock bit but the chosen trial's and the kept ones'; `rsvpProfilesKept` (a second persisted-only byte: the trials kept selectable for reading). Stock with nothing kept writes 0 and 0, the factory state |
| Settings > Display > "Speed read display" | `chosen` only, among Stock and the kept trials that are unlocked and made for this panel; never the unlock bits. The row exists only while a kept trial is unlocked (never with the factory byte, nor after a benchmark alone) |
| The guided acceptance's Part A | Nothing: it plays on the chosen profile as RSVP resolves it (`resolveProfile()`), and the menus stay on Stock |

The allow rule still decides at every word update: a chosen trial the panel does not allow, or any trial
while the screen is inverted, plays as Stock, and the Settings row shows Stock for it.

## Environment variables (simulator only)

| Variable | Meaning |
| --- | --- |
| `CROSSPOINT_SIM_UPDATE_MS` | Modelled word-update time for every profile, 0 to 60,000 ms; `0` adds no delay. Beats the timings file and the defaults |
| `CROSSPOINT_SIM_CLEAR_MS` | The same for clearing refreshes |
| `CROSSPOINT_SIM_DISPLAY_SELFTEST` | Run the self-test with this many updates (1 to 100) |
| `CROSSPOINT_SIM_BENCH_TIME_SCALE` | Task D3a: the device test benchmark's clock runs this many times faster (1 to 1,000), so a scenario's paced passages take seconds (`src/activities/devicetest/README.md`). Not read by the backend |

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
`docs/BASE.md`); E3 and D1 needed none (D1 uses only existing `HalDisplay` calls). D2 adds
`HalDisplay::setCustomLut()`, stubbed by `scripts/simulator_patches/0004-hal-display-set-custom-lut.patch`
(the simulator backend never calls it).
