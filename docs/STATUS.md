# Project status

The orchestrator keeps this file current. A new session should be able to resume from this file, the GitHub issues and the pull requests alone.

_Last updated: 2026-11-16_

## Where we are

- **Milestone:** M0 Foundation
- **In progress:** #1 bootstrap (this change), then #2 F1 and #3 F2
- **Blockers:** none
- **Waiting on the owner:** enable "Automatically delete head branches" in repo settings (currently off); confirm GitHub Actions is enabled

## Last decisions

- 2026-11-16: Owner confirmed the root `PLAN.md` is authoritative and that the three-commit starting state is intended. Plan and PRD moved under `docs/`.
- 2026-11-16: F1 (#2) is merged with a merge commit to keep upstream history; every other pull request is squash-merged.
- 2026-11-16: Plan tasks split further: P2 into P2a/P2b, R4 into R4a/R4b, C1 into C1a/C1b, D3 into D3a/D3b; added A0 (release workflow). E3's delivered-WPM scenario check moved to R4a, where the pause screen exists.

## Issue map

| ID | Issue | Milestone | State |
| --- | --- | --- | --- |
| B0 | #1 | M0 | in progress |
| F1 | #2 | M0 | open |
| F2 | #3 | M0 | open |
| F3 | #4 | M0 | open |
| F4 | #5 | M0 | open |
| E2 | #6 | M1 | open |
| E1 | #7 | M1 | open |
| E3 | #8 | M1 | open |
| S1 | #9 | M2 | open |
| S2 | #10 | M2 | open |
| S3 | #11 | M2 | open |
| S4 | #12 | M2 | open |
| P1 | #13 | M3 | open |
| P3 | #14 | M3 | open |
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

- Upstream `crosspoint-reader/crosspoint-reader` `develop` head on 2026-11-16 is `490813ff`; its `freeink-sdk` submodule is at `3f3e4c11`. F1 chooses and records the pin.
- Upstream's `picco` environment now attaches `scripts/app_only_upload.py`, which is described as writing only the app slot. The plan's upload-hazard note (section 2) may be out of date; F1 records what the code does. The rule stands: never run an upload target.
- `simulator_picco` is not defined in upstream's `platformio.ini`. It comes from the simulator repo's sample configuration, so F2 adds it.
- Host tests upstream are CMake and GoogleTest under `test/`.
- Network probe (not the F2 smoke test): all ten allowlisted hosts and the five upstream repos responded.

## How to resume

1. Read this file, then `docs/PLAN.md`.
2. List open issues and open pull requests.
3. Validate any open pull request (re-run its acceptance commands yourself), then pick the next issue whose dependencies are merged.
