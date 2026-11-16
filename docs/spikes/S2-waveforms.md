# S2 spike: SSD1677 waveform tables and how the SDK injects them

Issue #10. A code-reading finding. Nothing here was run on a device.

**Pinned commits.** Firmware `583c08ad` (CrossPoint `490813ff` + project docs). FreeInk SDK submodule `3f3e4c1126e2cb9a278c2c1076fb4a3334ffdc30`. Line numbers refer to these commits.

**Path shorthand.** `D/` = `freeink-sdk/libs/display/FreeInkDisplay/`. `BC` = `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h`. Firmware paths are relative to the repo root.

**Evidence labels.** Every claim is one of these:

- **Code**: cited at file:line.
- **Inferred**: reasoned from cited code. The reasoning is given.
- **Datasheet**: general SSD1677 or e-paper knowledge that cannot be cited from the repo. The confidence is given. No vendor datasheet site was consulted, because they are not on the network allowlist.

**Reproducing the numbers.** `python3 docs/spikes/s2_lut_times.py` reads the table bytes from the SDK headers and prints every frame count and duration in this document. `--c` also prints the proposed trial tables as C arrays.

## Summary

1. **The Picco has four known panels, not two controllers.** The boot probe picks one of them:
   - YRD0426, DEPG0397 or OPM040B3, all on the SSD1677 driver, each with its own stock waveforms;
   - SE0400NQW47 on the "SSD2677" driver.

   If none matches, the probe falls back to the generic SSD1677 driver with Sticky settings. The result is in `BoardConfig::ACTIVE.displayControllerVariant` (0..4), and the controller name is in `display.controllerName()`.
2. **Waveforms can be switched at run time without forking the SDK, but not in the way the plan says.** `Ssd1677Config` is chosen once and kept as a `const&` in a singleton. However, `FreeInkDisplay::setCustomLUT(true, table)` is public and makes the next FAST refresh run a host-supplied 110-byte table (CTRL2 `0xCC`). The firmware needs one pass-through line in the upstream file `lib/hal/HalDisplay.h`.
3. **There is no band update on the Picco.** Every update sends the full 800x480 frame. Unchanged pixels sit idle under the differential waveform. Waveform time does not depend on the changed area. The only exception is that on SSD2677 the area decides which table runs.
4. **The stock Picco reading waveforms differ by panel:**
   - DEPG0397: host table of 19 frames at 20 ms = **380 ms**.
   - YRD0426 and OPM040B3: the OTP display-mode-2 waveform, duration unknown.
   - SE0400 (SSD2677): an uploaded 535-byte table in an undocumented format, duration unknown.

   The X4 figure of 540 ms does not carry over.
5. **Trials A, B and C** (300, 220 and 160 ms) shorten the DEPG reading waveform `lut_picco_depg_custom_fast`. They keep each panel's stock voltages, drive unchanged pixels at 0 V, and are only used on the three SSD1677 panels.
6. **Trial D is reachable on all four known panels, SSD2677 included.** It is the stock BOOX "mode 3" UI waveform, which the vendor already ships for small interface updates:
   - DEPG: 160 ms.
   - YRD/OPM: about 80 ms, assuming frame-rate code 1 means 40 ms per frame.
   - SE0400: duration unknown.
7. **The plan's pace predictions leave out the SPI transfer.** Two planes of 48,000 bytes at 20 MHz take at least 38.4 ms. With that included, the ceilings are about 151, 188 and 232 WPM for trials A, B and C, not 167, 214 and 273.

## 1. Driver selection, initialisation, and SSD1677 versus SSD2677 detection

**Selection (Code).**

1. `FreeInkDisplay::begin()` calls `selectDriver()` (`D/src/FreeInkDisplay.cpp:257-260`).
2. On a Picco build, `selectDriver()` picks `ssd2677Driver()` if `BoardConfig::isPicco() && piccoProbePanel() == PiccoSe0400nqw47`. Otherwise it falls through to `ssd1677Driver()` (`D/src/FreeInkDisplay.cpp:214-221` and `232`).
3. The firmware reaches this through `HalDisplay::begin()` → `einkDisplay.begin()` (`lib/hal/HalDisplay.cpp:14-21`).

**Probe (Code).** `piccoProbePanel()` (`D/src/driver/Ssd1677Driver.cpp:266-288`) works as follows:

- It bit-bangs CMD `0x2E` (the 10-byte user ID) on the display pins before SPI starts (`:143-195`, `:271`).
- It classifies the ID as YRD0426, DEPG0397 or OPM040B3 (`:243-251`).
- If no ID matches, it reads CMD `0x70` and treats the reply `07 01 01` as SE0400NQW47, which is the stock "SSD2677" path (`:274-281`).
- It caches the result, writes it to `BoardConfig::ACTIVE.displayControllerVariant` (`:282`), and logs `[EPD] Picco panel <name>, ID ...` when Serial is up (`:283-286`).
- The enum values are `PiccoUnknown=0, PiccoYrd0426=1, PiccoDepg0397=2, PiccoOpm040b3=3, PiccoSe0400nqw47=4` (`D/src/driver/Ssd1677Driver.h:201-207`).

**Configuration per panel (Code).** `ssd1677ActiveConfig()` returns `ssd1677PiccoConfig()` for `Board::Picco` (`Ssd1677Driver.cpp:1312`). That function switches on the probe result (`:313-318`):

| Panel | Config | Source line |
| --- | --- | --- |
| YRD0426 | `piccoStockConfig(0x01, nullptr)` | `:301` |
| DEPG0397 | the `depg` lambda | `:302-310` |
| OPM040B3 | `piccoStockConfig(0x03, lut_picco_opm_image)` | `:311` |
| Unknown | `ssd1677StickyConfig()`, a generic config with no Picco waveforms | `:317` |

The driver is a singleton built with that config (`:1332-1335`) and holds it as `const Ssd1677Config& _cfg` (`Ssd1677Driver.h:170`). `begin()` resets the controller, runs the BOOX init list (border `0x3C=0x01` for YRD/DEPG at `:431`; OPM ends with `0x3C=0x80` at `:456-457`), and allocates the driver's own previous-frame buffer `_piccoOld` in PSRAM (`:401-410`).

**Where the result can be read at run time.**

- `BoardConfig::ACTIVE.displayControllerVariant`, a public `inline BoardProfile ACTIVE` (`BC:2263`, field at `BC:883-886`). This is the only place that tells the three SSD1677 panels apart.
- `display.controllerName()` (`lib/hal/HalDisplay.h:18`) → `FreeInkDisplay::getControllerName()` (`D/src/FreeInkDisplay.cpp:255`) → `"SSD1677"` (`Ssd1677Driver.h:88`) or `"SSD2677"` (`D/src/driver/Ssd2677Driver.h:34`). The About screen already shows this (`src/activities/settings/AboutActivity.cpp:143`).
- **Do not use `HalDisplay::getController()`.** It returns `ACTIVE.displayController`, which the Picco profile fixes at `SSD1677` (`BC:1816`). Nothing changes it after the probe (Inferred: no assignment to it outside XteinkDetect, per a grep of the SDK and firmware).

`PiccoPanel` lives in a private SDK header behind `FREEINK_DEVICE_PICCO`. `DisplayProfile` should keep its own copy of the values 0..4, with a comment pointing to `Ssd1677Driver.h:201-207`.

## 2. Refresh modes, the waveform each one uses, and what "fast refresh on the word band" means

**Public modes (Code).** `FULL_REFRESH`, `HALF_REFRESH` and `FAST_REFRESH` (`D/include/FreeInkDisplay.h:34`, `lib/hal/HalDisplay.h:31-36`), plus grayscale passes. RSVP uses no grays (plan section 6).

**How CTRL2 bits are read in this document (Code).**

| Bit | Meaning | Source |
| --- | --- | --- |
| `0xC0` | clock and analog on | `Ssd1677Driver.cpp:690` |
| `0x20` | load temperature | `:670` |
| `0x10` | load the OTP LUT | `:611` |
| `0x08` | display mode 2 | `:1158`, "MODE_SELECT" |
| `0x04` | display start | `:1158` |
| `0x03` | analog and clock off | `:694` |

**How the stock sequences are built (Code).** The driver clears `0x03` when the caller keeps power (`:617`) and clears `0xC0` while the rails are up (`:618`). That is why the host test expects `0x0C` or `0x1C` for consecutive fast updates (`D/test/host/test_picco.cpp:706-739`). FAST refreshes write `CTRL1=0x00` so the RED plane acts as "old" and the update is differential. FULL and HALF write `CTRL1=0x40` (bypass RED), which makes them absolute (`:566-567`).

| Panel | FULL | HALF | FAST while reading (`setReaderRefresh(true)`) | FAST in the UI |
| --- | --- | --- | --- | --- |
| YRD0426, OPM040B3 (SSD1677) | OTP mode 1 with temperature, `0xF7` (`:255`, `:600-603`) | OTP `0xD7`, temperature forced to `0x5A` (`:612-615`) | **OTP mode 2**, `0xDF`; `readerFastLut` is null with `piccoStockGrayscale` (`:579-580`, `:599`) | `lut_picco_fast`, 2 frames, `0xCC` (`:291`, `:584-586`, `:611`) |
| DEPG0397 (SSD1677) | `lut_picco_depg_full`, 48 frames = 960 ms, `0xE7` (`:303`, `:611`) | the same table, `0xC7` (`:262`) | `lut_picco_depg_custom_fast`, 19 frames = **380 ms**, `0xCF` (`:307`) | the same table, `0xCC` (`:303`, `:308`) |
| SE0400NQW47 ("SSD2677") | `lut_picco_se0400_full` (535 bytes) | same as FULL | `lut_picco_se0400_fast` (`D/src/driver/Ssd2677Driver.cpp:219-221`) | `lut_picco_se0400_mode3` if no more than 1/8 of the frame bytes changed, otherwise `_fast` (`:200-207`) |
| Unknown | Sticky config, OTP `0xF7` | OTP `0xF7` | OTP `0xFF` | OTP `0xFF` |

**What the reader actually uses (Code).**

- Book pages are drawn inside `ReaderRefreshScope`, which calls `setReaderRefresh(true)` (`src/activities/reader/ReaderUtils.h:245-250`, used at `src/activities/reader/EpubReaderActivity.cpp:2145`).
- `displayWithRefreshCycle` sends FAST and every N-th page HALF (`ReaderUtils.h:180-192`). The default N is 15 (`src/CrossPointSettings.h:334`).
- Small UI updates are FAST outside the reader scope, so they get the "UI" column above. The first FAST after leaving the reader scope still uses the reading waveform (`Ssd1677Driver.cpp:577-578`; `Ssd2677Driver.cpp:196-197`).

**What "CrossPoint's existing fast refresh on the word band" means in API terms.**

1. Draw the band into the full framebuffer.
2. Call `renderer.displayBuffer(HalDisplay::FAST_REFRESH)` (`lib/GfxRenderer/GfxRenderer.cpp:1779-1784`). This passes `fadingFix` as `turnOffScreen`.
3. That call goes through `HalDisplay::displayBuffer` (`lib/hal/HalDisplay.cpp:62-68`) to `FreeInkDisplay::displayBuffer` (`D/src/FreeInkDisplay.cpp:683-713`). The firmware is built single-buffer (`platformio.ini:42`).

**There is no usable sub-rectangle update (Code).**

- `FreeInkDisplay::displayWindow` is marked EXPERIMENTAL (`D/include/FreeInkDisplay.h:280-281`) and `HalDisplay` does not expose it.
- On the Picco SSD1677 path it becomes a full-frame `displayImpl(..., Fast, ...)` call (`Ssd1677Driver.cpp:927-930`).
- On SSD2677 it is the default `display(..., Fast, ...)` (`D/src/driver/PanelDriver.h:61-64`).
- The firmware never calls it. A grep finds only a comment at `lib/GfxRenderer/GfxRenderer.cpp:404`.
- Every FAST update writes both 48,000-byte planes: BW from `fb`, RED from `_piccoOld` (`Ssd1677Driver.cpp:794-802`, `:878`, `:896-897`). SSD2677 sends 96,000 bytes of packed 2-bit pixels (`Ssd2677Driver.cpp:218`, `:226-250`).

**Area does not change update time.**

- *Inferred:* the waveform length is fixed by the table's frame counts, and the SPI volume is always the full frame.
- *Datasheet, high confidence:* the gate scan covers every row each frame whatever RAM window is set.
- Plan section 2 quotes the same behaviour from X4 measurements.
- The one exception is SSD2677 outside the reader scope, where the share of changed bytes picks `mode3` or `fast` (`Ssd2677Driver.cpp:200-207`). Area therefore changes which table runs there.

## 3. How tables are supplied, and switching them at run time

**Build-time injection, as the plan claims, is only partly right (Code).**

- `Ssd1677Config` is the per-board injection struct: `fastLut`, `fullLut`, `halfLut`, `readerFastLut`, the CTRL2 sequence overrides, the grayscale LUTs and flags (`Ssd1677Driver.h:21-79`).
- A whole config can be injected at build time with `-DFREEINK_SSD1677_CONFIG=yourConfig` (`Ssd1677Driver.cpp:1286-1293`). That **replaces** `ssd1677ActiveConfig()`, including the Picco per-panel probe switch.
- On the Picco the config is chosen at **run time** from the panel ID (`:313-318`), out of `static const` objects (`:301-311`).
- After construction the driver holds it as `const Ssd1677Config&` (`Ssd1677Driver.h:170`) in a function-static singleton (`:1332-1335`).

So the struct cannot gain run-time alternatives. Editing a `static const` object through `const_cast` would be undefined behaviour.

**The run-time path that exists: the custom-LUT hook (Code).**

1. `FreeInkDisplay::setCustomLUT(bool enabled, const unsigned char* lut)` is public (`D/include/FreeInkDisplay.h:309`, `D/src/FreeInkDisplay.cpp:1225-1229`). It forwards to `PanelDriver::setCustomLut`.
2. `Ssd1677Driver::setCustomLut(true, data)` writes the table immediately: 105 bytes to CMD `0x32`, `data[105]` to `0x03` (VGH), `data[106..108]` to `0x04` (VSH1, VSH2, VSL) and `data[109]` to `0x2C` (VCOM). It then sets `_customLutActive` (`Ssd1677Driver.cpp:1245-1271`). `setCustomLut(false, ...)` only clears the flag (`:1246-1249`).
3. While the flag is set, a FAST refresh skips the board-LUT and Picco sequence paths (`:584`, `:609`, `:630`, `:668`). It reaches the generic path and fires `CTRL2=0xCC` (`0xCF` if `turnOff`): clock and analog on, mode 2, display, **no OTP reload** (`:703-712`). The frame is still sent in full and stays differential against `_piccoOld` (`:794-802`, `:896-897`).
4. The flag stays set after that refresh (`:721`, where `boardLut` is false).
5. On SSD2677, `setCustomLut` is the `PanelDriver` default no-op (`PanelDriver.h:290-294`; `Ssd2677Driver` does not override it). A trial table sent there silently runs the stock waveform, which is safe but would make any rating meaningless.

**What the firmware is missing.** `HalDisplay` keeps `EInkDisplay einkDisplay` private (`lib/hal/HalDisplay.h:138-139`) and has no LUT method. D1 or D2 needs one pass-through in an upstream file, `lib/hal/HalDisplay.h`, listed in the pull request as a registration-point edit:

```cpp
void setCustomLut(bool enabled, const unsigned char* lut = nullptr) { einkDisplay.setCustomLUT(enabled, lut); }
```

**Alternatives considered and rejected.**

- `FREEINK_SSD1677_CONFIG`: build-time only, and it discards the per-panel probe.
- A firmware driver subclass: `selectDriver()` hard-codes the singletons (`FreeInkDisplay.cpp:214-232`).
- Direct command writes: `EpdBus _bus` is private (`FreeInkDisplay.h:435`) and has no accessor.
- Forking the SDK: not needed.

**Least invasive per-update sequence (Inferred from the code paths above).**

```
display.waitRefreshComplete();             // setCustomLUT does NOT drain a pending async refresh (FreeInkDisplay.cpp:1225-1229)
display.setCustomLut(true, trialTable);    // ~115 bytes of SPI
renderer.displayBuffer(HalDisplay::FAST_REFRESH);
display.setCustomLut(false);               // flag only; no bus traffic
```

Re-load the table before every update and clear the flag afterwards. Otherwise any refresh that the driver promotes while the flag is set, or any clearing refresh, would leave the stock path:

- FAST is promoted to HALF or FULL after grayscale content or the first paint (`:805-808`, `:816-833`, `:849-854`).
- With the flag set, FULL becomes `0x34`-based (OTP reload, `:697-698`) and HALF becomes `0xD4` (`:702`). On DEPG that would skip `lut_picco_depg_full`.

With the flag cleared, the next stock refresh rewrites its own table and voltages: board LUT on DEPG and on YRD/OPM UI, OTP on YRD/OPM reading.

## 4. SSD1677 LUT format and the drive time of every SDK table

**Layout (Code).** Documented in the SDK at `D/src/driver/PaperMonoDriver.cpp:108-115` and used by `setCustomLut` (`Ssd1677Driver.cpp:1254-1268`):

| Bytes | Content |
| --- | --- |
| 0..49 | 5 LUT entries (L0..L4) x 10 groups. Each byte holds 4 phases A..D at 2 bits, phase A in the top bits. |
| 50..99 | 10 groups x {TP_A, TP_B, TP_C, TP_D, RP}. TP is in frames; a group runs RP+1 times. |
| 100..104 | Frame-rate codes, one nibble per group. |
| 105..109 | VGH, VSH1, VSH2, VSL, VCOM. |

Supporting detail:

- Voltage selectors: `00` = 0 V, `01` = VSH1 (+15 V, toward black), `10` = VSL (−15 V, toward white), `11` = VSH2 (+5 V) (`PaperMonoDriver.cpp:39-41`, `:56-58`).
- The RP semantics are confirmed by `lut_factory_quality`, whose comment says "50 waveform frames": its groups are 24 + 23 + 3 frames, each with RP = 0 (`D/src/lut/Ssd1677Luts.h:129-147`).
- The Picco stock "image/gray" tables are 105 bytes with no voltage tail (`Ssd1677Luts.h:274-322`).

**LUT entries and transitions in a B/W differential refresh (Inferred).** RED holds the old frame, BW the new one, and 0 is black. The entries map as:

| Entry | Transition |
| --- | --- |
| L0 | black stays black |
| L1 | black to white |
| L2 | white to black |
| L3 | white stays white |

Evidence:

- The host test requires L1 (byte 10) and L2 (byte 20) of the DEPG table to carry the 17 driven frames of changing pixels (`D/test/host/test_picco.cpp:61-72`).
- L0 carries the "final black top-up" with VSH1 (`Ssd1677Luts.h:242`, byte 0 = `0x01`).
- L1 uses VSL and L2 uses VSH1 in both stock fast tables, which matches the polarity above.

**Frame rate.** The high nibble of byte `100+g/2` belongs to the even group g.

- Code 2 = 50 Hz and code 4 = 100 Hz, from a vendor comment where only group 0 is in use and the high nibble changes (`freeink-sdk/docs/metalio-waveform-source.md:26-27`). That comment also shows the nibble order.
- Code 8 = 5,000 µs per frame, "the fastest the controller offers" (`PaperMonoDriver.cpp:43-45`).
- These three points fit **f = 25 Hz x code**. Code 1 (40 ms) and code 3 (13.3 ms) are extrapolated from that fit (*Inferred, medium confidence*). Code F is outside the documented range and its duration is left unknown.

**Drive time.**

```
T = Σ over groups of (TP_A + TP_B + TP_C + TP_D) × (RP + 1) × (1000 / (25 × FR_code)) ms
```

T counts waveform time only. The measured update time adds SPI (at least 38.4 ms for 2 x 48,000 bytes at 20 MHz), the LUT upload (about 115 bytes, negligible), a power-up when the rails were off (`0xC0`, duration unknown) and rendering.

| Table (`Ssd1677Luts.h` lines) | Origin | Groups: frames | FR | Total |
| --- | --- | --- | --- | --- |
| `lut_picco_depg_custom_fast` (242-255) | SDK edit of BOOX slot 1 | (8+1+9+1)x1 = 19 | 0x22, 20 ms | **380 ms** (17 driven + 1 rest + 1 black top-up) |
| `lut_picco_depg_fast_slot1` (227-240) | BOOX 2959 DEPG mode 2 | (17+0+1+0)x1 = 18 | 20 ms | 360 ms (unused by the SDK) |
| `lut_picco_depg_fast` (256-269) | BOOX 2959 DEPG mode 3 (UI) | (7+0+1+0)x1 = 8 | 20 ms | **160 ms** (unused by the SDK) |
| `lut_picco_depg_full` (213-226) | BOOX DEPG slots 0 and 3 | (5+7)x2 + (5+7)x2 = 48 | 20 ms | 960 ms, DC-balanced |
| `lut_picco_fast` (199-212) | BOOX YRD/OPM slot 2 (UI) | (2+0+0+0)x1 = 2 | 0x18; group 0 = code 1 | **80 ms** if code 1 = 40 ms (assumed) |
| `lut_fast_metalio` (172-192) | Metalio vendor | (3+16+1+1)x1 = 21 | 20 ms | 420 ms |
| `lut_factory_quality` (127-147) | X4 OEM | 24 + 23 + 3 = 50 | 20 ms | 1,000 ms |
| `lut_picco_depg_image` (300-308) | BOOX | 7 + 7 = 14 | 20 ms | 280 ms |
| `lut_picco_depg_gray` (313-321) | BOOX | 8 | 20 ms | 160 ms |
| `lut_picco_opm_image` (288-296) | BOOX | 30 + 60 + 16 = 106 | codes 3, 2, 3 | about 1,813 ms (assumed) |
| `kX4ProTextSsd1677` (`D/src/lut/X4ProTextAaLuts.h:6-14`) | X4 Pro firmware | 8 + 20 = 28 | code 3 | about 373 ms (assumed) |
| `lut_grayscale`, `_sticky`, `_metalio` (11-125) | X4 community SDK | 4 + 4 + 4 = 12 | 0x8F (codes 8, F) | unknown (code F) |
| `lut_picco_yrd_opm_gray` (275-283) | BOOX | 8 + 8 + 4 + 4 = 24 | 0x8F | unknown (code F) |

**Checking against timings stated in the code.** No Picco refresh time is measured or commented anywhere in the repo or the SDK.

- The SDK timings that do exist are for other panels, and they run OTP waveforms, not these tables:
  - X4 FULL about 1,800 ms and partial `0xFC` about 500 ms (`Ssd1677Driver.cpp:65-67`).
  - X4 `0x1C` DU about 77 ms (`:329`).
  - X4 HALF 1,720 ms (`lib/hal/HalDisplay.h:34`).
  - Waveshare 3.97 B/W wait 407 ms and 24 ms of SPI (`freeink-sdk/docs/waveshare-epaper-397-support.md:115-118`).
  - Sticky fast 610 ms of waveform plus about 40 ms of SPI (`freeink-sdk/docs/esp-idf.md:80-81`). That SPI figure, on the same 800x480 SSD1677 class at 20 MHz, supports the 38.4 ms lower bound used here.
- The only table-time cross-check is Paper Mono: 64 frames = 320 ms at code 8 (`PaperMonoDriver.cpp:452-453`). That is consistent with 5 ms per frame.

**SSD2677 tables.** These are 535 bytes, sent with CMD `0x20` (`Ssd2677Driver.cpp:76-82`, `:276-280`), and described only as "UltraChip-style" (`Ssd2677Driver.h:3-7`). The repo has no layout description and no datasheet for them, so their durations cannot be computed.

## 5. Completion detection, timing points, and the calling task

**BUSY wait (Code).**

- SSD1677 BUSY is active-high (`Ssd1677Driver.h:92`). SSD2677 BUSY on the Picco is active-low (`Ssd2677Driver.h:36-39`).
- After `MASTER_ACTIVATION` the driver calls `bus.waitRefreshComplete("refresh")` (`Ssd1677Driver.cpp:622`, `:649`, `:682`, `:720`; `Ssd2677Driver.cpp:272`).
- `EpdBus::waitRefreshComplete` (`D/src/bus/EpdBus.cpp:392-475`) proceeds like this:
  1. It sleeps the task on a semaphore released by a BUSY edge interrupt.
  2. If BUSY does not assert within 20 ms, the command was a no-op and it returns (`:448-455`).
  3. It gives up after 30 s (`:466-468`).
  4. It prints `Wait complete: refresh (N ms)` when Serial is up (`:472-474`).
- If a busy-wait slice hook is installed, the wait polls instead (`:412-431`). The firmware installs none (grep).

**Blocking, and which task.**

- `displayBuffer` blocks until the waveform ends (Code: the paths above).
- It runs on the caller's task. Activities render on `ActivityManagerRender`: a FreeRTOS task pinned to core 1, priority 1, with an 8 KB stack (16 KB with vector fonts). The RSVP activity's `render()` will run there too (`src/activities/ActivityManager.cpp:47-90`).
- An async split exists on SSD1677. `HalDisplay::displayBufferAsync` → `displayBufferAsyncNoShadow` (`lib/hal/HalDisplay.cpp:70-76`) returns after activation, because `Ssd1677Driver::supportsAsyncDisplay()` is true (`Ssd1677Driver.h:106`, `:765-786`). `waitRefreshComplete()` then finishes it.
- On SSD2677 the async call falls back to blocking, because it uses the default `supportsAsyncDisplay() == false` (`PanelDriver.h:66-70`, `D/src/FreeInkDisplay.cpp:737-740`).
- `GfxRenderer::displayBufferAsync` also forces the blocking path when `fadingFix` is on (`GfxRenderer.cpp:1786-1795`).

**Where to time an update.**

- **Delivered-WPM meter (R2):** take `t_end` when `displayBuffer(...)` returns, using `esp_timer_get_time()` or `millis()`. That is the "panel reports it has finished" moment in plan section 6, scheduler step 1. Take `t_start` just before the call.
- **Benchmark (D3a):** on SSD1677, use the async split to separate the parts:
  - `t0` before `displayBufferAsync`.
  - `t1` on return: SPI, the LUT upload and activation.
  - `t2` after `waitRefreshComplete()`: the waveform.
- **Fallback:** record the total, and keep the SDK's `Wait complete: refresh (N ms)` lines in the serial log.
- Log the `fadingFix` setting. With it on, every update powers the analog rails off and on again (`0xCF`, or `|0x03` on the Picco path at `:617`), which adds time the tables do not show.

## 6. Proposed trial tables A, B and C

**Starting table.** `lut_picco_depg_custom_fast` (`Ssd1677Luts.h:242-255`). It is DEPG0397's everyday reading and UI waveform in this SDK.

**Origin and licence.**

- The SDK states it splits the BOOX stock 17-frame reader waveform (`lut_picco_depg_fast_slot1`) around one rest frame (`Ssd1677Luts.h:242`; checked by `test_picco.cpp:61-76`).
- The BOOX tables are described as "copied byte-for-byte from the stock firmware (build 2912)" (`Ssd1677Luts.h:196-198`).
- The SDK is MIT, `Copyright (c) 2026 FreeInk` (`freeink-sdk/LICENSE:1-3`, `freeink-sdk/NOTICE:1-4`).
- **Provenance caveat:** the underlying bytes were extracted from Onyx's proprietary firmware. The SDK distributes them under MIT, but that does not change where they came from.
- Only the following bytes carry over into the trials:
  - the two selector bytes `0x88` and `0x44`;
  - the frame-rate code `0x22`;
  - the voltage tail, which is the panel's operating point.

  All timing bytes are new. No table from any other repository was used.

**Construction.** `trial()` in `docs/spikes/s2_lut_times.py` builds each table as follows:

1. **Keep** the selector rows: L1 = `0x88` (VSL in phases A and C) and L2 = `0x44` (VSH1 in phases A and C).
2. **Drop** the black top-up: set L0 to `0x00` and TP_D to 0. Unchanged pixels then receive 0 V.
3. **Shorten** phases A and C symmetrically. Keep the one-frame rest (phase B) for A and B; drop it for C.
4. **Keep** the frame rate at `0x22` (20 ms; vendor-documented 50 Hz) and RP at 0.
5. **Keep** the voltage tail of the panel's own shipped fast table:
   - DEPG: `17 41 A8 32 30`, from `lut_picco_depg_custom_fast`.
   - YRD and OPM: `17 41 A8 34 30`, from `lut_picco_fast` (`Ssd1677Luts.h:211`).

YRD and OPM have no host-side reading table (theirs is in OTP). Their only shipped host table, `lut_picco_fast`, has the same selector shape, a single VSL/VSH1 drive with idle unchanged pixels. The YRD/OPM trials are therefore that UI waveform lengthened, at the panel's own voltages. They are not a transplant of DEPG's analog settings.

| Trial | TP A, B, C, D | Frames | Total time | Driven frames (changing pixels) | Compared with the stock DEPG reader (17 driven) |
| --- | --- | --- | --- | --- | --- |
| A | 7, 1, 7, 0 | 15 | 15 x 20 = **300 ms** | 14 (280 ms) | 82 % |
| B | 5, 1, 5, 0 | 11 | 11 x 20 = **220 ms** | 10 (200 ms) | 59 % |
| C | 4, 0, 4, 0 | 8 | 8 x 20 = **160 ms** | 8 (160 ms) | 47 % |

Byte-exact C arrays: `python3 docs/spikes/s2_lut_times.py --c` prints `lut_rsvp_trial_{a,b,c}_{depg0397,yrd0426_opm040b3}`. D2 should generate or copy them from there, and add a host test that re-reads the SDK header and checks that the carried-over bytes still match.

**Every trial drives at least as long as a waveform the vendor already ships for that panel.**

- C has 8 driven frames. The DEPG BOOX UI table `lut_picco_depg_fast` has 7.
- On YRD/OPM the BOOX UI table drives 2 frames at code 1, about 80 ms. C drives 160 ms.

So no trial is weaker than vendor practice. They all sit between each panel's UI and reading waveforms.

**Pace ceilings, one word per update, 60 ms dwell (computed by the script).**

| Profile | Plan's arithmetic | With ≥ 38.4 ms SPI |
| --- | --- | --- |
| Stock DEPG | 136 WPM | 125 WPM |
| Trial A | 167 WPM | 151 WPM |
| Trial B | 214 WPM | 188 WPM |
| Trial C | 273 WPM | 232 WPM |

A 240 WPM target caps all of these at 240.

**Expected visual cost (Datasheet and general e-paper knowledge, medium confidence).**

- Less drive leaves transitions short of saturation. New black text looks grey, and erased pixels keep a faint image of the previous word.
- Optical response is not symmetric, so residue builds up over chained updates. That is why the trials need clearing refreshes, and lower limits for B and C.
- In the Paper Mono lab measurement (`PaperMonoDriver.cpp:61-64`), the ±15 V rails covered the full optical swing in about 10 frames x 5 ms = 50 ms. That was a different glass, so it is weak evidence only, but even C at 160 ms is not far below plausible saturation.
- The tables are not temperature-compensated: CTRL2 `0xCC` has no `0x20` bit (`Ssd1677Driver.cpp:710`). Neither is the stock DEPG table (`0xCF`). Below about 15 °C contrast will drop. The plan already records room temperature.

**DC balance (Inferred from the tables; Datasheet for the risk model, medium-high confidence).**

- A changing pixel gets +N frames of VSH1 going white to black and −N frames of VSL going black to white. A pixel can only alternate between the two, so its net DC never exceeds one transition, and it is zero after every pair.
- Unchanged pixels get 0 V. That is why the stock DEPG top-up is dropped: it adds +1 VSH1 frame to every static black pixel on every update.
- At RSVP rates (about 3 updates per second) under the **Stock** DEPG profile, that top-up gives static black pixels such as the focus ticks about 300 one-sided frames, 6 s at +15 V, over a 300-update endurance run. This is shipped behaviour, but it is worth knowing.
- The border (`0x3C=0x01`, `Ssd1677Driver.cpp:431`) follows a row of the loaded LUT (`Ssd1677Driver.h:45-48`). *Datasheet, medium confidence:* it follows L1, so the border gets a one-sided VSL pulse on every update under Stock as well. The trials shorten that pulse. They do not create it.

**What could plausibly damage or stress a panel, and how the proposal avoids it (Datasheet and general knowledge).**

1. **Rails above the panel's rating.** Avoided: the voltage bytes are copied unchanged from that panel's shipped fast table.
2. **Persistent DC on static pixels**, which causes image retention and electrochemical wear. Avoided: no top-up, symmetric drive, and bounded runs with clearing. The DEPG clearing table is balanced at 24+24 frames per entry (script output).
3. **Frame rates outside the controller's range.** Avoided: the vendor's own 50 Hz.
4. **Leaving trial tables resident.** Avoided: the flag is cleared after every update, and every pause or exit runs a stock clearing refresh.
5. **The wrong panel.** Avoided: A, B and C are enabled only for variants 1, 2 and 3. Unknown (0) and SE0400 (4) are disabled, and `setCustomLut` is a no-op on SSD2677 anyway.
6. **Inverted (night) mode.** On DEPG the dark-background path rewrites RED as the complement of the target, so **every** pixel is driven on every update (`Ssd1677Driver.cpp:879-889`). Under a trial table that would put DC on the whole screen. Trials must refuse to run while `display.isInverted()`, or RSVP must force normal polarity.

## 7. Trial D: a built-in non-flashing mode

**SSD1677 OTP.**

- The OTP waveforms are reached with the `0x10` reload bit and the `0x08` mode-2 bit.
- On YRD/OPM, OTP mode 2 **is** the stock reading waveform (`0xDF`, `Ssd1677Driver.cpp:599`). It is not a separate A2 mode.
- On DEPG, an OTP mode-2 refresh cannot be issued through the public API. Board LUTs always take priority (`:584`, `:609-611`). With a custom LUT active, FAST uses `0xCC`, and only HALF or FULL reload OTP, as mode 1.
- *Datasheet, medium confidence:* SSD1677 OTP holds only display modes 1 and 2, with no dedicated A2 or DU slot.

**What is reachable: the BOOX "mode 3" UI waveforms.** The stock firmware ships these for small interface updates.

| Panel | Trial D | Time | How |
| --- | --- | --- | --- |
| DEPG0397 | `lut_picco_depg_fast`, BOOX mode 3 (`Ssd1677Luts.h:256-269`), unused by the SDK | 8 frames x 20 ms = **160 ms** | `setCustomLut(true, lut_picco_depg_fast)`, with its stock tail `17 41 BA 32 04` |
| YRD0426, OPM040B3 | `lut_picco_fast`, BOOX slot 2 (`:199-212`) | 2 frames, **about 80 ms** if code 1 = 40 ms | The stock UI path: FAST outside `ReaderRefreshScope` (`Ssd1677Driver.cpp:579-586`). For determinism it is better loaded explicitly with `setCustomLut` (same bytes, same `0xCC`), so the `_afterReader` one-shot (`:577-578`) cannot interfere. |
| SE0400NQW47 (SSD2677) | `lut_picco_se0400_mode3`, BOOX build 2922 slot 2 (`Ssd2677Luts.h:83-119`) | unknown | The stock UI path: FAST outside the reader scope, while no more than 1/8 of the frame bytes change (`Ssd2677Driver.cpp:200-221`). Rails stay on (`:220`). The area condition is a heuristic RSVP cannot force; a large word may fall back to the reader table. The benchmark must log this. |

**Visual cost (Inferred).** These are the weakest vendor waveforms, built for highlight moves. Expect the most ghosting, especially from the 2-frame YRD/OPM table.

**Safety (Inferred).** They use vendor voltages and vendor timing, and SSD2677 needs no unknown table format. On DEPG the mode-3 table keeps its one-frame black top-up (+1 frame per update on static blacks, as discussed in section 6).

**What cannot be known without the device:**

- the YRD/OPM OTP mode-2 timing;
- the meaning of FR code 1, which sets whether `lut_picco_fast` is 80 ms;
- every SE0400 duration;
- whether the Sticky-style OTP latch (`0xE0`/`0xE6`/`0xA5`, `Ssd2677Driver.cpp:133-146`, Sticky only) would work on the Picco SE0400. It is private and gated to the Sticky in any case.

## 8. Safety hooks: clearing refreshes and the previous-frame buffer

**Requesting a clearing refresh (Code).** Clear the custom-LUT flag, then call `renderer.displayBuffer(HalDisplay::HALF_REFRESH)`. `GfxRenderer::promoteNextRefresh` is an alternative (`lib/GfxRenderer/GfxRenderer.h:243`, applied in `GfxRenderer.cpp:1773-1784`). On the Picco, HALF is absolute (`CTRL1=0x40`) and rewrites both RAM planes from the frame (`Ssd1677Driver.cpp:567`, `:873-876`):

| Panel | HALF clearing waveform |
| --- | --- |
| DEPG | `lut_picco_depg_full`, 960 ms, DC-balanced |
| YRD/OPM | OTP `0xD7`, duration unknown |
| SE0400 | `lut_picco_se0400_full` (`Ssd2677Driver.cpp:221`) |

FULL is the alternative, with temperature load on YRD/OPM.

**Chained partial updates (Code).** The Picco SSD1677 driver ignores the facade's `prev` and keeps its own `_piccoOld`:

- It is written into RED before every FAST update (`:794-802`, `:896-897`).
- It is replaced with the displayed frame after every synchronous refresh (`:915`), or in `displayFinish` for async ones (`:773`).
- It is seeded white at `begin()` (`:407`).

SSD2677 keeps `_old` the same way (`Ssd2677Driver.cpp:159-161`, `:222`). The differential baseline therefore never drifts, however many updates are chained. What builds up is physical residue on the glass, and only an absolute HALF or FULL refresh removes it. That is exactly the plan's 60/120 clearing policy.

The driver promotes FAST to HALF or FULL by itself in these cases:

- the first paint after `begin()` (`:816-833`);
- after grayscale content (`:805-808`, `:849-854`);
- after `requestResync()`, which `HalDisplay::begin` issues on some wake reasons (`lib/hal/HalDisplay.cpp:31-35`).

RSVP should start playback with its own clearing refresh, with the custom LUT off, so none of these promotions lands under a trial table.

**Proposed lower limits for trials** (the plan allows lower limits). These are judgement, to be settled in D2:

| Profile | Clear at paragraph end after | Clear unconditionally after |
| --- | --- | --- |
| Trial A | 60 updates | 120 updates (as Stock) |
| Trial B | 45 updates | 90 updates |
| Trial C | 30 updates | 60 updates |
| Trial D | 30 updates | 60 updates |

## 9. What a simulator backend and a portable fake must mirror

RSVP code only needs the following from `DisplayProfile`:

1. **`panel()`**: `{controllerName, variant}`, with variant 0..4 as in section 1, plus `Simulator` and `Fake` values. The simulator should report a configurable variant (default DEPG0397) so the D2 gating tests can run on the host.
2. **`available(ProfileId)`**: Stock is always available.

   | Profile | Available when |
   | --- | --- |
   | Stock | always |
   | A, B, C | variant ∈ {1, 2, 3}, not inverted, and enabled in the device-test menu |
   | D | variant ∈ {1, 2, 3, 4}, not inverted, and enabled |

3. **`updateBand(rect)`**: the framebuffer already holds the frame. The call blocks until the panel has finished and returns `{startUs, endUs}`. `rect` is used for logging and the simulator only, because the device always sends the full frame.
4. **`clear()`**: a HALF refresh with the custom LUT released. It blocks and returns the same timing struct.
5. **`enter(ProfileId)` and `leave()`**:
   - `enter` runs a clearing refresh.
   - `leave` releases the LUT and runs a clearing refresh.
   - The device backend calls `display.setCustomLut(false)` from both, and enters or leaves `ReaderRefreshScope` for Stock.
6. **No clearing-policy counters in the backend.** They belong to portable RSVP logic (R2/D2), so the fake and the simulator need none.

The simulator waits the configured time per update (E3). The fake advances a fake clock and records the call sequence (profile, update, clear, enter, leave), so host tests can check the safety rules:

- clear on pause;
- the 60/120 limits;
- the LUT is never left set across a clear.

## Recommended DisplayProfile design

- **One `DisplayProfile` with three backends.** The device backend (D1/D2) is a thin layer over `HalDisplay` and `GfxRenderer`. The simulator backend waits (E3). The fake has a fake clock. The interface is the one in section 9.
- **Stock profile.** FAST full-frame updates inside `ReaderRefreshScope`, using the panel's reading waveform. Clearing is HALF.
- **Trials A, B and C.** `waitRefreshComplete` → `setCustomLut(true, table[variant])` → FAST → `setCustomLut(false)`. Use the per-panel tables from section 6. Trials are allowed only on variants 1 to 3 and only when the display is not inverted.
- **Trial D.** The per-panel BOOX mode-3 waveform, as in section 7.
- **Timing.** Capture start and end around each call. The benchmark uses the async split on SSD1677 to separate SPI from waveform time.
- **Results file.** Record the panel variant name, not only the controller.
- **The one upstream edit.** A `setCustomLut` pass-through in `lib/hal/HalDisplay.h`.

## Proposed changes to docs/PLAN.md

Exact replacements. Section 1 is untouched.

1. **Section 2, table row "Panel".**
   - Replace: "controller SSD1677 or SSD2677, detected at boot"
   - With: "one of four panels, identified at boot from the panel's user ID (CMD 0x2E) and, failing that, CMD 0x70: YRD0426, DEPG0397 or OPM040B3 on the SSD1677 driver, or SE0400NQW47 on the stock "SSD2677" driver. Each has its own stock waveforms (spike S2)."
2. **Section 2, open questions, first bullet.**
   - Replace: "Which controller this unit carries (SSD1677 or SSD2677). The SDK drives SSD2677 with the panel's built-in waveforms on another device, so custom waveforms may not load on it."
   - With: "Which panel this unit carries (YRD0426, DEPG0397, OPM040B3 or SE0400NQW47; logged at boot as `[EPD] Picco panel ...`). On the SE0400NQW47 the SDK uploads an undocumented 535-byte waveform on every refresh, so trial tables A to C cannot be designed for it."
3. **Section 6, display profiles table.**

   | Row | Change |
   | --- | --- |
   | Stock | "Word update": "CrossPoint's fast refresh of the full frame (only the word band changes), using the panel's reading waveform". "Predicted pace": "DEPG0397: about 380 ms waveform plus at least 40 ms transfer, about 125 WPM; other panels unknown until measured". |
   | Trial A | Pace "About 151 WPM" |
   | Trial B | Pace "About 188 WPM" |
   | Trial C | Pace "About 232 WPM" |
   | Trial D | "Word update": "The panel's stock non-flashing UI waveform (BOOX mode 3); 160 ms on DEPG0397, about 80 ms on YRD0426/OPM040B3, unknown on SE0400NQW47" |

   Note under the table: "Predictions include the at least 38 ms needed to send two 48 KB planes at 20 MHz".
4. **Section 6, "How trial waveforms are supplied."**
   - Replace the paragraph with: "The SDK's `Ssd1677Config` is chosen once from the panel ID and cannot change at run time. Trial tables are loaded per update through the SDK's public `setCustomLUT()`, exposed by one pass-through in `HalDisplay`. The SDK is not forked."
5. **Section 6, "Safety limits."**
   - Replace: "are disabled automatically if the unit reports an SSD2677 controller"
   - With: "Trials A to C run only on the three SSD1677 panels. Trial D runs on any recognised panel because it uses only stock waveforms. All trials are disabled on an unrecognised panel and while the screen is inverted."
   - Add: "Trial tables keep the panel's stock voltages and leave unchanged pixels undriven."
6. **Section 10, benchmark: the sentence "Each results file also records the controller model".**
   - Replace "the controller model" with "the panel name and controller model".
7. **Section 13, risk "No trial waveform is clean, or the unit has an SSD2677".**
   - Replace with: "No trial waveform is clean, or the unit has the SE0400NQW47 panel (only Stock and Trial D available)".

Item 5 changes what D2's acceptance check "SSD2677 path disables trials" covers: it would then apply to A to C only. The orchestrator should confirm this, since D2 states it as written.

## What only the device session can tell us

- Which of the four panels this unit has, and the probe's ID bytes. The serial log at boot shows them.
- Real update and clearing times for Stock on every panel. YRD/OPM run an OTP waveform, and the SE0400 tables cannot be decoded.
- Whether frame-rate code 2 really means 20 ms per frame on this glass. If Trial A measures about 300 ms plus transfer, the arithmetic in this document holds.
- Whether frame-rate code 1 means 40 ms, which sets the YRD/OPM Trial D time.
- How much controller power-up adds per update with `fadingFix` on.
- Readability, residue after 30, 60 and 120 chained updates, and recovery after a clearing refresh, for each trial. Also the temperature sensitivity of the uncompensated tables.
- On SE0400: which of `mode3` and `fast` real word bands trigger.

## Consequences for issues E3, D1, D2, D3a

- **E3 (#8):** keep the X4 defaults (540 and 1,960 ms). Make the simulated panel variant configurable (default DEPG0397) and use the section 9 interface. Optionally offer a "DEPG estimate" preset of 420 and 1,000 ms.
- **D1 (#30):** Stock means FAST inside `ReaderRefreshScope` and clearing means HALF. Add the one-line `HalDisplay::setCustomLut` pass-through (an upstream edit). Read the variant from `BoardConfig::ACTIVE.displayControllerVariant`. Time each update around `displayBuffer`, with the async split for benchmarks.
- **D2 (#31):** the tables in section 6 (A to C per SSD1677 panel) and section 7 (D per panel). Gate on variant and inversion, re-load the table before each update and clear it after, use the lower limits in section 8, and add a host test that the table bytes match the SDK header. The acceptance check "SSD2677 disables trials" covers A to C only, if the plan change is accepted.
- **D3a (#32):** record the panel name, `fadingFix`, the SPI time, the waveform time and the total separately (async split on SSD1677). On SE0400 Trial D, also record which table ran.
