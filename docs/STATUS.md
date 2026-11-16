# Project status

The orchestrator keeps this file current. A new session should be able to resume from this file, the GitHub issues and the pull requests alone.

## Where we are

_Updated 2026-11-19._

- **Milestone:** M0 build gate met: `picco` and `simulator_picco` build in a cloud session (F1, F2 merged). CI (F3) is in progress and GitHub Actions is running. M2: S1 to S3 done, S4 in progress. M3: P1, P2a, P2b, P3, P6 done; P4 in progress. M4: R1, R2, R3 done (portable libraries). M5: C1a, C1b done. M6: D5 and the early-check sheet (D6a) done.
- **Merged:** #1 (PR #41), #2 F1 (#42, merge commit), #13 P1 (#43), #10 S2 (#45), #14 P3 (#46), #11 S3 (#47), #9 S1 (#48), plan change (#50), #18 R1 (#51), #15 P2a (#52), #24 C1a (#53), #19 R2 (#54), #3 F2 (#55), #49 P6 (#56), #25 C1b (#57), #35 D5 (#59), #60 D6a (#61), #16 P2b (#62), #20 R3 (#63)
- **Assigned, in progress:** #4 F3 (CI), #5 F4 (flag, skeletons, first ESP32 compile of RsvpCore and CheckpointCore), #12 S4 (control map), #6 E2 (scenario harness), #7 E1 (sim.sh, devcontainer), #17 P4 (/prep-book, checker)
- **Next when dependencies merge:** #8 E3 (needs F4, E2), then #21 R4a (needs E3, S4, E2), #34 D4 (needs F3), #38 P5 (needs P4), #30 D1 (needs R4a)
- **Blockers:** none hard. Soft: `usc1.contabostorage.com` is not allowlisted (see below).
- **Waiting on the owner:**
  1. Add `usc1.contabostorage.com` to the cloud environment's allowlist. It is a download mirror of the PlatformIO registry: `dl.registry.platformio.org` redirects to it or to `github.com` at random. Setup usually succeeds by retrying, but can fail; re-running `./scripts/cloud-setup.sh` is the workaround.
  2. Run the read-only early check on the Picco: `docs/DEVICE_EARLY_CHECK.md` (setup, identify, backup, summary). Return only the summary file.
  3. Enable "Automatically delete head branches" (merged task branches accumulate; sessions cannot delete them).

## Last decisions

- 2026-11-16: Owner confirmed the root `PLAN.md` is authoritative and that the three-commit starting state is intended. Plan and PRD moved under `docs/`.
- 2026-11-16: F1 (#2) merged with a merge commit to keep upstream history; every other pull request is squash-merged.
- 2026-11-16: Plan tasks split further: P2 into P2a/P2b, R4 into R4a/R4b, C1 into C1a/C1b, D3 into D3a/D3b; added A0 (release workflow). E3's delivered-WPM scenario check moved to R4a, where the pause screen exists.
- 2026-11-16: F1 merged without a successful firmware build: the import is proven byte-identical to the pin by git and upstream's 924 host tests pass; the build check moved to F2 and stays in the M0 gate.
- 2026-11-16: Checkpoint format v1 details fixed by P1 (PR #43 lists 13 decisions): unknown members ignored, limits count Unicode code points, one checkpoint per section, strict integers, plain-text definition. See `tools/prep/README.md`.
- 2026-11-16: Certificate fix constraint for F2: no disabling of TLS verification and no editing of any CA bundle (a permission check refused that once; it is the owner's call). F2 pre-fetches packages in processes that still have the session's CA settings.

- 2026-11-17: Spike decisions, now in `docs/PLAN.md`: WordIndex is its own XHTML pass mapped to pages through CrossPoint's visible-text offsets (no page table, no upstream EPUB edits); the firmware word rules in `docs/spikes/S1-word-stream.md` section 5 are the contract and the Python side is aligned by P6 (#49); footnote and endnote bodies are a note region RSVP passes over.
- 2026-11-17: Display: four possible panels; trial tables A to C only on the three SSD1677 panels, loaded at run time through `setCustomLUT` via one pass-through in `HalDisplay`; Trial D is the stock mode-3 waveform, allowed on any recognised panel.
- 2026-11-17: Two-slot table adopted (`docs/spikes/partitions_picco_2slot.proposed.csv`); `boot_app0.bin` at 0x15000; flash plan refuses 0xE000, the NVS range and `firmware.factory.bin`; Wi-Fi updater is pointed at this repo's releases in D4 (section 1 says updates come from SD or Wi-Fi); the "prove the SD update" step in the device session is optional.
- 2026-11-17: R1 (#18) and C1a (#24) were started before F4 (#5): they are portable libraries proven by host tests and do not need the firmware build. F4 adds the flag and remaining skeletons around them.
- 2026-11-17: C1a rule: the firmware parser must match the Python verdict for all 107 samples; exact issue codes are required for version and cross-reference samples, coarser codes are allowed for parse and schema samples.

- 2026-11-18: F2 results: `pio run -e picco` works in cloud sessions through `scripts/cloud-setup.sh`, which pre-fetches every package in processes that keep the session's CA settings (no CA bundle edits, no disabled verification). Only `collector.platformio.org` (telemetry) is refused; nothing needs adding to the allowlist. `dl.platformio.org` and `dl.espressif.com` were never contacted.
- 2026-11-18: The simulator's newest commit (`84c3421d`) does not compile against the pinned firmware. Decision: no fork; patches live in `scripts/simulator_patches/` and are applied to the fetched library by `scripts/patch_simulator.py`, following upstream's `patch_sdfat.py` convention. The same folder takes stubs for HAL methods this project adds.
- 2026-11-18: D5 design: logic in the Python package `tools/device/piccoflash/` (testable against a fake device), `session.ps1` as a thin launcher with a pinned private Python; no force flag.
- 2026-11-18: Checkpoint behaviour fixed by C1b (PR #57, 20 decisions): re-arming is literal (previous page then next page over a section end fires again); skip is offered after Incorrect too; only `book.source_sha256`-matching state keeps position; answer-revealed is never persisted.
- 2026-11-18: Hidden, page-break and `rt` content never splits a word (tags inside are ignored), per the S1 prototype.

- 2026-11-19: Fixture layout: chapter 2's notes moved to a separate endnotes document so the three-prompt checkpoint's section ends its chapter again; chapter 1 keeps an in-chapter footnote. A section followed by note bodies does not end its chapter (resume lands on the notes; RSVP passes over them).
- 2026-11-19: R3: a word before `<br>` is a paragraph end for RSVP; a source declares intentionally skipped note words through `Token::skippedBefore` so the word-order audit stays exact.
- 2026-11-19: P2b: `report` re-runs `validate` and `verify-check` and trusts no stored result; exclusions come from the optional draft; firmware limits are mirrored in Python with a test against `CheckpointLimits.h`.
- 2026-11-19: Lesson applied to assignments: when one in-flight task changes a shared fixture, tasks that read it are not merged until it lands, and briefs name the files a task must not change.

## Issue map

| ID | Issue | Milestone | State |
| --- | --- | --- | --- |
| B0 | #1 | M0 | merged |
| F1 | #2 | M0 | merged |
| F2 | #3 | M0 | merged |
| F3 | #4 | M0 | in progress |
| F4 | #5 | M0 | in progress |
| E2 | #6 | M1 | in progress |
| E1 | #7 | M1 | in progress |
| E3 | #8 | M1 | open |
| S1 | #9 | M2 | merged |
| S2 | #10 | M2 | merged |
| S3 | #11 | M2 | merged |
| S4 | #12 | M2 | in progress |
| P1 | #13 | M3 | merged |
| P3 | #14 | M3 | merged |
| P2a | #15 | M3 | merged |
| P2b | #16 | M3 | merged |
| P6 | #49 | M3 | merged |
| P4 | #17 | M3 | in progress |
| R1 | #18 | M4 | merged |
| R2 | #19 | M4 | merged |
| R3 | #20 | M4 | merged |
| R4a | #21 | M4 | open |
| R4b | #22 | M4 | open |
| R5 | #23 | M4 | open |
| C1a | #24 | M5 | merged |
| C1b | #25 | M5 | merged |
| C2 | #26 | M5 | open |
| C3 | #27 | M5 | open |
| C4 | #28 | M5 | open |
| C5 | #29 | M5 | open |
| D1 | #30 | M6 | open |
| D2 | #31 | M6 | open |
| D3a | #32 | M6 | open |
| D3b | #33 | M6 | open |
| D4 | #34 | M6 | open |
| D5 | #35 | M6 | merged |
| D6a | #60 | M7 | merged (early read-only check sheet) |
| D6 | #36 | M6 | open |
| A0 | #37 | M6 | open |
| P5 | #38 | M7 | open |
| A1 | #39 | M7 | open |
| A2 | #40 | M8 | open |

## Facts learned so far

- Base: upstream `develop` at `490813ff`, `freeink-sdk` at `3f3e4c11`, pioarduino platform 55.03.311, Arduino core 3.3.11. Details in `docs/BASE.md`.
- Upload hazard: upstream's comment says the `picco` upload writes only the app, but reading the build scripts shows `scripts/app_only_upload.py` runs too late to remove the bootloader (0x0), partition table (0x8000) and `boot_app0` (0xE000, inside stock NVS) from the upload command. The plan's hazard note stands. F2 confirms with `-t envdump`. Never run an upload target.
- `simulator_picco` is not defined in upstream's `platformio.ini`; it comes from the simulator repo's sample configuration. F2 adds it, pinned.
- Host tests upstream are CMake and GoogleTest under `test/`: 924 pass at the pinned commit (about 5 minutes to build on 4 cores).
- Cloud builds: run `./scripts/cloud-setup.sh` first in every session (about 3 minutes from empty caches, seconds afterwards). A plain `pio run` on empty caches fails with `CERTIFICATE_VERIFY_FAILED` because pioarduino replaces the CA settings; worse, its `idf_tools.py` falls back to unverified downloads, which the setup script treats as a failure. Building a `custom_sdkconfig` environment (upstream `default`) in the same core directory triggers a reinstall that hits the TLS problem again; re-run the setup script. `picco` builds in about 5.5 minutes: flash 5,449,518 B of the 7,431,782 B two-slot budget, RAM 34.4%.
- Upload hazard confirmed by `pio run -e picco -t envdump`: `UPLOADERFLAGS` holds 0x0, 0x8000 and 0xe000 images although `FLASH_EXTRA_IMAGES` is empty.
- Host suite on `main`: 1,209 tests (924 upstream, RsvpCore, CheckpointCore, WordIndex incl. renderer parity). Python: `python3 -m pytest tools/prep -q` (617, several minutes because EPUBCheck runs) and `python -m pytest tools/device -q` (190, in a virtualenv from `tools/device/requirements-test.txt`).
- Simulator: `pio run -e simulator_picco` applies `scripts/simulator_patches/` automatically; headless run: `xvfb-run -a .pio/build/simulator_picco/program` with `CROSSPOINT_SIM_INPUT_SCRIPT="0:WAIT:Entering activity: Home;1500:SHOT:home;2500:QUIT"` (SHOT writes a BMP at the given path).
- Upstream renderer quirks found by the parity test (not ours, not fixed): the last word of a table caption is never drawn; rows of a grid table split across pages are drawn line by line. Neither affects RSVP positions or anchors.
- The flash tool has never run on Windows or on a device; `docs/DEVICE_EARLY_CHECK.md` is the first real run. Never post backups or `.bin` blobs: the repo is public.
- GitHub Actions is enabled; upstream's "PR Formatting" workflow fails on every pull request here until F3 replaces it.
- The cloud VM pauses when the session is idle, so wall-clock durations are unreliable; scripts time with `/proc/uptime`.
- Network: all ten allowlisted hosts and the five upstream repos respond; no host refused so far. F2's smoke test gives the complete list.
- Pushing `task/<n>-<slug>` branches works. Merged branches are not deleted (setting is off and sessions cannot delete branches).
- Implementers' working copies live under `.claude/worktrees/` (ignored locally through `.git/info/exclude`).

## How to resume

1. Read this file, then `docs/PLAN.md`.
2. List open issues and open pull requests.
3. Validate any open pull request (re-run its acceptance commands yourself), then pick the next issue whose dependencies are merged.
