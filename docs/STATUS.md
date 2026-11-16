# Project status

The orchestrator keeps this file current. A new session should be able to resume from this file, the GitHub issues and the pull requests alone.

## Where we are

_Updated 2026-11-19 (after a container restart; see "How to resume")._

- **Milestones:** M0 complete (F1 to F4 merged, Project CI green). M1: E1 and E2 merged, E3 in progress. M2 complete (S1 to S4 merged and written into the plan). M3: P1, P2a, P2b, P3, P4, P6 merged. M4: R1, R2, R3 merged (libraries); the UI tasks start after E3. M5: C1a, C1b merged (library). M6: D5 and the early-check sheet merged; D4 in progress. M7: P5 in progress.
- **Merged since the last update:** #12 S4 (PR #66) and its plan change (#67), #6 E2 (#68), #17 P4 (#69), #5 F4 (#70), #7 E1 (#71), #4 F3 (#64).
- **Assigned, in progress (each has uncommitted work in its working copy under `.claude/worktrees/`; none had pushed a branch at the restart):**
  - #8 E3: DisplayProfile and simulator timing model (branch `task/8-display-profile-sim-timing`).
  - #34 D4: `picco_2slot` target, size gate, update policy (branch `task/34-picco-2slot-target`).
  - #38 P5: the real book (branch `task/38-prepare-real-book`). The whole Standard Ebooks collection *Essays* by Thomas Paine is prepared: 55 sections, 130 prompts; round 1 failed on the lead's own instruction to exclude six essays (withdrawn), round 2 passed; `report` says READY in the work folder `scratchpad/p5/out`. Remaining: commit, acceptance checks, pull request.
- **Next when dependencies merge:** #21 R4a (needs E3), then #22 R4b, #23 R5, #26 C2; #30 D1 (needs R4a); #37 A0 release workflow (needs D4); #36 D6 checklist; #72 housekeeping.
- **Blockers:** none.
- **Waiting on the owner:**
  1. Run the read-only early check on the Picco: `docs/DEVICE_EARLY_CHECK.md`. Return only the summary file.
  2. Optional: try the simulator in a Codespace (`docs/SIMULATOR.md`); it has never run in a real Codespace.
  3. Decide what the root `CLAUDE.md` should say for this fork. It is upstream CrossPoint's agent guide (imported with the base) and tells agents never to open pull requests, never to push without a human instruction, and to hand every firmware change to a human; that contradicts this project's workflow (plan section 11). The orchestrator follows the owner's direct instructions and the plan; a successor session will see the same conflict.

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
| E3 | #8 | M1 | in progress |
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
| D4 | #34 | M6 | in progress |
| D5 | #35 | M6 | merged |
| D6a | #60 | M7 | merged (early read-only check sheet) |
| D6 | #36 | M6 | open |
| A0 | #37 | M6 | open |
| P5 | #38 | M7 | in progress |
| A1 | #39 | M7 | open |
| A2 | #40 | M8 | open |
| H1 | #72 | M7 | open (housekeeping) |

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

1. Read this file, then `docs/PLAN.md` (the Change log at its top lists what the spikes changed).
2. List open issues and open pull requests. Validate any open pull request yourself (check out the branch, re-run its acceptance commands, read Project CI's checks), then pick the next issue whose dependencies are merged.
3. Run `./scripts/cloud-setup.sh` before any `pio` command.
4. After a container restart, background implementers are stopped but their working copies under `.claude/worktrees/agent-*/` survive: check `git status` in each, and resume the implementer with a message, or commit and push its work-in-progress yourself. Ask implementers to push work-in-progress early.
5. The preparation checker for a real book is run by the orchestrator as a fresh subagent: instructions are the body of `.claude/agents/prep-checker.md`, input is only `WORK/checker`, the reply goes to `WORK/rounds/NN/checker-reply.txt`.
