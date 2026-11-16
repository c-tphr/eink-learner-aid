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
| `platformio.project.ini` | Added | Project-owned environments: `[env:simulator]` and `[env:simulator_picco]` (see "Simulator" below) (task F2) |
| `scripts/cloud-setup.sh`, `scripts/pio_prefetch.py` | Added | Cloud session setup and the PlatformIO pre-fetch it runs (task F2) |
| `scripts/net-smoke.sh`, `scripts/net_smoke.py` | Added | Network smoke test (task F2) |
| `scripts/patch_simulator.py`, `scripts/simulator_patches/` | Added | Patches applied to the fetched simulator library (see "Simulator" below) (task F2) |
| `docs/net-smoke-report.md`, `docs/evidence/f2/` | Added | Smoke test report and evidence (task F2) |
| `test/CMakeLists.txt` | **Modified, three lines added**: `add_subdirectory(rsvp_core)` (task R1), `add_subdirectory(checkpoint_core)` (task C1a) and `add_subdirectory(word_index)` (task R3), after `add_subdirectory(protected_paths)` | Registers the project's host-test directories with upstream's test build |
| `platformio.ini` | **Modified, three lines added**: `-DCROSSPOINT_RSVP=1` at the end of `build_flags` in `[env:picco]`, `[env:picco-gh_release]` and `[env:picco-gh_release_rc]` | The RSVP feature flag, on for the Picco targets only (task F4). `simulator_picco` sets it in `platformio.project.ini`. See `docs/ARCHITECTURE-RSVP.md` |
| `src/main.cpp` | **Modified, two lines added**: `#include "reading/RsvpLinkCheck.h"` and `runRsvpLinkCheck();` in `setup()`, after the "Starting CrossPoint version" log line | Temporary start-up check that links RsvpCore and CheckpointCore; an empty inline function without the flag (task F4). Task C2 replaces it with ReadingSession |
| `src/RsvpFeature.h`, `src/reading/`, `src/activities/rsvp/`, `src/activities/checkpoint/`, `src/display/`, `src/activities/devicetest/`, `docs/ARCHITECTURE-RSVP.md` | Added | Feature flag header and the module skeletons, compiled only with the flag (task F4) |
| `.gitignore` | Two negation lines after `.claude/*` so `.claude/commands/` and `.claude/agents/` are tracked | The `/prep-book` command and the checker subagent live there (P4) |
| `.github/workflows/project-ci.yml`, `.github/scripts/check-format.sh`, `sim-smoke.sh`, `firmware-size.sh`, `no_skips.py`, `device_session_fixture.py`, `docs/CI.md` | Added | This project's CI (task F3); `docs/CI.md` describes it |
| `.github/workflows/ci.yml` ("CI (build)") | **Modified**: every job (`clang-format`, `cppcheck`, `build`, `unit-tests`, `test-status`) gets `if: github.repository == 'crosspoint-reader/crosspoint-reader'` (`test-status`: `always() &&` that) and a one-line comment | On every pull request it built eight devices, including the `custom_sdkconfig` ones, ran clang-format over every upstream file and duplicated the host tests; `project-ci.yml` covers this project's checks, with one other device (`x4pro`) as the compile guard (task F3) |
| `.github/workflows/pr-formatting-check.yml` ("PR Formatting") | **Modified**: `if:` as above on its one job | Requires Conventional Commits titles (`feat: ...`); this project's titles are `<task id>: <title>` (`docs/PLAN.md` section 11), so it failed on every pull request. It runs on `pull_request_target`, which uses the base branch's copy, so it stops only once this is on `main` (task F3) |
| `.github/workflows/pr-firmware-links.yml` | **Modified**: `&& github.repository == ...` added to its job's `if:` | Comments on pull requests (needs `pull-requests: write`) with links to "CI (build)" artifacts and upstream's flash tool; nothing to link here, and no writes from CI in this project (task F3) |
| `.github/workflows/release.yml` | **Modified**: `if:` as above on both jobs | Builds and attaches upstream's eight-device release assets on a published release, with upstream's `FREEINK_LCP_RELEASE_KEY` secret and `contents: write`; this project's release workflow is task A0 (task F3) |
| `.github/workflows/release-fonts.yml` | **Modified**: `if:` as above on its job | Manual only, but needs the `FONTS_REPO_TOKEN` secret and publishes to `crosspoint-reader/crosspoint-fonts`; cannot work here (task F3) |

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
#    test/CMakeLists.txt (three lines), src/main.cpp (two lines), .gitignore (two lines) and five
#    .github/workflows files (job conditions), all in the table above;
#    everything else listed is an added file from the table above
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

**Do not run `pio run -e picco -t upload`.** Reading the code at this commit, it still writes the Arduino bootloader at 0x0, a partition table at 0x8000 and `boot_app0` at 0xE000 (inside the stock NVS partition), as `docs/PLAN.md` section 2 ("Upload hazard") says. Upstream's comment claiming an app-only upload is not borne out by the build scripts.

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
