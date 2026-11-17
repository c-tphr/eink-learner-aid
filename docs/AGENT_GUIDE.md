# Agent guide

How work is done in this repository. One agent does all of it: planning, implementation, verification and merging. This page replaces the earlier two-agent model (an orchestrator that assigned issues to implementers and validated their pull requests), which the owner ended on 2026-11-17.

Read `docs/STATUS.md` first, then the sections of `docs/PLAN.md` that your change touches.

## The project in one paragraph

This is a fork of the CrossPoint e-reader firmware for the BOOX Picco (ESP32-S3, 480x800 e-ink). It adds retrieval-practice checkpoints at section breaks of prepared books, a Python workflow that prepares books (`tools/prep`), a simulator with scripted scenarios (`tools/simtest`), and a guarded flashing tool (`tools/device`). The owner has the only device, and it runs this firmware. The agent cannot touch the device: everything is proven in host unit tests and the simulator, and the owner tries releases on the device.

## One agent

- Do the work yourself, in one session. Do not hand implementation, review or validation to other agents or sessions, and do not create cloud sessions or scheduled tasks for it.
- The one exception is part of the product: the `prep-checker` agent that `/prep-book` runs to check a prepared book in a fresh context (`docs/PREPARING_BOOKS.md`). It checks books, not code.
- Because nobody else checks your work before it merges, you check it as a second person would. The section "Before you merge" is not optional.

## Hard rules

1. **Never run a PlatformIO `upload` target**, never open a serial port, and never write anything that flashes a device outside the guarded tool under `tools/device/`.
2. **Do not copy code** from `mreza-yousefi/crosspoint-reader-picco` (noncommercial licence), or from any fork whose licence you have not confirmed is MIT-compatible (KatiePoint, crossplay, crosspoint-rsvp, inkflow). Read for ideas, then write your own.
3. **Section 1 of the plan and `docs/PRD.md` are the owner's.** If a decision there blocks you or looks wrong, stop and ask; do not work around it or reinterpret it. Change them only on the owner's instruction, and say in the pull request which instruction.
4. **Add, don't rewrite.** Project code lives in the directories named in plan section 4. Edits to upstream files are limited to registration points and are listed in `docs/BASE.md`.
5. **Stay in scope.** No unrelated edits, no drive-by refactors, no upstream syncs.
6. If the plan states a fact that the code contradicts, do not silently follow either. Correct the plan by pull request, with the reason, before work that depends on it.
7. **Never work around a refusal.** If the session's safety checks refuse a command (a permission prompt, a guard, a blocked network host), do not reword it, copy the script somewhere else, or wrap it to get past the check. Run the plain form the message asks for. If that is refused too, stop that step and tell the owner the exact refusal.
8. **Never post device backups or `.bin` files**, and never disable TLS verification or edit a CA bundle. The repository is public.

## Workflow

- **Issues and status.** Work is tracked in GitHub issues and `docs/STATUS.md` (where we are, what is open, the last decisions, how to resume). Keep `docs/STATUS.md` current: your context does not outlast the session, and the repository is the project's memory.
- **One change, one branch, one pull request.** In a cloud session, use the branch the session is given; otherwise `task/<issue number>-<slug>` cut from the latest `main`. The pull request body follows `.github/PULL_REQUEST_TEMPLATE.md`: `Closes #<n>`, what changed, upstream files touched, each acceptance command with its real output, evidence, and what you could not do or verify.
- **Tests come with the change.** Host tests for portable logic, a simulator scenario for anything the user sees.
- **Report honestly.** "Could not verify X because Y" is a good report. Say plainly when something is proven only in the simulator.

## Before you merge

You merge your own pull requests, without waiting for the owner. Squash-merge, and only when all of this is true:

1. **CI is green on the pull request's final commit**, for every pull request, documents included (some tests read documents). A pull request that conflicts with `main` gets no CI runs: merge `main` into the branch first.
2. **You ran the acceptance commands yourself on the final tree** and pasted the real output, not a summary from memory.
3. **You read your own diff from top to bottom** as a reviewer would, against the plan and the issue, looking for what is missing as well as what is wrong.
4. **For anything the user sees, you opened the simulator screenshots** and compared them with the intended design.
5. **Edits to upstream files are in `docs/BASE.md`**, and builds without the feature flag are unchanged (`pio run -e x4pro`, no project symbols).
6. The pull request text says what is not proven.

Then tell the owner what merged, in plain language, with what was verified and what was not.

## When to stop and ask the owner

Only for: a decision in plan section 1 or the PRD; a change in scope; a blocked network host (report each refused host: only the owner can change the allowlist); anything that publishes outside the repository or cannot be undone (publishing a release, deleting data); or an approach that has failed twice. Batch questions into one message, say what is blocked and what is not, and carry on with the unblocked work.

## Releases and the device

- Releases are made only by the manual workflow `.github/workflows/release-kit.yml` (`docs/RELEASING.md`): run it with `publish=false` first. It creates a draft; publishing is the owner's step. A cloud session cannot create, edit or delete a release by any other route.
- The owner installs a new version from the draft: `update.bin` on the SD card (card reader or the firmware's Wi-Fi file transfer), then Settings > System > SD Card Firmware Update.
- Returning the device to Onyx's firmware needs the owner's backup and `session.ps1 restore`. Never ask for the backup file.

## Standard commands

| Purpose | Command |
| --- | --- |
| Cloud session setup (before any `pio` command) | `./scripts/cloud-setup.sh` |
| Host unit tests | `cmake -S test -B build/test && cmake --build build/test -j4 && ctest --test-dir build/test --output-on-failure -j4` |
| Firmware builds | `pio run -e picco`, `pio run -e picco_2slot` (then `.github/scripts/firmware-size.sh picco_2slot <log> --gate`), `pio run -e x4pro` |
| Simulator build | `pio run -e simulator_picco` |
| Simulator scenarios | `python3 -m tools.simtest run <scenario>` or `--all --jobs 2`; harness tests `python3 -m pytest tools/simtest -q` |
| Preparation-tool tests | `python3 -m pytest tools/prep -q -rs` |
| Flashing-tool and kit tests | `python -m pytest tools/device scripts/tests -q -rs` in a virtualenv from `tools/device/requirements-test.txt` |
| Format check | `.github/scripts/check-format.sh` with clang-format 21.1.8 |

Do not build the environments that use `custom_sdkconfig` (`default`, `gh_release*`, `slim`, `sticky*`) in a cloud session.

## Environment limits (Claude Code cloud sessions)

- `gh issue` and `gh pr` do not work. Use the GitHub tools provided to the session, or `gh api` with REST paths. `gh api` cannot reach Actions paths: start workflows and read job logs with the session's GitHub tools, and read results from a commit's check runs.
- You cannot push tags or delete branches.
- GitHub access covers this repository. Upstream repositories can be cloned read-only over git.
- Outbound network is limited to an allowlist. If a host is refused, record the exact host and error and report it; do not look for a mirror.
- The container restarts every few hours. Background jobs are lost and the disk is small: commit and push work in progress often, run long commands so that their output is kept in a file, and delete build output (`.pio/build/<env>`, `build/`, `.cache`) when a result is recorded.
