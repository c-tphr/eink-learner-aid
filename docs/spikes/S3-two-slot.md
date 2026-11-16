# Spike S3: two-slot partition table and image-size budget

Issue #11. Finding for the build target (D4), the flashing script (D5) and the device checklist (D6). Nothing here changes the firmware or flashes anything.

## Summary

| Item | Value |
| --- | --- |
| Proposed table | [`partitions_picco_2slot.proposed.csv`](partitions_picco_2slot.proposed.csv): `nvs` 0x9000/0xC000 and `otadata` 0x15000/0x2000 unchanged from stock, `app0` (ota_0) 0x20000, `app1` (ota_1) 0x800000, `coredump` 0xFF0000 |
| Slot size | 0x7E0000 = 8,257,536 bytes (7.875 MiB) per slot |
| Budget (slot minus 10%) | 7,431,782 bytes (0x716666) |
| Current `picco` image | **Estimate**: about 5.4 MB. Upstream's published `crosspoint-2.0.0-picco.bin` (60 commits before our pin) is 5,386,144 bytes = 72.5% of the budget. Confirm in D4 |
| Full-flash images | 0x0 `bootloader.bin`, 0x8000 `partitions.bin`, **0x15000** `boot_app0.bin`, 0x20000 `firmware.bin`. Nothing else is written |
| Biggest trap | The framework writes `boot_app0.bin` at a fixed 0xE000, not at the table's `otadata` offset. On this layout 0xE000 is inside `nvs` |
| Validation | Both `gen_esp32part.py` copies (arduino-esp32 3.3.11 and ESP-IDF v5.5.5) accept the table and produce the same binary. [`check_2slot_table.py`](check_2slot_table.py) checks the project rules (nvs/otadata equal to stock, two equal slots, alignment, overlaps, 16 MB) |

## How this was established

Run in this session:

- `pio run -e picco` once. It failed downloading `esp32-core-3.3.11.tar.xz` with `SSLCertVerificationError ... self-signed certificate in certificate chain`. Per the brief, the certificate problem was left alone, so there is no build of our own.
- Read-only copies of the external sources, at the versions this build uses:
  - arduino-esp32 tag `3.3.11` (sparse clone of `tools/` and `cores/esp32/`);
  - the Arduino 3.3.11 libs release asset `esp32-core-3.3.11-libs.tar.xz`, downloaded with curl to read `esp32s3/sdkconfig`, `esp32s3/dio_opi/include/sdkconfig.h` and the S3 bootloader ELFs;
  - ESP-IDF tag `v5.5.5`, the version the 3.3.11 bootloader reports (sparse clone of `components/app_update`, `components/bootloader_support`, `components/partition_table`);
  - `esp32-arduino-lib-builder` master `c26041da` (`configs/`), for how the prebuilt bootloaders are built;
  - the pioarduino platform 55.03.311 already installed in `~/.platformio/platforms/espressif32`.
- `esptool` 5.1.0 from PyPI, in a scratch virtualenv, to turn `bootloader_dio_80m.elf` into `bootloader.bin` the way the build does and read its header.
- `gen_esp32part.py` from both sources on the proposed CSV, plus three deliberately broken tables to show the tool rejects misalignment, overlap and overflow.
- HTTP `HEAD` requests on upstream release assets, to get published image sizes.

Line numbers in our repo refer to commit `origin/main` at the time of the spike (the pinned base `490813ff` plus project docs). External line numbers refer to the versions above.

## 1. The stock layout and what upstream does with it

### Stock table

From `partitions_picco.csv:12-17` (the header comment, lines 8-10, says it was read from a retail unit). Areas outside the table are added for completeness.

| Area | Type / subtype | Offset | Size | End (exclusive) | What it holds |
| --- | --- | --- | --- | --- | --- |
| (bootloader) | n/a | 0x0 | 0x8000 | 0x8000 | Onyx's second-stage bootloader |
| (partition table) | n/a | 0x8000 | 0x1000 | 0x9000 | The table itself (0xC00 bytes used) |
| `nvs` | data / nvs | 0x9000 | 0xC000 (48 KiB) | 0x15000 | Onyx key-value data; also CrossPoint's namespaces once CrossPoint has run (see section 3) |
| `otadata` | data / ota | 0x15000 | 0x2000 | 0x17000 | Two 4 KiB boot-selection records |
| (gap) | n/a | 0x17000 | 0x9000 | 0x20000 | Unallocated |
| `safeboot` | app / factory | 0x20000 | 0xC0000 (768 KiB) | 0xE0000 | Onyx recovery app. `platformio.ini:698-699` mentions a "stock web flasher" with an "OTA update" mode; that it lives here is likely but not verified |
| `app` | app / ota_0 | 0xE0000 | 0xE70000 (14.44 MiB) | 0xF50000 | Onyx's main app ("its app alone is ~9MB", `platformio.ini:694-695`), or CrossPoint once installed |
| `spiffs` | data / spiffs | 0xF50000 | 0x80000 (512 KiB) | 0xFD0000 | Onyx file-system data, contents unknown |
| `coredump` | data / coredump | 0xFD0000 | 0x10000 | 0xFE0000 | IDF core dump area |
| (gap) | n/a | 0xFE0000 | 0x20000 | 0x1000000 | Unallocated |

There is only one OTA app partition. `safeboot` is a factory app, not an OTA slot.

### What upstream's `picco` build writes

`docs/BASE.md` ("Picco upload target at this commit") already established this and I re-checked the key lines. `pio run -e picco -t upload` writes 0x0 bootloader, 0x8000 `partitions.bin`, 0xE000 `boot_app0.bin` and 0xE0000 `firmware.bin`. arduino-esp32 3.3.11 `tools/pioarduino-build.py:216-235` adds the three extra images. Platform `builder/main.py:1930-1933` copies them into the upload command. Platform `builder/main.py:360-365` takes the app offset from the `ota_0` row. `scripts/app_only_upload.py:7` runs too late to remove them. The 0xE000 write lands inside the stock `nvs` (0x9000-0x14FFF). The intent stated in `platformio.ini:695-698` is an app-only write at 0xE0000. The released `firmware.bin` (`release.yml:117`) is that app image alone.

### Why upstream hides reader-side updates on the Picco

1. Settings menu: `src/activities/settings/SettingsActivity.cpp:200-207` adds "Check for updates" (`SettingAction::CheckForUpdates`) and "SD Card Firmware Update" (`SettingAction::SdFirmwareUpdate`) only `if (!BoardConfig::isPicco())`. The comment gives the reason: "The Picco's stock layout has one app slot (partitions_picco.csv), so it cannot update itself either way."
2. Boot recovery picker: `src/main.cpp:609-611` sets `recoveryFirmwareMode` only when `!BoardConfig::isPaperMono() && !BoardConfig::isPicco()`. The comment reads "The Picco has no second app slot to flash into".
3. `BoardConfig::isPicco()` is `ACTIVE.board == Board::Picco` (`freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:2390`). `ACTIVE` is set to the `PICCO` profile when built with `-DFREEINK_DEVICE_PICCO=1` (`BoardConfig.h:2360-2363`; `platformio.ini:713`). This is a compile-time board check. The SDK has no OTA or partition capability flag (`BoardConfig.h` has no OTA or partition entries).

The hiding is necessary. ESP-IDF's `esp_ota_get_next_update_partition(NULL)` returns the first OTA partition when the running one is the last (or only) OTA partition (ESP-IDF v5.5.5 `components/app_update/esp_ota_ops.c:737-782`; `default_ota` is returned at line 780). On the stock table the running app *is* `ota_0`, so the "next" partition is the running one. The Wi-Fi path would then fail safely: `esp_ota_begin` refuses the running partition (`esp_ota_ops.c:159-161`, `ESP_ERR_OTA_PARTITION_CONFLICT`). The SD path would not. It erases and writes the partition it is handed with no running-slot check (`src/network/FirmwareFlasher.cpp:279`, `:314-347`), so it would erase the running app.

## 2. Upstream's two-slot devices and their update paths

### Layout

`partitions.csv:2-7` is used by every env that does not override `board_build.partitions` (`platformio.ini:131`), e.g. `x4pro`, `papermono`, `x4c`, `metalio_eink4`, `eego_a4`, `sticky`:

| Name | Subtype | Offset | Size | End |
| --- | --- | --- | --- | --- |
| nvs | nvs | 0x9000 | 0x5000 | 0xE000 |
| otadata | ota | 0xE000 | 0x2000 | 0x10000 |
| app0 | ota_0 | 0x10000 | 0x640000 (6.25 MiB) | 0x650000 |
| app1 | ota_1 | 0x650000 | 0x640000 | 0xC90000 |
| spiffs | spiffs | 0xC90000 | 0x360000 | 0xFF0000 |
| coredump | coredump | 0xFF0000 | 0x10000 | 0x1000000 |

This is where the framework's fixed `boot_app0` offset 0xE000 comes from: on upstream's table it is the `otadata` offset.

### SD-card update, end to end

1. Entry: Settings > General > "SD Card Firmware Update" (`SettingsActivity.cpp:206`, launched at `:471-472`). On X3/X4-class boards it can also be reached by holding Up (Down on X4 Pro/X4C) during a power-button wake (`src/main.cpp:605-611`, `:780-783`).
2. File: a picker opened at `/` (`src/activities/settings/SdFirmwareUpdateActivity.cpp:25-29`). It lists directories and files ending `.bin` (`src/activities/home/FileBrowserActivity.cpp:129-133`) and returns the chosen path (`:353-356`). There is no fixed name or folder. Any `*.bin` anywhere on the card works.
3. Validation before the confirmation prompt (`SdFirmwareUpdateActivity.cpp:68-114`). The size must fit the next update partition (`:81-93`). `firmware_flash::validateImageFile` (`src/network/FirmwareFlasher.cpp:112-273`) then checks:
   - at least 64 KiB (`:120-124`) and no larger than the partition (`:125-130`);
   - magic byte 0xE9 (`:138-142`);
   - chip ID equal to the running image's chip ID (`:143-153`, `:72-86`);
   - segment table within the file (`:175-208`);
   - board tag (`:170-173`, `:210-216`): an image tagged for another board is rejected; untagged images pass (`src/network/FirmwareBoardTag.h:6-16`; Picco tag `"picco"`, `src/network/FirmwareBoardTag.cpp:26-27`);
   - exact file length including padding and SHA-256 trailer (`:218-228`);
   - XOR checksum (`:245-251`);
   - SHA-256 (`:253-268`).
4. Confirmation ("Update firmware?", `SdFirmwareUpdateActivity.cpp:116-130`).
5. Flash (`SdFirmwareUpdateActivity.cpp:153-191`, then `FirmwareFlasher.cpp:275-355`):
   - The validation runs again in case the card changed (`:289-295`).
   - The slot written is `esp_ota_get_next_update_partition(nullptr)` (`:279`), so the other slot.
   - Erase and write are interleaved 64 KiB at a time (`:314-347`).
   - The boot slot is switched by writing otadata directly (`ota_boot::switchTo`, `:350`).
   - The device restarts (`SdFirmwareUpdateActivity.cpp:184-191`).
6. otadata switch (`src/network/OtaBootSwitch.cpp:14-84`). It reads both 4 KiB records and picks the valid one with the highest sequence number (`:35-46`). It computes the next sequence number that maps to the destination, assuming exactly **two** OTA slots (`:57-60`, `% 2u`). It erases and writes the *other* record with state `ESP_OTA_IMG_NEW` (`:62-79`). This deliberately bypasses `esp_ota_set_boot_partition`, whose image check rejects patched X4 vendor images (`src/network/OtaBootSwitch.h:8-13`).

### Wi-Fi update, end to end

Settings > General > "Check for updates" (`SettingsActivity.cpp:205`, `:468-469`) opens `OtaUpdateActivity`, which calls `OtaUpdater::checkForUpdate` and `installUpdate` (`src/activities/settings/OtaUpdateActivity.cpp:29`, `:182`).

- **Release source:** the latest release of **upstream** `crosspoint-reader/crosspoint-reader` (`src/network/OtaUpdater.cpp:22`). It looks for the asset `crosspoint-<tag>-<board>.bin` (`:41-60`), which is `crosspoint-<tag>-picco.bin` on the Picco. It installs only if the version is newer (`:94-135`).
- **Install:**
  - Partition: `esp_ota_get_next_update_partition` (`:149`).
  - Write: `esp_ota_begin`, `esp_ota_write` and `esp_ota_end` (`:156`, `:199`, `:229`). `esp_ota_end` verifies the image.
  - Checks while streaming: chip ID and board tag (`:177-198`).
  - Boot switch: `esp_ota_set_boot_partition` (`:235`).
  - Restart: `OtaUpdateActivity.cpp:242`.

### Rollback behaviour

- The Arduino 3.3.11 bootloader is built with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` (libs `esp32s3/sdkconfig:424`; `dio_opi/include/sdkconfig.h:404`).
- On the first boot of a slot marked `NEW`, the bootloader rewrites it as `PENDING_VERIFY` (ESP-IDF `bootloader_utility.c:447-450`). If the device resets while still `PENDING_VERIFY`, the next boot marks it `ABORTED` (`:397-404`) and boots the previous slot.
- Arduino's `initArduino()` marks the running image valid immediately unless the app overrides `verifyOta()`/`verifyRollbackLater()` (arduino-esp32 3.3.11 `cores/esp32/esp32-hal-misc.c:261-264`, `:285-288`, `:314-327`). Neither hook is overridden anywhere in `src/`, `lib/` or `freeink-sdk/libs/`.
- So rollback only catches an image that fails the bootloader's image check, or one that crashes before `initArduino()`. An image that fails verification falls through to the other slots (`bootloader_utility.c:579-627`). A build that boots but then misbehaves is **not** rolled back.

### Running-slot protection

- Wi-Fi path: ESP-IDF's `esp_ota_begin` returns `ESP_ERR_OTA_PARTITION_CONFLICT` for the running partition (`esp_ota_ops.c:159-161`).
- SD path: there is no explicit check. It relies on `esp_ota_get_next_update_partition` returning a different slot, which is true only with two or more OTA slots (section 1).
- On two-slot devices the next slot is always the other one (ota_0 to ota_1; from ota_1, the function falls back to ota_0).
- The plan cites crossplay's explicit guard as a pattern (`docs/PLAN.md` section 3 table). We would re-implement it, not copy it (section 6).

## 3. Proposed `partitions_picco_2slot.csv`

```
# Name,   Type, SubType,  Offset,   Size,     Flags
nvs,      data, nvs,      0x9000,   0xC000,
otadata,  data, ota,      0x15000,  0x2000,
app0,     app,  ota_0,    0x20000,  0x7E0000,
app1,     app,  ota_1,    0x800000, 0x7E0000,
coredump, data, coredump, 0xFF0000, 0x10000,
```

| Area | Offset | Size | End | Note |
| --- | --- | --- | --- | --- |
| bootloader | 0x0 | max 0x8000 | 0x8000 | Arduino bootloader, 18,720 bytes (0x4920) |
| partition table | 0x8000 | 0x1000 | 0x9000 | `CONFIG_PARTITION_TABLE_OFFSET 0x8000` (`dio_opi/include/sdkconfig.h:474`) |
| `nvs` | 0x9000 | 0xC000 | 0x15000 | Unchanged from stock |
| `otadata` | 0x15000 | 0x2000 | 0x17000 | Unchanged from stock |
| (gap) | 0x17000 | 0x9000 | 0x20000 | First 64 KiB boundary after otadata is 0x20000 |
| `app0` (ota_0) | 0x20000 | 0x7E0000 | 0x800000 | |
| `app1` (ota_1) | 0x800000 | 0x7E0000 | 0xFE0000 | |
| (gap) | 0xFE0000 | 0x10000 | 0xFF0000 | Left over by 64 KiB rounding |
| `coredump` | 0xFF0000 | 0x10000 | 0x1000000 | Same address as upstream `partitions.csv:7` |

### What "stock offset" means for `nvs`

"Stock" means the `nvs` row of the table read from a retail unit (`partitions_picco.csv:12`): offset 0x9000 and size 0xC000, ending 0x15000. The offset happens to equal the ESP-IDF default (table at 0x8000 plus one sector). The size is larger than upstream's 0x5000 (`partitions.csv:2`). We keep both offset and size, so the bytes in 0x9000-0x14FFF are never written by the move. The owner's unit is assumed to have the same table. `identify` must confirm it (section 5).

### Are the stock NVS contents compatible and harmless?

Yes, as far as the code shows.

**Format.** NVS is namespaced key-value storage. Our firmware only opens its own namespaces:

| Namespace | Holds | Source |
| --- | --- | --- |
| `cphw` | X3/X4 detection override; not used on the Picco | `lib/hal/HalGPIO.cpp:18-41` |
| `cptime` | time floor | `lib/TrustedTime/TrustedTime.cpp:31`, `:50-62` |
| `devid` | content-protection device secret | `lib/DeviceSecret/DeviceSecret.cpp:19-31` |
| `freeink-hid` | BLE remote bonds | `freeink-sdk/libs/network/BleKeyboardHost/src/BleKeyboardHost.cpp:1634-1656` |

ESP-IDF itself adds Wi-Fi and PHY calibration data (`CONFIG_ESP_WIFI_NVS_ENABLED`, `CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE`, `dio_opi/include/sdkconfig.h:966`, `:866`). Onyx's keys in other namespaces are ignored.

**Existing use.** Upstream's stock-table `picco` build already runs on top of the Onyx NVS in the same partition (`partitions_picco.csv:12`, `platformio.ini:695-698`).

**Worst case.** If Onyx's NVS is full or uses a newer format, Arduino's `initArduino()` erases the whole `nvs` partition and re-initialises it (`esp32-hal-misc.c:331-337`). That loses only Onyx data, which the backup holds.

**Encryption.** The stock table has no `nvs_keys` partition, so the stock NVS is not encrypted.

**Plan correction (rule 6).** `docs/PLAN.md` section 3 says "NVS stays at its stock offset so settings survive". The code contradicts the "settings" part.

- CrossPoint's settings live on the SD card, not in NVS: `/.crosspoint/settings.json` (`src/CrossPointSettings.h:501`), `state.json` (`src/CrossPointState.h:25`) and `wifi.json` (`src/WifiCredentialStore.h:44`). Wi-Fi persistence in the SDK is switched off (`src/activities/network/WifiSelectionActivity.cpp:468`).
- On the owner's device, which has only ever run Onyx firmware, NVS holds only Onyx data.

The real reasons to keep `nvs` where it is:

1. The move never has to write 0x9000-0x14FFF.
2. Vendor data stays intact for a later restore.
3. It matches the plan's precedent.

See the proposed plan text at the end.

### `otadata`

Kept at the stock 0x15000 and size 0x2000. Two 4 KiB records, as both the bootloader and `OtaBootSwitch` expect (`OtaBootSwitch.cpp:23-26`; ESP-IDF `bootloader_utility.c` reads records at `offset` and `offset + 0x1000`, lines 112-113). It cannot go to the upstream/framework position 0xE000, because that is inside our `nvs`.

### `spiffs`: dropped

Nothing mounts a SPIFFS, LittleFS or FAT partition: no `SPIFFS.begin`, `LittleFS.begin`, `FFat` or `esp_spiffs` in `src/`, `lib/` or `freeink-sdk/libs/`. The only hit is a stale comment in `src/SdCardFontSystem.cpp:50`. That code actually writes the SD card through `SETTINGS.saveToFile()`, line 55. Keeping a 512 KiB `spiffs` would cost 256 KiB per slot for nothing.

### `coredump`: kept, 64 KiB, at 0xFF0000

The prebuilt libs enable core dump to flash: `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH 1`, ELF format (`dio_opi/include/sdkconfig.h:989`, `:993`; `lib-builder configs/defconfig.common:124-127`). On a panic, IDF writes the dump to the `coredump` partition.

Our firmware does not read it. Its own crash report uses RTC memory (`lib/hal/HalSystem.cpp:20-24`, `:92-107`). But the dump is readable over USB with `esptool read-flash 0xFF0000 0x10000` and `esp-coredump`. That is useful if the device session hits a crash.

Same size as stock and upstream. Same address as upstream `partitions.csv:7`.

### App slots and the arithmetic

| Step | Value |
| --- | --- |
| Region available for apps | 0x20000 (first 64 KiB boundary after `otadata` ends at 0x17000) to 0xFF0000 (start of `coredump`): 0xFD0000 = 16,580,608 bytes |
| Half | 0x7E8000 = 8,290,304. Not 64 KiB aligned |
| Slot size, rounded down to 64 KiB | **0x7E0000 = 8,257,536 bytes** (126 x 64 KiB; 7.875 MiB; 8.26 MB) |
| Two slots | 0xFC0000; 0x10000 remains, left unallocated at 0xFE0000-0xFEFFFF |
| Budget, slot minus 10% | floor(8,257,536 x 0.9) = **7,431,782 bytes** (0x716666) |
| For comparison | upstream two-slot devices: 0x640000 = 6,553,600 bytes per slot (`partitions.csv:4-5`) |

This matches the plan's "roughly 7.5 to 7.9 MB per slot" (7.875 MiB).

### ESP-IDF rules checked

| Rule | How it is met |
| --- | --- |
| App partitions aligned to 0x10000 | 0x20000 and 0x800000 |
| otadata | 0x2000 long, sector aligned |
| Table position | 0x8000; nothing below 0x9000 |
| Bootloader below 0x8000 | Ends at 0x4920; 14,048 bytes spare |
| Fits 16 MB | Last byte 0xFFFFFF |
| Overlaps | None |

### Validation

```
$ python3 -I <arduino-esp32 3.3.11>/tools/gen_esp32part.py --flash-size 16MB \
    docs/spikes/partitions_picco_2slot.proposed.csv 2slot.bin
Parsing CSV input...
Verifying table...                                   (exit 0)
$ python3 -I <arduino-esp32 3.3.11>/tools/gen_esp32part.py 2slot.bin
nvs,data,nvs,0x9000,48K,
otadata,data,ota,0x15000,8K,
app0,app,ota_0,0x20000,8064K,
app1,app,ota_1,0x800000,8064K,
coredump,data,coredump,0xff0000,64K,
```

The ESP-IDF v5.5.5 copy (`components/partition_table/gen_esp32part.py`) also accepts it and produces a byte-identical `partitions.bin`. That file is 3,072 bytes, SHA-256 `0d3fad517e6421219fb3754b58c1e1153505655aba69dac3fc40cbd528d4a332`. `esp-idf-part` is not on PyPI, so it was not used.

To show the tool would catch the mistakes we care about, I fed it three broken tables. It rejected each one:

- a misaligned app ("Offset 0x808000 is not aligned to 0x10000");
- an overlap ("Partitions overlap");
- an overflow ("does not fit in configured flash size 16MB").

Project rules, which `gen_esp32part.py` cannot know:

```
$ python3 docs/spikes/check_2slot_table.py docs/spikes/partitions_picco_2slot.proposed.csv partitions_picco.csv
Offset     End        Size       Name
0x00009000 0x00015000 0x0000c000 nvs
0x00015000 0x00017000 0x00002000 otadata
0x00020000 0x00800000 0x007e0000 app0
0x00800000 0x00fe0000 0x007e0000 app1
0x00ff0000 0x01000000 0x00010000 coredump
slot size: 8257536 bytes (0x7e0000, 7.875 MiB)
budget (slot minus 10%, rounded down): 7431782 bytes (0x716666)
OK
```

Run against upstream's `partitions.csv`, the same script reports `nvs` and `otadata` differing from stock, as it should.

### Alternatives rejected

| Alternative | Why rejected |
| --- | --- |
| Keep `app` at 0xE0000 as `ota_0` (first write lands where the stock app was) and put `ota_1` after it | Slots shrink to at most 0x780000 (7.5 MiB) with `coredump` kept. 0x20000-0xDFFFF (768 KiB) is then wasted, or must hold `safeboot`. Nothing gains from the app sitting at 0xE0000: an ESP32 app image runs from any 64 KiB-aligned slot (the same CrossPoint `firmware.bin` is installed at 0x10000 on other boards and 0xE0000 on the Picco, `platformio.ini:36`, `:695-698`) |
| Keep `safeboot` (factory) at 0x20000 and add two OTA slots after it | Same 0x780000 slot limit. Onyx's recovery app is closed. How it behaves under a different bootloader and table, and which partition it would write, is unknown and cannot be tested. The plan already accepts that safeboot only comes back from the backup (`docs/PLAN.md` section 2, "Two-slot move") |
| Keep Onyx's app in the second slot as a fallback | It is about 9 MB (`platformio.ini:694-695`), larger than any equal split of 16 MB, and it expects the Onyx table |
| Keep `spiffs` (512 KiB) | Unused by the firmware (above). Costs 256 KiB per slot |
| Move `nvs`/`otadata` to upstream's 0x9000/0x5000 and 0xE000 | Changes `nvs` (fails the "unchanged" requirement). The move would then write over the stock NVS area |
| Unequal slots, or a third slot or factory app of our own | `OtaBootSwitch` assumes exactly two OTA slots (`OtaBootSwitch.cpp:57-60`), and each slot must hold any build. A recovery app of our own is out of v1 scope |
| A larger `coredump` filling 0xFE0000-0xFFFFFF | Possible but unneeded; 64 KiB matches stock and upstream. The spare 64 KiB is left unallocated so the table stays easy to compare |

## 4. Current image size against the budget

`pio run -e picco` failed on the certificate error (see "How this was established"), so these figures are an **estimate** and D4 must confirm them.

| Evidence | Bytes | Share of budget (7,431,782) | Share of slot (8,257,536) |
| --- | --- | --- | --- |
| `crosspoint-2.0.0-picco.bin`, upstream release 2.0.0 (tag commit `42d58daa`, an ancestor of our pin, 60 commits earlier; built from env `picco-gh_release`, `release.yml:41-43`, `:117`) | 5,386,144 | 72.5% | 65.2% |
| `crosspoint-2.0.0rc-picco.bin` | 5,357,248 | 72.1% | 64.9% |
| `crosspoint-2.0.0-x4pro.bin` (same base flags, `dio_opi`) | 5,377,104 | 72.4% | 65.1% |
| `crosspoint-2.0.0-x3-x4.bin` (largest asset of the release) | 5,537,104 | 74.5% | 67.1% |

Sizes come from the `Content-Length` of each release download (HTTP `HEAD`). Upstream's pages and API for that repo are not reachable from this session.

Between the 2.0.0 tag and our pin, `src/`, `lib/`, `scripts/` and `platformio.ini` changed by +3,823/-1,497 lines, and the SDK gitlink moved. Between rc and release the image grew by 28,896 bytes. Our `picco` env differs from `picco-gh_release` by `LOG_LEVEL=2` and `CROSSPOINT_WAIT_FOR_USB_SERIAL` (`platformio.ini:719-721` vs `:742-743`), which adds a little log text.

Estimate for `picco` at our pin: **about 5.4 MB, very likely under 5.6 MB**. That leaves roughly 1.8-2.0 MB of headroom under the budget.

Two related points for D4:

- **There is no size gate today.** `ci.yml` only records statistics. PlatformIO's own size check compares against `upload.maximum_size`, which the platform overwrites with the `ota_0` size (`builder/main.py:408-411`). On `picco_2slot` the check therefore fails only above 8,257,536 bytes. A 10% gate has to be D4's own check on the size of `firmware.bin`. Setting `board_upload.maximum_size` would be overwritten.
- **What to measure.** `firmware_size_history.py` parses PlatformIO's `Flash: ... bytes from ... bytes` line (`scripts/firmware_size_history.py:39-41`). That counts flash segments, not the file written. The gate should use the byte length of `firmware.bin`.

## 5. Images a full two-slot flash must write

| Offset | File | Comes from | Size | Must not exceed |
| --- | --- | --- | --- | --- |
| 0x0 | `bootloader.bin` | `.pio/build/picco_2slot/bootloader.bin`, made by `esptool elf2image` from `framework-arduinoespressif32-libs/esp32s3/bin/bootloader_dio_80m.elf` (`pioarduino-build.py:68-91`, `:93-116`) | 18,720 bytes, measured by doing the same conversion here | 0x8000 |
| 0x8000 | `partitions.bin` | `.pio/build/picco_2slot/partitions.bin`, made by the framework's `gen_esp32part.py` (`pioarduino-build.py:247-255`) | 3,072 bytes | 0x1000 |
| **0x15000** | `boot_app0.bin` | `framework-arduinoespressif32/tools/partitions/boot_app0.bin` (`pioarduino-build.py:229-232`) | 8,192 bytes, SHA-256 `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0` (3.3.11) | 0x2000 |
| 0x20000 | `firmware.bin` | `.pio/build/picco_2slot/firmware.bin` | about 5.4 MB (estimate) | 7,431,782 (budget); 0x7E0000 (slot) |

Nothing else is written. `nvs` (0x9000-0x14FFF), the gap 0x17000-0x1FFFF, `app1` and `coredump` keep whatever is there. Old Onyx bytes in `app1` are harmless:

- 0x800000 is the middle of the old Onyx app, not an image header, so the bootloader cannot boot it.
- `otadata` will point at `app0`.
- Every later update erases `app1` before writing it (`FirmwareFlasher.cpp:319-330`; `esp_ota_begin` with `OTA_SIZE_UNKNOWN` erases the whole slot).

### `boot_app0.bin` and its offset

- **What it is.** It is 8,192 bytes. Record 0 has `ota_seq = 1`, a valid CRC, and state `0xFFFFFFFF` (undefined, so no rollback pending). Record 1 has `ota_seq = 0` and CRC `0xFFFFFFFF`. *(Corrected after D5, pull request #59: this spike first said record 1's CRC was invalid. In fact `crc32_le(UINT32_MAX, ota_seq = 0)` is `0xFFFFFFFF`, so record 1 is CRC-valid with sequence 0. Record 0 still wins, because the valid record with the higher sequence is the active one. `tools/device/piccoflash/layout.py` rebuilds this file byte for byte from these two records, matching the SHA-256 below; `tools/device/tests/test_layout.py` checks it.)* The bootloader therefore boots `ota_0` (sequence 1 maps to slot (1-1) % 2 = 0, `bootloader_utility.c:443-445`). `OtaBootSwitch` later reads record 0 as the active one (`OtaBootSwitch.cpp:35-46`).
- **How the build places it.** The `picco` env (and a `picco_2slot` env built the same way) has no `custom_sdkconfig`. It is therefore built by `tools/pioarduino-build.py` (platform `builder/frameworks/arduino.py:534-535`). There the offset is the board-manifest key `upload.arduino.boot_app0`, default `"0xe000"` (`pioarduino-build.py:229-232`). It is a constant and does **not** follow the table.
  - Only envs with `custom_sdkconfig`, which recompile the libs through `espidf.py` (`arduino.py:418-420`), take the offset from the table's `otadata` row (`builder/frameworks/espidf.py:3142-3163`).
  - The board manifest `esp32-s3-devkitc1-n16r8.json` sets no `upload.arduino.*` keys.
- **Would the default land inside our `nvs`? Yes.** 0xE000-0xFFFF is the 6th and 7th 4 KiB page of `nvs` (0x9000-0x14FFF). It would also leave the stock otadata at 0x15000 in place.
- **What D4 and D5 do about it.**
  - D4: set `board_upload.arduino.boot_app0 = 0x15000` in `picco_2slot`. This is the same `board_<section>.<key>` override mechanism as `board_build.arduino.memory_type` (`platformio.ini:706`). Confirm with `pio run -e picco_2slot -t envdump` (`FLASH_EXTRA_IMAGES`).
  - D5: put 0x15000 in `flash-plan.json` explicitly and refuse any write that intersects 0x9000-0x14FFF.

### Do not use `firmware.factory.bin`

The platform also builds a merged `firmware.factory.bin` from `FLASH_EXTRA_IMAGES` plus the app (`builder/main.py:1755-1790`). `esptool merge-bin` fills gaps with 0xFF. Written at 0x0, it would erase `nvs` and put `boot_app0` at 0xE000. D5 must flash the four separate files.

### Header settings that matter

| Setting | Value and where it comes from |
| --- | --- |
| Flash mode | **DIO**. `board_build.flash_mode = dio` (`platformio.ini:129`) overrides the manifest's `qio`. The platform maps qio/qout to dio for images anyway (`builder/main.py:256-273`). `memory_type = dio_opi` (`platformio.ini:706`) is not `opi_*`, so the boot mode stays `dio` (`builder/main.py:276-291`) |
| Bootloader ELF | `bootloader_dio_80m.elf` |
| Flash frequency | **80 MHz** (manifest `f_flash: 80000000L`; `_get_board_f_image`/`_f_boot`, `builder/main.py:222-253`) |
| Flash size | **16MB** (`platformio.ini:34`, `:130`). Matters because the bootloader refuses partitions beyond the size in its header |
| Measured bootloader header | `Flash mode: DIO`, `Flash freq: 80m`, `Flash size: 16MB`, Chip ID 9, minimum chip revision v0.0, ESP-IDF v5.5.5, valid checksum and SHA-256 |
| App sdkconfig | Matches: `CONFIG_ESPTOOLPY_FLASHMODE "dio"`, `FLASHFREQ "80m"`, `FLASHSIZE "16MB"`, `CONFIG_SPIRAM_MODE_OCT` (`dio_opi/include/sdkconfig.h:459-465`, `:880-881`) |

**D5 and `--flash-size`.** The upstream upload passes `--flash-size detect` (`builder/main.py:1921-1927`), so esptool may rewrite the bootloader header and the bytes written differ from `bootloader.bin`. D5 should pass `--flash-mode keep --flash-freq keep --flash-size keep`, so the SHA-256 in `flash-plan.json` is exactly what lands in flash. The header is already right.

### Hardware dependencies of the bootloader, and what we rely on

**Octal PSRAM.** It is set up by the app, not the bootloader. The libs builder builds the S3 `dio/80m` bootloader with the quad-RAM config (`configs/builds.json`, esp32s3 `bootloaders` entry `["dio","80m","qio_ram"]`). The `dio_opi` app libs enable octal PSRAM. The same bootloader is what `pio run -t upload` installs on upstream's other ESP32-S3 16 MB/8 MB octal-PSRAM boards (`x4pro`, `papermono`, `x4c`, `metalio_eink4`, `eego_a4`; `platformio.ini:398`, `:424`, `:483`, `:577`, `:639`). That is in-repo evidence that it works with this memory configuration on other hardware.

**Flash type.** A DIO bootloader cannot boot octal flash.

- Upstream's `picco` app is built for quad flash in DIO mode (`dio_opi`). It reportedly runs on the Picco under Onyx's bootloader (`docs/PLAN.md` section 2, PR #3947), which implies quad flash.
- It is still an assumption. Check eFuse `FLASH_TYPE` and the stock bootloader header in `identify`.

**Flash/PSRAM voltage.** VDD_SPI is set by eFuse or the GPIO45 strap, not by this bootloader. The only related bootloader option is `CONFIG_BOOTLOADER_VDDSDIO_BOOST_1_9V=y` (`esp32s3/sdkconfig:474`), the IDF default for modules whose internal LDO runs at 1.8 V.

**Secure Boot / flash encryption.** If Onyx enabled Secure Boot V2, the unsigned Arduino bootloader will not boot and the device is stuck until the backup is restored. Restoring is possible only because the ROM's download mode survives. If flash encryption is on, the backup itself is ciphertext and plain writes break the device.

- Upstream's unsigned, unencrypted `picco` app running on stock suggests both are off.
- `identify` must read them and stop if either is on.

**Download mode over USB-Serial/JTAG.** esptool resets the S3 into ROM download mode through the USB-Serial/JTAG peripheral. Our firmware keeps that peripheral (`-DARDUINO_USB_MODE=1`, `platformio.ini:40`), except while the USB Drive (MSC) activity runs (`platformio.ini:395-397`). An eFuse can disable download mode or USB-Serial/JTAG; `identify` must check. If a broken image ever takes the USB port away, the only way in is the GPIO0 strap. Whether the Picco exposes it is unknown.

**The community precedent.** The plan cites KatiePoint 2.0.0-rc3 as having done this move. We cannot read it, so we rely only on the plan's summary (`docs/PLAN.md` section 2, row "Two-slot move"):

- a full flash with the Arduino bootloader, a two-slot table, otadata and the app boots a Picco;
- NVS kept its offset.

We do not know its exact offsets, its bootloader build, or how many units it was tried on.

### What `identify` should read before anything is written (all read-only)

1. `esptool --chip esp32s3 chip-id` and `flash-id`.
   - Read: chip and revision, "Embedded PSRAM 8MB (AP_3v3 or AP_1v8)", flash manufacturer and device ID, detected size.
   - Stop unless: ESP32-S3, 16 MB.
2. `esptool get-security-info`.
   - Stop if: Secure Boot or flash encryption is enabled (e.g. `SPI_BOOT_CRYPT_CNT` odd, `SECURE_BOOT_EN`).
3. `espefuse --chip esp32s3 summary`. This is a read-only subcommand; D5 must refuse every other `espefuse` subcommand.
   - Record: `FLASH_TYPE`, `VDD_SPI_FORCE`/`VDD_SPI_TIEH`/`VDD_SPI_XPD`, `DIS_DOWNLOAD_MODE`, `ENABLE_SECURITY_DOWNLOAD`, `DIS_USB_SERIAL_JTAG`, PSRAM/flash capacity and vendor.
   - Stop if: octal flash, download mode disabled, or security download enabled.
4. `esptool read-flash 0x0 0x8000 stock-bootloader.bin`, then `esptool image-info`.
   - Record: Onyx's flash mode, frequency and size, its ESP-IDF version, minimum chip revision, and whether a signature block is present.
   - Stop if: signed.
5. `esptool read-flash 0x8000 0xC00 stock-table.bin`, decoded with `gen_esp32part.py`.
   - Stop unless: it equals `partitions_picco.csv` row for row. Above all `nvs` 0x9000/0xC000 and `otadata` 0x15000/0x2000, which the proposed table assumes.
6. `esptool read-flash 0x15000 0x2000 stock-otadata.bin`. Informational: which stock slot was booting.

Steps 4 to 6 can equally be cut from the full backup once it exists. Doing them in `identify` lets the session stop before the long backup reads.

## 6. Telling at run time that the table has two app slots

ESP-IDF provides everything needed in the running firmware:

| Call | Gives |
| --- | --- |
| `esp_ota_get_app_partition_count()` | Number of `ota_N` app partitions (`components/app_update/include/esp_ota_ops.h:352`, `esp_ota_ops.c:542-550`). Stock Picco: 1. Proposed table: 2 |
| `esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr)` | The otadata partition and its size (as `OtaBootSwitch.cpp:17-27` already does) |
| `esp_ota_get_running_partition()` and `esp_ota_get_next_update_partition(nullptr)` | Whether the next slot differs from the running one |

Proposed decision, kept pure so it is host-testable. Only a thin adapter calls ESP-IDF.

```cpp
// src/<project dir>/UpdateSlotPolicy.h  (name and directory per plan section 4)
namespace update_slots {

struct Layout {
  uint8_t otaAppCount;      // esp_ota_get_app_partition_count()
  uint32_t otadataSize;     // 0 if no otadata partition
  bool runningIsOta;        // running->subtype in [OTA_0, OTA_MAX)
  bool nextIsRunning;       // next == running, or next == nullptr
  uint32_t runningSlotSize; // running->size
  uint32_t nextSlotSize;    // next ? next->size : 0
};

enum class Verdict { Allowed, NotTwoSlots, NoOtadata, NotRunningFromOta, NextIsRunning, SlotTooSmall };

constexpr Verdict decide(const Layout& l) {
  if (l.otaAppCount != 2) return Verdict::NotTwoSlots;        // OtaBootSwitch assumes exactly two
  if (l.otadataSize < 0x2000) return Verdict::NoOtadata;
  if (!l.runningIsOta) return Verdict::NotRunningFromOta;
  if (l.nextIsRunning) return Verdict::NextIsRunning;         // running-slot protection
  if (l.nextSlotSize < l.runningSlotSize) return Verdict::SlotTooSmall;
  return Verdict::Allowed;
}

}  // namespace update_slots
```

Host test vectors:

| Case | Inputs | Expected |
| --- | --- | --- |
| Stock Picco | count 1, otadata 0x2000, running ota_0, next == running | `NotTwoSlots` |
| Proposed table, running app0 | count 2, otadata 0x2000, next = app1, sizes 0x7E0000 | `Allowed` |
| Proposed table, running app1 | next = app0 | `Allowed` |
| Upstream `partitions.csv` | count 2, sizes 0x640000 | `Allowed` |
| Table with a factory app that is running | runningIsOta false | `NotRunningFromOta` |
| Three OTA slots | count 3 | `NotTwoSlots` |

D4 then replaces the board check at the registration points:

- `SettingsActivity.cpp:204` becomes "not Picco, or `decide(readLayout()) == Allowed`".
- The SD path should be guarded too: `flashFromSdPath` should refuse when the destination is the running partition. That needs an edit in `FirmwareFlasher.cpp:279-283`, or a check in our own wrapper before it is called.

`main.cpp:609-611` (boot recovery picker) can stay disabled for the Picco in v1. Which Picco keys can be read that early is spike S4's question.

## 7. The SD-card `update.bin`

**What it is.** The plain app image, i.e. `.pio/build/picco_2slot/firmware.bin` copied under any name ending in `.bin`. That is what `validateImageFile` accepts: magic 0xE9, chip ID 9, segment table, padding, checksum and appended SHA-256 (`FirmwareFlasher.cpp:112-273`).

**Name and place.** The plan's `update.bin` at the card root is fine, but any folder works, because the user picks the file (`SdFirmwareUpdateActivity.cpp:25-29`, `FileBrowserActivity.cpp:129-133`).

- It must **not** be `firmware.factory.bin`. Its first image is the bootloader, which also starts with 0xE9 and chip ID 9, so it passes the magic and chip checks. It then fails the length check, because the file is far longer than the bootloader image (`FirmwareFlasher.cpp:218-228`, `BAD_SIZE`).
- The SDK's `RecoveryBoot` library uses a fixed `/update.bin` (`freeink-sdk/libs/hardware/RecoveryBoot/include/RecoveryBoot.h:46-48`). CrossPoint does not link or call it (not in `platformio.ini` `lib_deps`; no reference in `src/`).

**Is the image tied to the table?** No. The image does not depend on the partition table. A `picco` (stock-table) `firmware.bin` and a `picco_2slot` one differ only if the code differs.

**Can upstream's SD path be reused as-is on a two-slot Picco? Yes, apart from the menu gate.**

- `esp_ota_get_next_update_partition` picks the other slot.
- `OtaBootSwitch`'s two-slot assumption holds.
- The board tag stays `"picco"` as long as `picco_2slot` keeps `-DFREEINK_DEVICE_PICCO=1`.

What D4 must change is the menu gate (`SettingsActivity.cpp:204`). The running-slot guard (section 6) is recommended.

**Wi-Fi path: not reusable as-is.** It asks **upstream's** releases for `crosspoint-<tag>-picco.bin` (`OtaUpdater.cpp:22`, `:57`). Upstream publishes exactly that asset, as the 2.0.0 HEAD request showed. Enabled unchanged, "Check for updates" would offer to replace this project's firmware with upstream CrossPoint. That firmware has no RSVP or checkpoints, and it hides the update menu again on the Picco, so getting back would need USB.

## 8. Restore to stock, and how the layout affects recovery

**Full restore.** `esptool write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x0 <16 MiB backup>`, followed by a read-back hash compare. This rewrites every byte, including:

- the bootloader, table, NVS and otadata;
- `safeboot`, `app`, `spiffs` and `coredump`;
- both unallocated gaps.

The `keep` flags matter: without them esptool may patch the stock bootloader header and the result no longer matches the backup. Restore only needs ROM download mode, which no flash content can break. It cannot be prevented by anything in the layout. Only eFuses (checked in `identify`) or losing USB access can.

**What the layout makes easier:**

- `nvs` and `otadata` are where stock expects them and `nvs` is never written by the move. A partial restore is therefore possible if a full one is ever impractical, e.g. time-limited or a bad sector: write the backup's 0x0-0x8FFF, 0x15000-0x16FFF and 0x20000-0xFFFFFF, and NVS is already correct. Plan section 10 should still prescribe the full write. It is simpler and is what the hashes protect.
- A broken app on the two-slot layout does not require USB if the other slot still holds a good build and the failure is caught by the bootloader (bad image, or a crash before `initArduino()`; section 2).

**What it makes harder:**

- There is no on-device recovery app any more (`safeboot` is gone, and the recovery picker is disabled on the Picco, `main.cpp:609-611`).
- A build that boots but has a broken UI can only be replaced over USB, or by SD if the settings menu still works.
- That is why the device session should prove the SD update once while the cable is still attached (proposed for D6 below).

## Proposed changes to docs/PLAN.md

1. **Section 3, "Two-slot layout", first bullet.** Replace "NVS stays at its stock offset so settings survive, as in the KatiePoint precedent." with:

   > NVS (0x9000, 48 KiB) and otadata (0x15000, 8 KiB) keep their stock offsets and sizes, so the move never writes the stock NVS area, as in the KatiePoint precedent. CrossPoint's own settings live on the SD card. `boot_app0.bin` is written at the otadata offset 0x15000, never at the framework default 0xE000, which lies inside the stock NVS. Table and reasoning: `docs/spikes/S3-two-slot.md`.

2. **Section 3, second bullet.** Replace "16 MB of flash split into two app slots leaves roughly 7.5 to 7.9 MB per slot. A CI gate fails any build whose image exceeds the slot size minus a 10% margin." with:

   > Two app slots of 0x7E0000 bytes (8,257,536; 7.875 MiB) at 0x20000 and 0x800000; safeboot and spiffs are dropped, coredump stays (0xFF0000). A CI gate fails any build whose `firmware.bin` exceeds the slot minus 10%, 7,431,782 bytes. Upstream's 2.0.0 Picco image is 5,386,144 bytes.

3. **Section 3, third bullet.** Replace "Re-enable SD and Wi-Fi updates only when the running table has two app slots; keep them hidden otherwise." with:

   > Re-enable SD updates only when the running table has exactly two app slots and the next slot is not the running one; keep them hidden otherwise. Wi-Fi updates stay hidden until the updater points at this repo's releases: as built it fetches upstream's Picco image (`src/network/OtaUpdater.cpp:22`).

4. **Section 10, "Session steps", step 2 (Identify).** Append:

   > It also reads the security eFuses and stops if Secure Boot or flash encryption is enabled, if download mode is restricted, or if the flash is octal. It saves the stock bootloader header and stops unless the partition table matches `partitions_picco.csv` exactly.

5. **Section 10, "The device kit", first row.** Replace "Bootloader, two-slot partition table, boot data and app images" with:

   > Bootloader (0x0), two-slot partition table (0x8000), boot data `boot_app0.bin` (0x15000) and app (0x20000), never the merged `firmware.factory.bin`

6. **Section 10, "Session steps", after step 6 (optional).** Add:

   > **Prove the update path.** With the cable still attached, install `update.bin` from the SD card through Settings and confirm the device reboots into it (it now runs from the second slot). Record the serial log.

7. **Section 10, "Stop conditions and recovery".** Add a row:

   > If: identify finds Secure Boot, flash encryption, restricted download mode, octal flash, or a partition table that differs from `partitions_picco.csv`. Then: stop. Nothing has changed. Report the output in the issue.

8. **Section 13, risk table.** Add a row:

   > Risk: the generic Arduino bootloader does not boot this unit (security eFuses, flash type or voltage). Likelihood: low. Consequence: device does not boot until restored. Mitigation: identify-step eFuse and header checks; same bootloader already runs upstream's other ESP32-S3 octal-PSRAM boards; restore from backup over ROM download mode.

## What only the device session can tell us

- Whether this unit's partition table is byte-for-byte the one in `partitions_picco.csv`.
- The security eFuses: Secure Boot, flash encryption, download-mode restrictions, USB-Serial/JTAG disable.
- Flash type (quad vs octal) and voltage, PSRAM type and voltage, and chip revision.
- Onyx's bootloader header (mode, frequency) and ESP-IDF version.
- Whether the Arduino `dio_80m` bootloader and our app actually boot.
- Whether Arduino keeps or erases the stock NVS on first boot (the serial log shows either way).
- The real `firmware.bin` size of the build that is flashed (also known from D4 once the toolchain installs).
- Whether esptool can reset the Picco into download mode reliably after our firmware runs, and whether a manual way into download mode (the GPIO0 strap) is reachable on this hardware if a broken image ever takes the USB port away.
- That an SD update actually switches slots on this unit (`app0` to `app1`) and boots.

## Consequences for issues D4, D5, D6

- **D4:**
  - Move the proposed CSV to `partitions_picco_2slot.csv`.
  - Add `picco_2slot`: as `picco` but without `post:scripts/app_only_upload.py`, with `board_build.partitions = partitions_picco_2slot.csv` and `board_upload.arduino.boot_app0 = 0x15000`, verified by `envdump`.
  - Gate `firmware.bin` at 7,431,782 bytes.
  - Replace the `isPicco()` hide with the pure slot policy and its host tests, and add a running-slot guard for the SD path.
  - Keep Wi-Fi update hidden unless the release URL changes.
- **D5:**
  - `flash-plan.json` lists exactly 0x0 `bootloader.bin`, 0x8000 `partitions.bin`, 0x15000 `boot_app0.bin` and 0x20000 `firmware.bin`, with SHA-256 for each.
  - Write with `--flash-mode/--flash-freq/--flash-size keep`. Refuse 0xE000, any write overlapping 0x9000-0x14FFF, any image longer than its region, and `firmware.factory.bin`.
  - `identify` adds the eFuse, security and stock-table checks above.
  - `restore` writes the backup at 0x0 with `keep` and verifies by read-back hash.
- **D6:**
  - The checklist gains the new identify stop conditions and an optional "prove SD update" step while USB is attached.
  - It states that `update.bin` is the plain `firmware.bin` (any `*.bin` name, picked from Settings > SD Card Firmware Update).
