NOT READY: fixture.prepared.epub must not be loaded onto the device yet; 5 issues are open.

# Validation report: The Lighthouse Keeper's Almanac

This book is not ready, for these reasons:

- The checker did not pass the draft.

Fix the open issues in the table below, prepare the book again and make a new report.

## About this book

- Original file: `fixture.epub`, SHA-256 `9b35113fe38d8374524edd01a5ef06e20f3035e1fdb4232eb0ed58e0a16ca92c`.
- Prepared file: `fixture.prepared.epub`, SHA-256 `658d6c24a463e214024855c7ce4fc63c2f0e2b9543c251c93db32de53e07ab76`.
- Preparation instructions version: 1.0.
- Text in no section: text/title.xhtml: words 0-19 excluded by the draft: Front matter: title page. (as listed in the draft). Footnote and endnote bodies need no section.
- Rounds of drafting and checking: 1.
- Drafter model: not recorded. Checker model: claude-fable-5-2 (2026-11-19) in round 1.
- Tools: tools.prep 1.0.0, word rules version 1, EPUBCheck 5.3.0.
- Report made: 2026-11-19T11:49:08Z.

## Checks

Each check ran again for this report; a stored result is only a record of what happened before.

| Check | What it confirms | Status |
| --- | --- | --- |
| open | Both files open as EPUBs and every spine document parses | Passed |
| source-hash | The checkpoint file names the original's SHA-256, and the original was not changed | Passed |
| checkpoint-item | The package declares rsvp/checkpoints.json exactly once, as application/json | Passed |
| checkpoint-file | The checkpoint file passes its schema and cross-reference rules (unique ids, plain text) | Passed |
| firmware-limits | The checkpoint file is within the firmware's limits | Passed |
| anchors | Every section's two anchors exist once, in its chapter, as empty spans, in order, without overlap | Passed |
| word-boundaries | Every anchor sits between two words, outside tables and code blocks | Passed |
| coverage | Sections cover the body text; everything else is footnotes or listed excluded matter | Passed |
| word-counts | Every section's word_count equals the words between its anchors | Passed |
| text-unchanged | Every spine document has the original's words, flags, markers and footnote marks | Passed |
| archive-unchanged | Only the anchors, one manifest line and the checkpoint file differ from the original | Passed |
| epubcheck | EPUBCheck reports no errors | Passed |
| validator-record | The latest stored validator result passed, for exactly this prepared file and original | Passed |
| checker-files | The checker's result is for exactly this prepared file, this original and these instructions | Passed |
| checker-complete | The checker gave one verdict per prompt and section, and every cited paragraph lies inside the prompt's own section | Passed |
| checker-verdict | The checker passed every prompt and every section, with no open blocking issue | Failed |

## Issues

| Round | Raised by | Issue | Where | Description | How it was resolved |
| --- | --- | --- | --- | --- | --- |
| 1 | checker | R1-I1 | c0001-p1 | The reference answer contains a claim that section s0001 does not state: 'made by a lens with four bright panels that turned once every forty seconds'. In s0001 the lens is described only in d02-p005 (rings of cut glass bending one oil flame into a narrow beam) and the pattern only in d02-p006 (one white flash every ten seconds). The four panels and the forty-second rotation appear in d02-p008, which belongs to section s0002. Suggested fix: Remove the clause 'made by a lens with four bright panels that turned once every forty seconds' from the answer, leaving: 'Every light had its own pattern of flashes, and sailors counted them. Gannet Rock gave one white flash every ten seconds; no other light on the coast did the same.' Cite d02-p006 only. | Open |
| 1 | checker | R1-I2 (minor) | s0003 | Section s0003 (d03-p001 to d03-p007, 311 words) has no checkpoint. It is a body section over 200 words, mostly prose, with ideas worth recalling: fog makes the light useless so keepers rely on sound (d03-p002, d03-p003), how the fog signal works and when it is started (d03-p004), and why the horn is not sounded all the time (d03-p007). None of the instructions' exceptions applies. Suggested fix: Add a checkpoint for s0003 with one or two prompts, for example on why keepers rely on sound in fog, or why the horn cannot be sounded continuously, citing only prose paragraphs and not the table d03-p005. | Open |
| 1 | checker | R1-I3 (minor) | s0005 | Section s0005 (d04-p001 to d04-p008, 321 words) has no checkpoint. It is a whole chapter of prose with ideas worth recalling: what the count of fog hours showed and why nobody had noticed (d04-p005), what Ione did when the clockwork stopped (d04-p006), and the request to the lighthouse board and its outcome (d04-p007). None of the instructions' exceptions applies. Suggested fix: Add a checkpoint for s0005 with one or two prompts drawn from d04-p005 to d04-p007, without relying on the code block d04-p004. | Open |
| 1 | checker | R1-I4 (minor) | s0002 | Section s0002 (d02-p007 to d02-p010, 194 words) has no checkpoint. It is just under 200 words, but it is not a mere bridge between two sections: it carries its own idea, how the clockwork turns the lens and why the weight must be rewound through the night (d02-p008, d02-p009). Suggested fix: Add a checkpoint for s0002 with one prompt, for example on why the weight had to be wound up every four hours and what happened if the lens stopped, citing d02-p008 and d02-p009. | Open |
| 1 | checker | verify-check: checker-verdict-fail | check.json#/verdict | the checker did not pass the draft: prompts c0001-p1 fail; blocking issues R1-I1 are open | Open |

Every detail, including each check's findings, is in `fixture.validation-report.json`.
