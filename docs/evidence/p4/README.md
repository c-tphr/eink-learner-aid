# P4 evidence: `/prep-book` end-to-end runs (issue #17)

All runs use the synthetic fixture `fixtures/books/fixture.epub` (SHA-256 `9b35113f…a92c`, unchanged by every run). Each folder is a `/prep-book` output folder: `work/` is the work folder (extracted text, `draft.json`, the prepared EPUB, `checker/`, and `rounds/NN/` with each round's `draft.json`, `validation.json`, `checker-reply.txt`, `check.json`, `verify-check.json`, `issues.md` when the round failed, and `checker-transcript.md`), and the report files sit beside it.

## How the runs were made

- **Drafter**: the implementer session (claude-opus-5-5), following `.claude/commands/prep-book.md` step by step with the exact commands it lists. For run (a) the draft was written new from the extracted text under `tools/prep/INSTRUCTIONS.md` (not copied from `fixtures/books/fixture.draft.json`). For runs (b) and (c) round 1 the draft is the planted negative fixture (`fixtures/books/negative/*.draft.json`), copied in as `draft.json`.
- **Checker**: the implementer session had no Agent tool, and `.claude/agents/` files written during a session are not loaded by that session anyway. Each checker run therefore happened in a **separate, fresh Claude Code cloud session** (claude-fable-5-2, Claude Code 2.1.325) started on this branch, which loads `.claude/agents/prep-checker.md` at start. Its main thread only relayed: it called the Agent tool with `subagent_type: prep-checker` and the one-line prompt `Checker folder: <root>/docs/evidence/p4/<run>/work/checker`, and returned the subagent's final message. So the checker that ran is the real subagent definition (tools `Read`, `Glob`, `Grep`), in a context with nothing from the drafting session. `checker-transcript.md` in each round folder lists every tool call of that session: the subagent read only files inside its checker folder.
- The subagent's final message was saved byte for byte from the session's event log as `checker-reply.txt`; then `save-check`, `verify-check`, `issues` and `report` ran exactly as the command says.
- Because the work folders had to be committed for the cloud sessions to see them, `work/checker/` holds the checker view of the **last** round of each run; the round-1 view of run (b) is in the branch history (commit `9ca78e5f`).

## Results

| Run | What | Rounds | Final report |
| --- | --- | --- | --- |
| `run-a/` | (a) Whole workflow on the fixture, new draft (3 sections, 3 checkpoints, 7 prompts) | 1: validator passed, checker passed every prompt and section, no issues | **READY**; delivered `fixture.prepared.epub` SHA-256 `38a9e29c…b179` equals the checked file |
| `run-b-unsupported-answer/` | (b) Negative fixture `unsupported-answer`, then the revise loop | 1: checker failed `c0001-p1` on `supported_by_section` (R1-I1, blocking) plus three minor issues (sections without a checkpoint); `report-after-round-1/` is **NOT READY**. 2: drafter fixed exactly R1-I1 to R1-I4 from `rounds/01/issues.md` (`x_changes` in the draft); checker passed all 9 prompts | **READY** after 2 rounds; every round-1 issue "Not raised again by the checker in round 2" |
| `run-b-answer-given-away/` | (b) Negative fixture `answer-given-away` | 1: checker failed `c0002-p1` on `does_not_give_away_answer` (R1-I1, blocking), passed the other three prompts | **NOT READY** |
| `run-c-unsupported-answer-2/` | (c) Second, independent checker run on `unsupported-answer` | 1: checker failed `c0001-p1` on `supported_by_section` (R1-I1, blocking), passed the other three prompts | **NOT READY** |

### The checker on the planted prompts

- `unsupported-answer`, run (b): "Unsupported claim: 'made by a lens with four bright panels that turned once every forty seconds'. No paragraph of section s0001 states this; d02-p005 describes the lens only as 'rings of cut glass that bent the light of a single oil flame into one narrow beam'. The four panels and the forty-second turn are in d02-p008, section s0002." Citations `d02-p006`.
- `unsupported-answer`, run (c): the same finding in nearly the same words. Citations `d02-p005`, `d02-p006`.
- `answer-given-away`, run (b): "the prompt ends 'by steering toward the sound of its fog horn', which is the answer itself ('He steered by the sound of the fog horn alone')." Citations `d03-p009`.

### Stability between the two independent runs on `unsupported-answer`

Same overall verdict (fail), same failing prompt and criterion, same pass verdicts on `c0002-p1`, `c0002-p2`, `c0002-p3`, every section `natural_boundary: true`, the same three minor issues (missing checkpoints on s0002, s0003, s0005; listed in a different order, so the ids R1-I2 to R1-I4 point at different sections). Differences: run (b) cited `d02-p006` for `c0001-p1`, run (c) cited `d02-p005` and `d02-p006`; wording of evidence differs. The `answer-given-away` run raised a fourth kind of minor issue that the others did not: that chapter 1 (427 words) is split into two sections although the instructions say a chapter under 600 words is one section unless it has two separate topics (it still judged both breaks natural).

## Guard probe (not completed)

A sixth cloud session was asked to make the `prep-checker` subagent try reads outside its checker folder, to see the `PreToolUse` guard block them live. That session's auto-mode permission classifier refused to launch the subagent with such a prompt ("Undermining Oversight"), so the live hook was **not** observed blocking anything. The guard script itself is covered by `tools/prep/tests/test_loop.py`; in the five real runs no read outside the checker folder was attempted.
