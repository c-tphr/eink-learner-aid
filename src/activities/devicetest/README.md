# src/activities/devicetest

**DeviceTestActivity** (`docs/PLAN.md` sections 4 and 10): the screens the owner uses in the one
device session to check the controls, measure the display, try the trial display profiles, walk
through the PRD's acceptance criteria and choose the profile to keep, and the results files under
`/rsvp-bench/`. Task D3a (#32) filled it with the Device test menu, the Controls check and the display
benchmark; task D3b (#33) added the guided acceptance walk-through, the profile choice and the
Settings row "Speed read display".

| File | What it holds | Host-tested |
| --- | --- | --- |
| `DeviceTestLaunch.h` | Settings' entry point: `devicetest::AVAILABLE`, `devicetest::create()`; without the flag `false` and an empty inline function | |
| `DeviceTestActivity.h/.cpp` | The activity: the screens, input, drawing, the results files on the SD card, the serial lines | no (scenarios) |
| `DeviceTestAcceptance.cpp` | The activity's guided acceptance and profile choice screens (D3b) | no (scenarios) |
| `Acceptance.h/.cpp` | The guided acceptance: Part A's two runs, Part B's step table (every text the owner reads), the walk-through (what each answer leads to), the verdicts per criterion | yes |
| `AcceptanceProgress.h/.cpp` | The walk-through's progress file: encoding and decoding | yes |
| `ProfileChoice.h/.cpp` | Which profiles may be chosen and kept, the stored values a choice gives, the Settings row's rule and options | yes, for every panel |
| `SpeedReadSetting.h/.cpp` | Settings' row "Speed read display": shown only once a trial is kept; without the flag empty inline functions | through `ProfileChoice` and a scenario |
| `ResultsFile.h` | Writing a file through a temporary file and a rename | |
| `ChoiceList.h/.cpp` | The device-test list (S4 row 34): rows, highlight rules, repeat mask. D3b reuses it | yes |
| `BenchPlan.h/.cpp` | The sweep's plan (`STEPS`), the results records, `Sweep` (what each answer leads to), the trials that may run and the stored unlock value, file names, the time estimate | yes |
| `SeriesRunner.h/.cpp` | Runs one step on the selected profile, one display call per `tick()`, and reports its figures | yes, through every step of every profile on the fake backend |
| `Passage.h/.cpp` | The fixed 150-word passage and its `rsvp::WordSource` | yes |
| `ControlsCheck.h/.cpp` | The Controls check's event record, names, key tracker and exit rule | yes |
| `BenchJson.h/.cpp` | The results files' records and their JSON encoding | yes, and against the schema |

Host tests: `test/device_test` (`DeviceTestTest`, and `DeviceTestResultsSchema`, which checks the
encoder's output against `tools/device/schema/rsvp-bench.schema.json`). Scenarios:
`devicetest_sweep` (touch), `devicetest_sweep_keys` (Up and Down only), `devicetest_controls`,
`devicetest_acceptance` (touch), `devicetest_acceptance_keys` (Up and Down only),
`devicetest_choose_profile`.

Everything here is compiled only with the RSVP feature flag (`docs/ARCHITECTURE-RSVP.md`).

## Entering, and the menu

Settings > Display > **Device test**, the first row of the Display section, in RSVP builds only (the
row is added through `DeviceTestLaunch.h`; builds without the flag show nothing). The menu:

| Row | Does |
| --- | --- |
| Controls check (default) | The Controls check below |
| Display benchmark | The benchmark below |
| Guided acceptance | The walk-through below. The row says where the owner is: "Guided acceptance: part A", "... step 4 of 12", "... done" |
| Choose a profile | The profile choice below |
| Clearing refresh | One clearing refresh of the menu at once (the plan's remedy for residue); the menu then shows its time |
| Leave the device test | Back to Settings (so does the left-edge Back swipe) |

Adding a row: one value in `MenuRow` before `Leave`, one label in `drawScreen()`'s menu case, one case
in `choose()`, and `MENU_ROW_COUNT` (with its log name) one larger. Leaving by a key hold waits until
every key is up, so Settings never takes the rest of the hold as its own.

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

## Guided acceptance

Plan section 10, step 10 and "The PRD's on-device criterion"; the PRD's acceptance criteria 1 to 5
(criterion 6, the preparation workflow, is not on the device). Two parts, recorded in one results file
`/rsvp-bench/NNNN-acceptance.json` and in the serial log as they happen. The decisions (what each
answer leads to, the verdicts) are `Walkthrough` in `Acceptance.h`; the screens are in
`DeviceTestAcceptance.cpp`.

### Part A: criterion 5, measured on the chosen profile

**Start screen.** Title "Guided acceptance", subtitle "Display profile: <name>": the profile RSVP plays
with now (`resolveProfile()` of the stored byte on this panel; Stock unless a trial was chosen and is
allowed). Rows: **Start part A** (default), Skip part A, Back. Below them: "Part A plays the test
passage twice: at 240 WPM, then at a 600 WPM target. Watch the words. Then answer two short
questions. Part B has 12 steps in the books. Your answers are saved after each one. All of it takes
about 50 minutes."

**The runs.** The benchmark's fixed 150-word passage, one word per update, through the same
`SeriesRunner` as the benchmark's playback steps (`ACCEPTANCE_RUNS`): `acceptance-240-1` at the 240
WPM default, then `acceptance-600-1` at a 600 WPM target, on the profile shown. Each run starts with a
clearing refresh and resets the backend's statistics, so its figures are exactly the run's. The screen
is the benchmark's step screen with the title "Part A · run k / 2 · <profile>". For each run the file
records the target, the delivered WPM, the words shown, the word-order audit (every word index drawn
exactly once and in order: drawn, segments, violations, gaps, repeats), the update statistics (median,
95th percentile, maximum, minimum, mean, timeouts), the promotions and the clearing refreshes.

**After a run** the last frame stays on screen until a key or a touch (as in the benchmark), then
**"Was the last sentence readable?"**, subtitle "Part A · run k / 2 · <profile>". Rows: **Repeat the
run** (default), "Yes, it was readable", No. Below them the measured figures: "Target 600 WPM ·
delivered 131.0 WPM", "Words: 150 / 150, each once, in order" (or "ORDER CHECK FAILED" with the gaps,
repeats and out-of-order counts), "Updates: median .. ms, p95 .. ms, max .. ms", "Clearing refresh: n ·
timeouts n · promotions n".

**After the 600 WPM run's answer**: **"Was it slower than 600 WPM?"** with the same rows ("Yes: slower,
no word missing"), the same figures, and "At 600 WPM each word would show for a tenth of a second.
The panel is slower. So the words should come more slowly, one after another, with no word missing.
The numbers above are what the device measured." "Repeat the run" there plays the 600 WPM run again
and asks both of its questions again.

**A key or a touch during a run stops it**: "Run stopped". Rows: **Repeat the run** (default), Skip part
A. Then **"Part A done"** (or "Part A skipped"): one line per run (delivered WPM, words, order, the
readability answer), the slowdown answer, "Automatic check: Pass · your answers: Pass". Rows: **Continue
to part B** (default), Device test menu. "Start part A" always plays both runs again: the owner must
have watched a run to answer for it.

**The second question, and the pause screen.** The plan asks the owner to confirm "that the pause
screen shows a delivered WPM below the target". Part A is played by the device test, not by RSVP, so
there is no pause screen to look at; drawing a copy of RSVP's status line here would only prove that
the device test can print two numbers, and sharing RSVP's own formatting would need an edit in
`src/activities/rsvp/` (another task's code). So the check is split, and neither part copies RSVP code:

1. Part A states the two numbers the firmware measured (target and delivered WPM, and the word count
   with the order check) and asks the owner to confirm what they saw: the words came slower than 600
   WPM, one after another, with no word missing.
2. Part B's last step, `pause-screen-600`, checks RSVP's real pause screen: the owner plays an
   ordinary book at 600 WPM, pauses, and confirms the top line reads "Paused · 600 WPM target · N
   actual" with N below 600. Its result is `criterion_5.pause_screen`.

**Verdicts for criterion 5**, kept apart so task A2 can see which one failed
(`Walkthrough::criterion5()`, host-tested):

| Member | Pass when |
| --- | --- |
| `criterion_5.measured`, `measured_pass` | Automatic: both runs ran to the end, every word index was drawn exactly once and in order (150 drawn, 150 shown, no violation, gap or repeat), and each was delivered at or below its target (`runMeasuredOk()`) |
| `criterion_5.owner`, `owner_pass` | The owner's answers: both runs readable, and slower with no word missing |
| `criterion_5.pause_screen` | Part B's step `pause-screen-600` passed |

Criterion 5's result combines the three: any Fail fails it, else anything not done leaves it Not done,
else anything skipped leaves it Skipped, else Pass. Skipping Part A gives Skipped for both of its
verdicts (null in `measured_pass` and `owner_pass`).

### Part B: criteria 1 to 4, done by the owner in the reader

The owner leaves the device test, opens a book, does the step, and comes back. Each step has two
screens:

1. **Its instructions**: title "Step k of 12", subtitle the step's title, then what to do, then
   "Expected:" and what should happen. Rows: **Leave to do this step** (default), "Done: give the
   result", Device test menu. "Leave to do this step" saves the progress (the step, and that the owner
   is away doing it) and leaves the device test, back to Settings > Display; the owner goes Home and
   does the step.
2. **Its question**, shown at once when the owner comes back to Guided acceptance (or after "Done: give
   the result"): title "Step k: how did it go?", the step's title, the expected result again. Rows:
   **Not done yet** (default: back to the instructions; nothing is recorded), Pass, Fail, Skip this
   step. Fail asks **"What went wrong?"**: **Back** (default), "It did not appear", "It appeared at
   the wrong place", "Wrong mode after it", "A grade was lost or wrong", "Something else".

Steps are answered in order; the menu row says which one is in progress ("Guided acceptance: step 4 of
12"). After the last answer, the summary.

**The book.** The steps are written against `fixture.prepared.epub` (the kit's `sd-card/books/`), *The
Lighthouse Keeper's Almanac* (`fixtures/books/README.md`): checkpoint **c0001** (one question) follows
section s0001 in chapter 1 "The Lamp Room", in the middle of the chapter, after the words "no other
light on the coast did the same." (word 232; the next words are the heading "Winding the Clockwork");
checkpoint **c0002** (three questions) follows section s0004 "The Visitor from the Mainland", the end of
chapter 2 "The Fog Season", after "to put out the lamp." Its second question asks "What caused the
green light on the water, and what did it tell Dr. Vane?". Step 11 uses `fixture.epub`, the same book
without checkpoints, which has an image in chapter 1, a table in chapter 2 and a code block in chapter
3. Navigation is described by touch (the middle of a page opens the toolbar; its More button, three
dots, opens the list with Speed read and Select Chapter); key paths follow S4 rows 17 to 29.

**Order and grades.** One pass through the book shows everything: c0001 graded Correct in step 1;
c0002's first question Correct and its second Incorrect in step 4, rereads in steps 5 and 6 (the
retry's Correct leaves Incorrect stored), the third skipped in step 7; step 8 reads the checkpoint
again in page view and must show "Recorded earlier: Correct" and "... Incorrect". Steps 9 and 10 use
c0001 again, whose stored grade stays Correct.

### The steps

Every text is in one table, `WALK_STEPS` in `Acceptance.cpp`, so a later task can change the wording
without touching the logic (the host test only checks lengths, ids and which criteria are covered).
Each step's id, criteria and "depends on" go into the results file. **"Depends on" names the tasks whose
behaviour the step describes and that were not on `main` when it was written**: checkpoint triggers
and their screens in the reader (C2, C3), rereads (C4), restore after closing or a restart (C5). Those
steps were written from `docs/PLAN.md` section 7 and S4; their wording must be checked again once those
tasks merge (list below).

| # | Id | Criteria | Depends on | Title | Instructions (on screen) | Expected (on screen) |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `c0001-rsvp-touch` | 1, 3 | C2, C3 | Short checkpoint in RSVP, by touch | Use touch only. Open Browse Files, then books, then fixture.prepared.epub. Tap the middle of the page. Tap More (three dots), then Select Chapter, then The Lamp Room. Tap the middle of the page. Tap More, then Speed read. Tap Play. At the picture a banner appears: tap Continue speed reading, then Play. | Playback stops after "no other light on the coast did the same." The checkpoint shows question 1 of 1. Tap Reveal answer, then Correct. RSVP comes back after the break, with Play highlighted. Nothing plays by itself. |
| 2 | `c0001-page-keys` | 1 | C2, C3 | Short checkpoint in page view, by keys | Tap Page view. Tap the middle of the page, More, Select Chapter, The Lamp Room. From here use the side keys only. Press the next-page key to turn pages. Stop on the page that shows "did the same." Press the next-page key once more. | The checkpoint shows question 1 of 1 and "Recorded earlier: Correct". Press Down twice, then hold Down: Skip checkpoint. Page view shows the page after the break. Nothing else happens. |
| 3 | `toc-jump` | 1 | C2 | A jump does not fire a checkpoint | Use touch. Open Select Chapter and choose The Fog Season. Open Select Chapter again and choose The Keeper's Log. Open Select Chapter again and choose The Lamp Room. | No checkpoint appears after any of the three jumps. |
| 4 | `c0002-rsvp-keys` | 1, 2 | C2, C3 | Long checkpoint in RSVP, by keys | Open Select Chapter and choose The Fog Season. Turn pages to the last page of the chapter. It ends with "to put out the lamp." From here use the side keys only. Hold Down: Speed read opens. Hold Down: Play. | Playback stops after "to put out the lamp." The checkpoint shows question 1 of 3. Hold Down: Reveal answer. Press Down, then hold: Correct. Question 2 of 3 shows. Hold Down: Reveal answer. Press Down twice, then hold: Incorrect. The screen says "marked Incorrect". Reread in RSVP is highlighted. |
| 5 | `reread-rsvp-end` | 2 | C4 | Reread in RSVP to the end | Hold Down: Reread in RSVP. Hold Down: Play. Let it play to the end of the section. You may raise the WPM first to make it shorter. | The top line starts with "Rereading". Under Exit, Menu and Page view is a wide row "Back to the question". Play is highlighted. Near the bottom: "The Visitor from the Mainland On the". After "to put out the lamp." question 2 of 3 shows again, answer hidden, "Recorded earlier: Incorrect". Hold Down: Reveal answer. Press Down twice, then hold: Incorrect. |
| 6 | `reread-page-early` | 2 | C4 | Reread in page view, return early, retry | Use touch. Tap Reread in page view. Swipe to turn one page. At the bottom of the screen, where the battery and page number usually are, a strip shows "Rereading" and a small framed button "Back to the question". Tap the button. It is small: if it is hard to hit, write that in the notes and hold a side key instead, then hold again on "Back to the question". | Page view first shows the page with the heading "The Visitor from the Mainland". The strip covers no line of text. After the tap or the hold, question 2 of 3 shows again, with the answer hidden and "Recorded earlier: Incorrect". Tap Reveal answer, then Correct. Question 3 of 3 shows. |
| 7 | `skip-question-resume` | 3 | C3 | Skip a question, reading resumes | On question 3 of 3, tap Skip question. | The checkpoint ends. RSVP shows its pause screen at the start of The Keeper's Log. Play is highlighted. Nothing plays by itself. |
| 8 | `later-pass-grades` | 1, 2, 3, 4 | C2, C3 | Read it again: the first grades are kept | Use touch. Tap Page view. Open Select Chapter and choose The Fog Season. Turn to the last page of the chapter. Swipe to the next page. | Question 1 of 3 shows "Recorded earlier: Correct". Tap Skip question. Question 2 of 3 shows "Recorded earlier: Incorrect", not Correct. Tap Skip checkpoint. Page view shows the start of The Keeper's Log. |
| 9 | `close-in-question` | 4 | C2, C3, C5 | Close the book during a question | Open Select Chapter and choose The Lamp Room. Turn pages until question 1 of 1 shows. Swipe up from the bottom edge: Home. Open fixture.prepared.epub again. | The book opens directly at question 1 of 1, with the answer hidden and "Recorded earlier: Correct". No page of the book shows first. Tap Skip checkpoint. Page view shows the page with "did the same." and the text after it. No question shows. |
| 10 | `sleep-in-reread` | 4 | C2, C3, C4, C5 | Sleep during a reread | Open Select Chapter and choose The Lamp Room. Turn pages until question 1 of 1 shows. Tap Reveal answer, then Incorrect, then Reread in page view. Turn one page. Hold the power button until the device sleeps. Wake it. If the book is not open, open fixture.prepared.epub. | Page view is in the reread, on the same page. At the bottom, the strip shows "Rereading" and the "Back to the question" button. Tap the button (or hold a side key, then hold again). Question 1 of 1 shows, answer hidden, "Recorded earlier: Correct". Tap Skip checkpoint. Page view shows the page with "did the same." again. No question shows. |
| 11 | `ordinary-book-items` | none (plan section 1) | - | An ordinary book: pictures, tables, code | Go Home. Open Browse Files, books, fixture.epub (the copy without checkpoints). Choose The Lamp Room. Open Speed read. Tap Play. At the picture tap Continue speed reading, then Play. Choose The Fog Season, open Speed read, Play. Choose The Keeper's Log, open Speed read, Play. | RSVP stops before the picture, the table and the code block. Each time a banner says "... Speed reading stopped here." with Continue speed reading and Stay in page view. No checkpoint appears in this book. |
| 12 | `pause-screen-600` | 5 | - | The pause screen shows the actual pace | Stay in fixture.epub. Open Speed read. Tap + next to the WPM until it shows 600. Tap Play. After about 20 seconds, touch the screen to pause. | The top line reads "Paused · 600 WPM target · N actual". N is below 600. The words came one at a time, slower than 600 WPM. |

**Checked against the firmware** (task C5, the last firmware task): steps 5, 6, 9 and 10 were
corrected to what the firmware shows: the RSVP reread pause screen (step 5, `cp_reread_rsvp_end`), the
page-view reread strip at the bottom of the screen and its small button, with the side-key hold as the
other way (step 6, `cp_reread_page_early`; the button is 26 px high with the default settings, so the
owner is asked to say if it is hard to tap), the restored question after closing the book (step 9) and
the restored reread after a sleep (step 10), both done exactly as written in `cp_walkthrough_restore`.
"Page after the break" became "the page with "did the same."": c0001's break is in the middle of a page,
and that page holds the first word after it. Steps 1 to 4, 7 and 8 were written against C2 and C3 and
match their scenarios; steps 11 and 12 describe R4b and R5.

### Summary

"Guided acceptance done" (or "... so far" when opened from the menu before the end), "Saved as
/rsvp-bench/NNNN-acceptance.json" (or "Could not save the results file: see the serial log"). One line
per criterion 1 to 4 (Pass, Fail, Skipped, Not done); criterion 5 with its three parts ("Criterion 5:
Pass. Automatic check Pass, your answers Pass, pause screen Pass."); one line per failed step with its
reason; "Criterion 6 (preparing books) is not checked on the device." Rows: **Back to the device test
menu** (default), Start a new walk-through (a new run number and file; the finished file stays).

### Progress, files and the serial log

After every answer and every run the walk-through writes two files, each through a temporary file and
a rename: first the progress file `/.crosspoint/acceptance-progress.txt` (`AcceptanceProgress.h`: plain
text, one record per line, read back without a JSON parser and without allocating, host-tested), then
the results file. When the screen opens it reads the progress file, so the menu row and Guided
acceptance resume where the owner was: after "Leave to do this step", after sleep, a crash or a flat
battery. A run cut short (sleep, Home, leaving) counts as an attempt and Part A starts again from its
start screen; no answer given is lost. A damaged progress file is refused as a whole (logged) and the
walk-through starts again with a new run number; the earlier results file stays. The run number is
given at the first answer (Start or Skip part A), not when the screen is only looked at.

| Line (INF unless noted) | When |
| --- | --- |
| `[DTS] acceptance progress none` (DBG), `[DTS] acceptance resume run=<n> file=<path> part_a=<pending\|running\|done\|skipped> step=<k>/12 answered=<n> away=<0\|1> status=<running\|complete>` | The device test opened |
| `[DTS] acceptance open state=<start\|step\|result\|summary> step=<k>/12 answered=<n>` (DBG) | Guided acceptance chosen in the menu |
| `[DTS] acceptance start run=<n> file=<path> profile=<key> setting=<byte> panel=<name>` | The first answer gave the walk-through its file |
| `[DTS] acceptance part_a choice=<start\|skip-part-a\|back> profile=<key>` | Part A's start screen |
| `[DTS] step start profile=<key> step=acceptance-<wpm>-1 attempt=<n>` (DBG), `[DTS] result profile=<key> step=acceptance-<wpm>-1 outcome=... ` and the playback `result` line | A run, as the benchmark logs its steps (README above, "Files and the serial log") |
| `[DTS] acceptance stopped run=<id> choice=<repeat\|skip-part-a>`, `... readable run=<id> choice=<repeat\|yes\|no>`, `... slower run=<id> choice=<repeat\|yes\|no>` | Part A's answers |
| `[DTS] acceptance part_a status=<status> measured=<result> owner=<result>` | Part A ended |
| `[DTS] acceptance leave step n=<k> id=<id>`, `[DTS] acceptance step n=<k> id=<id> not-done` | The owner left to do a step; came back and chose "Not done yet" |
| `[DTS] acceptance step n=<k> id=<id> result=<pass\|fail\|skipped> reason=<none\|did-not-appear\|wrong-place\|wrong-mode\|grade-wrong\|other> answers=<n>` | A step answered |
| `[DTS] acceptance criteria c1=<result> ... c5=<result> measured=<result> owner=<result> pause_screen=<result> answered=<n> status=<status>` | After every step answer |
| `[DTS] acceptance run=<id> interrupted` | Sleep or leaving during a run |
| `[DTS] saved file=<path> bytes=<n> status=<status> ok=<0\|1> progress=<0\|1>` | Every write (`progress`: the progress file) |
| `[DTS] screen acceptance-<start\|stopped\|readable\|slower\|part-a\|step\|result\|reason\|summary> focus=<row> refresh=<clear\|update>` (DBG) | A screen was drawn |

### How long it takes on the device

Part A: each run is 150 word updates; on the stock profile of DEPG0397 (about 418 ms an update plus
the 60 ms dwell) a run takes about 75 s at either target, so with the opening clearing refreshes and
the three answers about 4 minutes. Part B, with the device's predicted pace of about 125 WPM: steps 1,
4 and 5 read up to 300 words in RSVP (2 to 3 minutes each), step 11 plays three short stretches (about
4 minutes); the other steps take 1 to 3 minutes; coming back to the device test (Home, Settings,
Display, Device test, Guided acceptance) takes about a minute each time. In all **about 50 minutes**,
an hour at most.

## Choose a profile

Plan section 10, step 11, and section 6, "Outcome": the owner picks the display profile to keep.
The rules are `ProfileChoice.h` (host-tested for every panel).

1. **"Choose a profile"**, subtitle "Now: <profile>" (the one RSVP plays with). One row per profile
   offered: **Stock**, then each trial that the benchmark has unlocked and that the allow rule accepts
   on this panel now (`choiceCandidates()`, as `DisplayBackend::allows()` decides: on SE0400NQW47 only
   Trial D; on an unrecognised panel, or while the screen is inverted, Stock only), then Back. Each row
   shows what the newest benchmark file (the largest run number, read with a filter so only the names,
   ratings, residue and delivered WPM are kept) says about it: "Trial B · 6 clean · 188 WPM" (Clean
   ratings of the seven rated steps, and the 240 WPM passage's delivered WPM). Below: "Ratings from
   /rsvp-bench/NNNN-bench.json", one line per profile ("Trial B: 6 clean, 1 tolerable; residue no", or
   "not rated"), or "No benchmark results". **The default row is the current profile.**
2. **"Keep other trials?"**, subtitle "Chosen: <profile>". One row per other trial offered, starting
   as "Trial A: lock again" (default: keep none); choosing a row turns it into "Trial A: keep" and
   back. Then **Save the choice** and Back. Below: "Choose a trial to keep it, or to lock it again. A
   kept trial can be chosen later in Settings > Display > Speed read display. Keep none to show only
   Stock."
3. **Saving** (`applyChoice()`) writes the stored byte so that the chosen profile is chosen and only
   the chosen trial and the kept ones stay unlocked; every other trial is locked again. **Stock with
   nothing kept stores 0, the factory state.** It also stores `rsvpProfilesKept`, the trials kept
   selectable for reading (below), saves CrossPoint's settings file, and writes
   `/rsvp-bench/NNNN-choice.json` with the bytes before and after, what was kept and locked, what the
   screen offered and the ratings it was based on (`tools/device/schema/rsvp-bench.md`).
4. **"Profile saved"** (or "Could not save the settings"), "Saved as /rsvp-bench/NNNN-choice.json":
   "Speed reading uses: <profile>", "Kept: ...", "Locked again: ...", and whether Settings now shows
   the row. Row: Back to the device test menu. Leaving the device test selects the chosen profile in the
   shared backend; RSVP selects it when it opens (`RsvpActivity::onEnter`).

**Why a second stored value.** The benchmark unlocks every trial it runs (`0x0F` on DEPG0397), and
unlocked bits alone must not make trials selectable for reading (D3a, "What the benchmark stores").
The byte cannot tell "unlocked by a benchmark" from "kept by the owner" (its bit 7 is reserved and makes
the byte invalid), and a later benchmark run unlocks every trial again. So the choice also stores
`rsvpProfilesKept` (CrossPoint's settings file, persisted only, like the byte; never changed by the
benchmark). The Settings row offers only kept trials that are still unlocked.

| Line (INF) | When |
| --- | --- |
| `[DTS] choose open candidates=<keys> current=<key> setting=<byte> kept=<mask> bench=<file\|none>` | Choose a profile opened |
| `[DTS] choose chosen=<key> keepable=<n>`, `[DTS] choose keep <key>=<0\|1>` | A profile chosen; a keep row toggled |
| `[DTS] choice run=<n> file=<path> chosen=<key> kept=<keys\|none> locked=<keys\|none> setting_before=<b> setting_after=<b> kept_before=<m> kept_after=<m> settings_saved=<0\|1> row=<shown\|hidden>` | Saved |
| `[DTS] saved file=<path> bytes=<n> status=complete ok=<0\|1>` | The choice file was written |

## Settings: "Speed read display"

The plan's outcome: after the session a kept trial is a setting for ordinary reading, and otherwise
only Stock is visible. `SpeedReadSetting.h` adds one row to Settings > Display, after "Device test",
**only when at least one kept trial is still unlocked** (`speedReadRowVisible()`): never with the
factory byte 0, and not after a benchmark alone. Its value is the profile RSVP plays with; choosing the
row opens a list of Stock and the kept trials made for this panel (`speedReadOptions()`; the screen's
inversion is left out, since RSVP itself falls back to Stock while it lasts). Picking one changes only
the chosen profile in the byte, never the unlock bits, and saves the settings file
(`[DTS] speed read display chosen=<key> setting_before=<b> setting_after=<b> saved=<0|1>`). When the
stored choice is not allowed on this panel, the row shows Stock, and RSVP plays Stock
(`resolveProfile()`). The row's panel is the one the display backend resolves, read without creating
the backend. Settings rebuilds its rows when the device test closes, so the row appears or goes at
once.

## Simulator time scale

In the simulator only, `CROSSPOINT_SIM_BENCH_TIME_SCALE=<1..1000>` makes the benchmark's clock run
that many times faster, so the scenarios' six passages and endurance run take seconds. The file
records it (`simulator.time_scale`). Counts, order checks, statuses, the file and the stored byte are
real logic there; every time (the backend's modelled times, and delivered WPM and active time, which
also include the main loop's ~10 ms poll multiplied by the scale) is a modelled number, not a
prediction.

## Memory

One heap object for the screen's life (`activity_bytes` in the open line; 13,920 bytes in the
simulator, 12,752 before task D3b): the sweep's records (five profiles of ten steps, about 6.5 KB), the
Controls check's 64 events (about 2 KB), the scheduler with its 300-byte frame, the text of the screen
being drawn (2.5 KB, render task only), and since D3b the walk-through with its record (two runs'
figures, twelve answers, the header: about 0.6 KB) and the profile choice's record (about 0.5 KB).
Nothing is allocated per update: frames, text widths, pages and the JSON encoder use fixed storage (the
encoder streams through a 256-byte buffer). Each results write opens one file (the HAL's small file
handle) and runs per step or answer, never per update; the run number scan opens one handle per file
in `/rsvp-bench/` once per run.

Two short-lived allocations were added by D3b, both freed before the call returns: reading the
progress file when the screen opens takes a `ProgressDecoder` (about 1 KB; it keeps its own copy of the
record so a damaged file changes nothing; on the stack it would sit on the main task's stack beside the
line buffer), and "Choose a profile" reads the newest benchmark file into a filtered ArduinoJson
document (the profiles' keys and each step's name, rating, residue and delivered WPM: a few KB for five
profiles of ten steps, as `FontDownloadActivity` reads its manifest). If either allocation fails, the
walk-through starts as new (logged) or the choice shows "No benchmark results".

## What only the device session can confirm

- Every time: the medians, percentiles and delivered WPM of each profile on the unit's panel.
- Whether the "look, then press" pause leaves the last frame as the profile left it (no refresh runs
  until the key or touch; the SDK's own power-off on SE0400NQW47 does not redraw).
- How the full-screen series looks on a trial waveform (a whole page driven by a table made for a word
  band), and whether the full-screen time differs from the word band's at all (X4: no).
- Whether 15 s per rating is right for the owner.
- The guided acceptance's measured figures on the chosen profile, and whether the owner reads the last
  sentence at 600 WPM; every Part B step (the simulator proves the flow, the progress, the files and
  the stored byte, not the reader's behaviour, much of which C2 to C5 are still building).
- Whether the instructions are clear enough to follow alone, and the time the walk-through takes.

## For the session checklist

Screens in order, with the text they show (task D6 writes `docs/DEVICE_SESSION.md`):

| Screen | Title / text | Rows (default first) |
| --- | --- | --- |
| Settings > Display | "Device test" is the first row; "Speed read display" the second, only once a trial is kept | |
| Menu | "Device test", "Panel: <panel> (<controller>)" | Controls check · Display benchmark · Guided acceptance (": part A", ": step k of 12", ": done") · Choose a profile · Clearing refresh · Leave the device test |
| Controls check | "Controls check"; "Press each key and the power button once." "Hold each side key for a second. Tap and swipe." "To finish: tap Finish, or hold any key 3 seconds."; "Tried: ..." / "Not yet: ..."; numbered events, e.g. "2. key BTN_DOWN (5) -> Down PageForward NavNext, next, 906 ms, hold" | Button "Finish the controls check" |
| Room temperature | "Room temperature", "Room temperature: unknown" or "... 21 °C"; "Read a thermometer near the device, if there is one." | Continue · Warmer: +1 °C · Cooler: -1 °C · Temperature unknown · Back |
| Overview | "Display benchmark", panel, temperature, "Battery: n%", "To run: Stock, ...", "Not run: ..." with the reason, "Each profile takes about n minutes" | Start the benchmark · Back |
| Step | "<profile> · step k / 10", the step ("100 word updates, as fast as the panel allows", "10 full-screen updates", "5 clearing refreshes", "The passage at 120 WPM, 1 word per update", "Endurance: 300 word updates at 240 WPM"), "A key or a touch stops this step.", for rated steps "When the words stop, look, then press a key or touch." | none; a key or touch stops it |
| Rating | "How readable was it?", "<profile> · <step>", "delivered n WPM · 150 / 150 · order ok" | Repeat step · Clean · Tolerable · Unreadable · Unreadable, skip rest of profile |
| Residue (after endurance) | "Was residue visible after the run?" | Repeat step · No residue · Residue visible |
| Step stopped | "Step stopped" | Repeat step · Skip the rest of this profile · End the benchmark |
| Profile summary | "Display benchmark", "<profile> · done" or "· rest skipped", one line per step | Next profile: <name> · End the benchmark (or Finish the benchmark) |
| Run summary | "Benchmark finished" / "Benchmark ended", "Saved as /rsvp-bench/NNNN-bench.json" (or "Could not save the results file: see the serial log"), one line per profile | Back to the device test menu |

| Guided acceptance: start | "Guided acceptance", "Display profile: <name>", the introduction (Part A above) | Start part A · Skip part A · Back |
| Part A run | "Part A · run k / 2 · <profile>", "The passage at 240 WPM, 1 word per update" (then 600), "A key or a touch stops this step.", "When the words stop, look, then press a key or touch." | none; a key or touch stops it |
| Run stopped | "Run stopped", "Part A · run k / 2" | Repeat the run · Skip part A |
| Readable | "Was the last sentence readable?", "Part A · run k / 2 · <profile>", "Target 240 WPM · delivered n WPM", "Words: 150 / 150, each once, in order", the update figures | Repeat the run · Yes, it was readable · No |
| Slower (after the 600 WPM run) | "Was it slower than 600 WPM?", the same figures, "At 600 WPM each word would show for a tenth of a second. ..." | Repeat the run · Yes: slower, no word missing · No |
| Part A done | "Part A done" (or "Part A skipped"), one line per run, the slowdown answer, "Automatic check: Pass · your answers: Pass" | Continue to part B · Device test menu |
| Step | "Step k of 12", the step's title, its instructions, "Expected:", what should happen (the step table above) | Leave to do this step · Done: give the result · Device test menu |
| Step question | "Step k: how did it go?", the step's title, "Expected:" | Not done yet · Pass · Fail · Skip this step |
| Fail reason | "What went wrong?", "Step k · <title>" | Back · It did not appear · It appeared at the wrong place · Wrong mode after it · A grade was lost or wrong · Something else |
| Summary | "Guided acceptance done", "Saved as /rsvp-bench/NNNN-acceptance.json", "Criterion 1: Pass" ... "Criterion 5: Pass. Automatic check Pass, your answers Pass, pause screen Pass.", failed steps, "Criterion 6 (preparing books) is not checked on the device." | Back to the device test menu · Start a new walk-through |
| Choose a profile | "Choose a profile", "Now: <profile>", "Ratings from /rsvp-bench/NNNN-bench.json" (or "No benchmark results"), one line per profile | Stock · <each unlocked trial allowed here, e.g. "Trial B · 6 clean · 188 WPM"> · Back; the current profile is highlighted |
| Keep other trials? | "Keep other trials?", "Chosen: <profile>", "Choose a trial to keep it, or to lock it again. ..." | <each other trial>: lock again / keep · Save the choice · Back |
| Profile saved | "Profile saved", "Saved as /rsvp-bench/NNNN-choice.json", "Speed reading uses: <profile>", "Kept: ...", "Locked again: ...", whether Settings shows the row | Back to the device test menu |
| Settings > Display > Speed read display | A list: Stock and the kept trials | the profile in use |

Checklist notes: rate after looking at the frame left on screen; after "Unreadable, skip rest of
profile" the next screen is itself a clearing refresh; the menu's Clearing refresh row runs one more.
Guided acceptance: copy the step table above (the "Instructions" and "Expected" columns are what the
screen shows); the owner reads a step, chooses "Leave to do this step", does it, comes back through
Settings > Display > Device test > Guided acceptance and answers. "Not done yet" and Back never record
anything. Choose a profile: choose the profile to keep for reading; keep another trial only if it was
rated clean too; choose Stock and keep nothing to return to the factory state. Return every file in
`/rsvp-bench/` and the serial log.
