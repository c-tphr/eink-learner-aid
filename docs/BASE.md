# Firmware base

This repo is a fork of CrossPoint, pinned to one upstream commit. Per `docs/PLAN.md` section 3 ("Pin, don't track"), nothing is synced from upstream during v1 unless a dedicated issue says so.

## Pinned upstream

| Item | Value |
| --- | --- |
| Upstream repo | <https://github.com/crosspoint-reader/crosspoint-reader> |
| Branch | `develop` |
| Pinned commit | `490813ff4541e74e654f54f66ece9efc908361a0` |
| Commit date | 2026-11-16 07:17:29 UTC (author and committer date) |
| Commit subject | `test: give each ParserStopTest instance its own temp file (#4496)` |
| Firmware version string | `2.0.0` (`[crosspoint] version` in `platformio.ini`) |
| History | Merged with full history (1,601 upstream commits) by `git merge --allow-unrelated-histories`; one merge commit, no squash, no rebase |
| Licence | MIT, `Copyright (c) 2025 CrossPoint Reader organization` (`LICENSE`). Third-party notices are in `licenses/` |

## FreeInk SDK (git submodule)

| Item | Value |
| --- | --- |
| Path | `freeink-sdk` |
| Repo | <https://github.com/Free-Ink/freeink-sdk.git> (from `.gitmodules`, unchanged from upstream) |
| Pinned commit | `3f3e4c1126e2cb9a278c2c1076fb4a3334ffdc30` (the gitlink recorded in the pinned upstream commit) |
| Commit date and subject | 2026-11-13 20:16:53 UTC, `Replace barycentric touch calibration with linear` |
| Licence | MIT, `Copyright (c) 2026 FreeInk` |
| Nested submodule | `freeink-sdk/libs/assets/Icons/lucide` at `c81680e066f45b640743ca78ae36cdedda3f0318` (<https://github.com/lucide-icons/lucide>), pinned by the SDK |

## Build platform

| Item | Value |
| --- | --- |
| PlatformIO platform | pioarduino `platform-espressif32` **55.03.311**: `https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip` (`platformio.ini`, line 10) |
| Arduino core it pulls | arduino-esp32 3.3.11 (`esp32-core-3.3.11.tar.xz` and `esp32-core-3.3.11-libs.tar.xz`, from the platform's `platform.json`) |
| Host test framework | googletest `v1.17.0`, fetched by CMake `FetchContent` (`test/CMakeLists.txt`) |

## What differs from the pinned commit

Every upstream file is byte-identical to `490813ff`, except the ones listed here. This list must stay accurate: update it in any pull request that changes an upstream file.

| Path | Change | Reason |
| --- | --- | --- |
| `README.md` | Replaced by this project's README | Project README stays at the root |
| `docs/upstream/README.md` | Added; byte-identical to upstream `README.md` (blob `5479b689b38bcb9c7e34d6a384ac75402c1e4318`) | Upstream README moved here |
| `docs/BASE.md` | Added | This file |
| `docs/IMPLEMENTER_GUIDE.md`, `docs/PLAN.md`, `docs/PRD.md`, `docs/STATUS.md` | Added | Project documents, present before the import |
| `platformio.ini` | **Modified, one line** (line 4): `extra_configs = platformio.local.ini` became `extra_configs = platformio.project.ini, platformio.local.ini` | Loads this project's PlatformIO environments from a file of its own. `platformio.local.ini` is still read last, so personal overrides still win (task F2) |
| `platformio.project.ini` | Added | Project-owned environments: `[env:simulator]` and `[env:simulator_picco]` (see "Simulator" below) (task F2); `[env:picco_2slot]`, the two-slot Picco build (task D4, `docs/TWO_SLOT.md`); its release version through `scripts/release_version.py`, development string `-dev-picco2` (task A0, `docs/RELEASING.md`) |
| `scripts/cloud-setup.sh`, `scripts/pio_prefetch.py` | Added | Cloud session setup and the PlatformIO pre-fetch it runs (task F2); `picco_2slot` added to the pre-fetched environments (task D4) |
| `scripts/net-smoke.sh`, `scripts/net_smoke.py` | Added | Network smoke test (task F2) |
| `scripts/patch_simulator.py`, `scripts/simulator_patches/` | Added | Patches applied to the fetched simulator library (see "Simulator" below) (task F2) |
| `docs/net-smoke-report.md`, `docs/evidence/f2/` | Added | Smoke test report and evidence (task F2) |
| `test/CMakeLists.txt` | **Modified, seven lines added**: `add_subdirectory(rsvp_core)` (task R1), `add_subdirectory(checkpoint_core)` (task C1a), `add_subdirectory(word_index)` (task R3), `add_subdirectory(display_profile)` (task E3), `add_subdirectory(rsvp_activity)` (task R4a), `add_subdirectory(checkpoint_activity)` (task C3a) and `add_subdirectory(update_policy)` (task D4), after `add_subdirectory(protected_paths)` | Registers the project's host-test directories with upstream's test build |
| `platformio.ini` | **Modified, three lines added**: `-DCROSSPOINT_RSVP=1` at the end of `build_flags` in `[env:picco]`, `[env:picco-gh_release]` and `[env:picco-gh_release_rc]` | The RSVP feature flag, on for the Picco targets only (task F4). `simulator_picco` sets it in `platformio.project.ini`. See `docs/ARCHITECTURE-RSVP.md` |
| `src/main.cpp` | **Modified, two lines added**: `#include "reading/RsvpLinkCheck.h"` and `runRsvpLinkCheck();` in `setup()`, after the "Starting CrossPoint version" log line | Temporary start-up check that links RsvpCore and CheckpointCore; an empty inline function without the flag (task F4). Task C2 replaces it with ReadingSession |
| `src/main.cpp` | **Modified, two more lines added**: `#include "display/DisplaySelfTest.h"` (after `components/UITheme.h`) and `runDisplaySelfTestIfRequested(renderer);` in `loop()`, right after `activityManager.loop();` | Temporary simulator-only display self-test (task E3, `src/display/DisplaySelfTest.h`): runs only when `CROSSPOINT_SIM_DISPLAY_SELFTEST` is set; an empty inline function on the device and on every build without the RSVP flag. Removed when the RSVP activity exercises DisplayProfile |
| `src/main.cpp` | **Modified, two more lines added** (TEMPORARY, task C3a): `#include "activities/checkpoint/CheckpointSimEntry.h"` (after `activities/ActivityManager.h`) and `runCheckpointSimEntryIfRequested(renderer, mappedInputManager);` in `loop()`, right after `runDisplaySelfTestIfRequested(renderer);` | Simulator-only entry to the checkpoint screens (`src/activities/checkpoint/CheckpointSimEntry.h`): runs only when `CROSSPOINT_SIM_CHECKPOINT` is set; an empty inline function on the device and on every build without the RSVP flag. Task C3 (#27) removes both lines when it wires the screens to C2's triggers |
| `src/main.cpp` | **Modified, two more lines added**: `#include "controls/ControlDefaults.h"` (after `components/UITheme.h`) and `seedRsvpControlDefaults();` in `setup()`, right before `SETTINGS.loadFromFile()` | Seeds "Page turn long press: Speed read" as the default in RSVP builds, the way the lines above it seed the toolbar menu style; a saved choice still wins. An empty inline function without the flag (task R4a, `src/controls/ControlDefaults.h`) |
| `src/CrossPointSettings.h` | **Modified, one line added**: `SPEED_READ = 3` appended to `LONG_PRESS_BUTTON_BEHAVIOR` | The "Speed read" long-press option (task R4a; `docs/spikes/S4-control-map.md` section 5.1). Other builds never offer or store it: their label list has three entries, so a stored 3 loads as the default |
| `src/CrossPointSettings.h` | **Modified** (task R4b): `#include "RsvpFeature.h"`; after `longPressButtonBehavior`, two fields `rsvpWpmTens = 24` and `rsvpWordsPerUpdate = 1` inside `#if CROSSPOINT_RSVP_ENABLED` | The RSVP global defaults (WPM in tens, words per update; `src/reading/RsvpValues.h`). Builds without the flag have neither field |
| `src/SettingsList.cpp` | **Modified**: `#include "RsvpFeature.h"`; the "Page turn long press" labels become a vector that gains "Speed Read" when `RSVP_FEATURE_ENABLED` | Offers the option in RSVP builds only (task R4a) |
| `src/SettingsList.cpp` | **Modified** (task R4b): after "Page turn long press", inside `#if CROSSPOINT_RSVP_ENABLED`, two `SettingInfo::Value` entries `rsvpWpmTens` (6 to 100) and `rsvpWordsPerUpdate` (1 to 4) with no category and `withWebHidden()` | Persists the RSVP global defaults that "Save as default" sets, through upstream's settings file; not shown on the device's Settings screens or the web page, and absent (neither shown nor stored) without the flag |
| `src/activities/reader/EpubReaderMenuActivity.h` | **Modified**: `#include "RsvpFeature.h"`, `MenuAction::SPEED_READ` appended, `MAX_MENU_ITEMS` 20 in RSVP builds (19 otherwise) | The reader menu's "Speed read" row (task R4a) |
| `src/activities/reader/EpubReaderMenuActivity.cpp` | **Modified, one line added**: the "Speed read" row first in `buildMenuItems()` when `RSVP_FEATURE_ENABLED` | The touch path into RSVP: the first row of the list menu and of the toolbar's More panel (task R4a; S4 row 1) |
| `src/activities/reader/EpubReaderActivity.h` | **Modified, two lines added**: the declaration of `openSpeedRead()` | Task R4a |
| `src/activities/reader/EpubReaderActivity.h` | **Modified** (task R4b): `openSpeedRead(bool resume = false)` and its comment line; a `bool resumeSpeedRead` member | Reopening a book that was left in RSVP |
| `src/activities/reader/EpubReaderActivity.cpp` | **Modified**: `#include "activities/rsvp/RsvpLaunch.h"`; `openSpeedRead()` (pushes RSVP at the page on screen; its result handler jumps to the page of the RSVP position, opens the menu, or saves that page and goes Home); a `SPEED_READ` case in `onReaderMenuConfirm()`; a long-press branch next to the chapter-skip one | Entering and leaving RSVP (task R4a; S1 section 9, S4 rows 1, 12-14). Without the flag `openSpeedRead()` returns at once and `rsvpentry::create()` is an empty inline function |
| `src/activities/reader/EpubReaderActivity.cpp` | **Modified** (task R4b): `openSpeedRead()` passes an `rsvpentry::Entry` (adds the next page's start offset and `resume`) and sets `HandOff::saveReaderProgress`, a lambda holding the page lookup and `EpubReaderUtils::saveProgress` call that the Exit branch had inline (Exit now calls it); one line at the end of `loadBook()` (`resumeSpeedRead = rsvpentry::openedInRsvp(*epub)`); a comment and five lines in `loop()` after `rememberBookOnceRendered()` that open RSVP once the first page is shown when `resumeSpeedRead` is set | The saved RSVP position: the entry rule, the reader's progress on the Home and sleep routes, reopening a book in RSVP (`src/activities/rsvp/README.md`). Without the flag `openedInRsvp()` returns false, so the new lines do nothing |
| `lib/I18n/translations/english.yaml` | **Modified**: `STR_LONG_PRESS_BEHAVIOR_SPEED_READ`, `STR_SPEED_READ` and the `STR_RSVP_*` strings added; the `STR_CP_*` strings appended at the end after a blank line (task C3a; the file's parser, `scripts/gen_i18n.py`, accepts no comment lines) | The RSVP and checkpoint screens' text, through upstream's translation workflow (task R4a; R4b added `STR_RSVP_SAVE_DEFAULT`, `STR_RSVP_SAVE_BOOK`, `STR_RSVP_SAVED_DEFAULT`, `STR_RSVP_SAVED_BOOK`, `STR_RSVP_SAVE_FAILED`; C3a the 16 `STR_CP_*` strings). Other languages fall back to English |
| `scripts/display_timings.py`, `scripts/gen_display_timings.py` | Added | The display timings file's validator and header generator, and the `simulator_picco` pre-script that runs it (task E3; `lib/DisplayProfileCore/README.md`, "Timing model") |
| `src/main.cpp` | **Modified** (task D4): `#include "update/UpdateGate.h"`; in the `recoveryFirmwareMode` condition `!BoardConfig::isPicco()` became `update_gate::updateMenusVisible(!BoardConfig::isPicco())` in the same place; one comment line | Boot recovery SD picker on a two-slot Picco only. Without the flag the function returns its argument, so other devices are unchanged (the `x4pro` image is identical apart from the embedded ELF hash and the image checksum and hash, task D4's pull request) |
| `src/activities/settings/SettingsActivity.cpp` | **Modified** (task D4): `#include "update/UpdateGate.h"`; `if (!BoardConfig::isPicco())` before "Check for updates" and "SD Card Firmware Update" became `if (update_gate::updateMenusVisible(!BoardConfig::isPicco()))`; the comment above it extended by one sentence | Feature builds show the two update entries only when the running partition table allows self-update (`lib/UpdatePolicy`); upstream behaviour without the flag |
| `src/network/FirmwareFlasher.cpp` | **Modified** (task D4): `#include "update/UpdateGate.h"`; in `flashFromSdPath`, after the next-update partition is found, `if (!update_gate::mayWriteUpdateSlot(dest->address, sdPath)) return Result::NO_PARTITION;` (3 lines) | Running-slot guard for the SD path at the point of writing (spike S3: it had none). Always true without the flag |
| `src/network/OtaUpdater.cpp` | **Modified** (task D4): `#include "update/UpdateGate.h"`; `checkForUpdate` returns `NO_UPDATE` unless `update_gate::selfUpdateAllowed()`; the release URL goes through `update_gate::releaseUrl(...)`; the asset name through `update_gate::formatAssetName(...)` with upstream's `snprintf` as the fallback; `isUpdateNewer` returns `update_gate::isNewerRelease(...)` when `update_gate::PROJECT_RELEASES`; `installUpdate` calls `update_gate::mayWriteUpdateSlot(...)` before `esp_ota_begin` | Wi-Fi updater: this repository's releases and asset name, a version rule that never compares unparsed numbers, and the running-slot guard (task D4, `docs/TWO_SLOT.md`). Without the flag every call returns upstream's value |
| `.github/workflows/release-kit.yml`, `.github/scripts/release_preflight.py`, `scripts/release_version.py`, `scripts/make_release_kit.py`, `scripts/release_naming_cli.cpp`, `scripts/tests/`, `docs/RELEASING.md` | Added | The manual release workflow, the release version, the release kit builder and its tests (task A0) |
| `partitions_picco_2slot.csv`, `scripts/picco_2slot_pre.py`, `scripts/picco_2slot_post.py`, `scripts/make_device_kit.py`, `lib/UpdatePolicy/`, `src/update/`, `test/update_policy/`, `docs/TWO_SLOT.md`, `docs/evidence/d4/` | Added | The two-slot build, its upload refusal and checks, the device-kit builder, the update policy and its firmware glue (task D4) |
| `lib/hal/HalDisplay.h` | **Modified, three lines added** (task D2): `setCustomLut(bool enabled, const unsigned char* lut = nullptr)`, an inline pass-through to the SDK's public `FreeInkDisplay::setCustomLUT()` (`freeink-sdk/libs/display/FreeInkDisplay/include/FreeInkDisplay.h:309`), and its two-line comment | The trial display profiles load their waveform table for one refresh and release it (`docs/spikes/S2-waveforms.md` section 3; `src/display/README.md`, "Trial profiles"). Only `src/display/DeviceDisplayBackend.cpp` calls it, which is compiled with the RSVP flag only; on other builds the inline method is never instantiated (no symbol in `x4pro`, task D2's pull request) |
| `scripts/simulator_patches/0004-hal-display-set-custom-lut.patch` | Added | The simulator stub for `HalDisplay::setCustomLut()` (task D2; "Simulator" below) |
| `src/RsvpFeature.h`, `src/reading/`, `src/activities/rsvp/`, `src/activities/checkpoint/`, `src/display/`, `src/activities/devicetest/`, `docs/ARCHITECTURE-RSVP.md` | Added | Feature flag header and the module skeletons, compiled only with the flag (task F4) |
| `.gitignore` | Two negation lines after `.claude/*` so `.claude/commands/` and `.claude/agents/` are tracked | The `/prep-book` command and the checker subagent live there (P4) |
| `AGENTS.md` (and `CLAUDE.md`, a link to it) | **Modified**: a boxed preamble added directly under the title; the rest of the file is unchanged | States that this fork's workflow is `docs/PLAN.md` section 11 and `docs/IMPLEMENTER_GUIDE.md`, that upstream's rules on pull requests, pushes and human handoff apply to upstream contributions only, and that the technical guidance still applies (owner's decision, 2026-11-19) |
| `.github/workflows/project-ci.yml`, `.github/scripts/check-format.sh`, `sim-smoke.sh`, `firmware-size.sh`, `no_skips.py`, `device_session_fixture.py`, `docs/CI.md` | Added | This project's CI (task F3); `docs/CI.md` describes it |
| `.github/workflows/ci.yml` ("CI (build)") | **Modified**: every job (`clang-format`, `cppcheck`, `build`, `unit-tests`, `test-status`) gets `if: github.repository == 'crosspoint-reader/crosspoint-reader'` (`test-status`: `always() &&` that) and a one-line comment | On every pull request it built eight devices, including the `custom_sdkconfig` ones, ran clang-format over every upstream file and duplicated the host tests; `project-ci.yml` covers this project's checks, with one other device (`x4pro`) as the compile guard (task F3) |
| `.github/workflows/pr-formatting-check.yml` ("PR Formatting") | **Modified**: `if:` as above on its one job | Requires Conventional Commits titles (`feat: ...`); this project's titles are `<task id>: <title>` (`docs/PLAN.md` section 11), so it failed on every pull request. It runs on `pull_request_target`, which uses the base branch's copy, so it stops only once this is on `main` (task F3) |
| `.github/workflows/pr-firmware-links.yml` | **Modified**: `&& github.repository == ...` added to its job's `if:` | Comments on pull requests (needs `pull-requests: write`) with links to "CI (build)" artifacts and upstream's flash tool; nothing to link here, and no writes from CI in this project (task F3) |
| `.github/workflows/release.yml` | **Modified**: `if:` as above on both jobs | Builds and attaches upstream's eight-device release assets on a published release, with upstream's `FREEINK_LCP_RELEASE_KEY` secret and `contents: write`; this project's release workflow is task A0 (task F3) |
| `.github/workflows/release-fonts.yml` | **Modified**: `if:` as above on its job | Manual only, but needs the `FONTS_REPO_TOKEN` secret and publishes to `crosspoint-reader/crosspoint-fonts`; cannot work here (task F3) |
| `.github/FUNDING.yml` | **Deleted** | It pointed GitHub's "Sponsor" button at upstream's funding page. This repository takes no funding and points donations nowhere (task H1) |
| `.github/ISSUE_TEMPLATE/config.yml` | **Modified**: upstream's contact links (its discussions, `SCOPE.md`, `ROADMAP.md`) removed; `blank_issues_enabled` became `true` | Neutral for this repository, whose scope is `docs/PRD.md` and `docs/PLAN.md`. The issue forms themselves (`bug_report.yml`, `feature_request.yml`) are unchanged (task H1) |
| `.github/PULL_REQUEST_TEMPLATE.md` | **Replaced** | Upstream's scope checklist and AI-usage question replaced by this repository's structure (`docs/IMPLEMENTER_GUIDE.md`): "Closes #n", what changed, upstream files touched, acceptance commands with output, evidence, what could not be done (task H1) |

Upstream's own `docs/` files are merged into `docs/` unchanged; no file names collided. Files that later tasks add in their own directories (for example `tools/prep/`) are new files, not changes to upstream files; `git diff --diff-filter=MD` (step 3 below) lists only upstream files that were modified or deleted.

## Simulator

| Item | Value |
| --- | --- |
| Repo | <https://github.com/crosspoint-reader/crosspoint-simulator> (default branch `main`) |
| Pinned commit | `84c3421da6f707d6ebe919b3fafca7387ff06464`, 2026-11-10, `feat: add Metalio E-Ink 4 Plus simulator profile (#63)`: the head of `main` on 2026-11-16, and the newest commit there is |
| Where | `platformio.project.ini`, `[simulator_pin] commit`, used by `[env:simulator]` `lib_deps` |
| Environments | `[env:simulator]` and `[env:simulator_picco]` from the repo's `sample-platformio-linux-wsl.ini` at that commit; the other device profiles are left out; the library's pre-script path uses `${platformio.libdeps_dir}` |
| Licence | MIT (`LICENSE` in that repo) |
| Status at our firmware pin | Builds and runs **with this project's patches** (`scripts/simulator_patches/`); without them it does not compile |
| Evidence | `docs/evidence/f2/simulator_picco-home.png`: Home screen, headless under `xvfb-run`, scripted QUIT |

### Why the simulator is patched

The simulator replaces the firmware's `lib/hal` (and the SDK's `SecureNet`) with its own shims, so every HAL method the firmware gains needs a stub in the simulator. At the pin, the shims lag upstream firmware changes made after 2026-11-10, and `pio run -e simulator_picco` fails without patches:

| Firmware call site | Missing in the simulator | Upstream change that added it |
| --- | --- | --- |
| `src/MappedInputManager.cpp:23` | `BoardConfig::BoardProfile::inputStyle`, `BoardConfig::InputStyle` (simulator `src/BoardConfig.h`) | `867a1210`, 2026-11-15, #4407 |
| `src/activities/reader/ReaderUtils.h:246-247` | `HalDisplay::setReaderRefresh(bool)` | `5058eb43`, 2026-11-13, #4362 |
| `src/main.cpp:150` | `HalDisplay::controllerIdle()` | `5058eb43` |
| `lib/GfxRenderer/GfxRenderer.cpp:2451` | `HalDisplay::displayRetainedGrayscaleOverlay(...)` | `5058eb43` |
| `src/network/HttpDownloader.cpp:155` | `freeink::SecureHttpClient::getCombinedHeader(const char*)` (simulator `src/SecureHttpClient.h`) | SDK `SecureNet` at the pinned SDK commit |

No older simulator commit builds either; each is older still. `scripts/simulator_patches/0001-compat-firmware-490813ff.patch` adds the missing members (25 lines in three headers, additive, no-op or pass-through); its header says which firmware change each hunk answers.

`scripts/simulator_patches/0002-mutex-zero-timeout-take.patch` (task R4a) fixes a fidelity gap rather than a missing member: the simulator's `xSemaphoreTake()` ignored the timeout on a mutex and always blocked, so `RenderLock(RenderLock::Mode::Try)` in the main loop waited for every render. With the display timing model a render can hold the lock for a 540 ms word update, which stopped the simulated main loop (keys, touch, scripted screenshots) for that long. A zero timeout now polls with `try_lock()`, as FreeRTOS does on the device.

`scripts/simulator_patches/0003-qrcode-c17.patch` (task H1) makes the simulator build with GCC 15 (Ubuntu 26.04, GitHub's `ubuntu-latest`). The firmware's QR code library, `ricmoo/QRCode` 0.0.1, does `typedef unsigned char bool;` in `src/qrcode.h` when compiled as C, and GCC 15 compiles C as C23 by default, where `bool` is a keyword: `qrcode.h:37:23: error: 'bool' cannot be defined via 'typedef'`. QRCode is a registry dependency in `[env:simulator]` `lib_deps`, not part of the simulator library, so `qrcode.h` cannot be patched here: `patch_simulator.py` patches only the simulator's own git checkout, pinned by commit, and QRCode is an unpacked registry archive. The patch instead changes the simulator's build script `run_simulator.py`, which already adjusts other libraries' builders (its GfxRenderer screen-text hook): QRCode's builder alone gets `-std=gnu17` in its `CFLAGS`. Every other file keeps the compiler's default dialect. With GCC 13 (Ubuntu 24.04), whose default is already `gnu17`, `qrcode.o` is byte-identical with and without the flag. The device build is unaffected (its own toolchain, and the simulator library is not in it).

`scripts/simulator_patches/0004-hal-display-set-custom-lut.patch` (task D2) adds a no-op `HalDisplay::setCustomLut(bool, const unsigned char* = nullptr)` next to 0001's `setReaderRefresh()` stub, mirroring the pass-through D2 added to `lib/hal/HalDisplay.h`. The simulator backend never calls it (it models refresh time, not waveforms); the stub keeps the two HAL surfaces the same.

### How the patches are applied

Following upstream's convention for fetched libraries (`scripts/patch_sdfat.py` with `scripts/sdfat_patches/`, `scripts/patch_jpegdec.py` with `scripts/jpegdec_patches/`), `scripts/patch_simulator.py` is the first `pre:` script of `[env:simulator]` (and so of `[env:simulator_picco]`). Before anything compiles it:

1. finds the simulator library under `<libdeps_dir>/<env>/simulator`, the copy for the environment being built, and fails unless it is a git checkout whose `HEAD` is `[simulator_pin] commit`;
2. builds the expected bytes of every file the patches touch in a temporary directory: the pinned commit's version (`git show HEAD:<path>`), then each patch in file-name order with `git apply`. A patch that does not apply fails the build here, before any file in the library changes;
3. compares the library's files with those states. Equal to "pinned commit plus the first *k* patches" means those *k* are already applied and the rest are pending. Equal to no state (a hand edit) fails the build, as does any other modified file in the checkout;
4. writes only the files whose bytes differ from the fully patched state, so an incremental build with nothing pending changes no file and recompiles nothing.

`scripts/cloud-setup.sh` pre-fetches the simulator unpatched; the first `pio run -e simulator_picco` patches it. Deleting `.pio/libdeps/<env>/simulator` and building again restores a clean, patched copy.

### Adding a simulator patch

Later tasks that add a method to the firmware's HAL (or call one the simulator lacks) add a stub the same way:

1. Build once so `.pio/libdeps/simulator_picco/simulator` holds the pinned simulator with the existing patches applied. Then stage that state in the checkout's index (its `HEAD` stays at the pin): `cd .pio/libdeps/simulator_picco/simulator && git add -u`.
2. Edit that checkout: add the declaration to the matching `src/Hal*.h` (and a definition in `src/Hal*.cpp` if it is not inline), mirroring the firmware's signature. Keep it additive and minimal: a no-op, or a sensible default for a non-void method (see the simulator's `FORKING.md`).
3. `git diff` now shows only the new change (working tree against the staged state). Save it as the next number, for example `git diff > ../../../../scripts/simulator_patches/0002-<what>.patch`, add a header (text before the first `diff --git` line) naming the firmware change it answers, then `git reset -q` to unstage.
4. Delete `.pio/libdeps/simulator_picco/simulator` and run `pio run -e simulator_picco` twice: the first build must print `Applied simulator patch:` for every patch and succeed; the second must compile nothing.
5. Moving `[simulator_pin] commit` means re-checking every patch against the new commit: drop the ones upstream now covers, regenerate the rest.

## Re-verifying the pin

Run from the repo root.

```sh
# 1. The pinned commit is in our history
git merge-base --is-ancestor 490813ff4541e74e654f54f66ece9efc908361a0 HEAD && echo pinned

# 2. The SDK submodule is at the SHA recorded above
git submodule status

# 3. Outside docs/ and README.md, the only upstream files modified are platformio.ini (four lines),
#    test/CMakeLists.txt (six lines), src/main.cpp (six lines from tasks F4, E3 and R4a, plus the D4
#    edit), src/activities/settings/SettingsActivity.cpp, src/network/FirmwareFlasher.cpp,
#    src/network/OtaUpdater.cpp (registration points, task D4), lib/hal/HalDisplay.h (task D2), the RSVP registration points of task
#    R4a (src/CrossPointSettings.h, src/SettingsList.cpp, the reader and its menu,
#    lib/I18n/translations/english.yaml), .gitignore (two lines), AGENTS.md (a preamble under the
#    title), five .github/workflows files (job conditions), and .github/FUNDING.yml (deleted),
#    .github/ISSUE_TEMPLATE/config.yml and .github/PULL_REQUEST_TEMPLATE.md (task H1), all in the
#    table above; everything else listed is an added file from the table above
git diff --stat --diff-filter=MD 490813ff4541e74e654f54f66ece9efc908361a0 HEAD -- . ':!docs' ':!README.md'
git diff --stat 490813ff4541e74e654f54f66ece9efc908361a0 HEAD -- . ':!docs' ':!README.md'

# 4. The moved README is identical to upstream's (prints nothing; the two blob ids match)
git diff --stat 490813ff4541e74e654f54f66ece9efc908361a0:README.md HEAD:docs/upstream/README.md
git rev-parse 490813ff4541e74e654f54f66ece9efc908361a0:README.md HEAD:docs/upstream/README.md

# 5. Inside docs/, only the files listed above differ (upstream docs unchanged)
git diff --stat --diff-filter=MD 490813ff4541e74e654f54f66ece9efc908361a0 HEAD -- docs

# 6. Build the unchanged firmware (cloud-setup.sh installs PlatformIO 6.2.0, the submodules
#    and every package; see docs/net-smoke-report.md for why `pio run` alone fails in cloud sessions)
./scripts/cloud-setup.sh
pio run -e picco

# 7. Host tests
cmake -S test -B build/test && cmake --build build/test -j4 && ctest --test-dir build/test --output-on-failure -j4
```

## Picco upload target at this commit

**Do not run `pio run -e picco -t upload`.** The project's own two-slot environment, `picco_2slot`, has no upload target at all: `pio run -e picco_2slot -t upload` stops before anything runs (`docs/TWO_SLOT.md`). Reading the code at this commit, it still writes the Arduino bootloader at 0x0, a partition table at 0x8000 and `boot_app0` at 0xE000 (inside the stock NVS partition), as `docs/PLAN.md` section 2 ("Upload hazard") says. Upstream's comment claiming an app-only upload is not borne out by the build scripts.

### What the configuration intends

- `platformio.ini`, lines 692-701: the comment says the Picco targets the stock partition table and that `pio run -t upload` "writes only the app into the stock `app` slot at 0xE0000, so the vendor bootloader, NVS and the safeboot recovery partition survive."
- `platformio.ini`, lines 702-710 (`[env:picco]`): `board = esp32-s3-devkitc1-n16r8`, `board_build.partitions = partitions_picco.csv`, and `extra_scripts = ${base.extra_scripts}` plus `post:scripts/app_only_upload.py`. No `upload_protocol`, `upload_flags` or `upload_command` is set, so the default `esptool` protocol applies.
- `scripts/app_only_upload.py`, line 7: the whole mechanism is `env.Replace(FLASH_EXTRA_IMAGES=[])`.
- `partitions_picco.csv`, lines 5-10: `nvs` 0x9000 (size 0xC000, so it ends at 0x15000), `otadata` 0x15000, `safeboot` (factory) 0x20000, `app` (ota_0) 0xE0000, `spiffs` 0xF50000, `coredump` 0xFD0000.
- `platformio.ini`, line 36 (`[base]`): `board_upload.offset_address = 0x10000`. pioarduino 55.03.311 does not read this key for esptool uploads, so it does not set the app offset.

### Why it does not work

1. PlatformIO Core runs the platform build script first and the `post:` extra scripts afterwards (`platformio/builder/main.py` in PlatformIO Core 6.2.0, line 173 `env.SConscript("$BUILD_SCRIPT")`, then line 180 for the `post` scripts).
2. Inside the platform script (pioarduino 55.03.311 `builder/main.py`), `env.BuildProgram()` at line 1824 runs the Arduino framework script. arduino-esp32 3.3.11 `tools/pioarduino-build.py`, lines 216-235, appends `FLASH_EXTRA_IMAGES`. For the ESP32-S3 these are: `0x0000` bootloader, `0x8000` `partitions.bin`, and `0xe000` `boot_app0.bin`. This board sets none of the `upload.arduino.*` overrides.
3. Still in the platform script, lines 1907-1933 build the esptool command. Lines 1932-1933 copy each `FLASH_EXTRA_IMAGES` entry into `UPLOADERFLAGS` immediately (`env.Append(UPLOADERFLAGS=[image[0], env.subst(image[1])])`). `UPLOADCMD` is `$UPLOADER $UPLOADERFLAGS $ESP32_APP_OFFSET $SOURCE` (line 1930).
4. `scripts/app_only_upload.py` runs after that, as a `post:` script. Emptying `FLASH_EXTRA_IMAGES` at that point does not remove the entries already copied into `UPLOADERFLAGS`. It only affects later readers of the variable, such as the merged `firmware.factory.bin` (platform `builder/main.py`, line 1787).
5. The app offset comes from the partition table: platform `builder/main.py`, lines 360-365, takes the `ota_0` partition's offset as `ESP32_APP_OFFSET`. The `checkprogsize` step calls this before upload (platform lines 1837 and 1851-1857; PlatformIO Core `builder/main.py`, line 188, makes `upload` depend on `checkprogsize`). For `partitions_picco.csv` it gives 0xE0000.

### Offsets the upload target would write

| Offset | Image | Effect on a stock Picco |
| --- | --- | --- |
| 0x0 | Arduino `bootloader.bin` | Replaces Onyx's bootloader |
| 0x8000 | `partitions.bin` built from `partitions_picco.csv` | Rewrites the partition table |
| 0xE000 | `boot_app0.bin` (8 KB, 0xE000-0xFFFF) | Overwrites part of the stock `nvs` partition (0x9000-0x15000) |
| 0xE0000 | `firmware.bin` | The intended app write, into the `app` (ota_0) slot |

### How sure this is

Confirmed by running the build, without a device (task F2, 2026-11-17, PlatformIO 6.2.0, pioarduino 55.03.311, arduino-esp32 3.3.11). `pio run -e picco -t envdump` prints the construction environment after every build script, including `scripts/app_only_upload.py`, has run, and exits before any upload (PlatformIO Core `builder/main.py`, lines 214-216). It showed:

```text
'FLASH_EXTRA_IMAGES': [],
'UPLOADCMD': '$UPLOADER $UPLOADERFLAGS $ESP32_APP_OFFSET $SOURCE',
'UPLOADER': '<core_dir>/penv/bin/esptool',
'UPLOADERFLAGS': [ '--chip', 'esp32s3', '--port', '"$UPLOAD_PORT"', '--baud', '$UPLOAD_SPEED',
                   '--before', 'default-reset', '--after', 'hard-reset', 'write-flash', '-z',
                   '--flash-mode', '${__get_board_flash_mode(__env__)}',
                   '--flash-freq', '${__get_board_f_image(__env__)}', '--flash-size', 'detect',
                   '0x0000', '<project>/.pio/build/picco/bootloader.bin',
                   '0x8000', '<project>/.pio/build/picco/partitions.bin',
                   '0xe000', '<core_dir>/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'],
'UPLOAD_PROTOCOL': 'esptool',
```

So `FLASH_EXTRA_IMAGES` is empty, as `app_only_upload.py` intends, but the three images had already been copied into `UPLOADERFLAGS`, and the upload command still writes them at 0x0, 0x8000 and 0xE000. `ESP32_APP_OFFSET` is `None` in the dump because the platform sets it later, in the pre-action of `checkprogsize`, which `upload` depends on. The same build computed it as 0xE0000: `firmware.factory.bin` is assembled with `0xe0000 | firmware.bin` from that value (build log, "Creating binary"). The table above therefore stands.

A further hazard the build shows: `.pio/build/picco/firmware.factory.bin` (6,374,848 bytes) is `firmware.bin` placed at 0xE0000 with 0xE0000 bytes of `0xFF` in front of it. Written at offset 0x0, as a "factory" image normally is, it would erase the bootloader, the partition table, `nvs`, `otadata` and `safeboot`. Only `firmware.bin`, written at 0xE0000, is safe.

Line numbers for the pioarduino platform refer to the installed 55.03.311 release. Those for `pioarduino-build.py` refer to tag `3.3.11` of `espressif/arduino-esp32`. The project's own flashing scripts (plan section 10, task D5) must not rely on `scripts/app_only_upload.py`, and must not flash `firmware.factory.bin`.
