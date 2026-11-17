# RSVP and checkpoints: where the code lives

The project's firmware code sits beside upstream CrossPoint, not inside it (`docs/PLAN.md` section 3, "Add, don't rewrite"). This page maps each directory to its plan component (section 4) and states the rules for the build flag and for edits to upstream files.

## Directories and libraries

| Location | Plan component | What it holds | Task that fills it |
| --- | --- | --- | --- |
| `lib/RsvpCore/` | RsvpCore | Word classes, focus letter, pacing, frames, scheduler, delivered-WPM meter. Portable, host-tested (`test/rsvp_core`) | R1, R2 (done) |
| `lib/CheckpointCore/` | CheckpointCore | Checkpoint file parser, trigger rules, checkpoint and reread state machine, grade rules, state file encoding. Portable, host-tested (`test/checkpoint_core`) | C1a, C1b (done) |
| `lib/WordIndex/` | WordIndex | Per-chapter word stream with anchors and non-prose markers; page-to-word mapping. Portable, host-tested (`test/word_index`) | R3 (done) |
| `lib/DisplayProfileCore/` | DisplayProfile (portable part) | Profile table and clearing limits, backend interface, statistics (with median and percentiles), the `rsvp::DisplaySink` adapter, the simulator timing model, the device backend's timing and timeout logic, the per-panel record, the trial profiles' tables, allow rule, unlock value and exposure limits, fakes. Portable, host-tested (`test/display_profile`) | E3 (done), D1 (done), D2 (done) |
| `src/reading/` | ReadingSession | Position, mode, navigation source, checkpoint firing, saved state. Since R4a also the chapter word index on demand and the page/word and sentence helpers; since C3a the checkpoint data loader (`CheckpointBook`: the manifest item, a cached copy of the file, the index) | R4a (#21, index service), R4b (done), C3a (#93, loader), C2 (#26), C4, C5 |
| `src/activities/rsvp/` | RsvpActivity | Word band, pause screen, RSVP settings, touch and keys | R4a (#21), R4b, R5 |
| `src/controls/` | ControlMap | The single binding table (key roles, timings, event-to-action bindings, touch zones) and the input adapter (`docs/spikes/S4-control-map.md`, section 7) | R4a (#21), C3a (#93, checkpoint screens), C4, D3a |
| `src/activities/checkpoint/` | CheckpointActivity | Prompt, reveal, grade, reread choice, skip; a temporary simulator-only entry (`CheckpointSimEntry`) until C3 wires the activity to C2's triggers | C3a (#93), C3 (#27), C4 |
| `src/display/` | DisplayProfile | Millisecond clock, simulator backend (modelled timing), device backend (the stock and trial profiles' refreshes, timed), a temporary simulator self-test | E3 (done), D1 (done), D2 (done) |
| `src/activities/devicetest/` | DeviceTestActivity | Benchmark sweep, ratings, guided acceptance, results files | D3a (#32), D3b |
| `lib/UpdatePolicy/` | (two-slot build) | Self-update slot policy and the Wi-Fi updater's release source, asset name and version rule. Portable, host-tested (`test/update_policy`) | D4 (done) |
| `src/update/` | (two-slot build) | Reads the running partition table through ESP-IDF and gates the update menus, the boot recovery picker and every update write; points the Wi-Fi updater at this repository | D4 (done) |
| `src/RsvpFeature.h` | (all) | The feature flag test, below | F4 |

Each `src/` directory has a README naming its component and task. Until that task lands it holds a one-symbol placeholder `.cpp`. `src/reading/RsvpLinkCheck.cpp` is a temporary start-up check that links every source file of RsvpCore and CheckpointCore, and DisplayProfile's profiles, adapter, statistics and device backend, into the firmware; ReadingSession replaces it.

The libraries in `lib/` stay free of Arduino, ESP-IDF, HAL and SDK code, so the host tests keep covering them. They contain no flag test: only `src/` code under the flag includes their headers, so only flagged builds use them.

## The feature flag

| Item | Where |
| --- | --- |
| `-DCROSSPOINT_RSVP=1` for `picco`, `picco-gh_release`, `picco-gh_release_rc` | `platformio.ini`, one line in each environment's `build_flags` |
| `-DCROSSPOINT_RSVP=1` for `simulator_picco` | `platformio.project.ini`, `[env:simulator_picco]` `build_flags` |
| `-DCROSSPOINT_RSVP=1` for `picco_2slot` | `platformio.project.ini`, `[env:picco_2slot]` `build_flags` (the same list as `[env:picco]`; the build fails if they drift) |
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
| `src/main.cpp` | `#include "display/DisplaySelfTest.h"` and the call `runDisplaySelfTestIfRequested(renderer);` in `loop()`, after `activityManager.loop()`: simulator only, and only with `CROSSPOINT_SIM_DISPLAY_SELFTEST` set | E3 (temporary; removed when the RSVP activity covers the timing model) |
| `test/CMakeLists.txt` | `add_subdirectory(display_profile)` | E3 |
| `test/CMakeLists.txt` | `add_subdirectory(rsvp_activity)` | R4a |
| `src/activities/reader/EpubReaderMenuActivity.h/.cpp` | `MenuAction::SPEED_READ`; "Speed read" as the menu's first row (so also the toolbar's More panel) in RSVP builds; one more row of capacity | R4a |
| `src/activities/reader/EpubReaderActivity.h/.cpp` | `openSpeedRead()` and its result handler; the `SPEED_READ` menu action and long-press branch call it | R4a |
| `src/CrossPointSettings.h`, `src/SettingsList.cpp` | `LONG_PRESS_BUTTON_BEHAVIOR::SPEED_READ = 3`; its label "Speed Read" offered in RSVP builds | R4a |
| `lib/hal/HalDisplay.h` | `setCustomLut(bool, const unsigned char*)`, an inline pass-through to the SDK's public `FreeInkDisplay::setCustomLUT()`; called only by the device backend of the trial profiles (RSVP builds); the simulator stub is `scripts/simulator_patches/0004-hal-display-set-custom-lut.patch` | D2 |
| `src/main.cpp` | `#include "controls/ControlDefaults.h"` and `seedRsvpControlDefaults();` before `SETTINGS.loadFromFile()`: the Speed read long press is the default in RSVP builds | R4a |
| `lib/I18n/translations/english.yaml` | The RSVP screens' strings | R4a |
| `src/main.cpp` | Boot recovery picker condition: `update_gate::updateMenusVisible(!BoardConfig::isPicco())` | D4 |
| `src/activities/settings/SettingsActivity.cpp` | "Check for updates" and "SD Card Firmware Update" shown when `update_gate::updateMenusVisible(!BoardConfig::isPicco())` | D4 |
| `src/network/FirmwareFlasher.cpp` | `update_gate::mayWriteUpdateSlot` before the SD update erases the next slot | D4 |
| `src/network/OtaUpdater.cpp` | Release URL, asset name and version rule through `update_gate`; `selfUpdateAllowed` before checking; `mayWriteUpdateSlot` before `esp_ota_begin` | D4 |
| `test/CMakeLists.txt` | `add_subdirectory(update_policy)` | D4 |

## Simulator

`simulator_picco` builds the firmware against the simulator library's HAL shims. A HAL method that this project adds, or calls and the pinned simulator lacks, needs a stub there. Stubs are patch files in `scripts/simulator_patches/`, applied by `scripts/patch_simulator.py` before every simulator build. `docs/BASE.md`, "Adding a simulator patch", gives the steps.

### Display timing model

The simulator redraws instantly, so DisplayProfile's simulator backend (`src/display/SimulatorDisplayBackend.h`) draws each word update and clearing refresh through the renderer and then sleeps for the rest of a modelled time, so playback in the simulator slows down as it will on the panel. The modelled times come from built-in defaults (540 and 1,960 ms for stock, plan section 9), then `docs/device/picco-timings.json` once the device session has written it (compiled in by the `simulator_picco` pre-script `scripts/gen_display_timings.py`; a malformed file stops the build), then `CROSSPOINT_SIM_UPDATE_MS` / `CROSSPOINT_SIM_CLEAR_MS` (`0`: no added delay). Only calls made through DisplayProfile are slowed; the rest of the simulator is unchanged. Details: `lib/DisplayProfileCore/README.md`, "Timing model".

## The two Picco builds and self-update

| Environment | Partition table | How it reaches the device | Updates from the reader |
| --- | --- | --- | --- |
| `picco` | stock `partitions_picco.csv` (factory `safeboot` + one `ota_0` slot) | Not flashed by this project (upstream's `upload` target is unsafe, `docs/BASE.md`) | Never: the slot policy answers `NonOtaApp`, so the menus and the recovery picker stay hidden and every write is refused |
| `picco_2slot` | `partitions_picco_2slot.csv` (two 0x7E0000 slots) | Once, by `tools/device` (`session.ps1 flash`) from the kit of `scripts/make_device_kit.py`; it has no `upload` target | SD card (`update.bin`, Settings > SD Card Firmware Update) and Wi-Fi (Settings > Check for updates), from either slot |

Self-update is allowed only when the running table has exactly two `ota_N` app slots, no factory or test app, one otadata of at least 0x2000 bytes, the firmware runs from one of the slots, and the next update slot is the other slot and at least as large (`lib/UpdatePolicy/SlotPolicy.h`). Both update paths check the target partition again right before erasing it. The Wi-Fi updater asks `https://api.github.com/repos/c-tphr/eink-learner-aid/releases/latest` and installs the asset `eink-learner-aid-<tag_name>-picco2.bin` only if the tag's `MAJOR.MINOR.PATCH` is greater than the running `CROSSPOINT_VERSION`'s (equal counts when the running build is an `-rc`); details and the release workflow's contract: `docs/TWO_SLOT.md`.
