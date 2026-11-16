# simtest: scripted scenarios in the simulator

`tools/simtest` runs the real firmware in CrossPoint's SDL2 simulator
(`simulator_picco`, 480x800 portrait) without a screen, drives it with a
scenario file, and checks what it shows and what it writes to its simulated
SD card. Plan section 9 ("For agents: headless and scripted") describes its
role. Timing-sensitive logic (pacing, delivered WPM) is tested in host unit
tests, not here.

```sh
./scripts/cloud-setup.sh && pio run -e simulator_picco   # build the simulator once
python3 -m tools.simtest list                             # scenarios, and checks each file
python3 -m tools.simtest run open_book                    # one scenario
python3 -m tools.simtest run --all                        # all of them (what CI runs)
python3 -m pytest tools/simtest -q                        # the harness's own tests
```

Requirements: Python 3.11+, Pillow (`requirements.txt`), and `Xvfb` (the
`xvfb` package). Nothing else is read from your machine: see "Runs" below.

## Command line

`python3 -m tools.simtest run <scenario>... | --all` with:

| Option | Meaning |
| --- | --- |
| `--binary PATH` | Simulator program. Default `.pio/build/simulator_picco/program`. A binary built elsewhere (another CI job) works: the harness only needs the file. |
| `--report-dir DIR` | Report folder, emptied first. Default `build/simtest-report/` (git-ignored). A non-empty folder that is not a report is refused. |
| `--update-refs` | Write each compared screenshot as its new reference and print which ones are new or changed. |
| `--keep` | Keep each scenario's temporary SD folder; its path is printed. |
| `--jobs N` | Run N scenarios at once (default 1). Each run has its own Xvfb, SD folder and HTTP port, so this is safe. |
| `--repeat N` | Run each scenario N times and print the number of distinct screenshot hashes per shot (determinism check). |

The exit status is 0 only if every selected scenario passed (and, with
`--repeat`, every shot had a single hash). 2 means a usage or scenario-file
error.

## Writing a scenario

A scenario is `tools/simtest/scenarios/<name>/scenario.toml`; its reference
images are in `refs/` next to it. TOML was chosen because a reviewer can read
it like a script, it has comments, and Python reads it with the standard
library (`tomllib`). There is no Python in scenarios: every check is a
declarative step.

Worked example (a shortened `open_book`):

```toml
# Top-level keys only (no [table] headers needed).
description = "Open the fixture book and turn a page by key and by swipe"
timeout_s = 180          # optional: hard limit for the whole scenario
wait_timeout_ms = 15000  # optional: default timeout of each wait step

sd = [   # copied onto a fresh SD card before the first start
  { src = "fixtures/books/fixture.epub", dst = "books/fixture.epub" },  # src: repo-relative
  # { json = { ... }, dst = ".crosspoint/settings.json" },  # or a pre-seeded file
  # { text = "...", dst = "notes.txt" },
]
env = { slide_switch = "off" }  # optional; vars = { CROSSPOINT_SIM_FREE_HEAP = "200000" }

steps = [
  { wait_screen = "Browse Files" },          # sync: a screen showing this text
  { shot = "home" },                         # screenshot named "home"
  { press = "ENTER" },
  { wait_screen = "SD card | books" },
  { tap = [240, 138] },                      # touch the "books" row
  { wait_screen = "fixture | .epub" },
  { press = "ENTER" },
  { wait_log = "Progress saved: spine=0 offset=0 page=0" },   # sync: a log line (regex)
  { press = "DOWN" },
  { wait_log = "Progress saved: spine=1 offset=0 page=0" },
  { swipe = [400, 400, 80, 400] },           # swipe left = next page
  { wait_log = "Progress saved: spine=1 offset=\\d+ page=1" },
  { shot = "page_swipe" },
  { quit = true },                           # clean shutdown, exit status must be 0
  { expect_file = ".crosspoint/epub_*/progress.bin", hex_prefix = "0100 0100" },
  { expect_json = ".crosspoint/state.json", pointer = "/openEpubPath", equals = "/books/fixture.epub" },
  { compare = "home" },                      # against refs/home.png
  { compare = "page_swipe" },
]
```

Then `python3 -m tools.simtest run <name> --update-refs`, open the new PNGs in
`refs/` (and the report), check that each shows what its name says, and commit
them with the scenario.

Every step is a table with exactly one action key; the other keys are options.
`note = "..."` is allowed on every step and shows in the report. Mistakes
(unknown keys, a key the Picco does not have, a check in a place where it
would race the simulator, a compare of a shot never taken) are rejected
before anything runs, with the file and step number.

### Step reference

In-process steps become the simulator's `CROSSPOINT_SIM_INPUT_SCRIPT`:

| Step | Options | What it does |
| --- | --- | --- |
| `wait_log = "<regex>"` | `timeout_ms` | Wait until a firmware log line matches (Python and ECMAScript `std::regex` syntax; keep to the common subset). |
| `wait_screen = "<text>"` | `timeout_ms` | Wait until the text drawn on a screen contains this literal text. Entries are joined by `" \| "`, so `"SD card \| books"` matches two neighbouring strings. |
| `press = "<key>"` | `hold_ms` (80), `delay_ms` | Press a key: `ENTER` (alias `CONFIRM`, `RETURN`), `UP`, `DOWN`, `POWER` (alias `P`). The key goes down, stays down for `hold_ms`, then goes up (`<key>:<hold>` in the simulator script, timed on the same `SDL_GetTicks` clock as real key presses). A long press is a longer `hold_ms` (for example 700 for a 0.7 s hold); the firmware decides the threshold per action. Checked: POWER held 300 ms does not sleep, held 1500 ms sleeps with the firmware logging `Power button held 1237ms, sleeping`. |
| `tap = [x, y]` | `hold_ms` (80), `delay_ms` | Touch in screen pixels (portrait 480x800, as in the screenshots). The simulator's touch long press is 500 ms. A coordinate of exactly 1 is refused: the simulator reads 0..1 as a fraction of the screen, so 1 would mean the far edge. |
| `swipe = [x1, y1, x2, y2]` | `duration_ms` (250), `delay_ms` | Swipe. The reader's default page turn is a swipe (left = next); taps do not turn pages by default. |
| `switch = "on"\|"off"` | `delay_ms` | Move the slide switch. |
| `shot = "<name>"` | `delay_ms` | Screenshot (saved as `<name>.png`). Names: lower case, digits, `_`, `-`; unique in the scenario. |
| `sleep = true` | `delay_ms` | The simulator's SLEEP (holds power about 1.1 s; the firmware sleeps). Must be followed by `wait_log = "Entering deep sleep"` and then `wake`. |
| `wake = "<key>"` | `hold_ms`, `delay_ms` | Press a key while asleep. The simulator re-executes itself (same pid, same SD folder) and the steps after `wake` run in the new process. One sleep/wake per run. |

Read from the log at the position of the previous `wait_*` or `shot` (put one
directly before):

| Step | What it checks |
| --- | --- |
| `screen_has = "<text>"` | The screen text current at that point contains the text. |
| `screen_lacks = "<text>"` | ... does not contain it. |

Harness side, only where the simulator has no scripted input left in that run
(after the last in-process step, or after a kill/quit):

| Step | Options | What it checks |
| --- | --- | --- |
| `wait_file = "<sd glob>"` | `timeout_ms` | Polls until a file matching the glob exists. |
| `expect_file = "<sd glob>"` | `hex_prefix`, `contains` | At least one file matches; each match starts with these bytes / contains this text. |
| `expect_no_file = "<sd glob>"` | | Nothing matches. |
| `expect_json = "<sd glob>"` | `pointer` (RFC 6901, default whole document), `equals` | Exactly one JSON file matches and the value at the pointer equals (JSON equality: `1 == 1.0`, `true != 1`). |
| `expect_schema = "<sd glob>"` | `schema` (repo-relative path) | The JSON file matches the JSON Schema. The validator (`jsonchecks.py`) supports the common draft 2020-12 keywords and refuses any other keyword, so nothing is silently ignored. |

SD globs are relative to the SD root: `*` stays in one folder, `**` spans
folders. The firmware keeps its cache in `/.crosspoint/epub_<hash>/`, where
the hash is the C++ `std::hash` of the book path, so use `epub_*`.

Process control and images:

| Step | Options | What it does |
| --- | --- | --- |
| `kill = true` | | SIGKILL to the simulator's process group, like pulling the battery. No shutdown code runs. The SD folder stays. |
| `quit = true` | | SIGTERM; SDL turns it into a window-close, the simulator's normal shutdown. Must exit with status 0 within 15 s. |
| `start = true` | `slide_switch` | Start the simulator again on the same SD folder (after kill or quit). |
| `compare = "<shot>"` | `mask`, `max_diff_pixels`, `same_as` | After the scenario, compare a shot with `refs/<shot>.png`, exactly by default. `mask = [[x, y, w, h], ...]` ignores rectangles; `max_diff_pixels` allows a few pixels; `same_as = "<other shot>"` compares with another shot of the same run instead of a stored image (for "after restart equals before kill"). |

If a scenario ends with the simulator still running, the harness quits it
cleanly (SIGTERM) and requires exit status 0.

## How synchronisation works, and its limits

The simulator has no control channel: no stdin commands, no socket. Its
input script is an environment variable read once at start. It does offer
`WAIT:<regex>`, which pauses the script until a firmware log line matches;
times after a WAIT count from the moment it matched. The harness builds on
that:

1. **One script per process.** The steps of each run (from a start to the
   next kill/quit) are compiled into one `CROSSPOINT_SIM_INPUT_SCRIPT`.
   Every `wait_log` / `wait_screen` becomes a WAIT, so the script follows the
   firmware instead of a clock. Inputs between waits are spaced by their
   hold time plus 120 ms, so each lands in its own frame and a later event
   is never sorted before an earlier one (the simulator orders events in a
   segment by time).
2. **The harness follows the log live.** It reads the simulator's output as
   it comes. Each WAIT prints `[SIM] WAIT matched: <line>` and each shot
   `[SIM] Saved screenshot: <path>`; the harness ticks the steps off in
   order and enforces each step's own `timeout_ms` (the simulator's own WAIT
   timeout is set a few seconds longer as a backstop). On a timeout, a crash
   or a simulator error it fails the step with the last 40 log lines and
   kills the process group.
3. **Screen text.** The harness always sets `CROSSPOINT_SIM_SCREEN_TEXT=1`.
   After each display refresh whose text changed, the simulator logs one line,
   `[SIM] SCREEN: 100% | SD card | books | /`: every string the firmware drew
   since the last clear, in reading order (by y, then x), one entry per
   drawText call, so a book page lists one entry per word. It is what was
   drawn, not OCR; icons and images are not in it. `wait_screen` waits on
   these lines.
4. **Segments for everything else.** Kill, quit, restart, and file checks
   happen between processes or at the end of a run, when the script has
   nothing left to do. A file check in the middle of a run is rejected: the
   script would keep going while the harness looks at files.

Limits to know:

- **WAIT looks back to the last input, not to the previous WAIT.** Two waits
  with no input between them can both match lines logged before the first
  one. The harness has the whole log and fails such a step ("matched a line
  logged before the line the previous wait matched"). Wait for a line that
  only the new state logs.
- **No conditional input.** The script cannot react to what the harness
  sees; only to log lines.
- **A shot is taken at the next frame after it is due.** It shows the frame
  the firmware last pushed. Take shots after a wait for the line that ends
  the drawing (`Progress saved` for reader pages, the screen text for menus),
  not after "Entering activity", which comes before the first draw. Some
  screens draw in phases (Home redraws once the book card's thumbnail is
  resolved, ending with `No known cover image for thumbnail` for the fixture);
  wait for the last phase.
- **A redraw with the same text prints no screen line.** Moving a highlight
  in a menu redraws the screen, but the strings are the same, so the
  simulator prints no new `[SIM] SCREEN:` line and `wait_screen` cannot see
  it. Every full redraw logs `[GFX] Time = N ms from clearScreen to
  displayBuffer` after the frame is pushed; wait for that after the input
  (`wait_log = "from clearScreen to displayBuffer"`), or for a log line of
  your own.
- **No screenshots in deep sleep.** The sleep loop does not present frames.
- **Firmware log level.** Waits rely on DBG lines (`LOG_LEVEL=2` in
  `[env:simulator]`). If a log line you wait on changes upstream, the step
  fails by timeout with the log tail, never silently.

## Runs: isolation and cleanup

Each scenario run gets a new temporary SD folder (`CROSSPOINT_SIM_SD`), a
temporary `HOME` and working directory, a private `Xvfb` (started with
`-displayfd`, so parallel runs never pick the same display), and a free pair
of TCP ports for `CROSSPOINT_SIM_HTTP_PORT`. The simulator gets a clean
environment: a fixed `PATH`, `TZ=UTC`, `LANG=C.UTF-8`, and only the
`CROSSPOINT_SIM_*` variables the harness sets (plus any listed in the
scenario's `env.vars`). A developer's `./fs_`, home directory, proxies and
display are never seen. Every process runs in its own process group; a
failure, timeout, the scenario's `timeout_s` or Ctrl-C kills the group.

## Screenshots and references

The simulator writes 32-bit BMPs; the harness converts them to PNG in the
report and stores references in the smallest lossless form (1-bit when the
screen is pure black and white, 8-bit gray otherwise). Comparison is on
pixels, exact by default.

On a mismatch the step fails with the number of differing pixels and their
bounding box, for example
`shot 'key_ch1_p2' differs from tools/simtest/scenarios/open_book/refs/key_ch1_p2.png: 1600 pixel(s) differ in bounding box x=100..139, y=300..339 (40x40)`,
and the report's `diff/` folder holds `<shot>.actual.png`,
`<shot>.reference.png` and `<shot>.diff.png` (reference | actual | changed
pixels in red, masks in blue, bounding box in orange).

To accept a change: `run <name> --update-refs`, which lists the references
that are new or changed (with their old bounding box), then review them with
`git diff --stat` and by opening the images, and commit them in the same pull
request as the code that changed them.

### Determinism, and what could vary

Measured on the pinned simulator: 20 runs of `open_book` gave one hash per
shot, and so did 60 runs of all three scenarios with `--jobs 4` (four
simulators at once on four cores). Check again with `run <name> --repeat 20`.

| Source | Status |
| --- | --- |
| Battery | Fixed: the simulator always reports 100% (`HalPowerManager::getBatteryPercentage`). |
| Clock | Not fixed: the simulated RTC reads host time (`HalClock.cpp`, `std::time`), and there is no variable to pin it. None of the screens in these scenarios shows the time or date. A screen that does (a status-bar clock setting, a dated file list) needs a `mask` over it, or a simulator patch adding a fixed clock (not added; see the pull request for the proposal). The harness sets `TZ=UTC`. |
| Random choices | Sleep-screen images use an LCG whose state is in `/.crosspoint/state.json`, so a fresh SD card is reproducible. |
| Window scale | Under Xvfb there is no HiDPI scaling: screenshots are exactly 480x800. |
| Phased drawing | See the limits above: shoot after the last phase. |
| Host speed | Inputs are synchronised on log lines, not on time, so a slow CI machine changes timings in the log, not the screens. |

## The report

`build/simtest-report/` (or `--report-dir`):

- `index.html`: a table of scenarios, then per scenario each step with its
  result, time and detail, any diff images, the screenshots, the runs (pid,
  how each ended, exit status), the combined simulator and harness log
  (open by default when the scenario failed), and the SD card after the run
  with the contents of small text/JSON files (binary files of up to 256 bytes,
  such as `progress.bin`, as hex).
- `results.json`: the same, for scripts (`scenarios[].steps[]`, `shots` with
  pixel hashes, `runs`, `sd_listing`).
- `junit.xml`: one test case per scenario for CI.
- `<scenario>/log.txt`, `<scenario>/shots/*.png`, `<scenario>/diff/*.png`.

Harness lines in the log start with `[simtest]`, for example
`[simtest] run 1: sending SIGKILL to process group 1234 (imitates pulling the battery)`.

## Conventions for later tasks

- One flow per scenario, named `<feature>_<flow>_<input>`, for example
  `rsvp_play_touch` and `rsvp_play_keys`, `checkpoint_grade_touch`. A flow that
  must be proven by touch and by keys has two scenarios. Per
  `docs/spikes/S4-control-map.md` section 9, the `_keys` scenario uses only
  `UP` and `DOWN` (short press moves, `hold_ms = 900` activates), the `_touch`
  scenario only `tap` and `swipe`, and both wait on the `[CTL]` focus/select
  log lines the control code prints.
- Shot names say what the screen shows (`checkpoint_prompt2_incorrect`, not
  `shot3`), so the lead can review references by name.
- Synchronise on log lines your code prints (add a DBG line when the UI has
  no other observable event) and on screen text; avoid `delay_ms`.
- Kill-and-restart flows: wait for the log line that follows the state write,
  check the file before and after the kill, then `start` and compare with
  `same_as` the shot taken before the kill.
- Keep references small and reviewed: commit them only after looking at
  them; never with `--update-refs` alone in CI.
- Picco keys are ENTER, UP, DOWN and POWER, plus the slide switch. BACK,
  LEFT, RIGHT, HOME, PREV and NEXT do not exist on this profile; the
  simulator drops them silently (`HalGPIO.cpp` `buttonAvailable`), so the
  harness refuses them rather than let a scenario pass by pressing nothing.
- The firmware reopens the last book at boot only when the device went to
  sleep from the reader (`lastSleepFromReader` in `/.crosspoint/state.json`,
  `src/main.cpp` around line 802). After a kill or quit it boots to Home with
  a continue-reading card; press ENTER to reopen.

## Files

| File | Role |
| --- | --- |
| `__main__.py` | Command line |
| `scenario.py` | Parse, validate and compile scenario files |
| `process.py` | Xvfb, the simulator process, its log, process-group kill |
| `runner.py` | Run one scenario: SD setup, runs, checks, screenshots |
| `images.py` | BMP to PNG, comparison, masks, diff images |
| `jsonchecks.py` | JSON pointer, JSON Schema subset |
| `report.py` | `index.html`, `results.json`, `junit.xml` |
| `tests/` | Self-tests; `stub_sim.py` is a stand-in simulator |
