# Project status

The agent keeps this file current. A new session should be able to resume from this file, `docs/AGENT_GUIDE.md`, the GitHub issues and the pull requests alone.

## Where we are

_Updated 2026-11-17 (calendar date). Entries under "Last decisions" dated 2026-11-18 to 2026-11-20 were written earlier, with dates that ran ahead of the calendar; their order is right._

- **Direction changed by the owner (2026-11-17).** The owner flashed the Picco with release 2.0.1 and tried it: books load (also over Wi-Fi file transfer) and checkpoints in a prepared book work as intended, but RSVP reading is a poor experience on the e-ink screen. **RSVP is discontinued.** The project continues with checkpoints only, and the RSVP features and everything added only for them are to be removed (plan below).
- **One agent.** The two-agent model has ended. One agent plans, implements, verifies and merges: `docs/AGENT_GUIDE.md`.
- **Built so far:** milestones M0 to M7, under the old model. `main` still holds the product with RSVP; the device runs draft release **v2.0.1**.
- **In progress:** nothing. No other agent is running.
- **Next:** the removal plan below, step 1, once the owner has seen the plan and says go.
- **Blockers:** none.
- **Waiting on the owner:** a go-ahead for the removal plan. Nothing else. The formal device session is cancelled (see the decisions).

## Removal plan (RSVP and what was added only for it)

Presented to the owner on 2026-11-17; not started. Each step is one pull request that leaves `main` building, with green CI. The owner's seven decisions are under "Last decisions".

**Stays:** checkpoints in page view (`lib/CheckpointCore`, `src/activities/checkpoint`, `src/reading` session, loader and state file), the word index that maps a section's end to a page (`lib/WordIndex`), the control map as far as checkpoint screens and the reread bar use it (`src/controls`), the two-slot layout and updates (`lib/UpdatePolicy`, `src/update`), the preparation workflow (`tools/prep`, `/prep-book`, `prep-checker`), the flashing tool (`tools/device`: identify, backup, flash, restore), the simulator and scenario harness, CI and the release workflow.

**Goes:** RSVP reading (`lib/RsvpCore`, `src/activities/rsvp`, the position and value helpers for it in `src/reading`), the whole Device test menu (`src/activities/devicetest`: Controls check, benchmark, clearing refresh, guided acceptance and its twelve steps, Choose a profile, `/rsvp-bench/` results), display profiles and trial waveforms (`lib/DisplayProfileCore`, `src/display`, `HalDisplay::setCustomLut`, simulator patch 0004), the RSVP settings, menu rows and strings, the results schema and validator in `tools/device`, the device-session checklist and results issue template, the spike notes and evidence that are about RSVP only.

**File formats do not change:** inside a prepared book `rsvp/checkpoints.json`, the anchors `rsvp-<sid>-start|end` and the manifest id `rsvp-checkpoints`; on the device `<book>.rsvp-state.json`. Existing prepared books and saved grades keep working. A saved `mode: "rsvp"` is read as page view; saved `overrides` are ignored.

| Step | Pull request | What it does | Proof |
| --- | --- | --- | --- |
| 0 | Instructions (this one) | Single-agent model in `AGENTS.md`, `docs/AGENT_GUIDE.md`, plan section 11; this plan recorded | CI |
| 1 | Checkpoints stand alone | Every `cp_*` scenario reworked to page view only, with new page-view versions where a behaviour was shown only through RSVP (reread, restore, skip, grades). `CheckpointActivity` draws with the reader's own refresh instead of a display profile. Reread is page view only: the list becomes "Back to the question / Keep reading". No removal yet | All four checkpoint criteria pass in scenarios that never enter RSVP |
| 2 | Remove RSVP reading | Delete `src/activities/rsvp`, `lib/RsvpCore`, `RsvpPosition`, `RsvpValues`, `IndexWordSource`, the "Speed read" menu row, the long-press "Speed read" option and its seeding, the RSVP settings, strings, scenarios and host tests. `ReadingSession`, the trigger rules and the restore rules lose their RSVP branches. A device whose long-press setting says "Speed read" falls back to upstream's default | Host tests, scenarios, `x4pro` unchanged, size report |
| 3 | Remove the Device test menu and display profiles | Delete `src/activities/devicetest`, `src/display`, `lib/DisplayProfileCore`, the `HalDisplay` pass-through, simulator patch 0004 (and 0002 if nothing else needs it), the Settings rows, strings, scenarios and tests. `tools/device` loses the results schema and validator; the flashing commands stay | As step 2, plus the flashing-tool tests on Linux and Windows |
| 4 | Rename code-level names | Flag `CROSSPOINT_RSVP` to `CROSSPOINT_CHECKPOINTS`, `src/RsvpFeature.h`, `docs/ARCHITECTURE-RSVP.md`, log tags, test folder names, release notes text. No file-format name changes | Builds, scenarios, a test that the format names are unchanged |
| 5 | Documents | Rewrite `docs/PRD.md` and `docs/PLAN.md` for the checkpoint-only product; rewrite `docs/ACCEPTANCE.md`; replace the device checklist by a short install and update page (flash, SD update, restore); trim `docs/BASE.md`, `README.md`, `docs/SIMULATOR.md`, `docs/CI.md`, `docs/RELEASING.md`; delete the spike notes, evidence and pictures that are about RSVP only (git history keeps them) | Document tests, link check |
| 6 | Release 2.1.0 | Draft release from the release workflow; the owner installs `update.bin` through Settings > System > SD Card Firmware Update and uses the device. Close #40 and #110 | Workflow run, asset hashes in `docs/ACCEPTANCE.md` |

Risks to watch: the update from 2.0.1 to 2.1.0 must keep the owner's settings file readable (removed settings are skipped, not misread) and their saved grades; the page-view checkpoint hook must behave exactly as now once the RSVP branches are gone (step 1 pins it before anything is deleted); document tests read plan sections 7 and 10 and the checklist, so each is changed together with its test.

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

- 2026-11-19: A0: releases are made only by the manual workflow `.github/workflows/release-kit.yml` (`docs/RELEASING.md`). One build of `picco_2slot` with `EINK_RELEASE_VERSION`; development builds now show `2.0.0-dev-picco2`. The kit is `picco-2slot-<version>.zip`; the workflow only creates a draft. Both runs on `main` passed, including the draft-release job with attestation. Cloud sessions cannot create, edit or delete releases through the API (HTTP 403): publishing or deleting a draft is the owner's click, or a workflow step. Version 2.0.1 is used by the test draft; the next kit is 2.0.2 or higher. A `final` kit needs `docs/DEVICE_SESSION.md`.
- 2026-11-19: R4a: RSVP runs on the render task, one display call per step; entry is the first row of the reader's menu ("Speed read") or a 0.7 s side-key hold ("Page turn long press: Speed read", the default in feature builds). Leaving by Page view, Menu or Exit updates the reader's saved page; the Home gesture and sleep do not yet (R4b). A non-prose item stops playback with a note and Play continues after it (R5 shows the item). Simulator patch 0002 makes a zero-timeout mutex take non-blocking, as on the device. Firmware log lines listed in `src/activities/rsvp/README.md` are the contract for scenarios and the benchmark.
- 2026-11-19: The checker's guard hook did not run in a cloud session (first real run; issue #75): the checker judged correctly, but `save-check` refused the unguarded result, so `/prep-book` cannot reach READY there. Claude Code skips hooks in a project agent's frontmatter until the folder is trusted. Whether it runs in a local trusted session is unobserved. The committed real book is unaffected.
- 2026-11-18 (real date): Owner's decisions: the test draft release is deleted (2.0.1 is free again); the device kit will be handed over as a draft release that the owner downloads and publishes. `/prep-book` keeps its design and needs a local Claude Code session in a trusted folder; P8 (#84) added a one-minute guard probe as its first step (`python3 -m tools.prep guard-probe OUT`, the real checker, then `--check`). In a cloud session the real probe ends with `FAILED (guard-not-active)`, as intended. The "guard active" path is unobserved until someone runs it locally. `docs/PLAN.md` section 8 corrected to match.
- 2026-11-18: D1: the device backend times each refresh from the start of the frame transfer to the end of the driver's BUSY wait; `displayBuffer()` blocks on every Picco path (SDK lines in `src/display/README.md`); the clearing refresh is HALF; a call of 5 s (update) or 10 s (clear) or more counts as a timeout; statistics keep a median and 95th percentile over the last 128 calls. No upstream edit.
- 2026-11-18: H1: CI stays on `ubuntu-24.04`. On `ubuntu-latest` (GCC 15) the host tests and the simulator build now pass (StateCodec digit loop; QRCode compiled as C17 by simulator patch 0003), but the simulator's headless smoke run draws a blank screenshot there. `.github/FUNDING.yml` removed; the pull request template follows the implementer guide; `LINE_END` is a paragraph end in the test fixture helper too.
- 2026-11-18: R4b: steppers (WPM 60 to 1000 in steps of 20; 1 to 4 words per update), "Save as default" (two persisted-only settings, WPM stored in tens) and "Save for this book" (the state file's `overrides`). The position is written at every pause, once per chapter change and on every route out of RSVP, never per word. A book left in RSVP reopens in RSVP at its saved word; otherwise RSVP starts at the saved word only when it is on the page shown. Each read or write of the state file briefly allocates a 78 KB `BookState`. `book_sha256` is the checkpoint file's `book.source_sha256` (absent for a book without checkpoint data); R4b passes none, C2 will.
- 2026-11-18: D2: trials A to C are the DEPG0397 reading table with shorter frame counts (300, 220, 160 ms), loaded per word update through `HalDisplay::setCustomLut` and released straight after; Trial D is the panel's stock UI waveform. The backend itself enforces: trial waveforms only for updates marked as words, every trial run starts and ends with a clearing refresh, a hard cap per profile, and the allow rule (unlock bit, recognised panel, not inverted; the SE0400NQW47 panel gets Trial D only). Nothing is unlocked until D3a stores the unlock byte. All table bytes come from the pinned SDK (MIT), which says its Picco tables were copied from the BOOX stock firmware.
- 2026-11-18: An implementer ran copies of two repository scripts after the session's worktree guard refused the in-repo paths. No harm found (both only read files), but it worked around a safety check: rule 7 of the implementer guide now forbids it, and briefs repeat it.
- 2026-11-18: C3 split: C3a (#93) builds the loader and screens now, next to R5; C3 (#27) is the integration after C2.
- 2026-11-18: R5: at an image, table or code block RSVP hands over to page view on the item's page with a banner (Continue, the default; Stay). Continue returns to RSVP's pause screen at the first word after the item and waits for Play (plan section 6 over S4 row 15); the same playback session continues, so banner time is not in delivered WPM and the word-order audit stays one segment. A page-turn swipe on the banner is Stay plus the turn. Playback pauses when a display update times out. Not shown in the simulator: the timeout pause and an item at a chapter start (unit tests only).
- 2026-11-18: C3a: `reading::CheckpointBook::open()` finds the `rsvp-checkpoints` manifest item by scanning the OPF itself (no upstream edit), copies the checkpoint file once into the book's cache folder and indexes it (about 72.5 KB while a prepared book is open); a book without valid checkpoint data is an ordinary book. CheckpointActivity is driven by `CheckpointFlow`; grades go through `editBookState()` before the next screen; a grade's time is written only when the device clock is plausible. C2 and C3 were then merged into one task because the real screens exist.
- 2026-11-18: Orchestrator mistake, corrected: the plan wording change in #94 was merged without waiting for CI and broke a host test that reads the plan's state file example (`StateCodecTest`); fixed in #97. Rule from now on: wait for Project CI on every pull request, documents included, and do not edit the JSON example in plan section 7 without updating that test.
- 2026-11-18: The container restarts every few hours. Implementers lose background jobs and sometimes their memory of earlier work; their pushed branches survive. Briefs now ask for frequent pushes, foreground commands, and deleting build output and the working copy's `.cache` folder (1.7 GB each) when done. A pull request that conflicts with `main` gets no CI runs.
- 2026-11-19: D3a: Settings > Display > Device test holds the Controls check and the display benchmark (the sweep of plan section 10, ratings with "Repeat step" as default, results written after every step to `/rsvp-bench/NNNN-bench.json` and to the serial log; schema and validator in `tools/device`). The benchmark unlocks the trials it runs; the chosen profile stays Stock. Estimates: about 14 minutes per profile, about 50 minutes for a full sweep on DEPG0397. `idleDisplayWhileAwake()` does nothing on the Picco's drivers (the SDK's `controllerIdle()` is empty for SSD1677 and SSD2677).
- 2026-11-19: C2 and C3 (one task): `ReadingSession` holds the book's checkpoint data and decides firing through the library's rules only. RSVP stops with the section's last word shown and opens CheckpointActivity over itself; page view fires from one hook at the top of `pageTurn()`. Jumps (contents, percent, bookmarks, chapter skip, sentence steps) never fire. After a checkpoint the original mode resumes at the first word after the break; RSVP waits for Play with "Checkpoint done". Home or sleep during a checkpoint reopens before the break. Automatic page turn counts as forward reading and is switched off by a checkpoint. The state file's `book_sha256` is now the checkpoint file's hash.
- 2026-11-19: D3b: Guided acceptance = Part A (the passage at 240 and at a 600 WPM target, measured verdict stored apart from the owner's answers) and Part B (12 numbered steps the owner does in the reader, progress saved after every answer); Choose a profile writes the chosen profile and which trials stay kept (`rsvpProfilesKept`); the "Speed read display" setting appears only while a kept trial is unlocked. Results schema version 2. Estimate: about 50 minutes for the walk-through.
- 2026-11-19: D6: `docs/DEVICE_SESSION.md` is the printed checklist (twelve steps, a gate before `REPLACE ONYX`, Recovery, appendices with the walk-through's steps, a results sheet and the mapping to plan section 10) and `.github/ISSUE_TEMPLATE/device-session-results.md` the results issue. Tests keep both device sheets true: every marked command is a real `session.ps1` command, every marked quote is real tool output, links resolve, the printed walk-through equals the firmware's step table. The kit carries the pictures the sheets link to.
- 2026-11-19: D7: every command the flashing tool prints is the full `powershell -ExecutionPolicy Bypass -File .\session.ps1 ...` line (the short form fails on a PC that blocks scripts); the summary is `session-summary-<time>.txt` and covers flash and restore; the checklist's hand-typed PowerShell lines run in CI on Windows under both shells. The guard, the plan and the device calls are unchanged.
- 2026-11-19: CI's `scenarios` job runs scenarios two at a time with a 75-minute limit (38 scenarios took 27.6 minutes one at a time and reached the old 30-minute limit).
- 2026-11-20: C4: a reread starts at the section's first word in the chosen mode, with the flow kept in `ReadingSession`; no checkpoint fires during it; the section's end or "Back to the question" returns to the same prompt with the answer hidden. Orchestrator's decision in review: the page-view reread bar sits at the bottom over the reader's status row, placed from the reader's own text-area metrics, so no text is hidden with default settings (replaces S4 row 31's top bar); its button is 26 px high there.
- 2026-11-20: C5: on opening a book, `planRestore()` decides the view (active reread, else active prompt with the answer hidden, else the saved position); a restored prompt shows before any page. `BookStateFile` now recovers a complete `.tmp` when the main file is missing (a power cut between remove and rename used to lose all grades). C5 also corrected the walk-through's steps 5, 6, 9 and 10 and the checklist's Appendix A to the real behaviour.
- 2026-11-20: All six PRD criteria are mapped to passing evidence in `docs/ACCEPTANCE.md`; criterion 5 stays open until the device session.
- 2026-11-20: A1: all six PRD criteria mapped to passing evidence (`docs/ACCEPTANCE.md`); criterion 5 is open until the device session. The kit is a draft release because cloud sessions cannot create, edit or delete releases except through the workflow; publishing is the owner's step. The results issue is #110.

- 2026-11-17 (calendar date): **The owner's device results.** Identify, backup and flash worked with kit 2.0.1 (the first flash attempt failed only because it was run from the repository instead of the kit folder; the tool now says so, PR #111). Books load, also over Wi-Fi file transfer. A prepared book's checkpoint shows and takes answers as the owner expects. No results files were attached.
- 2026-11-17: **The owner's decisions on the new direction.** (1) RSVP is discontinued and removed, with everything added only for it, including the twelve-step device flow. (2) One agent from now on; it merges its own pull requests after green CI and its own checks. (3) The whole Device test menu goes. (4) Code-level names are renamed away from "rsvp"; names inside prepared books and the state file's name stay, so existing books and grades keep working. (5) The PRD and the plan are rewritten and RSVP-only documents and evidence deleted; the owner does not review the rewrite first and gets a summary. (6) No formal device session: #40 and the benchmark parts of #110 are closed, the owner uses the device and reports problems, and the simulator scenarios are the formal proof of the checkpoint criteria. (7) The next version is 2.1.0 as a draft release that the owner installs from the SD card.

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
| R4a | #21 | M4 | merged |
| R4b | #22 | M4 | merged |
| R5 | #23 | M4 | merged |
| C1a | #24 | M5 | merged |
| C1b | #25 | M5 | merged |
| C2 | #26 | M5 | merged (with C3, PR #100) |
| C3a | #93 | M5 | merged (loader and screens, split from C3) |
| C3 | #27 | M5 | merged (with C2, PR #100) |
| C4 | #28 | M5 | merged (one rework round) |
| C5 | #29 | M5 | merged |
| D1 | #30 | M6 | merged |
| D2 | #31 | M6 | merged |
| D3a | #32 | M6 | merged |
| D3b | #33 | M6 | merged |
| D4 | #34 | M6 | merged |
| D5 | #35 | M6 | merged |
| D6a | #60 | M7 | merged (early read-only check sheet) |
| D6 | #36 | M6 | merged |
| D7 | #105 | M7 | merged (flashing tool wording and summary) |
| A0 | #37 | M6 | merged |
| P5 | #38 | M7 | merged |
| A1 | #39 | M7 | done (acceptance report, draft release v2.0.1, results issue #110) |
| A2 | #40 | M8 | open |
| H1 | #72 | M7 | merged (housekeeping) |
| P7 | #75 | M7 | merged |
| P8 | #84 | M7 | merged (guard probe for `/prep-book`) |

## Facts learned so far

- Base: upstream `develop` at `490813ff`, `freeink-sdk` at `3f3e4c11`, pioarduino platform 55.03.311, Arduino core 3.3.11. Details in `docs/BASE.md`.
- Upload hazard: upstream's comment says the `picco` upload writes only the app, but reading the build scripts shows `scripts/app_only_upload.py` runs too late to remove the bootloader (0x0), partition table (0x8000) and `boot_app0` (0xE000, inside stock NVS) from the upload command. The plan's hazard note stands. F2 confirms with `-t envdump`. Never run an upload target.
- `simulator_picco` is not defined in upstream's `platformio.ini`; it comes from the simulator repo's sample configuration. F2 adds it, pinned.
- Host tests upstream are CMake and GoogleTest under `test/`: 924 pass at the pinned commit (about 5 minutes to build on 4 cores).
- Cloud builds: run `./scripts/cloud-setup.sh` first in every session (about 3 minutes from empty caches, seconds afterwards). A plain `pio run` on empty caches fails with `CERTIFICATE_VERIFY_FAILED` because pioarduino replaces the CA settings; worse, its `idf_tools.py` falls back to unverified downloads, which the setup script treats as a failure. Building a `custom_sdkconfig` environment (upstream `default`) in the same core directory triggers a reinstall that hits the TLS problem again; re-run the setup script. `picco` builds in about 5.5 minutes: flash 5,449,518 B of the 7,431,782 B two-slot budget, RAM 34.4%.
- Upload hazard confirmed by `pio run -e picco -t envdump`: `UPLOADERFLAGS` holds 0x0, 0x8000 and 0xe000 images although `FLASH_EXTRA_IMAGES` is empty.
- Host suite on `main`: 1,567 tests. Python: `tools/prep` 708 tests, `tools/device` and `scripts/tests` 353 together, `tools/simtest` 125 plus 49 scenarios. Project CI (`.github/workflows/project-ci.yml`, `docs/CI.md`) runs all of them plus the `picco`, `picco_2slot`, `x4pro` and simulator builds on every pull request; read its result with `gh api repos/c-tphr/eink-learner-aid/commits/<sha>/check-runs`.
- The session disk is small: delete build output (`.pio/build/<env>`, `build/`, `.cache`, and the same folders in old working copies under `.claude/worktrees/agent-*/`) when `df` shows little free space.
- Simulator: `pio run -e simulator_picco` applies `scripts/simulator_patches/` automatically; headless run: `xvfb-run -a .pio/build/simulator_picco/program` with `CROSSPOINT_SIM_INPUT_SCRIPT="0:WAIT:Entering activity: Home;1500:SHOT:home;2500:QUIT"` (SHOT writes a BMP at the given path).
- Upstream renderer quirks found by the parity test (not ours, not fixed): the last word of a table caption is never drawn; rows of a grid table split across pages are drawn line by line. Neither affects RSVP positions or anchors.
- The flash tool has never run on Windows or on a device; `docs/DEVICE_EARLY_CHECK.md` is the first real run. Never post backups or `.bin` blobs: the repo is public.
- GitHub Actions is enabled; upstream's "PR Formatting" workflow fails on every pull request here until F3 replaces it.
- The cloud VM pauses when the session is idle, so wall-clock durations are unreliable; scripts time with `/proc/uptime`.
- Network: all ten allowlisted hosts and the five upstream repos respond; no host refused so far. F2's smoke test gives the complete list.
- Pushing `task/<n>-<slug>` branches works. Merged branches are not deleted (setting is off and sessions cannot delete branches).
- Working copies from the two-agent model live under `.claude/worktrees/` (ignored locally through `.git/info/exclude`).

## How to resume

1. Read `docs/AGENT_GUIDE.md`, then this file, then the parts of `docs/PLAN.md` your next step touches (the Change log at its top lists what changed since the first version).
2. List open issues and open pull requests. An open pull request from an earlier session is yours to finish: check out its branch, re-run its acceptance commands, read Project CI's checks, then merge or fix it.
3. Run `./scripts/cloud-setup.sh` before any `pio` command.
4. After a container restart, background jobs are gone but pushed branches survive. Old working copies under `.claude/worktrees/agent-*/` are left over from the two-agent model and can be ignored (they are excluded through `.git/info/exclude`).
5. The preparation checker is the `prep-checker` agent (`.claude/agents/prep-checker.md`); in a cloud session its guard hook does not run, so `save-check` refuses its result (issue #75). Do not work around the guard.
6. Release runs: start `release-kit.yml` on `main` with the GitHub tool for running workflows (`gh api` cannot reach Actions paths), `publish=false` first. Read job results from the commit's check runs.
