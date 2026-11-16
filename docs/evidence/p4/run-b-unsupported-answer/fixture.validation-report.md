READY: fixture.prepared.epub has passed every check and may be loaded onto the device.

# Validation report: The Lighthouse Keeper's Almanac

The validator and the checker both passed this exact prepared file, and it was made from the original named below. The prepared book is in this folder as `fixture.prepared.epub`; the original EPUB stays a separate, unchanged file.

## About this book

- Original file: `fixture.epub`, SHA-256 `9b35113fe38d8374524edd01a5ef06e20f3035e1fdb4232eb0ed58e0a16ca92c`.
- Prepared file: `fixture.prepared.epub`, SHA-256 `b38765ad3bbf62a4f154204acf8e3d132caca47b5bb3a429247f965df214f810`.
- Preparation instructions version: 1.0.
- Text in no section: text/title.xhtml: words 0-19 excluded by the draft: Front matter: title page. (as listed in the draft). Footnote and endnote bodies need no section.
- Rounds of drafting and checking: 2.
- Drafter model: claude-opus-5-5 (2026-11-19) in round 2. Checker model: claude-fable-5-2 (2026-11-19) in round 1.
- Tools: tools.prep 1.0.0, word rules version 1, EPUBCheck 5.3.0.
- Report made: 2026-11-19T11:51:35Z.

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
| checker-verdict | The checker passed every prompt and every section, with no open blocking issue | Passed |

## Issues

| Round | Raised by | Issue | Where | Description | How it was resolved |
| --- | --- | --- | --- | --- | --- |
| 1 | checker | R1-I1 | c0001-p1 | The reference answer contains a claim that section s0001 does not state: 'made by a lens with four bright panels that turned once every forty seconds'. In s0001 the lens is described only in d02-p005 (rings of cut glass bending a single oil flame into one narrow beam); the flash pattern is in d02-p006. The four panels and the forty-second rotation appear only in d02-p008, which belongs to section s0002. Suggested fix: Remove the clause 'made by a lens with four bright panels that turned once every forty seconds' from the answer, leaving: 'Every light had its own pattern of flashes, and sailors counted them. Gannet Rock gave one white flash every ten seconds; no other light on the coast did the same.' Keep the citation to d02-p006. | Not raised again by the checker in round 2 |
| 1 | checker | R1-I2 (minor) | s0002 | Section s0002 (d02-p007 to d02-p010, 194 words) has no checkpoint. It is not a mere bridge: d02-p008 and d02-p009 explain how the clockwork weight turns the lens, why a stopped light is dangerous, and that the weight must be rewound every four hours. The instructions give every body section a checkpoint unless it only bridges two others, is mostly non-prose, or has nothing worth recalling. Suggested fix: Add a checkpoint with one prompt on s0002, for example on what turns the lens and why it has to be rewound through the night, supported by d02-p008 and d02-p009. | Not raised again by the checker in round 2 |
| 1 | checker | R1-I3 (minor) | s0003 | Section s0003 (d03-p001 to d03-p007, 311 words) has no checkpoint although it carries several ideas worth recalling in prose: why fog makes the light almost useless (d03-p002, d03-p003), how the fog signal works and when it is started (d03-p004), and why the horn is not sounded all the time (d03-p007). The table d03-p005 is only one block, so the section is not mostly non-prose. Suggested fix: Add a checkpoint with one or two prompts on s0003, for example on why keepers relied on sound in fog and why the horn could not be sounded continuously, supported by d03-p002 to d03-p004 and d03-p007. | Not raised again by the checker in round 2 |
| 1 | checker | R1-I4 (minor) | s0005 | Section s0005 (d04-p001 to d04-p008, 321 words) is a whole chapter with no checkpoint. Its prose states recallable ideas: what the count of fog hours showed and why it had gone unnoticed (d04-p005), what Ione did when the clockwork stopped (d04-p006), and what the lighthouse board agreed to (d04-p007). The code block d04-p004 is a single block whose result is restated in prose. Suggested fix: Add a checkpoint with one or two prompts on s0005, for example on what the log count revealed about the fog signal and what came of it, supported by d04-p005 and d04-p007. | Not raised again by the checker in round 2 |

Every detail, including each check's findings, is in `fixture.validation-report.json`.
