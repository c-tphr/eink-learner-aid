# P5 evidence: preparing the real book (issue #38)

The first `/prep-book` run on a real book: Thomas Paine's *Essays* in the Standard Ebooks edition. *Common Sense* is its first and longest essay. The run ended **READY after 2 rounds**. The delivered files are in `fixtures/books/` (see `fixtures/books/REAL_BOOKS.md`).

| | |
| --- | --- |
| Original | `fixtures/books/paine-essays.epub`, SHA-256 `96d9e4c1356048ed87bc5ebd2dcbcfa21db28e5679de16f70d35ebaac6529561` (unmodified download, 2026-11-19; licence in `paine-essays.LICENSE.md`) |
| Prepared | `fixtures/books/paine-essays.prepared.epub`, SHA-256 `c9afc99966c698b85c7755670264daaf2b32e1f331c932092bb02887ea23ca50`: the exact file the round-2 validator and checker passed, as `report` delivered it |
| Book | 12 spine documents, 52,786 words. 55 sections (49,158 words), 55 checkpoints, 130 prompts. Excluded: the title page, imprint, colophon and Uncopyright, and the title heading plus Editor's Introduction of *Agrarian Justice* (d09-p001 to d09-p005). The endnotes document has only endnote bodies, which need no section |
| Rounds | 2 (round 1 failed on one blocking issue; round 2 passed) |
| Drafter | claude-opus-5-5, the implementer session, following `.claude/commands/prep-book.md` |
| Checker | claude-fable-5-2, the `prep-checker` subagent, run by the lead in a fresh context on `WORK/checker/` only |
| Report | `fixtures/books/paine-essays.validation-report.md` and `.json`; first line `READY: ...` |

## What is here

`rounds/01/` and `rounds/02/` are the round folders of the work folder, copied unchanged: `draft.json` (the draft built in that round), `validation.json`, `checker-reply.txt` (the checker's final message, saved verbatim), `check.json` (`save-check`), `verify-check.json` and, for the failed round 1, `issues.md` (all the drafter saw of round 1). `report --history docs/evidence/p5` reads only these folders. The extracted text (`source.json`, `structure.json`, `text/`) and the checker folder are left out: `python3 -m tools.prep extract fixtures/books/paine-essays.epub <dir>` and `checker-input` rebuild them exactly.

Reproduce the result:

```sh
python3 -m tools.prep validate fixtures/books/paine-essays.epub fixtures/books/paine-essays.prepared.epub --draft fixtures/books/paine-essays.draft.json
python3 -m tools.prep report --original fixtures/books/paine-essays.epub --prepared fixtures/books/paine-essays.prepared.epub --history docs/evidence/p5 -o /tmp/p5-report
```

The second command reproduces READY, and its `.md` report matches the committed one apart from the "Report made" time.

## How the run went

The run followed `.claude/commands/prep-book.md` step by step, with two changes the lead decided before the run:

1. The drafter did not start the checker. At step 2f it handed back to the lead, who ran the `prep-checker` subagent in a fresh context with the one-line prompt `Checker folder: <WORK>/checker` and saved its final message verbatim as `rounds/NN/checker-reply.txt`. The drafter then continued from step 2g.
2. The output folder was in the session's scratchpad, not in the repository, so that both sessions could read and write it.

### Round 1: *Common Sense* only, the other essays excluded

The lead's first brief asked for *Common Sense* to be prepared and for the six other essays to be listed as excluded matter, with the reason "only Common Sense is prepared in this edition". The draft format and the validator could express this: one excluded range per document passed `build` and all 12 `validate` checks. The round-1 draft therefore had 23 sections, 23 checkpoints and 58 prompts over `text/common-sense.xhtml`, and ten exclusions.

The checker passed all 58 prompts and all 23 section breaks. It failed the book on one blocking issue:

- **R1-I1** (blocking, book): the six essays are body text, and `tools/prep/INSTRUCTIONS.md` section 3 allows only front and back matter to be excluded.
- **R1-I2** (minor, c0004-p3): the answer's "God said they had rejected him" left "him" ambiguous.
- **R1-I3** (minor, c0009-p2): "just before the Reformation" added "just", which the section does not say.

`verify-check` exited 1 with only `checker-verdict-fail`, so the checker's result itself was well formed.

**The lead's decision after round 1:** R1-I1 is correct. The instruction to exclude the essays asked for something the preparation rules forbid, and the checker was right to refuse it. The instruction was withdrawn, and the whole collection was prepared.

### Round 2: the whole collection

The drafter revised from `rounds/01/issues.md` only, under `INSTRUCTIONS.md` section 7:

- R1-I2 and R1-I3 were fixed with exactly the checker's suggested wording.
- For R1-I1, the six exclusions were removed. All six essays were read in full and divided into sections s0024 to s0055, with checkpoints c0024 to c0055 and 72 new prompts.
- The ids s0001 to s0023 and c0001 to c0023, and every unflagged prompt and answer, stayed byte-identical. There is one `x_changes` entry per issue.
- A judgement call: the *Agrarian Justice* title heading and its Editor's Introduction (d09-p001 to d09-p005) were kept excluded as front matter. That text is the edition editor's publication history, not Paine's.

`build` passed on the first try. `start-round` opened round 2 and only listed the added prompt ids: it raised no objection to sections added in a later round. `validate` passed all 12 checks.

The checker passed all 130 prompts and all 55 sections, and judged the d09 exclusion acceptable. It raised four minor issues on prompts that pass (below). `verify-check` passed, and `report` wrote READY.

On the lead's instruction, no third round was started for the minor issues: any change to the draft changes the prepared file and would void the pass.

## Every issue raised, and how it was resolved

| Round | Issue | Where | Summary | Resolution |
| --- | --- | --- | --- | --- |
| 1 | R1-I1 (blocking) | book | Six essays (d04 to d09, about 30,500 words) excluded as "only Common Sense is prepared"; only front and back matter may be excluded | Exclusions removed, essays sectioned (s0024 to s0055). Not raised again in round 2 |
| 1 | R1-I2 (minor) | c0004-p3 | Ambiguous "him" in "God said they had rejected him" | Replaced with "God said it was God, not Samuel, they had rejected". Not raised again |
| 1 | R1-I3 (minor) | c0009-p2 | "just before the Reformation" is more than the text says | "just" removed. Not raised again |
| 2 | R2-I1 (minor) | c0035-p1 | Prompt asks three things at once | Open (prompt passes); see below |
| 2 | R2-I2 (minor) | c0038-p1 | First sentence slightly stronger than d07-p084 | Open (prompt passes); see below |
| 2 | R2-I3 (minor) | c0046-p2 | "loans" not named in d08-p078 | Open (prompt passes); see below |
| 2 | R2-I4 (minor) | c0050-p3 | Gloss of "agrarian law" not the section's wording | Open (prompt passes); see below |

## Known minor issues in the delivered book

The round-2 checker passed these four prompts but raised a minor issue on each. They are open in the report and still in the delivered file. A later re-preparation can fix them by changing only these answers or prompts, keeping their ids (`INSTRUCTIONS.md` section 5). Each is quoted as the checker worded it (`rounds/02/check.json`):

- **R2-I1, c0035-p1.** "The prompt asks three things at once (titles, how aristocracy began, its effect on the mind), covering d07-p063 to d07-p068. The answer is fully supported and the prompt passes; the instructions ask for one question with at most two closely linked parts." Suggested fix: "Narrow the prompt to two linked parts (for example how aristocracy began and what its titles hide) and shorten the answer to match, or leave as is if the single prompt is preferred for this short section."
- **R2-I2, c0038-p1.** "The first sentence says 'Insurrection and discretionary power are justified only by necessity'. d07-p084 applies 'justified by necessity' expressly to the means of overthrow, 'in general, insurrections'; of the 'discretionary exercise of power' it says that the revolutionary party 'permit to themselves' it at the commencement and that 'were the practice to continue, liberty would never be established'. The reading is fair and the prompt passes, but the wording is slightly stronger than the text." Suggested fix: "Reword the first sentence along the lines of: Insurrection is justified by necessity while despotism is being overthrown, but the discretionary power revolutionaries allow themselves at the start must not continue."
- **R2-I3, c0046-p2.** "The answer ends 'which would stop new notes, loans and advances for the war'. d08-p078 names new emissions of bank notes, new advances to government or to the emperor, and new emissions on exchequer bills; it does not use the word loans (the stopping of 'new loans' is said of 1696 in d08-p052, section s0043). The sense is the same and the prompt passes." Suggested fix: "Replace 'new notes, loans and advances for the war' with 'new notes, new advances to the government for the war and new exchequer bills'."
- **R2-I4, c0050-p3.** "The answer glosses agrarian law as 'dividing up cultivated land'. d09-p036 does not define the term; it says only that agrarian law would be unjust 'in a country improved by cultivation' because a man is not 'a joint proprietor of cultivated earth'. The gloss matches that sense and the prompt passes, but the wording is not the section's." Suggested fix: "Replace 'Agrarian law, dividing up cultivated land, would be unjust' with 'Agrarian law, which would treat everyone as joint owner of cultivated land, would be unjust'."

## Stage notes

Wall-clock times are not meaningful here, because the machine pauses between turns (file times span hours while the drafter was idle waiting for the checker). These figures count the drafter's tool calls and the size of what was read and written. Token figures are rough, at about 4 bytes per token.

| Stage | Drafter tool calls | Size | Notes |
| --- | --- | --- | --- |
| Download, licence check, copy into repo | 4 | 420 KB EPUB | SHA-256 matched the lead's download |
| `extract` | 1 | about 2 s; 12 documents, 551 blocks, 52,786 words | Work folder: 11 KB `source.json`, 209 KB `structure.json`, 545 KB text |
| Round 1 reading | 6 | 116 KB *Common Sense* text (about 29k tokens) plus `source.json`; the other documents only skimmed | They were to be excluded under the first brief |
| Round 1 sectioning | 1 | word counts of candidate ranges from `structure.json`, computed by script | Not read by hand |
| Round 1 draft | 6 | 41 KB `draft.json` (about 10k tokens), one write, 3 self-review fixes | Sanity check by script: lengths, `?`, citations |
| Round 1 scripts | 4 | `build`, `start-round`, `validate` (about 5 s with EPUBCheck), `checker-input` (588 KB checker folder) | All exit 0 on the first try |
| Round 1 check (lead) | 0 | checker reply 75 KB (about 19k tokens) | Run by the lead |
| Round 2 reading | 6 | 187 KB of the six other essays (about 47k tokens) | Read in full before sectioning |
| Round 2 revision | 9 | draft grows to 92 KB (about 23k tokens); 3 self-review fixes | 2 minor fixes, 1 exclusion change, 32 sections, 32 checkpoints |
| Round 2 scripts | 4 | as round 1 | All exit 0 on the first try |
| Round 2 check (lead) | 0 | checker reply 160 KB (about 40k tokens) | Run by the lead |
| `save-check`, `verify-check`, `issues`, `report` | 6 | `report` about 5 s | |

In total, the drafter read about 310 KB of book text (about 78k tokens) and wrote about 130 KB of draft over two rounds. Each checker run read the whole checker folder: 588 KB in round 1 and somewhat more in round 2, because of the 32 extra section files.

## Problems found in the workflow

Nothing blocked the run, and no tool was changed. Observed at real size:

1. **Silent acceptance of large exclusions by the scripts.** `validate` passed a draft that excluded six essays (about 30,500 words of body text) with all 12 checks green. The `coverage` check only confirms that every word is in a section or a listed exclusion; whether an exclusion is allowed is left to the checker, which caught it. The control worked as designed, but a script-side warning would catch this before a checker round is spent: for example, when an excluded range holds headings or a large share of the book's words.
2. **The brief contradicted the drafter instructions.** Excluding other essays "as the draft format provides" was expressible in the format, but forbidden by `INSTRUCTIONS.md` section 3. The drafter followed the brief, and round 1 was spent finding this out. A brief for a preparation task should be checked against `INSTRUCTIONS.md` section 3 before the run.
3. **Front matter inside a document.** `INSTRUCTIONS.md` lists "publisher's or transcriber's notes" but not an edition editor's introduction placed inside an essay's document. The drafter excluded one (d09-p001 to d09-p005), together with the essay's title heading that precedes it. The checker accepted this, but the instructions could say so explicitly.
4. **Report wording on models.** The report says "Checker model: claude-fable-5-2 (2026-11-19) in round 1", although round 2 decided the result. `report.py` names the round in which each model first appears, which reads as if round 1 were the deciding check.
5. **Report wording on an empty exclusion.** In "Text in no section", the title page (one image, 0 words) is shown as "words 0--1": an empty range printed with end minus one.
6. **Minor issues on a READY book stay "Open".** The workflow has no path to fix minor issues raised on passing prompts without starting another round, and another round would change the file and need a fresh pass. A READY report with four "Open" rows may confuse a reader. Here they are recorded above for a later re-preparation.
7. **Checker replies are large.** The round-2 reply was 160 KB (about 40k tokens). The command has the drafter's conversation receive it and save it verbatim with the Write tool. At this size, a byte-exact copy through a model's output is a real risk. In this run, the lead saved the replies, and `save-check` and `verify-check` accepted both. A tool that saves a subagent's final message directly would remove the risk.
8. **Reading "every file in WORK/text/".** Command step 2a says to read every text file in round 1. For this book that is 545 KB (about 136k tokens), including front and back matter. It fits, but a drafter's context holds the whole book plus a draft of about 23k tokens. A book of 150,000 words would not fit comfortably in one pass.
9. **Stale estimate in `docs/PREPARING_BOOKS.md`.** It says "The first real book (*Common Sense*) will give a measured figure" for timing. This run measured only tool calls and sizes (above), and the book turned out to be the whole *Essays* collection. That page is outside this task's diff and is left for a follow-up.
10. **`fixtures/books/README.md` is generated.** It comes from `tools/prep/fixture/content.py`, so a real book cannot get a line there without changing the generator. The real book is described in the hand-maintained `fixtures/books/REAL_BOOKS.md` instead.
