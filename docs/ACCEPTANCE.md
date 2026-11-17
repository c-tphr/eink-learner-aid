# Acceptance report: release candidate

Task A1 (#39). This is the orchestrator's gate before the device kit is handed to the owner. It maps each of the six acceptance criteria of `docs/PRD.md` to evidence that passes on `main`, and says plainly what is not proven yet.

- **Commit tested:** `935fa9dd` on `main` (C5 merged; the last firmware change before the release).
- **Who ran it:** the orchestrator, in a cloud session, after validating every pull request the same way (check out, re-run, read the diff, open the screenshots). Project CI ran the same suites on the same commit.
- **What "passes" means here:** host tests and simulator scenarios. Nothing has run on a Picco. Criterion 5 is the only one the PRD ties to the physical device, and it stays open until the owner's session (`docs/DEVICE_SESSION.md`).

## The full run on `main`

| Suite | Command | Result |
| --- | --- | --- |
| Host tests | `cmake -S test -B build/test && cmake --build build/test -j4 && ctest --test-dir build/test --output-on-failure -j4` | 1,567 of 1,567 passed |
| Simulator build | `pio run -e simulator_picco` | success |
| Scenarios | `python3 -m tools.simtest run --all --jobs 2` | 49 passed, 0 failed |
| Scenario harness tests | `python3 -m pytest tools/simtest -q` | 125 passed |
| Preparation tools | `python3 -m pytest tools/prep -q -rs` | 708 passed, none skipped (EPUBCheck ran); `python3 tools/prep/fixture/generate.py --check`: all 4 generated files are current |
| Flashing tool and kit builder | `python -m pytest tools/device scripts/tests -q -rs` (virtualenv from `tools/device/requirements-test.txt`) | 353 passed, none skipped |
| Firmware, Picco | `pio run -e picco` | success; RAM 113,372 of 327,680 bytes (34.6%) |
| Firmware, two-slot Picco | `pio run -e picco_2slot`, then `.github/scripts/firmware-size.sh picco_2slot <log> --gate` | success; `firmware.bin` 5,655,408 bytes, 76.1% of the 7,431,782-byte budget; size gate PASS |
| Firmware, another device | `pio run -e x4pro` | success; `nm -C` finds 0 symbols of this project's features (they are compiled only with the `CROSSPOINT_RSVP` flag) |
| Format | `.github/scripts/check-format.sh` (clang-format 21.1.8) | all 249 files clean; every Python file compiles |
| Project CI | `.github/workflows/project-ci.yml` on `935fa9dd` | all jobs green, including `device-tool-windows` under Windows PowerShell 5.1 and PowerShell 7 |

Each scenario that involves input has a pass by touch and a pass by the two side keys alone (Up and Down); Confirm is never required.

## The six PRD criteria

### 1. Single-prompt and multi-prompt checkpoints fire on reading in, in both modes; a jump does not fire them

**Status: passes in the simulator.**

| Evidence | What it shows |
| --- | --- |
| Scenario `cp_fire_rsvp` | RSVP stops with the section's last word on screen and draws nothing beyond it, then the prompt appears: the one-prompt checkpoint `c0001` (section ends mid-chapter at "same.") and the three-prompt checkpoint `c0002` (section ends its chapter at "lamp."), also with two words per update. The words agree with the fixture's ground truth (`fixtures/books/fixture.expected.json`: `s0001` ends at word 233, `s0004` at word 608) |
| Scenario `cp_fire_page` | In page view the prompt appears on the advance from the page that shows the section's last word, not earlier; paging backward across the end does not fire; paging forward again fires again |
| Scenario `cp_toc_jump_no_fire` | Contents, go to percent and a bookmark jump over section ends fire nothing; reading forward inside the section then fires |
| Scenarios `cp_single_prompt`, `cp_multi_prompt` | Both kinds end to end, from reading to resuming |
| Scenarios `cp_ordinary_book`, `cp_paine_rsvp` | The same book without checkpoint data fires nothing; a real prepared book (Paine's *Essays*) fires at a real section end |
| Host tests | `CheckpointScenario.Prd1_ReadingIntoEitherCheckpointFiresItInBothModesAndJumpingPastDoesNot`; `ReadingSessionTest` (14 tests, including two section ends on one page and a group that must not cross a section end mid-sentence: no fixture has those two cases) |

### 2. Mark the second prompt Incorrect, reread in either mode, return early or at the end, retry with the answer hidden; a later Correct keeps the saved Incorrect and the first prompt's grade

**Status: passes in the simulator.**

| Evidence | What it shows |
| --- | --- |
| Scenario `cp_reread_rsvp_end` | Prompt 1 Correct, prompt 2 Incorrect, reread in RSVP from the section's first word (word 311) to its end; the prompt returns by itself with the answer hidden; marking it Correct saves no grade. Final state file: `c0002-p1` correct, `c0002-p2` incorrect, position at chapter 3 word 0 in the original mode |
| Scenario `cp_reread_page_early` | The same with a reread in page view and "Back to the question" after a page; paging to the section's end also returns by itself |
| Scenario `cp_reread_twice` | Incorrect, reread, back, Incorrect again, reread in the other mode, back, Continue: no grade changes, no loop, the resume position is unchanged |
| Scenario `cp_reread_no_other_fire` | During a reread neither the section's own checkpoint nor any other fires |
| Host tests | `CheckpointScenario.Prd2_IncorrectRereadEitherModeReturnEarlyOrAtEndRetryCorrectKeepsIncorrect`; `ReadingSessionReread.*` |

### 3. Skip one prompt or leave a partly completed checkpoint; unanswered prompts stay ungraded, saved grades survive, reading resumes after the break in the original mode without advancing by itself

**Status: passes in the simulator.**

| Evidence | What it shows |
| --- | --- |
| Scenarios `cp_skip_prompt`, `cp_skip_checkpoint` | After a skip, reading resumes at the first word after the break in the mode the checkpoint fired in; RSVP shows its pause screen with Play highlighted and "Checkpoint done" and waits |
| Scenario `cp_skip_grades` | State-file checks: a skipped prompt has no grade; a first grade survives a different second answer; each grade is in the file before the next screen appears |
| Scenarios `cp_screens_touch`, `cp_screens_keys`, `cp_long_answer` | Every checkpoint screen and action by touch and by keys; a long answer pages forward and back |
| Host tests | `CheckpointScenario.Prd3_SkipOnePromptOrExitPartwayKeepsGradesAndResumesAfterTheBreakWaiting`; `CheckpointSave.*`, `CheckpointScreens.*` |

### 4. Closing and reopening restores an interrupted prompt or reread with the correct return position; a later pass shows checkpoints again with first grades intact

**Status: passes in the simulator.**

| Evidence | What it shows |
| --- | --- |
| Scenario `cp_kill_in_prompt` | The simulator is killed with the answer to question 2 on screen and ungraded; on reopening, the book opens directly at question 2 of 3 with the answer hidden, question 1's grade is kept, and finishing resumes at chapter 3 word 0 in the original mode |
| Scenario `cp_kill_in_reread` | Killed during a reread in page view and during one in RSVP: each is restored at the saved place, with its way back to the question; the saved Incorrect stays |
| Scenario `cp_sleep_restore`, `cp_walkthrough_restore` | The same across sleep and wake; steps 9 and 10 of the owner's walk-through run exactly as written |
| Scenario `cp_second_pass_grades` | After a full pass and a restart, reading the section again in each mode shows every prompt with its recorded grade; a different answer changes nothing |
| Scenario `cp_restore_damaged` | A state file with an unknown checkpoint, a prompt index out of range, a reread outside its section, or another edition's hash opens at the best remaining view without a crash, and the file is cleaned |
| Host tests | `CheckpointScenario.Prd4_CloseAndReopenRestoresPromptOrRereadAndALaterPassShowsGradesIntact`; `test/book_state_file` (the save is cut after every step of the write: no step loses the grades) |

### 5. On a physical Picco, the default pace and a target above what the display can deliver cleanly slow down without dropping words, and the pause screen shows the delivered WPM

**Status: the logic passes on the host and in the simulator. The criterion itself is open until the device session.**

| Evidence | What it shows |
| --- | --- |
| Scenario `rsvp_slowdown` | With a modelled 540 ms word update and the 240 WPM default, 13 words are delivered at about 100 WPM, each drawn once and in order (`audit=ok … violations=0 gaps=0 repeats=0`), and the pause screen reads "240 WPM target · 100 actual" |
| Scenarios `devicetest_acceptance`, `devicetest_acceptance_keys` | The on-device walk-through that will prove this criterion runs end to end in the simulator and writes a results file that the validator accepts: Part A plays the fixed passage at 240 and at a 600 WPM target and records the measured verdict apart from the owner's answers; step 12 checks RSVP's real pause screen at 600 WPM |
| Host tests | `Scheduler.SlowDisplayHoldsRefreshPlusDwell`, `Scheduler.LateCallsAndStallsAreNeverRepaid`, `WordOrderAudit.*`, `DeliveredWpmMeter.*`, `PlaybackSweep.*`, `TimedBackend.*` (the device backend's timing with a fake port and clock) |
| Still to come | The owner's session: `docs/DEVICE_SESSION.md` step 10. The results file `/rsvp-bench/NNNN-acceptance.json` holds the word-order record and the owner's confirmations. Task A2 (#40) reads it |

### 6. The preparation workflow produces a ready file and report only after the checker passes the draft; a broken section link or an unsupported answer is flagged and sent back

**Status: passes, with one limit on where the workflow can run.**

| Evidence | What it shows |
| --- | --- |
| `python3 -m pytest tools/prep` (708 tests) | The validator's 13 checks, the round loop (three rounds at most), `report` refusing READY unless the latest validator run and the latest checker run both passed for the current file's hash |
| Negative fixture, broken link: `python3 -m tools.prep validate fixtures/books/fixture.epub fixtures/books/negative/broken-link.prepared.epub` | Exit 1: `anchor-missing … the end anchor 'rsvp-s0004-end' is not in text/chapter-2.xhtml … checkpoint c0002 links to this section and could never fire` |
| Negative fixture, unsupported answer: `python3 -m tools.prep verify-check fixtures/books/fixture.epub fixtures/books/negative/unsupported-answer.prepared.epub fixtures/books/negative/checker-failed.check.json` | Exit 1: `checker-verdict-fail … prompts c0002-p2 fail; blocking issues I1 are open` |
| Real checker runs in a fresh context (orchestrator) | On the fixture with a planted unsupported answer, the `prep-checker` agent failed prompt `c0001-p1` from files alone, and the report was NOT READY (issues #17 and #75) |
| Real book: `python3 -m tools.prep report --original fixtures/books/paine-essays.epub --prepared fixtures/books/paine-essays.prepared.epub --history docs/evidence/p5` | `READY: paine-essays.prepared.epub has passed every check and may be loaded onto the device.` after 2 rounds (round 1 was sent back); the prepared file's SHA-256 is `c9afc999…ca50`, as committed. 55 sections, 130 prompts |
| Limit | Since task P7 the checker's result counts only when its guard recorded the write, and Claude Code runs that guard only in a session started on the user's own computer in a trusted folder. In a cloud session `/prep-book` stops at its first step with instructions (task P8; seen for real). The "guard active" path has not been observed by this team: the first local run of `/prep-book` will show it in about a minute |

## Beyond the six criteria

| PRD requirement | Evidence |
| --- | --- |
| RSVP works on any EPUB, default 240 WPM and one word per update, adjustable while paused, saved as global defaults or per book | Scenarios `rsvp_any_epub`, `rsvp_play_touch`, `rsvp_play_keys`, `rsvp_settings_touch`, `rsvp_settings_keys`, `rsvp_restore_position`, `rsvp_mode_switch`, `rsvp_chapter_end`, `rsvp_sentence_step_*` |
| Images, tables and code blocks pause RSVP and show in page view; footnote marks are passed over | Scenarios `rsvp_nonprose_touch`, `rsvp_nonprose_keys` |
| Every RSVP and checkpoint action works by touch and by the side keys | Each scenario's touch and keys passes; `ControlMap.*` host tests |
| Updates after the first flash without a cable | `docs/TWO_SLOT.md`; scenario `update_entries_hidden_keys`; `lib/UpdatePolicy` host tests. Not run on hardware |
| The device kit and the session | `docs/RELEASING.md` (one build, the guard on every image, a dry run of the unpacked kit on Windows under both shells); `docs/DEVICE_SESSION.md` with tests that hold every quoted command and message to the tool's real output |

## The release

The device kit is the draft release **v2.0.1**, made by the manual workflow `.github/workflows/release-kit.yml` on `main` at `2e1aadbc` (this report's commit plus nothing else; the firmware sources equal `935fa9dd`). A draft is visible to the owner when signed in; publishing it is the owner's step (`docs/RELEASING.md`).

| Check | Result |
| --- | --- |
| Workflow run with `publish=false`, kind `final` ([run 44169984007](https://github.com/c-tphr/eink-learner-aid/actions/runs/44169984007)) | preflight, build and assemble, windows (powershell), windows (pwsh): green |
| Workflow run with `publish=true`, kind `final` ([run 44171702393](https://github.com/c-tphr/eink-learner-aid/actions/runs/44171702393)) | the same four jobs and `draft release`: green (second version check, build provenance attestation, draft created, five assets uploaded and read back) |
| The draft as GitHub stores it | "v2.0.1", draft, not a pre-release, target `2e1aadbc`; no tag exists until it is published |
| Assets, downloaded from the draft by the orchestrator | `picco-2slot-2.0.1.zip` (13,568,382 bytes, SHA-256 `2dfbf900…1269`) and `eink-learner-aid-v2.0.1-picco2.bin` (5,655,616 bytes, SHA-256 `3eebc331…3848`) match their `.sha256` files; plus `kit-info.json` |
| The kit, unpacked | 78 files; `MANIFEST.sha256` verifies (77 entries); `kit/` holds `flash-plan.json` and the four images at 0x0, 0x8000, 0x15000 and 0x20000 with firmware version `2.0.1-picco2`; `sd-card/update.bin` and the Wi-Fi asset equal `kit/firmware.bin`; `sd-card/books/` holds the fixture book, the prepared fixture book, Paine's *Essays* and its prepared copy; `docs/` holds `DEVICE_SESSION.md` with its 36 pictures and `DEVICE_EARLY_CHECK.md`; `session.ps1`, `piccoflash/` and `DEVICE_SESSION.md` are byte-identical to `main` |
| The same kit on Windows (workflow) | "Extract All" and `tar.exe` into paths with a space, `MANIFEST.sha256` verified in both, `session.ps1 setup`, `plan` and `flash -DryRun` end with `DRY RUN RESULT: READY` under Windows PowerShell 5.1 and PowerShell 7 |

Every item of the kit in `docs/PLAN.md` section 10 is present. The results come back in issue #110, "Device session results".

## Not proven, and where each will be answered

Nothing below can be answered without the device. `docs/DEVICE_SESSION.md` Appendix C lists where the owner records each.

- Whether the flashing tool reaches a stock Picco over USB, the real read and write times, and whether this firmware starts after the flash. The backup and `restore` are the way back; the write order was worked out from Espressif's documentation and has not been tried on a Picco.
- Which of the four possible panels the device has, the real refresh timings, and how any trial display profile looks. Until measured, only the stock refresh can run.
- Whether GPIO5 is a Confirm key, whether the two side keys are the way round the firmware assumes, and whether a 0.7 s hold is comfortable.
- Whether the 78 KB state block and the 72.5 KB checkpoint index land in the Picco's PSRAM, and SD-card write times at a pause.
- The SD-card and Wi-Fi update paths on hardware.
- Whether the 26 px "Back to the question" button of a page-view reread is comfortable to tap (a side-key hold and reading to the section's end are the alternatives).
- Running the simulator in a real GitHub Codespace (`docs/SIMULATOR.md`): the configuration is tested, a real Codespace was never started.

## Known behaviour worth knowing before the session

- Paging back over a section's end and forward again shows its checkpoint again (the plan's rule: nothing remembers that a checkpoint fired).
- Skip is offered after Incorrect too.
- Leaving a checkpoint by the Home gesture reopens the book at that question (since C5), with the answer hidden.
- Four small wording points in the delivered real book are listed in `docs/evidence/p5/README.md`; the checker passed the book with them.
- Upstream's `USER_GUIDE.md` says SD and Wi-Fi updates are not available on the Picco. That is true for upstream's single-slot layout and not for this project's two-slot build.
