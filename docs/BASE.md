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

Upstream's own `docs/` files are merged into `docs/` unchanged; no file names collided.

## Re-verifying the pin

Run from the repo root.

```sh
# 1. The pinned commit is in our history
git merge-base --is-ancestor 490813ff4541e74e654f54f66ece9efc908361a0 HEAD && echo pinned

# 2. The SDK submodule is at the SHA recorded above
git submodule status

# 3. Outside docs/ and README.md, nothing differs from the pinned commit (prints nothing)
git diff --stat 490813ff4541e74e654f54f66ece9efc908361a0 HEAD -- . ':!docs' ':!README.md'

# 4. The moved README is identical to upstream's (prints nothing; the two blob ids match)
git diff --stat 490813ff4541e74e654f54f66ece9efc908361a0:README.md HEAD:docs/upstream/README.md
git rev-parse 490813ff4541e74e654f54f66ece9efc908361a0:README.md HEAD:docs/upstream/README.md

# 5. Inside docs/, only the files listed above differ (upstream docs unchanged)
git diff --stat --diff-filter=MD 490813ff4541e74e654f54f66ece9efc908361a0 HEAD -- docs

# 6. Build the unchanged firmware
pip install platformio
git submodule update --init --recursive
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

This finding comes from reading the code, not from running it. The firmware build in the import session stopped while downloading the Arduino core (see the pull request), so the build environment could not be dumped. Line numbers for the pioarduino platform refer to the installed 55.03.311 release. Those for `pioarduino-build.py` refer to tag `3.3.11` of `espressif/arduino-esp32`. Once the toolchain installs, `pio run -e picco -t envdump` confirms it without a device: it prints the final `UPLOADERFLAGS` and exits without uploading (PlatformIO Core `builder/main.py`, line 214). The project's own flashing scripts (plan section 10, task D5) must not rely on `scripts/app_only_upload.py`.
