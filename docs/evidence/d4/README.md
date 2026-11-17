# Evidence for task D4 (#34): the `picco_2slot` build

Text outputs from a real `pio run -e picco_2slot` in a Claude Code cloud session (PlatformIO 6.2.0, pioarduino 55.03.311, arduino-esp32 3.3.11, esptool 5.6.0 for `image-info`). No binary is committed; the kit is rebuilt by `scripts/make_device_kit.py` and, on every pull request, by CI job `firmware-2slot`. Paths are shortened to `<repo>` and `<core_dir>` (`~/.platformio`). Rules and names: `docs/TWO_SLOT.md`.

| File | What it shows |
| --- | --- |
| `envdump-excerpt.txt` | `pio run -e picco_2slot -t envdump`: `FLASH_EXTRA_IMAGES` and the esptool `UPLOADERFLAGS` the platform built put `boot_app0.bin` at **0x15000** (not 0xe000); `PARTITIONS_TABLE_CSV` is `partitions_picco_2slot.csv`; `UPLOADCMD` and `ERASECMD` are the refusal. `ESP32_APP_OFFSET` is `None` in a dump because the platform sets it later, at `checkprogsize` (as `docs/BASE.md` found for `picco`); the app offset 0x20000 is in `size-gate.txt` (`firmware.factory.bin` assembly) and `kit-report.txt` (ota_0 of the built table) |
| `upload-refusal.txt` | `pio run -e picco_2slot -t upload` and `-t erase` exit 1 with the refusal after about a second, before the platform script runs: nothing built, no port, no tool |
| `size-gate.txt` | Build log lines (PlatformIO's own check against the 8,257,536-byte slot, the merged-image offsets, the post-build slot check) and `.github/scripts/firmware-size.sh picco_2slot <log> --gate` ending in `size gate: PASS` |
| `partitions.txt` | The built `partitions.bin` decoded by the framework's `gen_esp32part.py`, its SHA-256 (equal to the one spike S3 measured and the guard requires), the framework `boot_app0.bin` SHA-256, and `check_2slot_table.py` on the committed table |
| `image-info-bootloader.txt`, `image-info-app.txt` | `esptool image-info`: DIO / 80m / 16MB headers, chip 9, valid checksums and hashes, ESP-IDF v5.5.5 bootloader and application descriptions |
| `kit-report.txt` | `scripts/make_device_kit.py --rehearse`: offsets from the envdump and the built table, `piccoflash make-plan` with all four `--expect-offset`, `piccoflash plan`, the SD `update.bin`, and `flash --dry-run` on the real kit against the fake device: `DRY RUN RESULT: READY` |
| `kit-files.txt` | SHA-256 and sizes of the kit files and `update.bin` |
| `negative-checks.txt` | The build fails when `boot_app0` would land at 0xe000, and when `picco_2slot`'s `build_flags` drift from `picco`'s |
| `x4pro-unchanged.txt` | `x4pro` (no feature flag) built with and without this task's edits to upstream files: same symbols, same `.flash.text` and `.flash.rodata`; the images differ only in the embedded ELF hash and the image checksum and hash |

The outputs are from commit 815ebe28 (this branch merged with `main` at 3c2837d3, which brought task E3); `x4pro-unchanged.txt` was measured before that merge, when E3 was not yet on either side of the comparison.

Note on `image-info-app.txt`: the ESP-IDF application description says `Project name: arduino-lib-builder` and `App version: ee57070`. Those come from the prebuilt Arduino libraries, not from this firmware; the firmware's own version, `2.0.0-picco2`, is the `CROSSPOINT_VERSION` string shown on the About screen and in the serial log. The guard only requires that the description exists.
