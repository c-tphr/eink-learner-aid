# Project status

The orchestrator keeps this file current. A new session should be able to resume from this file, the GitHub issues and the pull requests alone.

_Last updated: 2026-11-16_

## Where we are

- **Milestone:** M0 Foundation (F1 done; F2 in progress), with M2 spikes and M3 preparation work running alongside
- **Merged:** #1 bootstrap (PR #41), #2 F1 base import (PR #42, merge commit), #13 P1 checkpoint schema (PR #43)
- **Assigned, in progress:** #3 F2, #9 S1, #10 S2, #11 S3, #14 P3
- **Blockers:** the firmware build (`pio run -e picco`) fails in cloud sessions with a certificate error; F2 (#3) owns the fix (see "Facts learned")
- **Waiting on the owner (not blocking yet):** enable "Automatically delete head branches"; confirm GitHub Actions is enabled (pull request #42 triggered no workflow run although upstream's `ci.yml` runs on `pull_request`)

## Last decisions

- 2026-11-16: Owner confirmed the root `PLAN.md` is authoritative and that the three-commit starting state is intended. Plan and PRD moved under `docs/`.
- 2026-11-16: F1 (#2) merged with a merge commit to keep upstream history; every other pull request is squash-merged.
- 2026-11-16: Plan tasks split further: P2 into P2a/P2b, R4 into R4a/R4b, C1 into C1a/C1b, D3 into D3a/D3b; added A0 (release workflow). E3's delivered-WPM scenario check moved to R4a, where the pause screen exists.
- 2026-11-16: F1 merged without a successful firmware build: the import is proven byte-identical to the pin by git and upstream's 924 host tests pass; the build check moved to F2 and stays in the M0 gate.
- 2026-11-16: Checkpoint format v1 details fixed by P1 (PR #43 lists 13 decisions): unknown members ignored, limits count Unicode code points, one checkpoint per section, strict integers, plain-text definition. See `tools/prep/README.md`.
- 2026-11-16: Certificate fix constraint for F2: no disabling of TLS verification and no editing of any CA bundle (a permission check refused that once; it is the owner's call). F2 pre-fetches packages in processes that still have the session's CA settings.

## Issue map

| ID | Issue | Milestone | State |
| --- | --- | --- | --- |
| B0 | #1 | M0 | merged |
| F1 | #2 | M0 | merged |
| F2 | #3 | M0 | in progress |
| F3 | #4 | M0 | open |
| F4 | #5 | M0 | open |
| E2 | #6 | M1 | open |
| E1 | #7 | M1 | open |
| E3 | #8 | M1 | open |
| S1 | #9 | M2 | in progress |
| S2 | #10 | M2 | in progress |
| S3 | #11 | M2 | in progress |
| S4 | #12 | M2 | open |
| P1 | #13 | M3 | merged |
| P3 | #14 | M3 | in progress |
| P2a | #15 | M3 | open |
| P2b | #16 | M3 | open |
| P4 | #17 | M3 | open |
| R1 | #18 | M4 | open |
| R2 | #19 | M4 | open |
| R3 | #20 | M4 | open |
| R4a | #21 | M4 | open |
| R4b | #22 | M4 | open |
| R5 | #23 | M4 | open |
| C1a | #24 | M5 | open |
| C1b | #25 | M5 | open |
| C2 | #26 | M5 | open |
| C3 | #27 | M5 | open |
| C4 | #28 | M5 | open |
| C5 | #29 | M5 | open |
| D1 | #30 | M6 | open |
| D2 | #31 | M6 | open |
| D3a | #32 | M6 | open |
| D3b | #33 | M6 | open |
| D4 | #34 | M6 | open |
| D5 | #35 | M6 | open |
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
- Cloud build blocker: pioarduino's `builder/penv_setup.py` (`_setup_certifi_env`) overwrites the CA environment variables with its private certifi bundle, which lacks the session proxy's CA, so downloads made after the platform loads fail with `CERTIFICATE_VERIFY_FAILED`. Not a blocked host. Does not affect GitHub Actions or Codespaces.
- Network: all ten allowlisted hosts and the five upstream repos respond; no host refused so far. F2's smoke test gives the complete list.
- Pushing `task/<n>-<slug>` branches works. Merged branches are not deleted (setting is off and sessions cannot delete branches).
- Implementers' working copies live under `.claude/worktrees/` (ignored locally through `.git/info/exclude`).

## How to resume

1. Read this file, then `docs/PLAN.md`.
2. List open issues and open pull requests.
3. Validate any open pull request (re-run its acceptance commands yourself), then pick the next issue whose dependencies are merged.
