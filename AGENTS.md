# CrossPoint Reader agent guide

> **This repository is the eink-learner-aid fork. Read this box first.**
>
> This file is upstream CrossPoint's guide for agents (`CLAUDE.md` is a link to it). It is kept as upstream wrote it, below this box, so the fork stays easy to compare with upstream. The project owner has set a different way of working for this fork, and where the two disagree, the fork's rules apply:
>
> - **Workflow.** One agent does the work in this fork: it plans, implements, verifies and merges. Read `docs/AGENT_GUIDE.md` before anything else; it holds the rules, the workflow and the checks to make before merging. Work is planned in `docs/PLAN.md` and tracked in `docs/STATUS.md` and the GitHub issues. Each change is one branch and one pull request, which the agent opens, describes and squash-merges itself once CI is green and its own checks pass; it does not hand work to other agents. The sections below named "Human ownership" and "Mandatory firmware handoff" (no autonomous agents, no commits or pushes without a human instruction, never open a pull request, pull request text written by a human, four reviews and human acceptance before a commit, hardware testing before a pull request) are upstream's rules for contributions sent to upstream. They do not apply to work in this fork. Integration targets `main`, not `develop`.
> - **Still binding here.** The owner's fixed decisions in `docs/PLAN.md` section 1 and `docs/PRD.md`: ask before changing or reinterpreting them. Never run a PlatformIO `upload` target or anything that flashes a device outside the guarded tool in `tools/device/`. Only the owner has the device: agents prove work in host tests and the simulator, and the owner tries releases on the device. Do not copy code from forks whose licence is not confirmed compatible. Never post device backups or `.bin` files: the repository is public.
> - **Still applies from the guide below.** All technical guidance: the FreeInk SDK and HAL contracts, hardware and memory limits, coding conventions and comment rules, UI and input rules, generated files and cache versioning, build and test workflow, plain English in repository prose. New code for this fork lives behind the `CROSSPOINT_RSVP` flag (`docs/ARCHITECTURE-RSVP.md`), and every edit to an upstream file is listed in `docs/BASE.md`.
> - **Scope.** `SCOPE.md` describes what upstream accepts. This fork's scope is `docs/PRD.md` and `docs/PLAN.md`.
> - **In a cloud session**, run `./scripts/cloud-setup.sh` before any `pio` command.

CrossPoint Reader is open-source e-reader firmware for Xteink and other FreeInk-supported devices. Its mission is a lightweight, high-performance reading experience focused on EPUB rendering. The X3/X4's ESP32-C3 remains the resource baseline: roughly 380 KB usable RAM, no PSRAM, and a single framebuffer sized for the selected panel. Other board profiles have different CPUs, display sizes, input, and memory capabilities; check the selected profile before assuming them.

## FreeInk SDK

The [FreeInk SDK](freeink-sdk/) supplies the board profiles, hardware drivers,
and shared UI components. Before adding an API or device-specific code, check
the firmware HAL and the SDK source at the revision pinned by this repository.
Use the [SDK documentation](freeink-sdk/docs/README.md) and
[FreeInk SDK index](https://freeink.org/llms.txt) to find the relevant contract.
Check `git submodule status`: a different local SDK revision must be accounted
for, not silently treated as the pinned firmware dependency.

## Start here

At session start, run `uname -s`, `git branch --show-current`, `git remote -v`, and `git status --short`. Integration work targets `develop`.

Act as a senior embedded C++ engineer. Base claims on repository evidence: cite the paths and line numbers that justify a proposed change. Explain the mechanism behind performance or memory claims and justify every new heap allocation. For every fix, tell the human how to verify it.

First inspect the relevant code and current behavior to establish whether a change is needed. Before adding an API, setting, task, persistent state, driver change, or scheduling dependency, tie it to the human requirement and check existing mechanisms. Identify unexplained scope inherited from a stash or branch.

Use the table to find guidance for the decisions being changed. Read relevant sections, not every file associated with a touched subsystem. Load implementation skills when implementation is needed and `firmware-handoff` at final handoff. Reuse guidance already in context unless it changed or a concrete question requires another read.

| Decision being changed | Relevant guidance |
| --- | --- |
| Routine builds or host setup | [existing environment](.agents/rules/environment.md#build-in-an-existing-environment) |
| Personal build flags or ports | [local overrides](.agents/rules/environment.md#platformiolocalini-personal-overrides) |
| Allocation size/failure, buffer or cache lifetime, hot-path containers, or hardware limits | [hardware-resources.md](.agents/rules/hardware-resources.md); use `heap-discipline` for allocation decisions |
| Hardware/storage access, rendering contracts, input ownership, or SDK boundaries | [HAL contracts](.agents/rules/architecture-hal.md#hardware-abstraction-layer-hal) and relevant sections of `hal-and-abstractions` |
| Build flags, dependencies, or board profiles | [build environment and flags](.agents/rules/architecture-hal.md#build-environment) |
| C or C++ implementation | [coding conventions](.agents/rules/coding-standards.md#naming-conventions); consult the relevant pitfall/allocation section and use `control-flow-clarity` for state modeling |
| Lists, controls, touch routing, or layout | [shared UI](.agents/rules/ui-activities.md#shared-ui-and-input) and [orientation](.agents/rules/ui-activities.md#orientation-aware-logic) |
| Input edges or activity/popup transitions | [input ownership](.agents/rules/ui-activities.md#input-frames-and-ownership) and its transition/verification sections |
| Activity resources or worker tasks | [activity lifecycle](.agents/rules/ui-activities.md#activity-lifecycle-and-memory-management) and [workers](.agents/rules/ui-activities.md#freertos-task-guidelines) |
| Runtime font discovery, rendering, or cache lifetime | [font ownership](.agents/rules/ui-activities.md#runtime-fonts-and-cache-ownership) |
| Plugin, service, web endpoint, or protected-book contracts | [service contracts](.agents/rules/architecture-hal.md#service-and-plugin-contracts) and the linked contract that applies |
| Builds, formatting, or verification | [testing workflow](.agents/rules/testing-debugging.md#testing-and-verification-workflow) and [compact build output](.agents/rules/testing-debugging.md#compact-build-output) |
| Crashes, CI, or serial measurements | [crash diagnosis](.agents/rules/testing-debugging.md#debugging-crashes) or the matching CI/serial section |
| Branch creation, merges, commits, or publication | [git-workflow.md](.agents/rules/git-workflow.md) |
| User-facing strings or language metadata | [translation workflow](.agents/rules/generated-files-cache.md#translation-workflow) |
| Generated HTML/i18n or font assets | [generated sources](.agents/rules/generated-files-cache.md#files-generated-by-build-scripts) or [font generation](.agents/rules/generated-files-cache.md#font-generation) |
| Cache formats, layout persistence, or invalidation | [versioning](.agents/rules/generated-files-cache.md#cache-file-format-versioning) and [invalidation](.agents/rules/generated-files-cache.md#cache-invalidation-rules) |
| New features, activities, settings, libraries, or dependencies | `SCOPE.md` and the `scope-discipline` skill |
| Restructuring, extraction, or separating unrelated cleanup | the `refactor-for-review` skill |

Repository-local skills live under `.agents/skills/`. Their descriptions identify specific procedures to use when needed; a keyword match alone does not require loading a skill.

## Human ownership

A PR is a long-term maintenance commitment. Working code is not enough: prefer the simplest design that meets the real requirement, fits `SCOPE.md`, and can be understood and maintained by its human owner.

Fully autonomous end-to-end agents are forbidden. Review subagents inspect code,
diffs, history, and recorded checks only. The main agent owns verification:
reviewers may not write files (including temporary diagnostics), run tests/builds,
commit, publish, deploy, or flash.

The human must write PR descriptions; agents may give concise factual notes and test
results, never ready-to-paste PR prose. Creating/amending local commits requires
explicit human approval. Push only on an explicit human instruction to push;
edit/commit approval does not authorize it. Never open or close a PR.

Repository-facing prose: use plain English for non-native readers and standard
technical terms when clearest. Code comments must be short and useful after merge; follow the
[comment rules](.agents/rules/coding-standards.md#comment-style).

## Mandatory firmware handoff

For every logical change that can affect shipped firmware or its build—including C/C++, build configuration, partitions, code-generation sources, translations, and release scripts—the main agent must complete [firmware-handoff](.agents/skills/firmware-handoff/SKILL.md) before declaring the work ready or making an approved local commit. That skill owns the required checks, four independent reviews, bounded follow-up, and explicit human acceptance of behavior, architecture, and maintenance ownership.

Read-only inquiries, audits, and reviewers do not run implementation handoff. Pure tests, diagnostics, documentation, and host-only Python scripts use a lighter review unless they alter firmware output or its build. Hardware testing remains the human's responsibility and is required before a PR is opened.
