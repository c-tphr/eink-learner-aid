---
name: Device early check
about: Return the result of the read-only early check (setup, identify, backup) from docs/DEVICE_EARLY_CHECK.md
title: "Device early check results"
labels: ["area:device"]
---

<!--
This repository is PUBLIC. Attach or paste ONLY the summary file
(work\early-check-summary-<time>.txt) made by `.\session.ps1 summary`.

NEVER attach:
  - picco-backup-*.bin, or its .json (the backup holds the Picco's settings area, possibly
    Wi-Fi details, and Onyx's firmware);
  - identify-*-bootloader.bin, identify-*-table.bin, identify-*-otadata.bin;
  - the work folder, or a ZIP of it.
Keep those on your PC and on your USB stick or second disk only.
-->

Instructions: docs/DEVICE_EARLY_CHECK.md

## How far did it get?

- [ ] Setup finished (`Private Python ready.`)
- [ ] Identify said `RESULT: PASS`
- [ ] Backup said `BACKUP VERIFIED`, and I typed the confirmation
- [ ] I copied both backup files to a USB stick or another disk

## If setup stopped

Paste the `[session] STOPPED:` line here (it contains no private data):

```

```

## Summary file

Attach `early-check-summary-....txt`, or paste its whole text between the lines below:

```

```

## Anything else

Windows version, the cable you used, what the Picco's screen did, how long the backup took:
