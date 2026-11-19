# Orchestrator's acceptance run: unsupported answer (PRD criterion 6)

Plan section 8 assigns this run to the orchestrator: the checker, in a fresh context, on the prepared fixture whose prompt `c0001-p1` has a reference answer containing a claim its section does not support.

How it was run (2026-11-19, on the pull request branch for issue #17):

1. `extract` on `fixtures/books/fixture.epub`; `fixtures/books/negative/unsupported-answer.draft.json` copied in as `draft.json`; `build`; `start-round`; `validate` (all 12 checks passed: the book is structurally sound, so only the checker can catch the fault); `checker-input`.
2. The checker ran as a fresh subagent of the orchestrator's session with no conversation history. Its instructions were the body of `.claude/agents/prep-checker.md` and its only other input was the path of the `checker/` folder. It was told to read nothing outside that folder. One deviation from the command as written: so that the reply could be saved without retyping, the subagent was allowed a single write of its final message to `checker-reply.txt` outside the checker folder; the file here is that reply.
3. `save-check`, `verify-check`, `issues`, `report`.

Result:

- The checker failed `c0001-p1` on `supported_by_section`, citing `d02-p006`, quoting the unsupported clause ("made by a lens with four bright panels that turned once every forty seconds") and naming where the claim does appear (`d02-p008`, section s0002). It passed the other three prompts and judged all five section boundaries natural. It also raised three minor issues (sections without a checkpoint).
- `verify-check` exit 1: `checker-verdict-fail … prompts c0001-p1 fail; blocking issues R1-I1 are open`.
- `issues` wrote the list that goes back to the drafter (`issues.md`).
- `report` exit 1, first line `NOT READY: fixture.prepared.epub must not be loaded onto the device yet; 5 issues are open.` No prepared EPUB was copied to the output folder.

This is the third independent checker run on this fixture (the implementer's two are in the sibling folders); all three failed the same prompt for the same criterion and passed the others.
