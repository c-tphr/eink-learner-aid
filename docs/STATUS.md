# Project status

The orchestrator keeps this file current. A new session should be able to resume from this file, the GitHub issues and the pull requests alone.

## Where we are

_Updated 2026-11-19._

- **Milestones:** M0, M1 and M2 complete. M3 complete (P1, P2a, P2b, P3, P4, P6; plus P7 fixes). M4: libraries merged (R1, R2, R3); **R4a in progress**, then R4b, R5. M5: libraries merged (C1a, C1b); C2 to C5 follow R5. M6: D4 and D5 merged, the early-check sheet merged, **A0 in progress**; D1, D2, D3a, D3b, D6 open. M7: P5 merged (the real book is READY); A1 open.
- **Merged since the last update:** #38 P5 (PR #74), fork preamble in `AGENTS.md` (#76), #75 P7 (#77), #8 E3 (#78), #34 D4 (#79).
- **Assigned, in progress:**
  - #21 R4a: RsvpActivity playback and pause screen, control map, word index on demand, seven scenarios (branch `task/21-rsvp-activity`).
  - #37 A0: release workflow and kit packaging (branch `task/37-release-workflow`).
- **Next when dependencies merge:** after R4a: #22 R4b, #30 D1; then #23 R5, #26 C2, #31 D2, #32 D3a. #36 D6 needs D3b. #72 housekeeping any time.
- **Blockers:** none.
- **Waiting on the owner:**
  1. Run the read-only early check on the Picco: `docs/DEVICE_EARLY_CHECK.md`. Return only the summary file.
  2. Optional: try the simulator in a Codespace (`docs/SIMULATOR.md`); it has never run in a real Codespace.

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

- 2026-11-19: The checker runs as a subagent of the orchestrator's session (implementers must not create cloud sessions); for long books it writes its JSON reply to the round's `checker-reply.txt` and returns only a summary.
- 2026-11-19: Real book: Common Sense is not a standalone Standard Ebooks title; the collection *Essays* is used unmodified and prepared in full, because the preparation rules allow excluding only front and back matter.
- 2026-11-19: Control scheme from S4 adopted (short press moves the highlight, 0.7 s hold activates; hold a side key in page view to enter RSVP); bindings in `src/controls/ControlMap.h`.
- 2026-11-19: CI is pinned to `ubuntu-24.04` (GCC 15 on the newer image breaks two files; issue #72). Upstream workflows are restricted to the upstream repository.

- 2026-11-19: D4: `picco_2slot` builds the four images (boot data at 0x15000); its upload target refuses; the app image is not reproducible between builds, so a kit and its plan hashes must come from one build. Wi-Fi updates look at this repo's latest published release for `eink-learner-aid-<tag>-picco2.bin` and install only a higher `MAJOR.MINOR.PATCH`; the first release must be above 2.0.0. The boot recovery picker is enabled on the two-slot table only.
- 2026-11-19: P7: `validate` has a 13th check (`exclusions`); instructions are version 1.1 (already prepared books stay valid); the checker writes its result through one guarded write. The guarded write has not yet been observed in a real Claude Code run (needs a session started after the merge); a checker that does not deliver through it fails the round.
- 2026-11-19: E3: the simulator models update time through `DisplayProfile` (540 ms update, 1,960 ms clear by default; `CROSSPOINT_SIM_UPDATE_MS` / `CROSSPOINT_SIM_CLEAR_MS`, 0 = none); a real `docs/device/picco-timings.json` is compiled in at build time when present.

## Issue map

| ID | Issue | Milestone | State |
| --- | --- | --- | --- |
| B0 | #1 | M0 | merged |
| F1 | #2 | M0 | merged |
| F2 | #3 | M0 | merged |
| F3 | #4 | M0 | merged |
| F4 | #5 | M0 | merged |
| E2 | #6 | M1 | merged |
| E1 | #7 | M1 | merged |
| E3 | #8 | M1 | merged |
| S1 | #9 | M2 | merged |
| S2 | #10 | M2 | merged |
| S3 | #11 | M2 | merged |
| S4 | #12 | M2 | merged |
| P1 | #13 | M3 | merged |
| P3 | #14 | M3 | merged |
| P2a | #15 | M3 | merged |
| P2b | #16 | M3 | merged |
| P6 | #49 | M3 | merged |
| P4 | #17 | M3 | merged |
| R1 | #18 | M4 | merged |
| R2 | #19 | M4 | merged |
| R3 | #20 | M4 | merged |
| R4a | #21 | M4 | in progress |
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
| D4 | #34 | M6 | merged |
| D5 | #35 | M6 | merged |
| D6a | #60 | M7 | merged (early read-only check sheet) |
| D6 | #36 | M6 | open |
| A0 | #37 | M6 | in progress |
| P5 | #38 | M7 | merged |
| A1 | #39 | M7 | open |
| A2 | #40 | M8 | open |
| H1 | #72 | M7 | open (housekeeping) |
| P7 | #75 | M7 | merged |

## Facts learned so far

- Base: upstream `develop` at `490813ff`, `freeink-sdk` at `3f3e4c11`, pioarduino platform 55.03.311, Arduino core 3.3.11. Details in `docs/BASE.md`.
- Upload hazard: upstream's comment says the `picco` upload writes only the app, but reading the build scripts shows `scripts/app_only_upload.py` runs too late to remove the bootloader (0x0), partition table (0x8000) and `boot_app0` (0xE000, inside stock NVS) from the upload command. The plan's hazard note stands. F2 confirms with `-t envdump`. Never run an upload target.
- `simulator_picco` is not defined in upstream's `platformio.ini`; it comes from the simulator repo's sample configuration. F2 adds it, pinned.
- Host tests upstream are CMake and GoogleTest under `test/`: 924 pass at the pinned commit (about 5 minutes to build on 4 cores).
- Cloud builds: run `./scripts/cloud-setup.sh` first in every session (about 3 minutes from empty caches, seconds afterwards). A plain `pio run` on empty caches fails with `CERTIFICATE_VERIFY_FAILED` because pioarduino replaces the CA settings; worse, its `idf_tools.py` falls back to unverified downloads, which the setup script treats as a failure. Building a `custom_sdkconfig` environment (upstream `default`) in the same core directory triggers a reinstall that hits the TLS problem again; re-run the setup script. `picco` builds in about 5.5 minutes: flash 5,449,518 B of the 7,431,782 B two-slot budget, RAM 34.4%.
- Upload hazard confirmed by `pio run -e picco -t envdump`: `UPLOADERFLAGS` holds 0x0, 0x8000 and 0xe000 images although `FLASH_EXTRA_IMAGES` is empty.
- Host suite on `main`: 1,280 tests. Python: `tools/prep` 686 tests, `tools/device` 190, `tools/simtest` 81 plus 6 scenarios. Project CI (`.github/workflows/project-ci.yml`, `docs/CI.md`) runs all of them plus the `picco`, `picco_2slot`, `x4pro` and simulator builds on every pull request; read its result with `gh api repos/c-tphr/eink-learner-aid/commits/<sha>/check-runs`.
- The session disk is small: finished implementers' working copies keep build output under `.claude/worktrees/agent-*/.pio` and `build`; delete those folders (not the working copies of running tasks) when `df` shows little free space.
- Simulator: `pio run -e simulator_picco` applies `scripts/simulator_patches/` automatically; headless run: `xvfb-run -a .pio/build/simulator_picco/program` with `CROSSPOINT_SIM_INPUT_SCRIPT="0:WAIT:Entering activity: Home;1500:SHOT:home;2500:QUIT"` (SHOT writes a BMP at the given path).
- Upstream renderer quirks found by the parity test (not ours, not fixed): the last word of a table caption is never drawn; rows of a grid table split across pages are drawn line by line. Neither affects RSVP positions or anchors.
- The flash tool has never run on Windows or on a device; `docs/DEVICE_EARLY_CHECK.md` is the first real run. Never post backups or `.bin` blobs: the repo is public.
- GitHub Actions is enabled; upstream's "PR Formatting" workflow fails on every pull request here until F3 replaces it.
- The cloud VM pauses when the session is idle, so wall-clock durations are unreliable; scripts time with `/proc/uptime`.
- Network: all ten allowlisted hosts and the five upstream repos respond; no host refused so far. F2's smoke test gives the complete list.
- Pushing `task/<n>-<slug>` branches works. Merged branches are not deleted (setting is off and sessions cannot delete branches).
- Implementers' working copies live under `.claude/worktrees/` (ignored locally through `.git/info/exclude`).

## How to resume

1. Read this file, then `docs/PLAN.md` (the Change log at its top lists what the spikes changed).
2. List open issues and open pull requests. Validate any open pull request yourself (check out the branch, re-run its acceptance commands, read Project CI's checks), then pick the next issue whose dependencies are merged.
3. Run `./scripts/cloud-setup.sh` before any `pio` command.
4. After a container restart, background implementers are stopped but their working copies under `.claude/worktrees/agent-*/` survive: check `git status` in each, and resume the implementer with a message, or commit and push its work-in-progress yourself. Ask implementers to push work-in-progress early.
5. The preparation checker for a real book is run by the orchestrator as a fresh subagent: instructions are the body of `.claude/agents/prep-checker.md`, input is only `WORK/checker`, the reply goes to `WORK/rounds/NN/checker-reply.txt`.
