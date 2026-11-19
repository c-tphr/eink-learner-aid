NOT READY: fixture.prepared.epub must not be loaded onto the device yet; 5 issues are open.

# Validation report: The Lighthouse Keeper's Almanac

This book is not ready, for these reasons:

- The checker did not pass the draft.

Fix the open issues in the table below, prepare the book again and make a new report.

## About this book

- Original file: `fixture.epub`, SHA-256 `9b35113fe38d8374524edd01a5ef06e20f3035e1fdb4232eb0ed58e0a16ca92c`.
- Prepared file: `fixture.prepared.epub`, SHA-256 `5ce896509473045e9ccd44409bd707c1cee08d8982ff7b6c2fe7e67f66cb9d49`.
- Preparation instructions version: 1.0.
- Text in no section: text/title.xhtml: words 0-19 excluded by the draft: Front matter: title page. (as listed in the draft). Footnote and endnote bodies need no section.
- Rounds of drafting and checking: 1.
- Drafter model: not recorded. Checker model: claude-fable-5-2 (2026-11-19) in round 1.
- Tools: tools.prep 1.0.0, word rules version 1, EPUBCheck 5.3.0.
- Report made: 2026-11-19T11:49:01Z.

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
| 1 | checker | R1-I1 | c0002-p1 | The prompt gives away its answer. It asks 'How did the fisherman find his way to the lighthouse in the fog by steering toward the sound of its fog horn?', and the reference answer is that he steered by the sound of the fog horn (d03-p009). The key fact to be recalled is stated in the question. Suggested fix: Remove the clause 'by steering toward the sound of its fog horn' from the prompt, for example: 'How did the fisherman find his way to the lighthouse in the fog?' Keep the id and the answer unchanged. | Open |
| 1 | checker | R1-I2 (minor) | s0003 | The section has no checkpoint. It is a 311-word body section, mostly prose, with ideas worth recalling: why fog makes the light useless (d03-p002, d03-p003), how the fog signal works and when it is started (d03-p004), and why the horn is not sounded all the time (d03-p007). None of the instructions' exceptions (a bridge under about 200 words, mostly table/code/images, nothing worth recalling) applies; the table d03-p005 is one block among six prose paragraphs. Suggested fix: Add a checkpoint for s0003 with one prompt on an essential idea stated in its prose, for example why the horn could not be sounded continuously (d03-p007). | Open |
| 1 | checker | R1-I3 (minor) | s0005 | The section has no checkpoint. It is the whole of chapter 3 (321 words) and its prose states recallable findings: what the count of fog hours showed (d04-p005), what Ione did when the clockwork stopped (d04-p006), and what the lighthouse board agreed to (d04-p007). The code block d04-p004 is a single block, so the section is not mostly code. Suggested fix: Add a checkpoint for s0005 with one or two prompts drawn from the prose paragraphs, for example what the count of fog hours revealed and what came of it (d04-p005, d04-p007). | Open |
| 1 | checker | R1-I4 (minor) | s0002 | Chapter 1 is 427 words in total, and the instructions say a chapter shorter than 600 words is one section unless it has two clearly separate topics. It is split at the h2 heading d02-p007 into s0001 (233 words) and s0002 (194 words). The split is defensible because the heading separates two topics, but both parts are far below the 600-word target, and s0002 has no checkpoint although it is not merely a bridge: d02-p008 and d02-p009 explain how the clockwork drive turns the lens and why it must be rewound. Suggested fix: Either merge s0002 into s0001 so that chapter 1 is one section (keeping id s0001 and its checkpoint), or keep the split and add a checkpoint for s0002 with one prompt on why and how the weight is rewound (d02-p008, d02-p009). | Open |
| 1 | checker | verify-check: checker-verdict-fail | check.json#/verdict | the checker did not pass the draft: prompts c0002-p1 fail; blocking issues R1-I1 are open | Open |

Every detail, including each check's findings, is in `fixture.validation-report.json`.
