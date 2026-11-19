# RSVP and checkpoints: where the code lives

The project's firmware code sits beside upstream CrossPoint, not inside it (`docs/PLAN.md` section 3, "Add, don't rewrite"). This page maps each directory to its plan component (section 4) and states the rules for the build flag and for edits to upstream files.

## Directories and libraries

| Location | Plan component | What it holds | Task that fills it |
| --- | --- | --- | --- |
| `lib/RsvpCore/` | RsvpCore | Word classes, focus letter, pacing, frames, scheduler, delivered-WPM meter. Portable, host-tested (`test/rsvp_core`) | R1, R2 (done) |
| `lib/CheckpointCore/` | CheckpointCore | Checkpoint file parser, trigger rules, checkpoint and reread state machine, grade rules, state file encoding. Portable, host-tested (`test/checkpoint_core`) | C1a, C1b (done) |
| `lib/WordIndex/` | WordIndex | Per-chapter word stream with anchors and non-prose markers; page-to-word mapping. Portable, host-tested (`test/word_index`) | R3 (done) |
| `src/reading/` | ReadingSession | Position, mode, navigation source, checkpoint firing, saved state | C2 (#26), C4, C5 |
| `src/activities/rsvp/` | RsvpActivity | Word band, pause screen, RSVP settings, touch and keys | R4a (#21), R4b, R5 |
| `src/activities/checkpoint/` | CheckpointActivity | Prompt, reveal, grade, reread choice, skip | C3 (#27), C4 |
| `src/display/` | DisplayProfile | Refresh strategies, per-update timing, clearing policy | E3 (#8), D1, D2 |
| `src/activities/devicetest/` | DeviceTestActivity | Benchmark sweep, ratings, guided acceptance, results files | D3a (#32), D3b |
| `src/RsvpFeature.h` | (all) | The feature flag test, below | F4 |

Each `src/` directory has a README naming its component and task. Until that task lands it holds a one-symbol placeholder `.cpp`. `src/reading/RsvpLinkCheck.cpp` is a temporary start-up check that links every source file of RsvpCore and CheckpointCore into the firmware; ReadingSession replaces it.

The libraries in `lib/` stay free of Arduino, ESP-IDF, HAL and SDK code, so the host tests keep covering them. They contain no flag test: only `src/` code under the flag includes their headers, so only flagged builds use them.

## The feature flag

| Item | Where |
| --- | --- |
| `-DCROSSPOINT_RSVP=1` for `picco`, `picco-gh_release`, `picco-gh_release_rc` | `platformio.ini`, one line in each environment's `build_flags` |
| `-DCROSSPOINT_RSVP=1` for `simulator_picco` | `platformio.project.ini`, `[env:simulator_picco]` `build_flags` |
| The one place code tests it | `src/RsvpFeature.h`: `CROSSPOINT_RSVP_ENABLED` (always defined, 0 or 1) and `RSVP_FEATURE_ENABLED` (`constexpr bool`) |

No other environment defines the flag, so every other CrossPoint device builds as before. Rules:

- Include `RsvpFeature.h` and test `#if CROSSPOINT_RSVP_ENABLED` (or `if constexpr (RSVP_FEATURE_ENABLED)`); never test `CROSSPOINT_RSVP` itself.
- Every `.cpp` under the directories above wraps its whole body, includes of library headers too, in `#if CROSSPOINT_RSVP_ENABLED`. Without the flag it compiles to an empty object.
- A header that an upstream file includes (a registration point) gives a do-nothing inline fallback when the flag is off, as `src/reading/RsvpLinkCheck.h` does. Then the upstream file needs no `#if` of its own.

PlatformIO's default dependency finder (`lib_ldf_mode = chain`) does not evaluate `#if`, so on the other ESP32 devices it still compiles RsvpCore and CheckpointCore and hands their objects to the linker. Nothing references them there, so `--gc-sections` drops all their code and data: the `x4pro` image is the same as before the flag existed, apart from the hash of the ELF file that the image embeds (task F4's pull request shows the comparison). The libraries must therefore keep compiling for every target CrossPoint builds.

## Edits to upstream files

New code goes into the directories above. An upstream file may change only at a registration point: activity routing, a reader menu entry, a setting, a build environment, or a start-up call (`docs/PLAN.md` section 3, rule 2). Each such edit:

- is as small as possible, and guarded by the flag through a header from this project, as above;
- is listed in `docs/BASE.md`, "What differs from the pinned commit", with the reason and the task, in the same pull request.

Registration points so far:

| Upstream file | Edit | Task |
| --- | --- | --- |
| `platformio.ini` | `-DCROSSPOINT_RSVP=1` in `[env:picco]`, `[env:picco-gh_release]`, `[env:picco-gh_release_rc]` | F4 |
| `src/main.cpp` | `#include "reading/RsvpLinkCheck.h"` and the call `runRsvpLinkCheck();` in `setup()` | F4 (temporary; replaced by C2) |

## Simulator

`simulator_picco` builds the firmware against the simulator library's HAL shims. A HAL method that this project adds, or calls and the pinned simulator lacks, needs a stub there. Stubs are patch files in `scripts/simulator_patches/`, applied by `scripts/patch_simulator.py` before every simulator build. `docs/BASE.md`, "Adding a simulator patch", gives the steps.
