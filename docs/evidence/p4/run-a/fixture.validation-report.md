READY: fixture.prepared.epub has passed every check and may be loaded onto the device.

# Validation report: The Lighthouse Keeper's Almanac

The validator and the checker both passed this exact prepared file, and it was made from the original named below. The prepared book is in this folder as `fixture.prepared.epub`; the original EPUB stays a separate, unchanged file.

## About this book

- Original file: `fixture.epub`, SHA-256 `9b35113fe38d8374524edd01a5ef06e20f3035e1fdb4232eb0ed58e0a16ca92c`.
- Prepared file: `fixture.prepared.epub`, SHA-256 `38a9e29c1a7eb371819e45a57d1aabb88d8b2d75022fc7e097b7f1bac2f8b179`.
- Preparation instructions version: 1.0.
- Text in no section: text/title.xhtml: words 0-19 excluded by the draft: Front matter: title page with the title, author and a note that the book is synthetic. (as listed in the draft). Footnote and endnote bodies need no section.
- Rounds of drafting and checking: 1.
- Drafter model: claude-opus-5-5 (2026-11-19) in round 1. Checker model: claude-fable-5-2 (2026-11-19) in round 1.
- Tools: tools.prep 1.0.0, word rules version 1, EPUBCheck 5.3.0.
- Report made: 2026-11-19T11:48:12Z.

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

No issues were raised.

Every detail, including each check's findings, is in `fixture.validation-report.json`.
