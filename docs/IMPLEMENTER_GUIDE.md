# Implementer guide

Standing rules for every task in this repo. Your issue tells you what to build; this page tells you how work is done here. Read `docs/PLAN.md` sections named in your issue before writing code.

## The project in one paragraph

This is a fork of the CrossPoint e-reader firmware for the BOOX Picco (ESP32-S3, 800x480 e-ink). It adds an RSVP reading mode and retrieval-practice checkpoints, a Python workflow that prepares books, and a simulator. Nobody on the team has the device: everything is proven in host unit tests and the SDL simulator, and the owner runs one hands-on session at the end.

## Hard rules

1. **Never run a PlatformIO `upload` target**, and never write anything that flashes a device outside the guarded scripts under `tools/device/`.
2. **Do not copy code** from `mreza-yousefi/crosspoint-reader-picco` (noncommercial licence), or from any fork whose licence you have not confirmed is MIT-compatible (KatiePoint, crossplay, crosspoint-rsvp, inkflow). Read for ideas, then write your own.
3. **Section 1 of the plan is fixed.** If a decision there blocks you, stop and say so in your report; do not work around it.
4. **Add, don't rewrite.** New code goes in the directories named in plan section 4. Edits to upstream files are limited to registration points and must be listed in your pull request.
5. **Stay in scope.** No unrelated edits, no drive-by refactors, no upstream syncs.
6. If the plan states a fact that the code contradicts, do not silently follow either. Report it; the orchestrator corrects the plan by pull request.
7. **Never work around a refusal.** If the session's safety checks refuse a command (a permission prompt, the worktree guard, a blocked network host), do not reword it, copy the script somewhere else, or wrap it to get past the check. Run the plain form the message asks for (one simple command, from your working copy). If that is refused too, stop that step and report the exact refusal. Example: the worktree guard can refuse a command that names a path under `.github/` together with pipes or redirections; run the script as one plain command on its own line (`bash .github/scripts/check-format.sh`), and if that is refused as well, report it and rely on the CI job that runs the same script.

## Workflow

- One issue, one branch, one pull request. Branch name `task/<issue number>-<slug>`, cut from the latest `main`.
- Pull request body starts with `Closes #<n>`, then: what changed, upstream files touched, each acceptance command with its real output (trimmed), evidence (screenshots, reports), anything you could not do.
- Tests are added with the change. Run every acceptance command in the issue before opening the pull request and paste what happened, including failures.
- Follow upstream conventions once the base is imported: `AGENTS.md`, `CLAUDE.md`, `docs/contributing/`, and the repo's `clang-format` rules.
- Report honestly. "Could not verify X because Y" is a good report. A claim of success that the orchestrator cannot reproduce sends the task back.

## Standard commands

| Purpose | Command |
| --- | --- |
| Cloud session setup | `./scripts/cloud-setup.sh` |
| Host unit tests | `cmake -S test -B build/test && cmake --build build/test -j4 && ctest --test-dir build/test --output-on-failure -j4` |
| Firmware build | `pio run -e picco` (later also `picco_2slot`) |
| Simulator build | `pio run -e simulator_picco` |
| Simulator scenarios | `python3 -m tools.simtest run <scenario>` or `--all` |
| Preparation-tool tests | `python3 -m pytest tools/prep -q` |

Some of these exist only after the task that creates them has merged; your issue lists what you can rely on.

## Environment limits (Claude Code cloud sessions)

- `gh issue` and `gh pr` do not work. Use the GitHub tools provided to the session, or `gh api` with REST paths.
- You cannot push tags or delete branches.
- GitHub access covers this repo. Upstream repos can be cloned read-only over git, but their issues and pull requests cannot be read through the API.
- Outbound network is limited to an allowlist. If a host is refused, record the exact host and error and report it; do not look for a mirror.
