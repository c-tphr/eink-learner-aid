# src/activities/devicetest

**DeviceTestActivity** (`docs/PLAN.md` sections 4 and 10): the screens the owner uses in the one
device session to check the controls, measure the display and try the trial display profiles, and the
results files under `/rsvp-bench/`. Task D3a (#32) filled it with the Device test menu, the Controls
check and the display benchmark; task D3b adds the guided acceptance walk-through and the profile
choice.

| File | What it holds | Host-tested |
| --- | --- | --- |
| `DeviceTestLaunch.h` | Settings' entry point: `devicetest::AVAILABLE`, `devicetest::create()`; without the flag `false` and an empty inline function | |
| `DeviceTestActivity.h/.cpp` | The activity: the screens, input, drawing, the results files on the SD card, the serial lines | no (scenarios) |
| `ChoiceList.h/.cpp` | The device-test list (S4 row 34): rows, highlight rules, repeat mask. D3b reuses it | yes |
| `BenchPlan.h/.cpp` | The sweep's plan (`STEPS`), the results records, `Sweep` (what each answer leads to), the trials that may run and the stored unlock value, file names, the time estimate | yes |
| `SeriesRunner.h/.cpp` | Runs one step on the selected profile, one display call per `tick()`, and reports its figures | yes, through every step of every profile on the fake backend |
| `Passage.h/.cpp` | The fixed 150-word passage and its `rsvp::WordSource` | yes |
| `ControlsCheck.h/.cpp` | The Controls check's event record, names, key tracker and exit rule | yes |
| `BenchJson.h/.cpp` | The results files' records and their JSON encoding | yes, and against the schema |

Host tests: `test/device_test` (`DeviceTestTest`, and `DeviceTestResultsSchema`, which checks the
encoder's output against `tools/device/schema/rsvp-bench.schema.json`). Scenarios:
`devicetest_sweep` (touch), `devicetest_sweep_keys` (Up and Down only), `devicetest_controls`.

Everything here is compiled only with the RSVP feature flag (`docs/ARCHITECTURE-RSVP.md`).

## Entering, and the menu

Settings > Display > **Device test**, the first row of the Display section, in RSVP builds only (the
row is added through `DeviceTestLaunch.h`; builds without the flag show nothing). The menu:

| Row | Does |
| --- | --- |
| Controls check (default) | The Controls check below |
| Display benchmark | The benchmark below |
| Clearing refresh | One clearing refresh of the menu at once (the plan's remedy for residue); the menu then shows its time |
| Leave the device test | Back to Settings (so does the left-edge Back swipe) |

Adding a row (task D3b: "Guided acceptance", "Choose a profile"): one value in `MenuRow` before
`Leave`, one label in `drawScreen()`'s menu case, one case in `choose()`, and `MENU_ROWS` (with its
log name) one larger.

## Lists and controls

Every list on these screens is a `ChoiceList`: up to eight rows of 448x72 px from y=72, 8 px apart
(`controls::CHOICE_ZONES`, S4 row 34). A short press of a side key moves the highlight on release
(Up: previous, Down: next; no wrap), a 0.7 s hold of either side key chooses the highlighted row,
Confirm chooses it too, a tap chooses the row under the finger, and the Back gesture leaves where
leaving is harmless (menu, temperature, overview, run summary; a prompt during a benchmark needs an
answer). A stepper row repeats every 0.4 s while held (the temperature). The default row commits
nothing ("Repeat step", "Continue", "Controls check"). A new screen ignores input for 0.3 s and until
every key and finger is up. The key hint "Up/Down: move · hold: select" is drawn at the bottom
(upstream draws none on touch boards).

S4 section 13 suggested a `ChoiceList` subclass of `UiListActivity`. It is a plain component instead:
the rows must sit at S4's geometry rather than the theme's list layout, the benchmark moves between
eight prompts and a running series inside one activity (a sub-activity per prompt would push and pop
through a series, and its records would have to be handed back and forth), and the RSVP pause screen
already draws this way from the same table. D3b uses it the same way.

## Controls check

Plan section 10, step 7; S4 sections 7 and 12. The screen shows the instructions, a line "Tried:
..." / "Not yet: ..." (Up, Up held, Down, Down held, Confirm, Power click, Touch), the newest nine
events and a **Finish the controls check** button. Every input is recorded, shown and logged at once:

| Input | Recorded |
| --- | --- |
| A key (side keys, GPIO5 Confirm, a power click) | The raw `HalGPIO` index and name, every `MappedInputManager` button that reported its press or release, the role on the new screens (`prev`, `next`, `confirm`, `none`), the hold time, and `short` or `hold` (at least 0.7 s; a power click is always short, since a power hold sleeps) |
| A tap, a long press, a swipe (four directions), the left-edge Back swipe, the bottom-edge Home swipe | The kind and the position (portrait logical pixels; a swipe's start point) |

Reading raw button indices is reserved for remap-style screens (`.agents/rules/ui-activities.md`);
S4 section 7 makes this screen the second one, like `getPressedFrontButton()`. A key that was down
when the screen opened (the hold that chose the row) is ignored until released.

**Leaving does not depend on any one input:** a tap on Finish (touch only), a 3 s hold of any key
other than power (either side key alone, or Confirm), the bottom-edge Home swipe, or a power hold
(which sleeps the device, as everywhere). The exit hold's release is handed to `MappedInputManager`,
so the menu never sees it. The file is written when the owner leaves, when the device sleeps, and
whenever 2 s pass after an event (never right after a press, so a write cannot delay reading the next
one); every event is also in the serial log at once.

## Display benchmark

Plan section 10, step 9 and "Benchmark sweep, per display profile"; spike S2 "Consequences ... D3a".

### What the owner sees and does

1. **Room temperature.** Rows: Continue (default), Warmer +1 °C, Cooler −1 °C (both repeat while
   held), Temperature unknown, Back. It starts as "unknown"; the first Warmer or Cooler sets 20 °C.
   "Unknown" is recorded as `room_temperature_c: null`.
2. **Overview.** Panel and controller, temperature, battery, the profiles that will run and, for each
   one that will not, why ("not made for this panel": Trials A to C on SE0400NQW47; "panel not
   recognised": no trial at all, Stock only; "the screen is inverted": night mode), and the expected
   time per profile. Rows: Start the benchmark (default), Back. Start stores the unlock value (below)
   and writes the results file for the first time.
3. **Each step** shows "<profile> · step k / 10", what it does, "A key or a touch stops this step."
   and, for rated steps, "When the words stop, look, then press a key or touch." The word band and
   the endurance run show "Update n / count" under the band; the six passages show only the band, as
   in reading.
4. **After a rated step** the last frame stays on screen (no refresh) until the owner presses a key
   or touches the screen, so residue can be judged on the frame the profile left. Then the **rating
   prompt**: Repeat step (default), Clean, Tolerable, Unreadable, "Unreadable, skip rest of profile";
   below the rows the delivered WPM, the words drawn and the order check. After the endurance run a
   second prompt: Repeat step (default), No residue, Residue visible.
5. **A key or a touch during a series stops it** (also the Home swipe; a screen pushed over this one,
   the light panel, stops it too). The **stop prompt**: Repeat step (default), Skip the rest of this
   profile, End the benchmark. The stopped step keeps its partial figures (`stopped`).
6. **End of a profile:** the profile summary (word band median, p95 and max; full-screen and clearing
   medians; delivered WPM and rating of each passage; endurance rating and residue; what was skipped
   or stopped). Rows: "Next profile: <name>" (default) and End the benchmark, or "Finish the
   benchmark" after the last one.
7. **End of the run:** "Benchmark finished" or "Benchmark ended", where the file was saved (or that it
   could not be, and that the figures are in the serial log), and one line per profile. Row: Back to
   the device test menu.

Every prompt after a series is drawn with a clearing refresh, which also ends a trial's run (exposure
rule 4). **The plan's stop condition** "a trial profile leaves heavy residue: rate it Unreadable, run a
clearing refresh, carry on with the next profile" is "Unreadable, skip rest of profile" (or stopping
the series and "Skip the rest of this profile"): the profile summary that follows is drawn with a
clearing refresh, and its default row goes on to the next profile. The menu's Clearing refresh row
runs another one whenever wanted.

### The sweep

Stock first, then every trial the panel allows, in table order. For each, these ten steps
(`BenchPlan.h`, `STEPS`):

| # | Step | How it runs | Recorded |
| --- | --- | --- | --- |
| 1 | `word-band` | 100 word updates with the passage's first 100 words, through the RSVP scheduler at 1000 WPM: each update follows the last after the 60 ms dwell | `updates` (median, p95, max, min, mean, timeouts), clears by the policy, promotions |
| 2 | `full-screen` | 10 updates of a whole page of the passage, two pages alternating so almost every line changes, presented as word updates (`UpdateKind::WORD`, so a trial runs its own waveform) | `updates` median and the rest |
| 3 | `clearing` | 5 clearing refreshes of the same pages | `clears` median and the rest |
| 4-7 | `playback-120-1` ... `playback-300-1` | The 150-word passage in RSVP at 120, 180, 240 and 300 WPM, one word per update | delivered WPM, words drawn, order check, updates, rating |
| 8-9 | `playback-240-2`, `playback-240-3` | The passage at 240 WPM with 2 and 3 words per update | the same |
| 10 | `endurance` | 300 consecutive word updates at 240 WPM: the passage twice, word indexes counting on | the same, rating, residue |

Every step starts with a clearing refresh of its own screen (so a trial run starts from a clear, rule
2 of `TrialExposure.h`, and the driver's own promotions after a wake or grey content are absorbed),
then `resetStats()`: the backend's median and p95 cover exactly the series (the window holds 128
calls; the largest timing series is 100; the endurance run's median and p95 cover its last 128
updates, its count, min, max and mean all 300). Scheduler steps run with the profile's clearing limits
(`ClearingPolicy`), so clearing refreshes happen when the profile's policy says and are counted under
`clears`, never `updates`. Promotions (requests the backend ran as clearing refreshes) are recorded
per series; they should be 0 (the host tests check every step of every profile).

### What the benchmark stores

Trials are locked by default (task D2). Start unlocks, in the stored byte (`TrialUnlock.h`), the trials
it is about to run (only those `profileAllowed()` accepts on this panel and screen), in addition to
any already unlocked; **the chosen profile is not changed**, so it stays Stock until task D3b's
"Choose a profile". The byte is `rsvpDisplayProfile` in CrossPoint's settings file (persisted only,
like R4b's two RSVP values; not shown in Settings or on the web page). After a benchmark on a DEPG0397
panel it is `0x0F` (all four unlocked, Stock chosen); on SE0400NQW47 `0x08`; on an unrecognised panel
unchanged. RSVP opens with `select(chosen)` (`RsvpActivity::onEnter`), so ordinary reading stays on
the chosen profile, and only a trial the owner keeps in D3b becomes selectable for reading. Unlocked
bits alone only let later device-test runs and D3b use the trial. The run's file records the byte
before and after (`trial_setting`). Leaving the screen re-selects the chosen profile in the shared
backend.

### Files and the serial log

`/rsvp-bench/NNNN-bench.json` and `NNNN-controls.json`: format, names and every member in
`tools/device/schema/rsvp-bench.md`. The benchmark file is rewritten after every step, rating and
answer (temporary file, then rename, as `src/reading/BookStateFile.cpp` does); a sleep or the Home
swipe writes it with status `interrupted` and what was measured so far; a crash leaves the last
complete write with status `running`. The run number is the folder's largest plus one, so names sort
by run whether or not the clock is set and never overwrite each other.

The same figures go to the serial log at once (INF lines, so they appear at any log level):

| Line | When |
| --- | --- |
| `[DTS] open panel=<name> setting=<byte> activity_bytes=<n>` | The screen opened (DBG) |
| `[DTS] bench start run=<n> file=<path> panel=<name> controller=<c> temperature=<t> known=<0\|1> battery=<%> profiles=<keys> not_run=<key:reason,...\|none> setting_before=<b> setting_after=<b>` | Start |
| `[DTS] step start profile=<key> step=<step> attempt=<n>` | A series begins (DBG) |
| `[DTS] result profile=<key> step=<step> outcome=<complete\|stopped> attempt=<n> updates=<n> median=.. p95=.. max=.. min=.. mean=.. timeouts=.. clears=<n> clear_median=.. clear_max=.. clear_timeouts=.. promotions=<n> duration_ms=<ms>` | A series ended |
| `[DTS] result profile=<key> step=<step> target=<wpm> words_per_update=<k> delivered=<wpm.d> words=<n> frames=<n> clearing=<n> active_ms=<ms> order=<ok\|violated> drawn=<n> segments=<n> violations=<n> gaps=<n> repeats=<n>` | After the line above, for scheduler steps; then `[DSP] stats ...` |
| `[DTS] rating ... choice=<repeat\|clean\|tolerable\|unreadable\|unreadable-skip>`, `[DTS] residue ... choice=<repeat\|no\|yes>`, `[DTS] stopped ... choice=<repeat\|skip-profile\|end-run>` | An answer |
| `[DTS] saved file=<path> bytes=<n> status=<run status> ok=<0\|1>` | Every write |
| `[DTS] bench end run=<n> status=<status> saved=<0\|1>` | The run ended |
| `[DTS] clearing refresh ms=<ms>` | The menu's Clearing refresh |
| `[DTS] controls start run=<n> file=<path> side_layout=<name> short_power=<name>`, `[DTS] controls event n=<n> <description>`, `[DTS] controls end run=<n> events=<n> status=<status>` | The Controls check; the description is the screen's line, e.g. `key BTN_DOWN (5) -> Down PageForward NavNext, next, 906 ms, hold` |
| `[DTS] screen <name> focus=<row> refresh=<clear\|update>` | A list screen was drawn (DBG) |
| `[CTL] DeviceTest armed screen=<name>`, `[CTL] DeviceTest focus=<screen>:<row>`, `[CTL] DeviceTest select=<screen>:<row>` | Input (DBG) |

### Auto-sleep, sleep and other screens

`preventAutoSleep()` is true while a series runs and while the last frame of a rated step waits for
the owner, so the auto-sleep timer cannot cut a series (prompts and menus may sleep as anywhere else;
the file is up to date then). A power hold still sleeps the device: `prepareForSleep()` writes the
files first. As in RSVP, after 30 s without input the main loop may power the panel's controller down
between two refreshes (`idleDisplayWhileAwake`, `src/main.cpp`); during a long playback step the next
update then includes the controller's wake-up, as it does in reading. A screen pushed over this one
(the light panel) stops a running series within 1.5 s.

### Time per profile, for planning the session

`estimateProfileSeconds()` (host-tested): each step's opening clear, the series at the update time plus
the 60 ms dwell, each passage frame at its paced slot or the update plus dwell, whichever is longer,
a clearing refresh every soft-limit updates, and 15 s of the owner's time per rated step (7 per
profile).

| Profile (update / clear, transfer included) | Per profile | of which the owner |
| --- | --- | --- |
| Stock at the Xteink X4 figures (540 / 1,960 ms), the overview's estimate when the panel's are unknown | about 14 min (852 s) | 105 s |
| Stock on DEPG0397 as predicted (418 / 998 ms) | about 12 min (695 s) | 105 s |
| Trial A on DEPG0397 (338 / 998 ms) | about 10 min (627 s) | 105 s |
| Trial B (258 / 998 ms, soft limit 45) | about 9.5 min (567 s) | 105 s |
| Trial C, Trial D (198 / 998 ms, soft limit 30) | about 9 min (538 s) | 105 s |

A full sweep on DEPG0397 is therefore about 50 minutes (12 + 10 + 9.5 + 9 + 9); at the X4 figures
for every profile about an hour; on SE0400NQW47 (Stock and Trial D) about 25 minutes. Most of it is
pacing: the six passages and the endurance run take about 340 s per profile even on an instant display.

## Simulator time scale

In the simulator only, `CROSSPOINT_SIM_BENCH_TIME_SCALE=<1..1000>` makes the benchmark's clock run
that many times faster, so the scenarios' six passages and endurance run take seconds. The file
records it (`simulator.time_scale`). Counts, order checks, statuses, the file and the stored byte are
real logic there; every time (the backend's modelled times, and delivered WPM and active time, which
also include the main loop's ~10 ms poll multiplied by the scale) is a modelled number, not a
prediction.

## Memory

One heap object for the screen's life (`activity_bytes` in the open line; 12,752 bytes in the
simulator): the sweep's records (five profiles of ten steps, about 6.5 KB), the Controls check's 64
events (about 2 KB), the scheduler with its 300-byte frame, and the text of the screen being drawn
(2.5 KB, render task only). Nothing is allocated per update: frames, text widths, pages and the JSON
encoder use fixed storage (the encoder streams through a 256-byte buffer). Each results write opens
one file (the HAL's small file handle) and runs per step, never per update; the run number scan opens
one handle per file in `/rsvp-bench/` once per run.

## What only the device session can confirm

- Every time: the medians, percentiles and delivered WPM of each profile on the unit's panel.
- Whether the "look, then press" pause leaves the last frame as the profile left it (no refresh runs
  until the key or touch; the SDK's own power-off on SE0400NQW47 does not redraw).
- How the full-screen series looks on a trial waveform (a whole page driven by a table made for a word
  band), and whether the full-screen time differs from the word band's at all (X4: no).
- Whether 15 s per rating is right for the owner.

## For the session checklist

Screens in order, with the text they show (task D6 writes `docs/DEVICE_SESSION.md`):

| Screen | Title / text | Rows (default first) |
| --- | --- | --- |
| Settings > Display | "Device test" is the first row | |
| Menu | "Device test", "Panel: <panel> (<controller>)" | Controls check · Display benchmark · Clearing refresh · Leave the device test |
| Controls check | "Controls check"; "Press each key and the power button once." "Hold each side key for a second. Tap and swipe." "To finish: tap Finish, or hold any key 3 seconds."; "Tried: ..." / "Not yet: ..."; numbered events, e.g. "2. key BTN_DOWN (5) -> Down PageForward NavNext, next, 906 ms, hold" | Button "Finish the controls check" |
| Room temperature | "Room temperature", "Room temperature: unknown" or "... 21 °C"; "Read a thermometer near the device, if there is one." | Continue · Warmer: +1 °C · Cooler: -1 °C · Temperature unknown · Back |
| Overview | "Display benchmark", panel, temperature, "Battery: n%", "To run: Stock, ...", "Not run: ..." with the reason, "Each profile takes about n minutes" | Start the benchmark · Back |
| Step | "<profile> · step k / 10", the step ("100 word updates, as fast as the panel allows", "10 full-screen updates", "5 clearing refreshes", "The passage at 120 WPM, 1 word per update", "Endurance: 300 word updates at 240 WPM"), "A key or a touch stops this step.", for rated steps "When the words stop, look, then press a key or touch." | none; a key or touch stops it |
| Rating | "How readable was it?", "<profile> · <step>", "delivered n WPM · 150 / 150 · order ok" | Repeat step · Clean · Tolerable · Unreadable · Unreadable, skip rest of profile |
| Residue (after endurance) | "Was residue visible after the run?" | Repeat step · No residue · Residue visible |
| Step stopped | "Step stopped" | Repeat step · Skip the rest of this profile · End the benchmark |
| Profile summary | "Display benchmark", "<profile> · done" or "· rest skipped", one line per step | Next profile: <name> · End the benchmark (or Finish the benchmark) |
| Run summary | "Benchmark finished" / "Benchmark ended", "Saved as /rsvp-bench/NNNN-bench.json" (or "Could not save the results file: see the serial log"), one line per profile | Back to the device test menu |

Checklist notes: rate after looking at the frame left on screen; after "Unreadable, skip rest of
profile" the next screen is itself a clearing refresh; the menu's Clearing refresh row runs one more.
Return every file in `/rsvp-bench/` and the serial log.
