# Book preparation instructions

instructions_version: 1.0

These instructions are for the **drafter**: the AI session that turns an extracted book into `draft.json` (sections, checkpoints, prompts and reference answers). The **checker**, a separate session, judges the result against these same instructions. Follow them literally. When the version above changes, copy the new value into the draft.

## 1. What this is for

The reader reads a section once, often in RSVP at about 240 words per minute (one word at a time, no going back). At the end of the section the device stops and shows a prompt. The reader recalls the answer **in their head**, with the book hidden, then reveals your reference answer and marks themselves Correct or Incorrect. There is no typing and nobody else grades.

So every checkpoint is a **retrieval-practice** exercise: it makes the reader pull the section's essential ideas out of memory (recall), not pick them out from cues (recognition). A good checkpoint asks for what a thoughtful reader should keep from this section. A bad one asks for trivia, can be answered without having read the section, or gives the answer away.

## 2. What you have

The work folder that `python3 -m tools.prep extract` wrote. Read only these files:

- `source.json`: the book's title, language, `source_sha256`, and its spine (reading order). Each spine entry gives `href` (use it verbatim as `chapter_href`) and `text_file`.
- `text/dNN-<name>.txt`: one file per spine document, its blocks in order. Each block is a header line `[d03-p011] paragraph, 43 words, id ...` followed by the block's text. `d03-p011` is a **paragraph reference**: spine document 3, block 11. These references are the only way to name text in the draft.
  - `[Image: ...]`, `[Table: ...]`, `[Code: ...]` lines stand for non-prose blocks. You cannot see table cells or code, and the RSVP reader pauses at them and may skip them.
  - `[^2]` after a word is a footnote mark. Footnote and endnote bodies (`dNN-nNNN`) are listed separately at the end of a file. RSVP passes over them. They belong to no section and can never be cited.
- `structure.json`: the same blocks, machine-readable (useful for word counts).

Do not use anything you know about the book or its author from elsewhere. Everything you write must come from the extracted text.

## 3. Sections

A section is an inclusive range of paragraphs, `first_paragraph` to `last_paragraph`, inside **one** spine document. Every reading-stream block (`dNN-pNNN`) of every document must be in exactly one section or one excluded range. List sections in reading order.

### Where to break

- **Chapter boundaries are always section boundaries.** A section never crosses from one spine document into the next.
- Break at **natural boundaries**: where a complete argument, example, episode or scene ends and the next begins. Good places: just before a heading (`heading h2`, `h3`), after a paragraph that sums up, where the topic or scene clearly changes.
- Never break in the middle of an argument (between a claim and its support), a list of steps, a worked example, or a scene.
- A section that starts at a heading includes that heading.

### How long

- **Target 600 to 2,000 words** (`structure.json` gives each block's `word_count`). At 240 words per minute that is about 3 to 8 minutes of reading: long enough to hold ideas worth recalling, short enough that the reader can reread it after a wrong answer without losing the thread.
- A chapter shorter than 600 words is one section. Do not split a chapter unless each part would still be at least about 600 words, or the chapter has two clearly separate topics.
- Over 2,000 words: split at the best natural boundary inside it. A section over 3,000 words needs a clear reason (one unbroken argument).
- Natural boundaries come first: a section of 500 words that is one complete argument is better than 700 words that cut the next argument in half.

### What is excluded

Put these in `excluded`, each range inside one document, with a short `reason`:

- Front matter: title page, copyright page, dedication, epigraph, table of contents, lists of illustrations, publisher's or transcriber's notes.
- Back matter: licences (for example a Project Gutenberg licence), index, bibliography, "about the author", advertisements, colophon.

A preface, introduction, afterword or appendix that carries the author's own argument or content is **not** excluded: it gets sections like any chapter. Footnote and endnote bodies are never listed (they need neither a section nor an exclusion).

### Which sections get a checkpoint

Give every body section a checkpoint, except:

- a section of under about 200 words that only bridges two others (a short transition, a one-paragraph chapter opening);
- a section that is mostly a table, list, code or images whose content is not restated in prose;
- matter you kept as a section but that has nothing worth recalling (a short dedication-like preface).

A section without a checkpoint is still a section: it simply has no checkpoint entry. At most one checkpoint per section.

## 4. Prompts

### How many

- **1 prompt** for a section with one main idea (most sections under about 800 words).
- **2 or 3 prompts** for a section with several distinct ideas: two scenes, a claim and a separate example, a process and its result. Longer sections usually have several. Never more than 4.
- Each prompt targets a **different** essential idea. Order them as the ideas appear in the section.

### A good prompt

- Asks for an **essential idea of this section**: its main claim and the reasons for it, the key steps, the turning point of a scene, the distinction it draws. Ask yourself: "What should a reader who understood this section still be able to say tomorrow?"
- Is **answerable from this section alone**. The reader must not need another section, a footnote, a table they may have skipped, or outside knowledge.
- **Does not contain or telegraph its answer.** Do not state the answer in the question, do not quote the key phrase, do not offer the answer as a choice ("was it A or B?" when A is the answer).
- Asks **one question**. Two closely linked parts are fine ("what caused X, and what did it show?"); unrelated things are two prompts.
- Uses varied forms: "Why does the author say ...?", "What does the author claim about ..., and what support is given?", "What are the steps ...?", "How does ... differ from ...?", "What happened when ..., and why did it matter?"
- Avoids trivia (dates, numbers, names) unless the detail is the point of the section; never asks for a verbatim quotation; never asks about a figure, table or code block unless the section's prose states the same content and it is central.
- Plain text, at most **300 characters**, ending with a question mark.

### A good reference answer

- States the **essential ideas plainly**, in a sentence or three, so a reader who recalled them in their own words can recognise their answer as Correct. Lead with the core idea; add only the detail needed to make it complete.
- **Self-contained**: understandable without the question beside it and without the book.
- **Supported entirely by the cited paragraphs of the same section.** Every claim in the answer must be stated in, or follow directly and obviously from, those paragraphs. No detail from another section, a footnote, another chapter, or general knowledge, **even if it is true**. When in doubt, leave the detail out.
- Plain text, at most **600 characters**. No markup, no bullet characters, no quotation marks around the whole answer.

### Citations

Every prompt has `supporting_paragraphs`: the paragraph references that support its answer, **all inside the checkpoint's own section**. Cite every paragraph the answer draws on, and only those. Headings and image placeholders support nothing.

## 5. Ids, and why they must never change

Saved grades on the reader's device are keyed by prompt id. A changed id loses the reader's history; a reused id attaches an old grade to a different question.

- Sections `s0001`, `s0002`, ... in reading order. Checkpoints `c0001`, `c0002`, ... in reading order. Prompts `<checkpoint id>-p1`, `-p2`, ...
- **When revising, change text, not ids.** Fixing a prompt keeps its id. Fixing a section's range keeps its id.
- A new prompt gets the next unused number in its checkpoint (`c0003-p3` after `-p2`), even if that leaves gaps. A new checkpoint gets the next unused `cNNNN`. Never renumber to close gaps. Never give a deleted id to something else. Never move an id to a different section.
- These are naming conventions on the first draft; after that, stability wins over neatness.

## 6. The file you write: `draft.json`

Write exactly one file, `draft.json`, in the work folder. Its schema is `tools/prep/schema/draft.schema.json` (closed: an unknown member is an error, except members starting with `x_`).

```json
{
  "format": "rsvp-draft",
  "version": 1,
  "book": {
    "title": "<title from source.json>",
    "language": "<language from source.json>",
    "source_sha256": "<source_sha256 from source.json>",
    "instructions_version": "1.0"
  },
  "excluded": [
    { "chapter_href": "text/title.xhtml", "first_paragraph": "d01-p001", "last_paragraph": "d01-p003", "reason": "Front matter: title page." }
  ],
  "sections": [
    { "id": "s0001", "chapter_href": "text/chapter-1.xhtml", "first_paragraph": "d02-p001", "last_paragraph": "d02-p014", "label": "Why the bridge failed" }
  ],
  "checkpoints": [
    {
      "id": "c0001",
      "section": "s0001",
      "prompts": [
        { "id": "c0001-p1", "prompt": "...?", "answer": "...", "supporting_paragraphs": ["d02-p009", "d02-p010"] }
      ]
    }
  ],
  "x_drafter": { "model": "<your model id>", "date": "YYYY-MM-DD" },
  "x_changes": []
}
```

- `book.instructions_version` is the version at the top of this file.
- `label`: a short title for the section (at most 200 characters), from its heading when it has one.
- `x_drafter`: your model id and today's date. It goes into the report.
- `x_changes`: empty on the first draft. On each revision, append one entry per issue: `{ "round": <round being fixed>, "issue": "<issue id or validator code and location>", "change": "<what you changed, in one sentence>" }`.
- Use the paragraph references from the text files, never the `id ...` values shown in block headers.

## 7. Revising after an issue list

After a failed round you receive an issue list (validator failures and the checker's issues). Then:

1. Fix **exactly** what each issue flags. If an issue says an answer contains an unsupported claim, remove or correct that claim; do not rewrite unrelated prompts.
2. Keep everything else **byte-identical**: other sections, prompts, answers, ids and their order.
3. Keep ids (section 5). A rewritten prompt keeps its id; a replacement for a deleted prompt gets a new id.
4. If a fix needs a section break moved, move it, keep the section ids, and check that each prompt's citations still lie inside its section.
5. Append one `x_changes` entry per issue.
6. If you believe an issue is wrong, still make the smallest change that removes the problem the checker describes (for example, drop the disputed detail from the answer). You cannot argue with the checker; the next check decides.

## 8. Common mistakes (invented examples)

**Answer adds a detail from elsewhere.** Section s0004 describes how a mountain guide led a party down in a storm by following stone cairns. That the cairns were built by shepherds appears only in section s0002.

- Bad answer: "He followed the line of stone cairns, built by shepherds long before, from one to the next until they reached the hut."
- Good answer: "He followed the line of stone cairns from one to the next until they reached the hut."

**Answer adds a reasonable inference the text never states.**

- Bad answer: "The council closed the mill because it was losing money and the river was polluted." (The section says only that the river was polluted.)
- Good answer: "The council closed the mill because its waste had polluted the river."

**Prompt gives the answer away.**

- Bad prompt: "How did the engineer stop the boiler exploding by opening the relief valve?"
- Good prompt: "How did the engineer stop the boiler from exploding?"

**Prompt is recognition, not recall.**

- Bad prompt: "Did the author argue that rest days improve training?"
- Good prompt: "Why does the author say rest days improve training?"

**Trivia instead of the idea.**

- Bad prompt: "In what year did the survey begin?"
- Good prompt: "What did the survey show about winter nesting, and why did it surprise the team?"

**Not answerable from the section alone.**

- Bad prompt: "How does this method differ from the one in chapter 2?" (needs chapter 2)
- Good prompt: "What two weaknesses of the older method does the author point out here?"

**Answer too thin to grade.**

- Bad answer: "Because of the soil."
- Good answer: "The clay soil held water through the winter, so the roots rotted before spring."

**Citation outside the section, or missing.** Every paragraph in `supporting_paragraphs` must lie between the section's `first_paragraph` and `last_paragraph`, and every paragraph the answer draws on must be listed.

**Renumbered ids on revision.** Round 1 had `c0002-p1`, `-p2`, `-p3`; the checker failed `-p2` and you deleted it. Bad: renaming `-p3` to `-p2`. Good: keep `-p1` and `-p3`; a replacement prompt becomes `-p4`.
