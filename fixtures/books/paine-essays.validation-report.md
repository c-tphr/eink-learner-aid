READY: paine-essays.prepared.epub has passed every check and may be loaded onto the device.

# Validation report: Essays

The validator and the checker both passed this exact prepared file, and it was made from the original named below. The prepared book is in this folder as `paine-essays.prepared.epub`; the original EPUB stays a separate, unchanged file.

## About this book

- Original file: `paine-essays.epub`, SHA-256 `96d9e4c1356048ed87bc5ebd2dcbcfa21db28e5679de16f70d35ebaac6529561`.
- Prepared file: `paine-essays.prepared.epub`, SHA-256 `c9afc99966c698b85c7755670264daaf2b32e1f331c932092bb02887ea23ca50`.
- Preparation instructions version: 1.0.
- Text in no section: text/titlepage.xhtml: words 0--1 excluded by the draft: Front matter: title page (an image only).; text/imprint.xhtml: words 0-191 excluded by the draft: Front matter: the publisher's imprint.; text/agrarian-justice.xhtml: words 0-647 excluded by the draft: Front matter of this essay: its title heading followed by the Editor's Introduction (publication history written by the edition's editor, not by the author); the author's own text begins at the Author's Inscription (d09-p006).; text/colophon.xhtml: words 0-163 excluded by the draft: Back matter: the publisher's colophon.; text/uncopyright.xhtml: words 0-344 excluded by the draft: Back matter: the publisher's licence statement (Uncopyright). (as listed in the draft). Footnote and endnote bodies need no section.
- Rounds of drafting and checking: 2.
- Drafter model: claude-opus-5-5 (2026-11-19) in round 1. Checker model: claude-fable-5-2 (2026-11-19) in round 1.
- Tools: tools.prep 1.0.0, word rules version 1, EPUBCheck 5.3.0.
- Report made: 2026-11-19T22:43:17Z.

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
| 1 | checker | R1-I1 | book | Six essays of the book's body are excluded instead of being divided into sections: d04-p001 to d04-p012 (The Republican Proclamation, 657 words), d05-p001 to d05-p039 (Anti-Monarchal Essay, 2563 words), d06-p001 to d06-p038 (Declaration of Rights, 847 words), d07-p001 to d07-p088 (Dissertation on First Principles of Government, 8704 words), d08-p001 to d08-p087 (The Decline and Fall of the English System of Finance, 9624 words) and d09-p001 to d09-p090 (Agrarian Justice, 8076 words). The stated reason, 'only Common Sense is prepared in this edition', is not one of the grounds the instructions allow: section 3 permits excluding only front matter and back matter, and requires that content carrying the author's own argument gets sections like any chapter. These documents are body text (for example d07-p002 'There is no subject more interesting to every man than the subject of government'). Suggested fix: Remove the six exclusions for d04 to d09 and divide each of those documents into sections under the section rules (each short essay under 600 words or with one topic as one section), with checkpoints where the instructions call for them; keep the existing ids s0001 to s0023 and c0001 to c0023 unchanged and give the new sections and checkpoints the next unused numbers. Within d09, only matter that is truly front or back matter may stay excluded. | Not raised again by the checker in round 2 |
| 1 | checker | R1-I2 (minor) | c0004-p3 | In the answer, 'God said they had rejected him' leaves it unclear whether 'him' is God or Samuel. d03-p047 says the opposite of the second reading: 'they have not rejected thee, but they have rejected me'. Suggested fix: Replace 'God said they had rejected him' with 'God said it was God, not Samuel, they had rejected'. | Not raised again by the checker in round 2 |
| 1 | checker | R1-I3 (minor) | c0009-p2 | The answer says America was discovered 'just before the Reformation'. d03-p082 says only 'The reformation was preceded by the discovery of America'; the section does not say how closely. Suggested fix: Remove the word 'just' ('the time of America's discovery, before the Reformation, ...'). | Not raised again by the checker in round 2 |
| 2 | checker | R2-I1 (minor) | c0035-p1 | The prompt asks three things at once (titles, how aristocracy began, its effect on the mind), covering d07-p063 to d07-p068. The answer is fully supported and the prompt passes; the instructions ask for one question with at most two closely linked parts. Suggested fix: Narrow the prompt to two linked parts (for example how aristocracy began and what its titles hide) and shorten the answer to match, or leave as is if the single prompt is preferred for this short section. | Open |
| 2 | checker | R2-I2 (minor) | c0038-p1 | The first sentence says 'Insurrection and discretionary power are justified only by necessity'. d07-p084 applies 'justified by necessity' expressly to the means of overthrow, 'in general, insurrections'; of the 'discretionary exercise of power' it says that the revolutionary party 'permit to themselves' it at the commencement and that 'were the practice to continue, liberty would never be established'. The reading is fair and the prompt passes, but the wording is slightly stronger than the text. Suggested fix: Reword the first sentence along the lines of: Insurrection is justified by necessity while despotism is being overthrown, but the discretionary power revolutionaries allow themselves at the start must not continue. | Open |
| 2 | checker | R2-I3 (minor) | c0046-p2 | The answer ends 'which would stop new notes, loans and advances for the war'. d08-p078 names new emissions of bank notes, new advances to government or to the emperor, and new emissions on exchequer bills; it does not use the word loans (the stopping of 'new loans' is said of 1696 in d08-p052, section s0043). The sense is the same and the prompt passes. Suggested fix: Replace 'new notes, loans and advances for the war' with 'new notes, new advances to the government for the war and new exchequer bills'. | Open |
| 2 | checker | R2-I4 (minor) | c0050-p3 | The answer glosses agrarian law as 'dividing up cultivated land'. d09-p036 does not define the term; it says only that agrarian law would be unjust 'in a country improved by cultivation' because a man is not 'a joint proprietor of cultivated earth'. The gloss matches that sense and the prompt passes, but the wording is not the section's. Suggested fix: Replace 'Agrarian law, dividing up cultivated land, would be unjust' with 'Agrarian law, which would treat everyone as joint owner of cultivated land, would be unjust'. | Open |

Every detail, including each check's findings, is in `paine-essays.validation-report.json`.
