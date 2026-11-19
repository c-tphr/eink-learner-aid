---
name: Device session results
about: Return the results of the device session (flash, controls, benchmark, guided acceptance) from docs/DEVICE_SESSION.md
title: "Device session results"
labels: ["area:device"]
---

<!--
This repository is PUBLIC.

NEVER attach or paste:
  - picco-backup-*.bin, or its .json (the backup holds the Picco's settings area, possibly
    Wi-Fi details, and Onyx's firmware);
  - ANY .bin file (identify-*.bin, *.read1.bin, *.read2.bin, update.bin, the kit's images);
  - the work folder, a ZIP of it, or the files in work\logs (they hold full paths and the full MAC).
Keep those on your PC and on your USB stick or second disk only.

Attach ONLY:
  - every file from /rsvp-bench/ on the Picco's SD card (*.json; ignore *.tmp);
  - the serial logs work\serial-<time>.log (search them for your Wi-Fi network's name first);
  - the summary work\session-summary-<time>.txt made by
    `powershell -ExecutionPolicy Bypass -File .\session.ps1 summary`.
If GitHub refuses a .json or .log file, put them in one ZIP (with no .bin inside) and attach that.

Copy the answers from the results sheet (Appendix E of docs/DEVICE_SESSION.md).
-->

Instructions: docs/DEVICE_SESSION.md

## How far did it get?

- [ ] 1. Prepare: setup finished (`Private Python ready.`)
- [ ] 2. Identify said `RESULT: PASS`
- [ ] 3. Backup said `BACKUP VERIFIED`, I typed the confirmation, and both backup files are on a USB stick or second disk
- [ ] 4. Dry run said `DRY RUN RESULT: READY`, and I typed the phrase
- [ ] 5. Flash said `DONE: all four images written and verified.`
- [ ] 6. The new firmware started and showed the Home screen
- [ ] 7. Controls check done
- [ ] 8. SD-card update done (optional)
- [ ] 9. Display benchmark done
- [ ] 10. Guided acceptance done
- [ ] 11. Profile chosen
- [ ] The session was split into two sittings

## Steps 1 to 3 (read-only)

- Kit version:
- Onyx firmware version noted in step 1:
- Identify result (PASS, STOP, ALREADY CONVERTED, not reached), and anything the Picco's screen did:
- Backup: size in bytes, whether the two reads matched, time for the two reads (the file itself NEVER goes here):
- Backup copied to a second place and its SHA-256 checked (yes / no):

## Steps 4 to 6: flash and first boot

- Dry run result:
- Did the flash complete (`DONE`)? Time it took:
- If it did not complete: what the window showed, and the restore result:
- First boot: did the Home screen appear, after how long, was the power button needed?
- Panel from the serial log line `[EPD] Picco panel` (YRD0426, DEPG0397, OPM040B3, SE0400NQW47):
- Version shown in Settings:
- `running` slot from the `[UPD] self-update allowed:` line:
- How the books were copied (File Transfer or card reader), and whether the SD card can be taken out:

Flash output from `Writing (do not unplug the Picco until this says DONE)` to `DONE` (optional):

```

```

## Step 7: Controls check

- Which physical key showed `BTN_UP`, and which `BTN_DOWN`:
- Did any key show `BTN_CONFIRM` (GPIO5)? Which?
- Did a short power press register without sleeping?
- Was the 0.7 s hold comfortable?
- Did anything fire without being pressed?
- Settings changed (Layout (reader only), Short click), with the new values:

## Step 8: SD-card update (optional)

- Done, skipped, or failed (with the message):
- `running` slot after the update (expected `0x800000`):

## Step 9: Display benchmark

- Benchmark file(s) attached (`NNNN-bench.json`):
- Room temperature and battery at the start:
- Profiles run, and any not run with the reason shown:
- Profiles with heavy residue, or rated "Unreadable, skip rest of profile":
- Did the last frame stay unchanged until you pressed? Was 15 s per rating enough?

## Step 10: Guided acceptance

- Acceptance file attached (`NNNN-acceptance.json`):
- Part A: delivered WPM at 240 and at 600; automatic check; your answers:
- Part B: failed or skipped steps, and what you saw:
- Were the on-screen instructions clear enough to follow alone? How long did it take?

## Step 11: Choose a profile

- Choice file attached (`NNNN-choice.json`):
- Profile chosen, and trials kept:

## Files attached

- [ ] Every file from `/rsvp-bench/`
- [ ] The serial log(s) `serial-<time>.log`, checked for my Wi-Fi network's name
- [ ] The summary `session-summary-<time>.txt`
- [ ] I attached no `.bin` file, no backup, no `work` folder and nothing from `work\logs`

## How it felt (open questions)

- Touch targets: easy to hit? Near the edges?
- Did a grip or palm touch pause speed reading by mistake? How often?
- Moving the highlight: quick enough? Is the highlight easy to see after many updates?
- Slide switch: which direction is "on"?

## PC and tool

- Windows version, PowerShell version:
- COM port; did it change after a restart?
- Antivirus, network or other problems with the tool:

## Anything that went wrong

What happened, what you did, and what the screen or window showed:
