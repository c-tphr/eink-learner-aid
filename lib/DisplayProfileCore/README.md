# DisplayProfileCore

The hardware-free part of **DisplayProfile** (plan sections 6 and 9; `docs/spikes/S2-waveforms.md`,
sections 8 and 9): the display profiles and their clearing limits, the backend interface RSVP draws
through, refresh statistics, the simulator's timing model, the device backend's timing logic, what
the backend does on each panel, and fakes for host tests. The firmware part (the millisecond clock,
the simulator backend, the device backend's two hardware calls, the self-test) is in `src/display/`.
Host-tested in `test/display_profile`.

| File | What it holds |
| --- | --- |
| `DisplayProfiles.h/.cpp` | `ProfileId`, `ProfileInfo`, `PROFILES` (ids, names, clearing limits, simulator defaults), `findProfile`, `PanelId`, `panelName`, `panelFromVariant`, `defaultProfileAllowed` |
| `DisplayBackend.h/.cpp` | `DisplayBackend` (the interface and its calling contract), `Presenter`, `Sleeper`, `ModelledCall`, `ModelledBackend` |
| `DisplayStats.h/.cpp` | `RefreshStats` (count, min, max, mean, last, timeouts, and median and percentiles over the last 128 calls), `DisplayStats` (updates, clears) |
| `TimedBackend.h/.cpp` | The device backend's logic (task D1): `RefreshPort` (the two hardware calls), `TimedBackend` (timing window, timeouts), `UPDATE_TIMEOUT_MS`, `CLEAR_TIMEOUT_MS`, `TimedCall` |
| `PanelRecords.h/.cpp` | `Controller`, `PanelRecord`, `PANEL_RECORDS` (one per panel: controller, recognised, async split, predicted times, waveforms), `panelRecord`, `resolvePanel` |
| `DisplayTiming.h/.cpp` | The timing model: `TimingModel`, `TimingFile`, `TimingOverrides`, `parseOverrideMs`, `checkTimingFile`, `remainderToSleepMs` |
| `ProfileDisplaySink.h/.cpp` | `ProfileDisplaySink` (an `rsvp::DisplaySink` over a backend) and `FramePainter` |
| `FakeDisplayBackend.h` | Host tests only: `FakeClock`, `FakeDisplayBackend` (the modelled backend with a fake presenter and sleeper, and an event log), `FakeRefreshPort` (the device backend's hardware calls, with a transfer time, a BUSY time and a pending refresh) |

Everything is in namespace `displayprofile`. It depends on `lib/RsvpCore` headers only
(`ClearingLimits`, `Clock`, `DisplaySink`, `Frame`). Like RsvpCore it has no Arduino, HAL, SDK,
heap allocation (except the test-only fake), exceptions or RTTI, and it keeps compiling for every
target: on devices without the RSVP flag nothing references it and the linker drops it.

Why a library of its own and not headers in `lib/RsvpCore`: RsvpCore must stay unaware of profiles
(the scheduler sees only a `DisplaySink`), and the profile table, panel identity and timing model are
display concerns that D1, D2 and D3a extend. A separate library keeps RsvpCore unchanged and lets
both be tested on the host.

## Profiles

| Id | Name | Experimental | Clearing limits (soft/hard) | Simulator default update / clear |
| --- | --- | --- | --- | --- |
| `stock` | Stock | no | 60 / 120 | 540 / 1,960 ms |
| `trial-a` | Trial A | yes | 60 / 120 | 338 / 1,960 ms |
| `trial-b` | Trial B | yes | 45 / 90 | 258 / 1,960 ms |
| `trial-c` | Trial C | yes | 30 / 60 | 198 / 1,960 ms |
| `trial-d` | Trial D | yes | 30 / 60 | 198 / 1,960 ms |

- The ids are stable strings for settings and results files. `findProfile()` matches them exactly.
- Clearing limits are those of S2, section 8. RSVP passes them to `Scheduler::setClearingLimits()`;
  the scheduler itself knows nothing of profiles.
- Simulator defaults: stock uses the Xteink X4 figures of plan section 9 (540 and 1,960 ms). Trials
  use S2's predicted waveform time on the DEPG0397 panel (300, 220, 160 and 160 ms) plus
  `FRAME_TRANSFER_MS` (38 ms, the at least 38.4 ms needed to send the full frame), and clear like
  stock: the clearing refresh is the same HALF refresh with any trial table released.

## Panel identity and the allow hook

`PanelId` copies the SDK's private `PiccoPanel` values 0..4 (`UNKNOWN`, `YRD0426`, `DEPG0397`,
`OPM040B3`, `SE0400NQW47`) and adds `SIMULATED` (100). It is not called `SIMULATOR` because the
simulator build defines that macro. `panelName()` gives `unknown`, `YRD0426`, ..., `simulator`.

`DisplayBackend::allows(id)` is virtual. Its default, `defaultProfileAllowed()`, always allows
stock, allows every profile in the simulator (no glass to protect, so the trial timings can be
tried there), and allows no trial on a real panel. D2 overrides it on the device with S2's rule.

## The backend

`DisplayBackend` is what RSVP and the benchmark call, and what D1 and D2 implement on hardware:

| Call | What it does |
| --- | --- |
| `panel()` | The panel the driver found |
| `allows(id)` | Whether a profile may be used now (see above) |
| `select(id)` | Makes it the profile of later calls; false (and no change) if not allowed. Refreshes nothing |
| `profile()`, `info()` | The selected profile and its table entry |
| `update()` | Presents the frame buffer as one word update; blocks until finished; returns ms |
| `clear()` | One clearing refresh; blocks until finished; returns ms |
| `stats()`, `resetStats()` | Count, min, max, mean, last, median, percentiles and timeouts of updates and of clears |

The calling contract (who draws, who refreshes, which task, blocking) is at the top of
`DisplayBackend.h` and repeated in `src/display/DisplayProfile.h`. In short: the caller draws the
frame into the renderer's frame buffer, the backend alone refreshes, both on the task holding the
RenderLock (for RSVP, the render task inside `Scheduler::step()`), and both calls block until the
refresh has finished.

`update()` and `clear()` are non-virtual: they call the backend's `presentUpdate()` /
`presentClear()` and record the returned duration, so every backend keeps statistics the same way.
A subclass overrides `presentUpdate()`, `presentClear()`, `panel()`, and optionally `allows()` and
`onSelect()` (called after a successful `select()`, for example to prepare a waveform).

`ProfileDisplaySink` adapts a backend to `rsvp::DisplaySink`: `showFrame(frame)` calls the
activity's `FramePainter::paintFrame(frame)` and then `update()`; `clearingRefresh()` calls
`paintClearedBand()` and then `clear()`. The painter only draws.

## Timing model

`ModelledBackend` is the simulator's backend logic, kept here so it is host-tested. Each call:

1. reads the clock, has the `Presenter` do the real refresh, reads the clock again (`realMs`);
2. sleeps `remainderToSleepMs(modelMs, realMs)` = `modelMs - realMs`, or nothing when the real call
   already took as long as the model or longer (`sleptMs`), through the `Sleeper` (a thread sleep;
   never a busy wait);
3. returns the whole call's duration (`totalMs`), which is never below the model.

The simulator uses the renderer, `millis()` and `delay()`. The fake uses a fake clock, a presenter
that advances it by a configurable "real" time, and a sleeper that advances it by the remainder.

### Where the modelled times come from

In increasing priority; `TimingModel::resolve()` does the arithmetic and records each value's source:

| Priority | Source | Applies to |
| --- | --- | --- |
| 1 | Built-in defaults (the table above) | Every profile |
| 2 | `docs/device/picco-timings.json`, compiled in at build time | The profiles it lists: their word-update median and clear median. A profile it does not list keeps its default update time and takes the file's stock clear median |
| 3 | `CROSSPOINT_SIM_UPDATE_MS`, `CROSSPOINT_SIM_CLEAR_MS` (environment) | Every profile. Whole milliseconds 0 to 60,000; `0` means no added delay |

A malformed environment value stops the simulator with an error line naming it, when the backend is
first created (the self-test at start-up; later, the RSVP activity when it opens). Nothing is half
applied: `parseOverrides()` sets both values or neither.

The simulator logs the resolved model when its backend is created, one line per profile:

```
[DSP] No timings file (docs/device/picco-timings.json): built-in defaults
[DSP] Simulator model stock: update 200 ms (env), clear 400 ms (env)
```

and one line per call (`n` counts calls since the last `resetStats()`):

```
[DSP] update n=1 profile=stock ms=200 model=200 real=6 slept=194
[DSP] clear n=1 profile=stock ms=400 model=400 real=4 slept=396
```

### The timings file

`docs/device/picco-timings.json` is written after the device session from its benchmark results.
Its format is `tools/device/schema/picco-timings.schema.json`; `docs/device/picco-timings.example.json`
shows it with the Xteink X4 figures (marked `"example": true`). There is no real file until the
device session: no numbers are invented.

```json
{
  "format": "crosspoint-display-timings",
  "version": 1,
  "panel": "DEPG0397",
  "controller": "SSD1677",
  "source": "rsvp-bench results files and serial log of the device session",
  "date": "2026-12-01",
  "profiles": {
    "stock":   {"wordUpdateMs": {"median": 421, "p95": 433, "max": 470}, "clearMs": {"median": 1002}},
    "trial-c": {"wordUpdateMs": {"median": 207, "p95": 215, "max": 240}, "clearMs": {"median": 998}}
  }
}
```

- Times are whole milliseconds. The model uses the medians; p95 and max are recorded for the
  record (`null` means not measured).
- `stock` is required; the trials are optional (a trial that was rated unreadable need not be listed).
- **Unknown profile ids are rejected**, not ignored: a typo such as `trial_a` would otherwise leave
  that profile silently on its default.
- Also rejected: invalid JSON, duplicate members, fractions, `NaN`/`Infinity`, values outside the
  schema's ranges (word-update median 1 to 5,000 ms, clear median 1 to 10,000 ms), p95 or max below
  the median, `"example": true`, and the X4 panel name outside the example.

How the simulator gets it: `scripts/gen_display_timings.py`, a pre-script of `[env:simulator_picco]`,
calls `scripts/display_timings.py`, which validates the file and writes
`DisplayTimingsFile.generated.h` into the build directory (rewritten only when its text changes, so
an unchanged file recompiles nothing). A missing file gives a header that says "no file" and the
defaults apply; a malformed file stops the build before anything compiles, with the reason. At
start-up the simulator checks the compiled-in values once more (`checkTimingFile()`).

Why at build time and not by reading the file at start-up: the simulator runs with a working
directory and environment it does not choose (the scenario harness gives each run a temporary
directory and only `CROSSPOINT_SIM_*` variables), so a run-time path would depend on where it was
started and would silently fall back to defaults; and a binary built in one CI job and run in
another carries its numbers with it. A bad file also fails before the simulator ever starts.

Check a file by hand: `python3 scripts/display_timings.py check [FILE]`.

## Statistics

`RefreshStats::add()` keeps count, min, max, last and a 64-bit total over every call; `meanMs()`
rounds half up. It also keeps the last `SAMPLE_WINDOW` (128) durations in a fixed ring of 16-bit
values, for:

- `medianMs()`: the middle duration, or the mean of the two middle ones rounded half up;
- `percentileMs(p)`: nearest rank, the smallest duration that at least p % of the window do not
  exceed (`percentileMs(95)` is the 95th percentile, `percentileMs(100)` the window's maximum).

Both are exact while the window holds every call since `resetStats()`: the benchmark's largest
series is 100 updates (plan section 10), so D3a resets before each series and reads exact figures.
Over a longer run they describe the most recent 128 calls; count, min, max and mean still cover all
of them. A duration above 65,535 ms is kept as 65,535 in the window only. `timeouts` counts calls
the device backend classed as timed out (below); their durations are recorded like any other call.

`DisplayBackend` keeps one `RefreshStats` for updates and one for clears, from construction or
`resetStats()`. RAM: `sizeof(DisplayStats)` is 576 bytes (2 x 256-byte window plus counters), inside
the one backend object, allocated statically; no heap. The median and percentiles are computed
when asked (a selection over the window, at most 16,384 comparisons, no copy and no stack buffer),
never per refresh. `src/display/DisplayProfile.h`'s `logStats()` logs them as one line.

## Device backend logic

`TimedBackend` is the device backend without hardware: `src/display/DeviceDisplayBackend` is this
with a `RefreshPort` over `HalDisplay` and the renderer, and `MillisClock`; the host tests use
`FakeRefreshPort` and `FakeClock`. Each `update()` or `clear()`:

1. calls `RefreshPort::finishPending()`, which returns once no refresh started elsewhere is still
   running (device: `HalDisplay::waitRefreshComplete()`). Time spent here is `TimedCall::waitedMs`
   and is not part of the call's duration;
2. reads the clock, calls `RefreshPort::refresh(kind)` (the frame transfer, then the driver's wait
   for BUSY), reads the clock again: that difference is the duration returned and recorded;
3. classes the call as timed out when it lasted `UPDATE_TIMEOUT_MS` (5,000 ms) or longer for an
   update, `CLEAR_TIMEOUT_MS` (10,000 ms) for a clear, and counts it in `stats().<kind>.timeouts`.

The limits equal the largest medians the timings file accepts, so a measured median is never a
timeout. Why a timeout can only be classed after the call returns, and why the call does return:
`src/display/README.md`, "Timeouts".

## Panel records

`PANEL_RECORDS` holds one `PanelRecord` per panel the SDK's Picco probe can report (`UNKNOWN`,
`YRD0426`, `DEPG0397`, `OPM040B3`, `SE0400NQW47`) plus the simulator. It is the one place D2 and D3a
read from:

| Field | Meaning |
| --- | --- |
| `controller` | The SDK driver for the panel: `SSD1677` for the three SSD1677 panels and for an unrecognised one (generic configuration), `SSD2677` for SE0400NQW47 |
| `recognised` | One of the four panels with stock Picco waveforms. D2 keeps every trial off on an unrecognised panel |
| `asyncSplit` | `displayBufferAsync()` returns before the waveform ends and the driver keeps its own previous frame, so a benchmark can time transfer and waveform apart: the three recognised SSD1677 panels only |
| `predictedUpdateMs`, `predictedClearMs` | S2's waveform-only predictions (DEPG0397: 380 and 960 ms); 0 where the SDK runs OTP or undocumented tables |
| `updateWaveform`, `clearWaveform` | What a stock word update and a clearing refresh run, as text for logs and results files |

`resolvePanel(variant, driverName)` gives the panel the device backend acts on: the probe's variant
(`BoardConfig::ACTIVE.displayControllerVariant`), kept only when the driver the SDK selected
(`HalDisplay::controllerName()`) is the record's controller. Anything else is `UNKNOWN`: the stock
path, no trial. The per-call behaviour with source lines is in `src/display/README.md`.

## What later tasks do with this

- **RSVP activity (R4a and later):** implement a `FramePainter` for the word band, get the backend
  with `firmwareBackend(renderer)`, `select()` the profile from settings (falling back to stock when
  it returns false), pass `info().clearing` to the scheduler, wrap both in `ProfileDisplaySink`,
  and use `MillisClock` as the scheduler's clock. Run `clear()` (with the pause screen drawn) on
  pause and before the first update of a playback. Remove the self-test hook and its `main.cpp`
  call when the activity scenario covers the timing model.
- **D1 (done):** `DeviceDisplayBackend` runs stock's real path (FAST with the reader flag, HALF to
  clear) through `TimedBackend`, with timeouts, percentiles and the panel records.
- **D2:** load and release trial tables around the port's refresh (a second `RefreshPort` call, or
  a port that knows the selected profile), release before a clear, and override `allows()` with
  S2's rule, reading `panelRecord(panel()).recognised` and `controller`. The fakes' logs can check
  the safety sequence on the host (`FakeDisplayBackend::events`, `FakeRefreshPort::log`).
- **D3a:** reset the statistics before each series and read `medianMs()`, `percentileMs(95)`,
  `maxMs` and `timeouts`; record `panelName(panel())` and the record's `controller`.
- **Device session:** commit `docs/device/picco-timings.json` from the benchmark; the next simulator
  build uses it.
