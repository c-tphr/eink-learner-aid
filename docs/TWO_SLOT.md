# The two-slot Picco build (`picco_2slot`)

Task D4 (#34). The owner's Picco moves once from Onyx's single-slot table to a two-slot table (`docs/PLAN.md` sections 1 and 3, "Two-slot layout"; reasoning and offsets: `docs/spikes/S3-two-slot.md`). This page says how the build for that layout is made, how its images reach the device, and when the firmware updates itself afterwards. Evidence from a real build: `docs/evidence/d4/`.

## The two Picco environments

| | `picco` (upstream, `platformio.ini`) | `picco_2slot` (this project, `platformio.project.ini`) |
| --- | --- | --- |
| Partition table | `partitions_picco.csv` (stock: factory `safeboot`, one `ota_0` app at 0xE0000) | `partitions_picco_2slot.csv`: `nvs` 0x9000/0xC000, `otadata` 0x15000/0x2000 (both as stock), `app0` 0x20000 and `app1` 0x800000 of 0x7E0000 each, `coredump` 0xFF0000 |
| Firmware | the same (`picco_2slot` extends `picco`; its `build_flags` are `picco`'s, which `scripts/picco_2slot_pre.py` checks on every build) | the same, with `CROSSPOINT_VERSION` = `<version>-picco2` (About screen, serial log) |
| `boot_app0.bin` | 0xE000 (framework default, inside the stock NVS) | 0x15000: `board_upload.arduino.boot_app0 = 0x15000`, the board key `upload.arduino.boot_app0` that arduino-esp32 3.3.11 `tools/pioarduino-build.py` reads for `FLASH_EXTRA_IMAGES` |
| `upload` target | exists and is unsafe (`docs/BASE.md`); never used | none (below) |
| Self-update | never: the slot policy says `NonOtaApp` | SD card and Wi-Fi, from either slot |

## Getting the images onto the device

Only `tools/device` writes to the Picco (`session.ps1 flash`, `tools/device/README.md`). Its input is the kit:

```sh
./scripts/cloud-setup.sh
pio run -e picco_2slot
python3 scripts/make_device_kit.py --out build/device-kit --rehearse
```

`scripts/make_device_kit.py` takes the image offsets from the build's own environment (`FLASH_EXTRA_IMAGES` of `pio run -e picco_2slot -t envdump`, and the `ota_0` row of the built `partitions.bin`), passes each one to `python -m piccoflash make-plan` as `--expect-offset`, so the guard refuses the kit if the build and the guard disagree, then verifies the kit with `python -m piccoflash plan`. It writes:

| Output | What |
| --- | --- |
| `build/device-kit/kit/` | `flash-plan.json`, `bootloader.bin` (0x0), `partitions.bin` (0x8000), `boot_app0.bin` (0x15000), `firmware.bin` (0x20000). Never `firmware.factory.bin` |
| `build/device-kit/update.bin` | the SD-card update file: `firmware.bin` under another name |
| `build/device-kit/kit-report.txt` | everything it printed |
| `build/device-kit/rehearsal/` | with `--rehearse` only: a work folder made against the in-memory FAKE device of `tools/device/tests`, used to run `flash --dry-run` on the real kit. Not for shipping |

### No upload target

`pio run -e picco_2slot -t upload` (and `uploadfs`, `uploadfsota`, `erase`, `erase_upload`, `download_fs`, `coredump`, `coredump-only`, and `pio debug`'s `__debug`) exits 1 with a message before the platform script runs: nothing is built, no serial port is opened and no tool is started (`scripts/picco_2slot_pre.py`). As a second line, `scripts/picco_2slot_post.py` replaces `UPLOADCMD` and `ERASECMD` with a refusing command and gives every device target a refusing pre-action. It leaves `UPLOADERFLAGS` as the platform built it, so `-t envdump` still shows the offsets a stock upload would have used. `scripts/app_only_upload.py` is not used: it does not keep the extra images out of the upload (`docs/BASE.md`).

### Checks in the build

`scripts/picco_2slot_post.py` fails the build unless `FLASH_EXTRA_IMAGES` is exactly 0x0 `bootloader.bin`, 0x8000 `partitions.bin`, 0x15000 `boot_app0.bin`, and the table is `partitions_picco_2slot.csv` with `ota_0` at 0x20000, size 0x7E0000. After `firmware.bin` is written it prints its size against the slot and the budget and fails the build if it is larger than the slot (0x7E0000 = 8,257,536 bytes). PlatformIO's own program-size check uses the same slot: the platform sets `upload.maximum_size` to the `ota_0` size at `checkprogsize` (pioarduino 55.03.311 `builder/main.py`, `_update_max_upload_size`), so the build log reads `Flash: ... from 8257536 bytes`.

### Size gate

The app image must not exceed **7,431,782 bytes** (the slot minus 10%, rounded down):

```sh
.github/scripts/firmware-size.sh picco_2slot "" --gate
```

prints the size, the budget, the headroom and the share used, then `size gate: PASS` or `size gate: FAIL` (exit 1). CI job `firmware-2slot` runs it on every pull request (`docs/CI.md`).

## Self-update rules

### When updating is offered and allowed

`lib/UpdatePolicy/SlotPolicy.h`, `decide()`, on the RUNNING partition table read through ESP-IDF (`src/update/UpdateGate.cpp`). Allowed only when all hold, checked in this order:

| Rule | Verdict when it fails |
| --- | --- |
| The whole table could be read | `TableIncomplete` |
| No factory or test app partition | `NonOtaApp` (the stock table's `safeboot`) |
| Exactly two `ota_N` app partitions (`OtaBootSwitch` assumes two) | `NotTwoSlots` |
| Exactly one otadata partition, at least 0x2000 bytes | `NoOtadata` |
| The running partition is one of the two slots | `NotRunningFromOta` |
| `esp_ota_get_next_update_partition(NULL)` is one of the two slots | `NoNextSlot` |
| ... and not the running slot | `NextIsRunning` |
| ... and at least as large as the running slot | `SlotTooSmall` |

Where the answer is used (feature builds; other devices keep upstream's `!BoardConfig::isPicco()`):

- Settings > General shows "Check for updates" and "SD Card Firmware Update" only when allowed.
- The boot recovery picker (hold the recovery key, Up on the Picco, while waking with the power button) opens only when allowed. Spike S3 suggested leaving it off on the Picco in v1; it is enabled on the two-slot table because it is the only way to install an SD update when the firmware boots but its menus do not work. It is entered only while the key is held at a power-button wake, and the power button's hold-to-sleep leaves it. Every power-button wake therefore reads the table once and logs the verdict, for example `[UPD] self-update allowed: Allowed (5 partitions, running 0x20000, next 0x800000)`; the serial log of the device session shows which layout is running.
- `OtaUpdater::checkForUpdate` reports no update when not allowed.
- **At the point of writing** both paths call `checkWrite()` on the partition they are about to erase: the SD path in `firmware_flash::flashFromSdPath` (before the first erase; refusal = `NO_PARTITION`, shown as "write failed") and the Wi-Fi path in `OtaUpdater::installUpdate` (before `esp_ota_begin`; refusal = `INTERNAL_UPDATE_ERROR`). A write is allowed only if `decide()` allows it, the target is the next update slot (`TargetNotNext`) and not the running slot (`TargetIsRunning`). The log line is `[UPD] refusing to write update slot 0x...: <verdict>`.

In the simulator nothing is offered or written (no partition table there); the OTA sources are not even compiled (`build_src_filter`). The scenario `tools/simtest/scenarios/update_entries_hidden_keys` checks that Settings > System lists neither entry.

### Wi-Fi updater: where it looks and what it installs

`lib/UpdatePolicy/ReleaseNaming.h`, used by `src/network/OtaUpdater.cpp` under the feature flag:

| Item | Value |
| --- | --- |
| Release source | `https://api.github.com/repos/c-tphr/eink-learner-aid/releases/latest` (GitHub skips drafts and pre-releases there) instead of upstream's `crosspoint-reader/crosspoint-reader` |
| Asset name | `eink-learner-aid-<tag_name>-picco2.bin`, for example tag `2.1.0` -> `eink-learner-aid-2.1.0-picco2.bin` |
| Asset content | the plain app image, `firmware.bin` of a `picco_2slot` build (the same file as `update.bin`); its board tag is `picco` |
| Tag limits | 1 to 19 characters of `A-Z a-z 0-9 . _ + -` (the updater keeps upstream's 48-byte name buffer: 17 + 19 + 11 + 1). Any other tag makes the updater look for an asset named `/`, which matches nothing, so it reports no update |
| Version rule | The tag and the running `CROSSPOINT_VERSION` must both start with `MAJOR.MINOR.PATCH` (optional leading `v`; 1 to 9 digits each; not followed by a digit or `.`). Install only if the tag's numbers are greater, compared as numbers (`2.0.10` > `2.0.9`), or equal while the running version contains `-rc`. If either does not parse, never install |
| Example | running `2.0.0-picco2`: tag `2.0.1` installs; `2.0.0`, `1.9.9`, `v2`, `latest` do not |

The release workflow (a later task) must therefore publish the `picco_2slot` `firmware.bin` as `eink-learner-aid-<tag>-picco2.bin` on a non-draft, non-pre-release release whose tag is `MAJOR.MINOR.PATCH`, and must build it with a `CROSSPOINT_VERSION` whose numbers equal the tag (for example `<tag>-picco2`), or the device would offer the same release again. With `[crosspoint] version = 2.0.0` today, the first release must be tagged above `2.0.0`.

Wrong-device protection is upstream's and unchanged: the image's chip id must match and its board tag must be `picco` (`src/network/FirmwareBoardTag.h`).

### The SD-card update file

`update.bin` from the kit (`firmware.bin` copied). The SD update lets the user pick any `*.bin` anywhere on the card (`src/activities/settings/SdFirmwareUpdateActivity.cpp`, `FileBrowserActivity` in `PickFirmware` mode), so the name only needs to end in `.bin`; the plan's convention is `update.bin` at the card root. Never `firmware.factory.bin`. The firmware checks it fully before the confirmation prompt and again before writing (`firmware_flash::validateImageFile`), then writes the other slot and switches otadata.
