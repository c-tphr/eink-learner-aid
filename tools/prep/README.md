# tools/prep

Book preparation tools (docs/PLAN.md section 8). This package holds the `extract`, `build`, `validate`, `verify-check` and `report` commands, the script steps of the `/prep-book` loop (`start-round`, `checker-input`, `save-check`, `issues`), the drafter's instructions (`INSTRUCTIONS.md`), the checker's read guard (`checker_guard.py`), the EPUB reading library they share, the checkpoint file schema and its validator, the draft, check, validation-result and report schemas, the synthetic fixture book generator and the prepared and negative fixture generator (`fixture/`) and an EPUBCheck runner (`epubcheck.py`).

```sh
pip install -r tools/prep/requirements.txt
python3 -m pytest tools/prep -q

python3 -m tools.prep extract BOOK.epub WORKDIR                        # step 1: work folder for the drafter
python3 -m tools.prep build BOOK.epub DRAFT.json -o BOOK.prepared.epub  # step 3: prepared EPUB
python3 -m tools.prep validate BOOK.epub BOOK.prepared.epub --work-dir WORKDIR --json validation.json  # step 4
python3 -m tools.prep verify-check BOOK.epub BOOK.prepared.epub check.json  # script side of step 5
python3 -m tools.prep report --original BOOK.epub --prepared BOOK.prepared.epub --history WORKDIR -o OUTDIR  # step 7

# the /prep-book loop (see "/prep-book" below)
python3 -m tools.prep start-round WORKDIR BOOK.epub BOOK.prepared.epub
python3 -m tools.prep checker-input WORKDIR BOOK.prepared.epub
python3 -m tools.prep save-check WORKDIR
python3 -m tools.prep issues WORKDIR
```

For the owner's view (how to run `/prep-book`, what READY means, where the files are) see `docs/PREPARING_BOOKS.md`.

Exit status of every command: 0 success, 1 a problem with the input (every problem is listed on stderr), 2 a usage error. For `validate` and `verify-check`, 0 means everything passed and 1 that something failed; for `report`, 0 means READY and 1 NOT READY.

| Module | Contents |
| --- | --- |
| `__main__.py` | The command line (`python3 -m tools.prep`) |
| `words.py` | `word_stream`, the one word pass: the firmware's word definition (see "Words" below) |
| `entities.py` | The firmware's named HTML entity table (`lib/Epub/Epub/htmlEntities.cpp`), for undeclared entities |
| `refs.py` | Paragraph references (`d02-p006`) |
| `xmltree.py` | Expat-based XML tree that keeps the byte offset of every tag, for splicing |
| `epubzip.py` | Zip reader and rewriter that copies untouched entries byte for byte |
| `epub.py` | `load_book(path)` → `Book`: package, spine, table of contents, and the word stream and blocks of every spine document. `all_text` and `prose_text` give the visible text of an element |
| `extract.py` | Writes the work folder |
| `draft.py` | `check_draft(raw, book)`: draft parse, schema and cross-reference checks |
| `build.py` | `build_file(epub, draft, out)`, anchor insertion, the manifest line, `checkpoints.json` |
| `checkpoints.py` | Validator for `checkpoints.json` |
| `limits.py` | The firmware's limits on `checkpoints.json` (`lib/CheckpointCore/CheckpointLimits.h`), mirrored and checked |
| `validate.py` | `prep validate`: every script-verifiable check of a prepared EPUB against its original |
| `verify_check.py` | `prep verify-check`: what a script can confirm about the checker's `check.json` |
| `report.py` | `prep report`: the readiness rule, the round history and the validation report |
| `version.py` | Tool and word-rules versions recorded in results |
| `loop.py` | The `/prep-book` loop steps: `start-round`, `checker-input`, `save-check`, `issues` |
| `checker_guard.py` | PreToolUse hook of the `prep-checker` subagent: blocks reads outside the checker folder and every write but its one result file |
| `matter.py` | Which spine documents the EPUB marks as front, body or back matter (for the `exclusions` check) |
| `INSTRUCTIONS.md` | The drafter's (and checker's) preparation instructions, versioned (`instructions_version: 1.1`). A book prepared under an earlier version stays valid: `validate`, `verify-check` and `report` do not depend on the current version; `start-round` and `checker-input` accept only the current one, so a new run (a re-preparation) uses it |

```python
from tools.prep.checkpoints import check_file, load_checkpoints, validate_checkpoints, verdict

issues = check_file("rsvp/checkpoints.json")   # list[Issue(code, path, message)]
verdict(issues)                                # "valid", "invalid" or "unsupported-version"
data = load_checkpoints("rsvp/checkpoints.json")  # parsed dict, or raises CheckpointFileError
```

## Words

A word is what the firmware's WordIndex calls a word. The rules are **`docs/spikes/S1-word-stream.md` section 5 ("What a word is")**, the contract between `lib/WordIndex` and these tools; this README does not restate them. `words.py` implements them once, and `extract`, `build` and the fixture generator's self-check all use it. Rule-by-rule tests are in `tests/test_words.py`, including a parity test that feeds the C++ prototype's worked example (`test/spike_word_stream/fixture_s1.xhtml`) through the Python pass.

One decision goes beyond section 5: the words of a footnote or endnote body (an element whose `epub:type` has the token `footnote`, `endnote` or `endnotes`, or whose `role` has `doc-footnote`, `doc-endnote` or `doc-endnotes`) stay in the stream and form a `NOTE` region. RSVP passes over it; page view shows it.

```python
from tools.prep.words import word_stream

s = word_stream(xhtml_bytes)          # or an xmltree.Document
s.words                               # [Word(text, flags)], flags: PARA_END LINE_END NONPROSE NOTEREF HEADING BYTE_SPLIT
s.markers                             # [Marker(kind, start, end)]: IMAGE (start == end), TABLE, CODE, NOTE
s.anchors                             # [Anchor(id, index, inside_word)] for ids starting "rsvp-"
s.noterefs                            # [NoteRef(mark, word)]: each footnote mark and the word that carries NOTEREF
s.range_of(element)                   # (words started before its start tag, ... before its end tag)
```

- Flag bits are the firmware's (`WordStreamSpike.h`). There is no sentence-end flag: sentence ends are decided once, by the firmware (`lib/RsvpCore`).
- Every word of `<body>` is in the stream: headings, figure captions (prose), table and `pre` text (inside `TABLE` and `CODE` regions, flag `NONPROSE`) and footnote bodies (inside `NOTE` regions). Image alt text and footnote reference marks are never words.
- Named entities that expat hands over undeclared (`&nbsp;` under an XHTML 1.1 doctype) expand through the firmware's table in `entities.py`, which a test compares with the C++ file; unknown names, including HTML 5-only ones such as `&check;`, stay literal.
- `tokenize(text)` splits one run of plain text with the same character rules.
- Hidden, page-break and `rt` content never splits a word: the element's own tags and every tag inside it are ignored entirely (no boundary, no flag, no marker), as in the prototype. Anchors inside are still recorded (decision on pull request #56).

**Known differences from the firmware** (they cannot affect English prose in ordinary XHTML):

- **NFC.** Python composes with full NFC (`unicodedata.normalize("NFC")`, as section 5 says). The firmware's `utf8ComposeNfc` uses a smaller table (`lib/Utf8/Utf8ComposeTable.h`): combining marks U+0300 to U+036F on Latin and Vietnamese letters, plus Hangul. Text that full NFC changes beyond that (singletons such as U+212B ANGSTROM SIGN, marks of other scripts, mark reordering) can come out differently. Decomposed accented Latin letters, the only case English books have, compose the same on both sides.
- **Element names.** Python matches every element by its lowercased local name, in any namespace. The C++ prototype matches `body` and the non-visible elements (`head`, `style`, `script`, `title`, `rp`) by their qualified name, so a document that writes XHTML with a namespace prefix (`<h:body>`) can differ. Ordinary XHTML uses the default namespace, where the two agree.

## The checkpoint file, version 1

`rsvp/checkpoints.json` in a prepared EPUB. The JSON Schema is `schema/checkpoints.schema.json` (draft 2020-12), and the example in plan section 5 is `fixtures/checkpoints/valid/plan-example.json`. Validation runs in four layers, in this order. **The first layer that finds a problem stops validation**, so every issue list comes from exactly one layer.

### Layer 1: parse

- The file is UTF-8 with no byte order mark.
- It holds exactly one JSON value (RFC 8259). `NaN`, `Infinity` and trailing commas are rejected.
- No object has the same member name twice (`duplicate-key`). JSON libraries disagree on which duplicate wins, so the file is rejected.
- No string or member name contains an unpaired surrogate escape such as `"\ud800"`. A correctly paired escape such as `"😀"` is fine.

### Layer 2: version

If the top level is an object whose `format` is `"rsvp-checkpoints"` and whose `version` is an integer greater than 1, the result is a single `unsupported-version` issue at `/version`. Nothing else is checked, because a later version may use any structure. The firmware opens such a book as an ordinary EPUB with a one-line notice (plan rule 6). Any other bad `version` (0, `"1"`, `1.0`, `true`, missing) is a `schema` error.

### Layer 3: schema

| Where | Rule |
| --- | --- |
| top level | object with `format` = `"rsvp-checkpoints"`, `version` = 1, `book`, `sections`, `checkpoints`, all required |
| `book` | `title`, `language`, `instructions_version`: non-empty strings. `source_sha256`: exactly 64 lowercase hex digits |
| `sections` | array of at least one section |
| section | `id` (id syntax), `chapter_href`, `start_anchor`, `end_anchor`, `label`: non-empty strings. `word_count`: integer from 1 to 2147483647 |
| `checkpoints` | array, may be empty, order not significant |
| checkpoint | `id` (id syntax), `section` (id syntax), `prompts`: array of at least one prompt, kept in file order |
| prompt | `id` (id syntax), `prompt`: string of 1 to 300 characters, `answer`: string of 1 to 600 characters |

Definitions:

- **Characters** are Unicode code points of the decoded string, after JSON escapes are resolved. They are not UTF-8 bytes, UTF-16 code units or graphemes. `é` (U+00E9) counts 1. `e` followed by U+0301 COMBINING ACUTE ACCENT counts 2. `😀` (U+1F600) counts 1, although it is 4 UTF-8 bytes and 2 UTF-16 units. A newline counts 1.
- **Integer** means a JSON number written without a fraction or exponent. `120.0` and `1.2e2` are rejected even though their value is whole. Booleans are not integers. Stock JSON Schema validators accept `1.0` as an integer; the Python validator plugs in a strict type checker, and the C++ parser must also reject `1.0`.
- **Id syntax**: 1 to 64 characters from ASCII letters, digits, `_` and `-`, starting with a letter or digit. Ids are used inside XHTML `id` attributes and as keys in the state file. Comparison is exact and case-sensitive.
- **Unknown members are allowed at every level and are ignored** (see "Strictness" below).

### Layer 4: cross-reference

These rules are in `validate_cross_references` in `checkpoints.py`. They run only on a document that passed the schema, and every issue found is reported, not just the first.

| Code | Rule | Reported at |
| --- | --- | --- |
| `duplicate-id` | Section ids are unique among sections, checkpoint ids among checkpoints, and prompt ids among all prompts in the file. The three are separate namespaces, so a section and a checkpoint may share an id | Each repeat after the first, for example `/sections/1/id` |
| `anchor-name` | `start_anchor` is exactly `rsvp-<section id>-start` and `end_anchor` is exactly `rsvp-<section id>-end` | The anchor member |
| `unknown-section` | A checkpoint's `section` names an existing section id | `/checkpoints/<i>/section` |
| `multiple-checkpoints-for-section` | At most one checkpoint per section (rule 2: a checkpoint fires at its section's end anchor) | Each checkpoint after the first that names the section |
| `not-plain-text` | `prompt` and `answer` are plain text (definition below) | The text member |
| `blank-text` | `prompt` and `answer` are not made only of white space | The text member |

**Plain text.** A string is plain text when it contains none of the following:

1. A code point of Unicode general category Cc other than U+000A LINE FEED. That is U+0000 to U+0009, U+000B to U+001F, and U+007F to U+009F. So tab, carriage return, NUL, ESC, DEL and C1 controls such as U+0085 are rejected. Lines are separated by LF alone.
2. A markup start: `<` immediately followed by an ASCII letter, `/`, `!` or `?`. `a < b`, `x<3` and `a <= b` are allowed. `x<y` is rejected; write `x < y`.
3. A complete character reference: `&` followed by a name (ASCII letter, then letters or digits), by `#` and decimal digits, or by `#x`/`#X` and hex digits, then `;`. `Q&A`, `R & D`, `&amp` without a semicolon, `&#;` and `&#x;` are allowed.

Other code points, including U+00A0, U+200B and U+2028, are allowed. Only `prompt` and `answer` are checked; `label` and `book.title` are not.

**White space** means the code points with the Unicode White_Space property: U+0009 to U+000D, U+0020, U+0085, U+00A0, U+1680, U+2000 to U+200A, U+2028, U+2029, U+202F, U+205F and U+3000. U+200B ZERO WIDTH SPACE is not white space. An empty string is caught earlier as a `schema` error.

### Strictness and forward compatibility

The schema does not set `additionalProperties: false`. Unknown members are allowed at every level and every reader must ignore them, including their contents (markup or control characters in an unknown member are not checked). The reasons:

- **Additive changes stay readable.** A later preparation tool can add an optional member, such as supporting paragraph numbers for a prompt, without a version bump. Firmware already on a device keeps reading the checkpoints and ignores the new member. With a closed schema, every addition would need version 2, and older firmware would then drop all checkpoints for that book.
- **Typos are still caught.** Every version 1 member is required, so a misspelled required member shows up as a missing one.
- **Breaking changes use `version`.** A change that a version 1 reader cannot safely ignore (a new meaning for an existing member, or a member that changes when a checkpoint fires) must raise `version`, so old firmware falls back to an ordinary EPUB.

Producers should prefix experimental members with `x_` so they never collide with members a future version defines.

## Issue codes

| Code | Layer | Meaning |
| --- | --- | --- |
| `parse` | parse | Not UTF-8, has a BOM, not one JSON value, NaN/Infinity, or an unpaired surrogate |
| `duplicate-key` | parse | An object repeats a member name |
| `unsupported-version` | version | `version` is an integer above 1. The verdict is `unsupported-version`, not `invalid` |
| `schema` | schema | Fails `checkpoints.schema.json` |
| `duplicate-id` | cross-reference | Repeated section, checkpoint or prompt id |
| `anchor-name` | cross-reference | Anchor does not match the section id |
| `unknown-section` | cross-reference | Checkpoint names a missing section |
| `multiple-checkpoints-for-section` | cross-reference | Second or later checkpoint on one section |
| `not-plain-text` | cross-reference | Control character, markup start or character reference in a prompt or answer |
| `blank-text` | cross-reference | Prompt or answer is white space only |

Each issue also has `path`, an RFC 6901 JSON Pointer (`""` is the document root; for a missing member it points at the object that lacks it), and a human-readable `message`. Messages are not stable; codes and paths are.

## Shared samples: `fixtures/checkpoints/`

`valid/*.json` and `invalid/*.json` are run through this validator by `tests/test_checkpoints.py`, and are meant to be run through the C++ parser tests too (task C1a). `manifest.json` lists every file:

```json
{
  "file": "invalid/two-checkpoints-one-section.json",
  "verdict": "invalid",
  "layer": "cross-reference",
  "issues": [{ "code": "multiple-checkpoints-for-section", "path": "/checkpoints/1/section" }],
  "reason": "Two checkpoints name section 's0001'; a section has at most one checkpoint (rule 2)."
}
```

- `file` is relative to `fixtures/checkpoints/`.
- `verdict` is `valid`, `invalid` or `unsupported-version`. Every `valid/` file is `valid`. The `unsupported-version` samples live in `invalid/`.
- `layer` is `parse`, `version`, `schema` or `cross-reference` (`null` for valid samples).
- `issues` is the expected set of `{code, path}` pairs. Each invalid sample breaks exactly one rule, so all its issues share one code. A schema sample can list two entries with the same code and path when one value breaks two keywords (`version: "1"` is both the wrong type and not 1); compare as a set.

How a C++ parser test should consume it:

1. Read `manifest.json` and loop over `samples`. Fail if any file in `valid/` or `invalid/` is missing from the manifest.
2. Feed each file's bytes to the parser and compare the verdict. This is the minimum requirement.
3. Map parser errors to these codes and compare the set of codes for each sample. For `parse`, `duplicate-key`, `unsupported-version` and the cross-reference codes, also compare paths. For `schema` issues the path is the location the Python validator reports; a C++ parser may report a different number of issues at a location, so compare schema issues by code, and by path where practical.
4. Apply the same layer order and stop at the first failing layer, so the sets match.

A sample may be added only together with its manifest entry. `tests/test_checkpoints.py` fails if the two drift apart, if an invalid sample lists more than one code, or if any issue code has no sample.

## The draft file, version 1

`draft.json` is what the drafter session writes (plan section 8, step 2) and what `prep build` reads (step 3). Its JSON Schema is `schema/draft.schema.json` (draft 2020-12). The worked example is `fixtures/books/fixture.draft.json`.

```json
{
  "format": "rsvp-draft",
  "version": 1,
  "book": { "title": "...", "language": "en", "source_sha256": "<hash of the original EPUB>", "instructions_version": "1.0" },
  "excluded": [
    { "chapter_href": "text/title.xhtml", "first_paragraph": "d01-p001", "last_paragraph": "d01-p003", "reason": "Front matter: title page." }
  ],
  "sections": [
    { "id": "s0001", "chapter_href": "text/chapter-1.xhtml", "first_paragraph": "d02-p001", "last_paragraph": "d02-p006", "label": "Arrival at Gannet Rock" }
  ],
  "checkpoints": [
    {
      "id": "c0001",
      "section": "s0001",
      "prompts": [
        { "id": "c0001-p1", "prompt": "...?", "answer": "...", "supporting_paragraphs": ["d02-p006"] }
      ]
    }
  ]
}
```

| Member | Rule |
| --- | --- |
| `format`, `version` | `"rsvp-draft"` and integer `1` |
| `book` | `title`, `language`, `instructions_version`: non-empty strings. `source_sha256`: SHA-256 of the original EPUB, 64 lowercase hex digits; `prep build` refuses a draft made from another file |
| `excluded` | Array, may be empty. Each entry is a paragraph range (`chapter_href`, `first_paragraph`, `last_paragraph`) plus a non-empty `reason`. Lists front or back matter that belongs to no section |
| `sections` | At least one, in reading order. Each is a paragraph range plus `id` (id syntax) and a non-empty `label`. It becomes one section of `checkpoints.json`; `word_count` and the anchors are computed by `prep build`. `book.title`, `book.language`, `book.source_sha256` and `book.instructions_version` are copied into `checkpoints.json` unchanged |
| `checkpoints` | Array, may be empty. `id`, `section` (a section id) and `prompts` (at least one, file order kept) |
| prompt | `id`, `prompt` (1 to 300 characters), `answer` (1 to 600 characters), `supporting_paragraphs` (at least one paragraph reference, no repeats) |

Definitions:

- **Paragraph range.** `chapter_href` is a spine document's manifest href, exactly as `source.json` lists it (relative to the OPF, as in `checkpoints.json`). `first_paragraph` and `last_paragraph` are inclusive paragraph references (`d02-p001`, see "Paragraph identity" below) to reading-stream blocks of that document. They are never the source's XHTML ids: the fixture's blocks carry ids such as `c1-p003` only to show that `prep extract` records them; its draft cites `d02-p003`. The schema checks the reference syntax (`^d[0-9]{2,}-p[0-9]{3,}$`), so a footnote body (`d02-n001`) or a source id cannot be cited.
- **Ids** use the id syntax of `checkpoints.json`. Conventions (not rules): sections `s0001`, `s0002`, ...; checkpoints `c0001`, ... in order; prompts `<checkpoint id>-p1`, `-p2`, ...
- **Closed schema.** Unlike `checkpoints.json`, every object rejects unknown members, so a misspelt member in a drafter's output is caught at once. Members whose names start with `x_` are allowed and ignored.

Cross-reference rules (checked by `prep build` in `draft.py`, and for the fixture also by `tests/test_fixture.py`):

1. Section ids are unique, checkpoint ids are unique, prompt ids are unique across the file.
2. Every paragraph reference exists in its `chapter_href`. `last_paragraph` is not before `first_paragraph`, so a range lies inside one document (plan rule 1).
3. Ranges (sections and exclusions together) do not overlap, and every reading-stream block of every spine document is in exactly one of them. Footnote bodies (`n` blocks) are neither covered nor excluded; their words are in the word stream as NOTE regions, but a draft never cites them.
4. Sections appear in reading order.
5. A checkpoint names an existing section; at most one checkpoint per section (plan rule 2).
6. Every `supporting_paragraphs` entry lies inside the checkpoint's section.
7. `prompt` and `answer` are plain text and not blank, as defined for `checkpoints.json` above.
8. A section has at least one word between its anchors (a section holding only an uncaptioned image would get `word_count` 0, which `checkpoints.json` forbids).
9. The section's anchors can be placed (see "Anchor placement") and their ids are not already used in the document.

`prep build` checks in three stages and stops after the first stage that finds anything: (1) parse (as `checkpoints.json` layer 1) and schema, (2) `book.source_sha256` equals the SHA-256 of the EPUB given, (3) every cross-reference rule, all reported together. Each problem is printed as `<code> at <JSON pointer>: <message>`:

| Code | Meaning |
| --- | --- |
| `parse`, `duplicate-key`, `schema` | As for `checkpoints.json`. Over-long text reads "answer has 601 characters; the limit is 600"; a source id reads "'c1-p001' is not a paragraph reference such as 'd02-p006'" |
| `source-mismatch` | The draft was made from another file |
| `already-prepared` | The input already holds `rsvp/checkpoints.json` or the manifest item |
| `unknown-document` | `chapter_href` is not an XHTML spine document |
| `unknown-paragraph` | A reference that does not exist (the message gives the document's range), or a footnote body |
| `wrong-document` | Both ends exist, but in another document than `chapter_href` |
| `range-across-documents` | The two ends are in different documents |
| `reversed-range` | `last_paragraph` comes before `first_paragraph` |
| `overlapping-ranges` | Paragraphs claimed by two ranges (names the other range) |
| `uncovered-paragraphs` | Paragraphs in no section and not excluded, as runs |
| `section-order` | Sections not in reading order |
| `empty-section` | A section without words between its anchors |
| `duplicate-id`, `unknown-section`, `multiple-checkpoints-for-section`, `not-plain-text`, `blank-text` | As for `checkpoints.json` |
| `support-outside-section` | A supporting paragraph outside the checkpoint's section |
| `anchor-placement`, `anchor-id-conflict` | An anchor cannot be placed validly (no element may hold it there, or the word pass would put it strictly inside a word), or its id is taken |

## `prep extract`: the work folder

`python3 -m tools.prep extract BOOK.epub WORKDIR` writes what the drafter and checker sessions read; they see only these files. `WORKDIR` is created if missing. An existing folder must be empty or hold a previous `source.json`; re-extracting replaces `source.json`, `structure.json` and `text/*.txt` and leaves other files (a `draft.json`, say) alone. Output is deterministic.

```
WORKDIR/
  source.json                 the book: file name, SHA-256, title, language, OPF path, spine, table of contents
  structure.json              every block of every spine document, machine-readable
  text/d01-title.txt          one file per XHTML spine document: its blocks as numbered text
  text/d02-chapter-1.txt
  ...
```

- `source.json`: `source_file`, `source_sha256` (copy it into the draft's `book.source_sha256`), `title`, `language`, `opf_path`, `opf_dir`; `spine` in reading order, each with `index`, `prefix` (`d02`), `idref`, `href` (verbatim from the manifest: use it as `chapter_href`), `media_type`, `linear`, `title` (the document's `<title>`), `text_file` (the index from text file to href; `null` for a non-XHTML spine item), `blocks`, `first_paragraph`, `last_paragraph`, `words` (the length of the document's word stream), `footnote_blocks`; `toc`, the nav document's `toc` list (or the NCX when there is no nav), each entry with `label`, `href` (relative to the OPF directory), `depth`, `source` and `paragraph`, the block the entry points at.
- `structure.json` (`version` 2): per document (`index`, `prefix`, `href`, `zip_path`, `media_type`, `linear`, `text_file`, `word_count`, the length of its word stream) the `blocks` and the `footnotes`, each with `ref`, `number`, `kind`, `element`, `source_id` (the element's `id`, or `null`), `word_count`, `level` (headings), `first_word` (0-based in the document's word stream: the block's words are `[first_word, first_word + word_count)`; for a block without words, the index of the next word), `placeholder` (non-prose), `note_id` (footnotes: the footnote container's id), `path` (an XPath-like locator such as `/html[1]/body[1]/p[3]`), `line` (source line of the start tag) and `anchor_placement` (`inside`, `around` or `none`). Version 1 had `last_word` and, for non-prose blocks, `after_word` instead, and counted no words in figures, tables, code and footnote bodies.
- `text/dNN-<file name>.txt`: a header, then one entry per block, separated by blank lines: `[d02-p005] paragraph, 66 words, id c1-p005` and on the next line the block's words joined by single spaces. A footnote mark is shown glued to the word that carries its `NOTEREF` flag as `[^1]` and is not a word. A non-prose block's header gives its word count too (caption, cells, code), and its line shows a placeholder such as `[Image: <figure>; alt "..."; caption "..."]`, `[Table: <table>; 4 rows x 3 columns; caption "..."]` or `[Code: <pre>; 6 lines]`. Footnote bodies follow under their own heading as `[d02-n001] footnote, 24 words, note fn1, id c1-n001`.

## Paragraph identity

`prep extract` is the single source of paragraph identities: the drafter, the checker and `prep build` all use its references, and `build` recomputes them from the same EPUB with the same code (`epub.py`), which the SHA-256 check guarantees is the same file. Identities never depend on ids in the source markup, which real books mostly lack.

- **Reference**: `d<spine position>-p<block number>`, both 1-based. The spine position is zero-padded to the width of the spine length (at least 2 digits), the block number to the width of the document's block count (at least 3): `d02-p006`. Footnote bodies are numbered in their own series, `d02-n001`. Every spine item gets a position, including non-linear and non-XHTML ones, so a reference stays fixed when the drafter excludes a document.
- **What is a block** (in document order inside `body`):
  1. Text that is in no word (section 5: `head`, `style`, `script`, `title`, `rp`, `rt`, elements with a `hidden` attribute, page-break markers with `role="doc-pagebreak"` or `epub:type="pagebreak"`) never makes a block.
  2. Non-prose blocks: `table` (kind `table`), `pre` (`code`), `img`, `svg`, `picture` (`image`), `math` (`math`), `video`, `audio`, `object`, `iframe`, `embed`, `canvas` (`media`), and `figure` when it holds one of those (kind from its content).
  3. Footnote and endnote bodies (the `NOTE` regions: `epub:type` with a footnote, endnote or endnotes token, or `role` doc-footnote, doc-endnote or doc-endnotes) get kind `footnote` and `n` references. Their words are in the stream, but a draft cannot cite them.
  4. Any other element with text of its own (a non-blank text node, or an inline child with words, not counting footnote marks, non-prose descendants and footnote bodies) is one prose block, with everything inside it. Kinds: `heading` (`h1` to `h6`), `list-item` (`li`, `dt`, `dd`, or inside one), `blockquote` (or inside one), `aside` (inside a non-footnote `aside`), else `paragraph`. An element without text of its own is not a block; its children are visited. "Block-level" here means the elements at which the word pass splits words (section 5's boundary elements other than `br`, `img` and `image`), so a block's words never straddle its tags.
  5. A prose block without words (an empty spacer paragraph, including one holding only a no-break space) is not a block.
- **Words of a block**: the words of the document's word stream (see "Words") that start inside the block's element: `[first_word, first_word + word_count)`. Every word of the fixture belongs to exactly one block. `fixture.expected.json` is reproduced exactly: same words, flags, markers, kinds, block boundaries and footnote bodies.
- **Changes from aligning with the firmware's rules (issue #49).** Block numbering is unchanged for ordinary books. It changes only where a rule forced it: `noscript` and `template` text are now words (section 5 does not exclude them), so they no longer hide a paragraph; `dialog`, `fieldset`, `form` and `menu` are no longer block-level (they do not split words); `rearnote`, `note`, `footnotes` and `rearnotes` containers are no longer footnote bodies (the NOTE rule names footnote, endnote and endnotes only), so their blocks are ordinary blocks a draft must cover; and a paragraph holding only no-break spaces, soft hyphens or U+FEFF has no words and is no longer a block.

## `prep build`: the prepared EPUB

`python3 -m tools.prep build BOOK.epub DRAFT.json -o OUT.prepared.epub` checks the draft (above), then writes a copy of the EPUB that differs in exactly three ways. It refuses an output path that is the input (also through `..` or a hard link) or the draft, writes through a temporary file in the output folder, and never opens the input for writing. Building twice gives identical bytes.

1. **Anchors** in the section's chapter document: `<span id="rsvp-<section id>-start"></span>` and `...-end"></span>`, spliced into the original bytes at offsets found by the parser. Nothing is re-serialised, so the XML declaration, doctype, namespaces, entity references, attribute order and white space of every other byte stay as they were. Tests remove the inserted spans from each changed document and compare the rest byte for byte with the original.
2. **One manifest line** in the package document, `<item id="rsvp-checkpoints" href="rsvp/checkpoints.json" media-type="application/json"/>`, inserted before `</manifest>`. When `</manifest>` starts its own line, the item is a new line indented like the manifest's last item and ended with the file's own line break (LF or CRLF); otherwise it is inserted right before `</manifest>`.
3. **`rsvp/checkpoints.json`** in the package document's directory (`OEBPS/rsvp/checkpoints.json`, or `rsvp/checkpoints.json` for an OPF at the archive root), version 1, UTF-8, two-space indented, ending in a newline. `book` holds the draft's `title`, `language`, `source_sha256` and `instructions_version`; `sections` follow the draft's order with `chapter_href` as the manifest href, the two anchor names, the draft's `label` and `word_count`, the number of words in `[start, end)` where `start` and `end` are the word indexes of the start and end anchors as the firmware computes them (every word between the anchors counts: headings, captions, table cells, code and any footnote body that falls inside); `checkpoints` and their prompts keep the draft's order, ids, prompts and answers. `supporting_paragraphs` stay in the draft. The file is validated with `checkpoints.validate_checkpoints` before it is written.

Zip container: every entry the build does not change is copied byte for byte (local header, extra fields, compressed data, data descriptor), with its compression method, timestamp and attributes, in the original order, with the original archive comment; `mimetype` therefore stays first and stored. A changed entry keeps its method (stored or deflate), timestamp, name and extra fields; only its CRC and sizes change, and a data descriptor is not written for it. The new JSON entry goes last and copies the package document's method, timestamp and attributes. ZIP64, encrypted entries and multi-disk archives are refused. Before writing, the build runs the word pass on every changed document and refuses (`anchor-placement`) if an anchor would sit strictly inside a word. After building, the tool re-reads its output and stops if any document's words, flags, markers, footnote marks, visible text or blocks changed, or an anchor is missing, repeated or not at the word index the section's `word_count` was computed from.

EPUBCheck 5.3.0 reports no error and no warning for the `application/json` manifest item that the spine does not reference; with `--usage` it lists one USAGE message, `OPF-097` ("listed in the manifest, but no reference to it was found in content documents"). The format the plan specifies therefore needs no change.

### Anchor placement

The start anchor precedes the section's first word and the end anchor follows its last word, at a word boundary, and no word between two sections is left outside them other than excluded matter and footnote bodies that lie between the sections' blocks.

| Section's first or last block | Start anchor | End anchor |
| --- | --- | --- |
| Prose (`p`, `h1`-`h6`, `li`, `dt`, `dd`, `div`, `blockquote`, ...): placement `inside` | First child of the block, right after its start tag: `<p><span id="rsvp-s0001-start"></span>The ...` | Last child of the block, right before its end tag, after any footnote mark: `... one.<sup>...</sup><span id="rsvp-s0001-end"></span></p>` |
| Non-prose (`figure`, `table`, `pre`, `img`, `svg`, `math`, media): placement `around` | A sibling right before the block's start tag | A sibling right after the block's end tag |

- Non-prose blocks get sibling anchors because a `span` may not be a child of `table`, `tr` or `tbody`, may break `figure`'s rule that `figcaption` is its first or last child, and inside `pre` would sit in preformatted text; anchors therefore never fall inside a table or code block. A sibling `span` is allowed wherever the block's parent takes phrasing content (`body`, `div`, `section`, `li`, `td`, ...). When the parent does not (`ul`, `ol`, `dl`, `table` parts, `figure`, `hgroup`, ...), the placement is `none` and the build refuses with `anchor-placement` rather than write invalid XHTML.
- Two sections meeting in adjacent blocks get separate anchors: the end anchor inside or after the last block of one, the start anchor inside or before the first block of the next. When both fall on the same byte offset (a section ending with a table directly followed by one starting with a `pre`), the end anchor is written first: `</table><span id="rsvp-s0001-end"></span><span id="rsvp-s0002-start"></span><pre>`.
- The span is written with the namespace prefix of its parent element, so a book that writes XHTML with a prefix still gets XHTML spans.
- Anchors are placed at element boundaries of blocks, and blocks are elements at which the firmware's word pass splits words, so they sit between words (`docs/spikes/S1-word-stream.md` section 5). The build checks this with the word pass itself (above).

## `prep validate`: the prepared EPUB against its original

```sh
python3 -m tools.prep validate ORIGINAL.epub PREPARED.epub [--draft DRAFT.json | --work-dir WORKDIR] [--json OUT] [--timestamp ISO]
```

Everything a script can verify about a prepared book is verified here (plan section 8, "What the validator script checks"); the AI checker is left only what needs reading comprehension. The command prints a table of the checks to stdout and every failure to stderr as `<check>: <code> at <location>: <message>`, and exits 0 only when **every** check has status `pass`. A check has one of four statuses:

| Status | Meaning |
| --- | --- |
| `pass` | The check ran on the whole book and found nothing |
| `fail` | Failures are listed |
| `incomplete` | No failure found, but part of the book could not be judged because an earlier check failed there (a section whose anchors did not resolve is not counted, covered or boundary-checked). Not a pass |
| `not-run` | The check could not run (the file does not open; EPUBCheck cannot be obtained). Not a pass: an unchecked book is never a passed one |

`--json OUT` writes the result (`schema/validation.schema.json`): `format` `rsvp-validation`, `version` 1, `tool` (`name`, `version` from `version.py`, `word_rules_version`, `epubcheck_version` or null), `created`, `original` and `prepared` (file name and SHA-256), `draft` (the draft used for exclusions, or null), `coverage_mode` (`draft` or `implicit`), `passed`, and `checks`: each with `name`, `title`, `status`, `failures` (`code`, `location`, `message`) and `details` (what was looked at). There are 13 checks; a result written by tools.prep 1.0.0, before the `exclusions` check existed, has 12 and remains a valid record (the schema ties the count to `tool.version`). File names are recorded without their directory and the time is `--timestamp`, else `$SOURCE_DATE_EPOCH`, else the clock, so the same inputs and time give the same bytes. The original is only ever read, and is hashed again at the end to prove it did not change.

Locations are a zip path, with `#<JSON pointer>` into the checkpoint file, `#<id>` for an anchor, or ` words A-B` / ` word N` (0-based indexes into the document's word stream). Codes are stable; messages are not.

| Check | Code | Meaning |
| --- | --- | --- |
| `open` | `unreadable-original`, `unreadable-prepared` | The file is not a readable EPUB (zip, container, package document, every XHTML spine document parses). Every later check that needs it is `not-run` |
| `source-hash` | `source-mismatch` | `book.source_sha256` is not the SHA-256 of the original given |
| | `original-changed` | The original's bytes differ after validation from before |
| `checkpoint-item` | `checkpoint-item-missing`, `checkpoint-item-repeated` | Not exactly one manifest item with id `rsvp-checkpoints` (a second item declaring the file also counts as repeated) |
| | `checkpoint-item-media-type`, `checkpoint-item-href` | The item is not `application/json`, or does not resolve to `rsvp/checkpoints.json` beside the package document |
| | `checkpoint-file-missing` | The archive has no such member |
| `checkpoint-file` | `parse`, `duplicate-key`, `unsupported-version`, `schema`, `duplicate-id`, `anchor-name`, `unknown-section`, `multiple-checkpoints-for-section`, `not-plain-text`, `blank-text` | `checkpoints.py`'s four layers, codes as in "The checkpoint file, version 1" above. After a parse, version or schema failure the structure checks below are `not-run` |
| `firmware-limits` | `limit` | A limit of `lib/CheckpointCore/CheckpointLimits.h` is exceeded; the message starts with the constant's name (`MAX_LABEL_CHARS: ...`). See "Firmware limits" below |
| `anchors` | `unknown-document` | A section's `chapter_href` is not an XHTML spine document |
| | `anchor-missing` | `rsvp-<id>-start` or `-end` is in no document (for an end anchor the message names the checkpoint that could never fire) |
| | `anchor-repeated` | The anchor id occurs more than once |
| | `anchor-wrong-document` | The anchor is in another document than the section's `chapter_href` |
| | `section-spans-documents` | The two anchors are in different documents (plan rule 1) |
| | `anchor-element` | The anchor is not an empty XHTML `<span>` whose only attribute is `id` |
| | `anchor-not-in-body` | The anchor is outside the visible body, where the firmware does not look |
| | `anchor-order` | The start anchor is not before the end anchor |
| | `section-overlap`, `section-order` | Two sections of one document overlap; sections are not listed in reading order (document order inside a document, spine order across documents) |
| | `unaccounted-anchor` | An element of an XHTML document in the manifest has an id starting `rsvp-` that is no section's anchor (the firmware treats every such id as an anchor) |
| `word-boundaries` | `anchor-inside-word` | The shared word pass puts the anchor strictly inside a word |
| | `anchor-in-table`, `anchor-in-code` | The anchor has a `table` or `pre` ancestor |
| `coverage` | `uncovered-words` | A run of words of a spine document is in no section, in no footnote or endnote body (NOTE region) and not excluded |
| | `excluded-in-section` | Words both excluded by the draft and inside a section |
| | `draft-unreadable`, `draft-mismatch`, `draft-exclusion` | The draft given fails to parse or its schema; it was made from another original or has other sections (ids, chapters, labels) than the checkpoint file; an excluded range does not resolve |
| `exclusions` | `exclusion-category` | An excluded range's `reason` does not start with `Front matter`, `Back matter` or `Editorial matter` (draft mode only). See "The exclusions check" below |
| | `excluded-body-document` | A whole document with words is excluded, it lies between two documents that hold sections, and the EPUB does not mark it as front or back matter |
| | `excluded-body-text` | Excluded words in documents the EPUB marks as body matter, plus excluded words in unmarked documents between the first and the last section, are more than 10% of the book's words outside NOTE regions |
| `word-counts` | `word-count` | A section's `word_count` is not the number of words in `[start, end)` of its anchors' word indexes |
| `text-unchanged` | `spine-changed` | The spine differs from the original's |
| | `words-changed`, `flags-changed`, `markers-changed`, `noterefs-changed` | The word pass (`words.py`) on a spine document gives other words (the first difference is shown), other flags, IMAGE/TABLE/CODE/NOTE markers or footnote marks than on the original |
| `archive-unchanged` | `member-added`, `member-removed` | The members are not exactly the original's plus the checkpoint file |
| | `opf-changed` | Removing the one `rsvp-checkpoints` item (as an element, or as its own line) does not give the original package document |
| | `document-changed` | Removing every empty element with an `rsvp-` id from a changed XHTML spine document does not give the original bytes |
| | `member-changed`, `member-unreadable` | Any other member is not byte-identical (compared uncompressed) or cannot be read |
| `epubcheck` | `epubcheck-error` | EPUBCheck 5.3.0 reports an error or fatal error (warnings go to `details`) |
| | `epubcheck-unavailable` | EPUBCheck cannot be obtained or produced no report; status `not-run` |

A broken file usually fails one check: a missing end anchor fails `anchors` and leaves `word-boundaries`, `coverage` and `word-counts` `incomplete` for that chapter. A changed word fails both `text-unchanged` and `archive-unchanged`, by design: one compares words, the other bytes.

### Exclusions: how `validate` knows them

The checkpoint file does not list excluded front and back matter, and its format does not change for that. The exclusions live in the draft the book was built from (`excluded`, with a reason each), which is the drafter's explicit record and which `/prep-book` keeps in the work folder and in each round's folder. So the draft is an optional third input:

- `--draft DRAFT.json`, or `--work-dir WORKDIR` (its `draft.json`): coverage mode `draft`. Each excluded paragraph range is resolved against the **original** (the word streams are proved identical by `text-unchanged`), and every word of every XHTML spine document must be in exactly one place: a section, an excluded range or a NOTE region. The draft must be made from this original and have the checkpoint file's sections, or coverage fails with `draft-mismatch`. `prep report` passes the latest round's draft.
- No draft: coverage mode `implicit`. A spine document with no section at all counts as excluded as a whole and is listed in `details` (`text/title.xhtml: no section; treated as excluded matter (20 words ...)`); a document with any section must be covered completely apart from NOTE regions. This can wrongly fail a book that excludes part of a chapter (pass the draft). The `exclusions` check below judges these whole-document exclusions too, without the reason rule.

### The exclusions check

`coverage` proves that every word is in a section or a listed exclusion; `exclusions` judges whether the exclusions are allowed (`INSTRUCTIONS.md` section 3, which allows front matter, back matter and editorial matter only). It was added after the first real book, whose first round excluded six of its seven essays (about 30,500 words, 60% of the book) with every other check green; only the checker caught it. The rule uses the EPUB's structural markup only, never the book's words, so it does not depend on the book's language:

1. **Category** (`exclusion-category`, draft mode only). Each `reason` starts with `Front matter`, `Back matter` or `Editorial matter` (any letter case), followed by a colon, a space or nothing.
2. **Where the EPUB says a document belongs** (`matter.py`). A spine document is *front-back* when `epub:type` on its `html` or `body` element (or the body's only child element) names front or back matter (`frontmatter`, `backmatter`, `titlepage`, `imprint`, `colophon`, `copyright-page`, `toc`, `index`, `endnotes`, and the like) and no body matter; *body* when it names `bodymatter`, `chapter`, `part`, `division` or `volume`. Otherwise the nav document's `landmarks` and the EPUB 2 `guide` decide: a link to the document with such a type, then its place: documents before the one the `bodymatter` landmark (or the guide's `text` reference) points to are front-back, documents from there to the first front or back matter document after it are body, and documents after that are front-back. A book with none of this markup has only *unmarked* documents.
3. **No body document left out** (`excluded-body-document`). A whole document with words may not be excluded between two documents that hold sections, unless it is front-back.
4. **A limit on excluded body text** (`excluded-body-text`). Excluded words in body documents, plus excluded words in unmarked documents that lie between the first and the last section, may be at most 10% of the book's words outside NOTE regions (`MAX_EXCLUDED_BODY_PERCENT` in `validate.py`). That leaves room for editorial matter inside a body document, such as an editor's introduction to one essay. Exclusions in front-back documents never count, and nor do exclusions in unmarked documents before the first or after the last section (a title page, a Project Gutenberg licence at the end).

`details` list each document with excluded words and the markup that classified it, and the counted total against the limit. On the committed real book (`fixtures/books/paine-essays.*`) the check passes: 1,349 words are excluded, of which only the editor's introduction of *Agrarian Justice* (648 words, 1.3%) is in a body document. On that book's round-1 draft (`docs/evidence/p5/rounds/01/draft.json`) it fails with `exclusion-category` six times and `excluded-body-text` (60.3%). `fixtures/books/negative/excluded-chapter.*` excludes chapter 2 of the synthetic fixture and fails with all three codes.

What it cannot see: a book without structural markup that leaves out its last chapters (they come after the last section, so they are not counted), and a body document excluded at the start or the end of a long book where the excluded text is under 10%. The checker still judges every exclusion against the instructions; this check only catches the large mistakes before a round is spent on them.

### Firmware limits

`limits.py` mirrors every limit of `lib/CheckpointCore/CheckpointLimits.h` by its C++ name, and `tests/test_limits.py` parses the header and fails if a number differs or a new limit is not mirrored. The limits a schema can express are also in `schema/checkpoints.schema.json`: `maxItems` 512 on `sections` and `checkpoints`, 32 on a checkpoint's `prompts`; `maxLength` 300 on `book.title`, 35 on `book.language`, 32 on `book.instructions_version`, 255 on `chapter_href`, 200 on `label` (ids, prompts and answers were already limited). Adding them changed no verdict of the shared samples in `fixtures/checkpoints/`. Only `limits.py` checks the rest: file size (1,048,576 bytes), prompts in the file (1,024), distinct `chapter_href` values (256), the string pool (16,384 bytes: section ids, checkpoint ids, checkpoint `section` values and distinct chapter hrefs, each as UTF-8 plus one byte), nesting depth (32), members of open objects (128) and number length (64 characters).

## `check.json`: the checker's result

The checker session (plan section 8, step 5; a separate task writes its instructions) reads the work folder and the prepared book's checkpoint file, and writes `check.json` (`schema/check.schema.json`, closed, `x_` members allowed). Example: `fixtures/books/fixture.check.json`.

```json
{
  "format": "rsvp-check",
  "version": 1,
  "prepared_sha256": "<SHA-256 of the prepared EPUB judged>",
  "original_sha256": "<SHA-256 of the original, as in source.json>",
  "instructions_version": "1.0",
  "checker": { "model": "<model id>", "date": "2026-11-18" },
  "round": 1,
  "prompts": [
    {
      "id": "c0001-p1",
      "verdict": "pass",
      "supported_by_section": true,
      "citations": ["d02-p006"],
      "accepts_paraphrase": true,
      "answerable_from_section": true,
      "does_not_give_away_answer": true,
      "evidence": "d02-p006 says each light had its own pattern of flashes ..."
    }
  ],
  "sections": [{ "id": "s0001", "natural_boundary": true, "evidence": "Ends after ..." }],
  "issues": [
    { "id": "I1", "severity": "blocking", "target": "c0002-p2", "description": "...", "suggested_fix": "...", "status": "open" }
  ],
  "verdict": "fail"
}
```

- One entry per prompt and per section of the checkpoint file. A prompt's four booleans are the plan's criteria: the answer is supported by the prompt's own section; it states the essential ideas clearly enough that a correct paraphrase can be accepted; the prompt can be answered from its section alone; it does not give its answer away. `evidence` is free text.
- **What the checker must cite:** `citations` are the paragraph references `prep extract` wrote (`d02-p006`, as in `text/*.txt`), of reading-stream paragraphs inside the prompt's own section. Never XHTML ids, never footnote bodies (`dNN-nNNN`). A `pass` needs at least one.
- `issues`: `severity` `blocking` (the book cannot be delivered) or `minor`; `target` a prompt, checkpoint or section id, or `book`; `status` `open` or `resolved` (with an optional `resolution`). An issue keeps its `id` across rounds, so a later round can mark it `resolved`.
- `verdict` is `pass` only if every prompt and section passes and no blocking issue is open.

`python3 -m tools.prep verify-check ORIGINAL.epub PREPARED.epub check.json [--json OUT]` enforces what a script can, exit 0 only when nothing fails **and** the checker's verdict is pass:

| Code | Meaning |
| --- | --- |
| `check-unreadable`, `check-schema` | Not one JSON object without duplicate names; fails the schema |
| `stale-check` | `prepared_sha256` is not the prepared file given: the check is no evidence for it |
| `original-mismatch` | `original_sha256` is not the original given |
| `instructions-version-mismatch` | Not the checkpoint file's `book.instructions_version` |
| `prepared-unreadable` | The prepared file or its checkpoint file cannot be read |
| `missing-prompt`, `unknown-prompt`, `duplicate-prompt` | Every prompt of the book needs exactly one entry, and no other prompt may appear |
| `missing-section`, `unknown-section`, `duplicate-section` | The same for sections |
| `unknown-paragraph` | A citation names no paragraph of the book |
| `citation-outside-section` | A cited paragraph's words are not inside the prompt's section, as the prepared file's anchors place it |
| `section-unresolved` | The prompt's section has no valid anchors, so its citations cannot be confirmed |
| `pass-without-citation`, `pass-with-failed-criterion` | A `pass` prompt with no citation, or with a criterion false |
| `verdict-unjustified` | Overall `pass` although a prompt or section fails or a blocking issue is open |
| `unknown-issue-target`, `duplicate-issue` | An issue targets nothing in the book; two issues share an id |
| `checker-verdict-fail` | Everything is consistent, but the checker failed the draft |

## `prep report`: ready or not, and the validation report

```sh
python3 -m tools.prep report --original BOOK.epub --prepared BOOK.prepared.epub \
    [--validation validation.json] [--check check.json] [--history WORKDIR] [--draft draft.json] -o OUTDIR [--timestamp ISO]
```

The book is **READY** only when all of these hold (plan "Controls": no pass, no delivery):

1. `validate`, **run again here** on the files given (with the latest draft's exclusions), passes every check, EPUBCheck included;
2. the latest stored validator result passed, and was made against the SHA-256 of exactly this prepared file and this original;
3. `verify-check`, **run again here** on the latest checker result, passes: same prepared file and original, complete, consistent, citations inside their sections, verdict pass.

Stored results are evidence of history, not authority: editing `validation.json` or `check.json` cannot make a book ready, because the validator and the script-side checks run again on the bytes being delivered. What a script cannot re-run (whether an answer is supported) rests on the checker's verdict for exactly this file.

| Validator (re-run) | Checker | Results made against this file | EPUBCheck | Report |
| --- | --- | --- | --- | --- |
| pass | pass | yes | available | READY, exit 0 |
| any other combination | | | | NOT READY, exit 1 |

Outputs in `OUTDIR`, where `<name>` is the prepared file's name without `.prepared.epub`:

- `<name>.validation-report.md`: for a non-developer. The first line is `READY: ...` or `NOT READY: ...`; then plain sentences (why it is not ready), the book's facts (both SHA-256s, instructions version, the text in no section, number of rounds, drafter and checker models, tool versions, time), **one table of checks** (the validator checks, the stored validator record and three checker rows, each Passed, Failed, Incomplete or Not run) and **one table of issues** (round, raised by, issue, where, description, how it was resolved, or Open). The checker model named first is the one of the deciding round, the round whose `check.json` the report was decided on; models of earlier rounds follow as "earlier rounds". An excluded range without words (an image-only title page) reads "a range with no words". On a READY book the checker's open issues are minor ones on prompts that passed (a blocking issue fails the checker's verdict), so they are listed in a separate table, "Minor issues: these do not block delivery", and not as Open rows of the issue table; the JSON keeps them as open issues.
- `<name>.validation-report.json` (`schema/report.schema.json`): the same facts, `ready`, `reasons`, `checks`, `issues`, the full re-run validation result and the verify-check result.
- `<name>.prepared.epub`, only when READY, written from the very bytes that were checked (skipped when it is the prepared file itself).

A NOT READY report is still written, after the third failed round too, listing the open issues. Models come from `check.json` (`checker`) and from the draft's optional `x_drafter` member (`{"model": "...", "date": "YYYY-MM-DD"}`; the draft schema allows `x_` members). Each `models` entry has `round` (the first round it was used in), `rounds` (every round) and `deciding` (true for the checker of the deciding round); reports made by tools.prep 1.0.0 have only `round`.

### Round history

`/prep-book` (a later task) keeps one folder per round in the work folder; `report --history WORKDIR` reads it:

```
WORKDIR/
  source.json, structure.json, text/      prep extract
  draft.json                              the current draft
  rounds/01/draft.json                    the draft built in round 1
  rounds/01/validation.json               prep validate --json for that round's prepared file
  rounds/01/check.json                    the checker's result for it (absent if the checker did not run)
  rounds/02/...
```

Round folders are numbered (`01`, `02`, `03`) and read in numeric order. Without `--validation`, `--check` or `--draft`, the latest round that has the file supplies it. Each issue of each round is listed once, with its resolution: a validator failure (check, code, location) is "Fixed in round N" when round N's validation no longer reports it; a checker issue is "Resolved in round N" when round N's `check.json` marks it resolved (with its `resolution`), or "Not raised again ... in round N" when it is gone; an issue the final results still show is "Open". Without a history the files given count as one round.

## `/prep-book`: the loop

`.claude/commands/prep-book.md` is the command; `.claude/agents/prep-checker.md` is the checker; `INSTRUCTIONS.md` is what the drafter follows and the checker judges against. The command runs, per round (at most three): the drafter writes `WORKDIR/draft.json`; `build`; `start-round`; `validate` (on failure the checker is skipped); `checker-input`; the `prep-checker` subagent; `save-check`; `verify-check`; on failure `issues`, and the drafter revises from that list only. It always ends with `report`, which alone decides READY. Work folder after a run:

```
WORKDIR/
  source.json, structure.json, text/      prep extract
  draft.json                              the current draft (the drafter's only file)
  BOOK.prepared.epub                      prep build of draft.json
  checker/                                checker-input: the checker's only input (rewritten each round)
  rounds/NN/draft.json                    start-round: the draft built in round NN
  rounds/NN/validation.json               prep validate --json
  rounds/NN/checker-reply.txt             the checker's result, written by the checker itself (one Write call)
  rounds/NN/checker-write.json            the checker's guard: SHA-256 of what the checker wrote there
  rounds/NN/check.json                    save-check: the JSON object in that result, never repaired
  rounds/NN/verify-check.json             prep verify-check --json
  rounds/NN/issues.md                     issues: what the drafter may see about the failed round
```

| Command | Does | Refuses (exit 1) when |
| --- | --- | --- |
| `start-round WORKDIR ORIGINAL PREPARED` | Opens round NN (or reopens the latest round if it never reached the validator) and copies `draft.json` to `rounds/NN/draft.json`. Prints ids added or removed since the last round | the original is not the extracted file; the draft's `book.instructions_version` is not `INSTRUCTIONS.md`'s; `PREPARED` is not byte-for-byte `prep build` of the current draft; three rounds are used; the last round already passed; a prompt or checkpoint id moved to another section (`id-moved`); a prompt's exact text reappears under another id (`id-renumbered`) |
| `checker-input WORKDIR PREPARED [--date D]` | Writes `WORKDIR/checker/`: `book.json` (format `rsvp-checker-input`, version 2; values to copy into `check.json`: round, date, both SHA-256s, instructions version, issue id prefix `R<round>-`; `result_file`, the absolute path of `rounds/NN/checker-reply.txt`; every section with its paragraph range from the prepared file's anchors, the blocks just before and after it, its checkpoint and prompt ids; the excluded ranges with their reasons), `INSTRUCTIONS.md`, `text/*.txt`, and `sections/<id>.txt` (the section's paragraphs, then its prompts and reference answers) | the round's `validation.json` is missing, failed, or is for another prepared file; the book's instructions version is not the current one; the folder exists and was not written by `checker-input` |
| `save-check WORKDIR` | Writes `rounds/NN/check.json` from `checker-reply.txt`: the whole reply if it is a JSON object, else the last fenced block that is one, else the text from the first `{` to the last `}`; byte for byte, not re-serialised. A reply without a JSON object is written unchanged (exit 1) so that `verify-check` rejects it | there is no round; there is no `checker-reply.txt`; `checker-write.json` is missing or for another round; the reply's SHA-256 (line endings and trailing white space ignored) is not the one the guard recorded. Then `check.json` holds only the reason, as text, so `verify-check` fails the round (`check-unreadable`) and `issues` shows the reason |
| `issues WORKDIR` | Writes and prints `rounds/NN/issues.md`: validator failures; a note when the checker's result was unusable (`verify-check` codes other than `checker-verdict-fail`); the checker's open issues; failed prompts with the failed criteria and the checker's evidence; failed section breaks. Prompts that passed are never listed | there is no round |

**The checker sees only files, and writes one.** `checker/` holds no draft (so none of the drafter's `supporting_paragraphs`, `x_changes` or other notes), no earlier `check.json` and no round history. The subagent's tools are `Read`, `Glob`, `Grep` and `Write`. Its `PreToolUse` hook (`checker_guard.py`, with `onFailure: block`, matching `Read|Glob|Grep|Write|Edit|MultiEdit|NotebookEdit|Bash`) blocks any read outside a folder whose `book.json` has `format` `rsvp-checker-input`, a Glob or Grep without a `path`, and absolute or `..` patterns; it blocks Edit, MultiEdit, NotebookEdit and Bash outright; and it allows exactly one Write: to the `result_file` that `book.json` names, while that file and the round's `check.json` do not exist yet. For that write it first records the SHA-256 of the content in `rounds/NN/checker-write.json`, then answers with a `permissionDecision` of `allow`, so the owner is not asked to approve it. The checker's final message is only a one-line summary.

Why a file, not the final message: in the first real run the replies were 75 KB and 160 KB, and the command asked the drafter model to save the final message verbatim with its own Write call, a slow copy that could change a character unnoticed. Now the result reaches the disk in the checker's own Write call, and `save-check` adopts it only if it is byte for byte (apart from line endings and trailing white space) what the guard saw the checker write. So a missing result, a file written by anyone but the checker through its guard, or a result changed afterwards fails the round; a truncated or malformed result is adopted as it is and fails `verify-check`; a result for another prepared file fails `verify-check` with `stale-check`. A malformed result is a failed round, never a pass.

What enforces what:

| Rule | Enforced by |
| --- | --- |
| The checker reads only its folder; Glob and Grep need a `path` there | `checker_guard.py` (tested with simulated hook input in `tests/test_loop.py`) |
| The checker writes only `result_file`, once, before `save-check`; no Edit, MultiEdit, NotebookEdit or Bash | `checker_guard.py` (tested the same way) and the agent's `tools` list |
| The result adopted is exactly what the checker wrote; a missing result fails the round | `save-check` with the guard's record (tested) |
| The result is complete and consistent, and for exactly this prepared file and original | `verify-check` (tested) |
| The checker folder holds only its inputs | `checker-input` (tested); the result file is outside it |
| The drafter never reads the result or the checker's message, and revises from `issues.md` only | the command's instructions only |
| The hook actually runs | Claude Code, only for a project subagent in a trusted workspace. If it does not run, no record is written, so `save-check` refuses the result and the round fails, rather than accepting an unguarded one |

The guard has been tested only with simulated hook input (the JSON Claude Code documents for `PreToolUse`), not inside a real `prep-checker` run: it is not yet observed that Claude Code's Write tool writes the content exactly as the hook saw it (a difference other than line endings would fail every round, with the reason in `issues.md`), or how a very large single Write call behaves.

Checker issue ids carry the round (`R1-I1`), because the checker never sees earlier rounds; `report` lists an issue a later round does not raise again as "Not raised again".

## Where `/prep-book` works: the guard probe

`/prep-book` can reach READY only in a session where Claude Code runs the checker's guard. Claude Code skips the hooks in a project agent's frontmatter until the folder is trusted, and a non-interactive session does not count as trusted. In a Claude Code cloud session the hook did not run (issue #75): the checker wrote its result, no record was made, and `save-check` refused it. So:

- **Works:** Claude Code started in a terminal on your own computer, in this repository's folder, after you answered yes to "trust this folder" (and restarted Claude Code, if you trusted it during the session). This is what Claude Code's documentation implies; a pass there has not yet been observed by the team.
- **Does not work:** Claude Code cloud sessions, and any session where the probe below fails.
- **Works anywhere:** the scripts themselves: `extract`, `build`, `validate`, `verify-check` and `report` (Python 3; Java for EPUBCheck). Only the checker's step needs the guard.

The probe (`probe.py`) is step 1 of the command, before anything reads the book:

```sh
python3 -m tools.prep guard-probe OUTDIR            # writes OUTDIR/guard-probe/ and prints the checker's prompt
# the prep-checker agent, prompt: "Checker folder: OUTDIR/guard-probe/input"
python3 -m tools.prep guard-probe OUTDIR --check    # 0 PASSED, 1 FAILED (code), 2 usage error
```

```
OUTDIR/guard-probe/
  input/                         the probe's checker folder: book.json (format rsvp-guard-probe-input),
                                 INSTRUCTIONS.md (a short note), text/d01-probe.txt, sections/s0001.txt:
                                 two made-up paragraphs and one prompt
  result/checker-reply.txt       written by the checker, through its guard
  result/checker-write.json      written by the guard (format rsvp-guard-probe-write)
```

`--check` judges the result with `save-check`'s own code (`loop.reply_fault`), and also requires the record to carry the probe's `prepared_sha256`. Each outcome prints its own message (stdout for a pass, stderr for a failure):

| First line | Exit | Meaning |
| --- | --- | --- |
| `PASSED: the checker's guard is active` | 0 | The guard recorded the checker's write, and the file is exactly what it recorded (line endings and trailing white space ignored, as in `save-check`) |
| `FAILED (guard-not-active)` | 1 | The result exists but the guard recorded nothing: the hook did not run. The message says that the book was not touched and what to do (Claude Code on your own computer, in this repository, folder trusted, restart, run `/prep-book` again), and that a cloud session cannot do this |
| `FAILED (no-result)` | 1 | No result file: the agent did not run, did not finish, or its write was blocked |
| `FAILED (other-folder)` | 1 | The record is not a probe record for this probe: a round's record, another round number, another `prepared_sha256`, or not a record at all |
| `FAILED (other-file)` | 1 | The record names another file |
| `FAILED (changed)` | 1 | The result differs from what the guard recorded |

`guard-probe OUTDIR` replaces an earlier probe in `OUTDIR/guard-probe/` and refuses (exit 1) to replace anything else there. `--check` without a probe exits 1.

**A probe is never a round.** Its folder is neither `checker/` nor `rounds/`, and it is outside `WORKDIR`. `start-round`, `save-check` and `report --history` look only at `WORKDIR/rounds/NN/`, so they ignore it; `save-check` refuses a probe record copied into a round (its `format` differs); `verify-check` and `report` reject a probe result for any book (its hashes are of the made-up text). The guard (`checker_guard.py`) allows reads in a folder whose `book.json` has `format` `rsvp-guard-probe-input`, and one Write to `guard-probe/result/checker-reply.txt` when `guard-probe/input/book.json` names it. A probe `book.json` cannot authorise a round's result file, and a round's checker folder (`rsvp-checker-input`) cannot authorise a probe's result file. Its rules for rounds are unchanged. All of this is tested in `tests/test_probe.py`, which runs `checker_guard.py` with simulated hook input; a real `prep-checker` run in a trusted session has not been observed.

The probe only shows that the guard **allows and records** the checker's one write. It does not show that the guard **blocks** anything in the real session: the checker's normal behaviour never makes a call the guard would block, and the probe does not try to make it misbehave. The blocking rules are tested with simulated hook input only (`tests/test_loop.py`).

## Synthetic fixture book

`fixture/generate.py` writes `fixtures/books/fixture.epub`, `fixture.draft.json`, `fixture.expected.json` and `README.md` from the data in `fixture/content.py`, byte for byte reproducibly and with the standard library only. `fixtures/books/README.md` has the content inventory and word counts.

```sh
python3 tools/prep/fixture/generate.py           # regenerate after editing fixture/content.py
python3 tools/prep/fixture/generate.py --check   # exit 1 and name each stale file
```

`fixture/prepared.py` then writes, from the committed `fixture.epub` and `fixture.draft.json`, the prepared fixture `fixture.prepared.epub` (`prep build` output), a passing sample checker result `fixture.check.json`, and `negative/`: prepared copies broken in one way each (`broken-link.prepared.epub`: checkpoint c0002's section s0004 has no end anchor; also a checkpoint naming a missing section, a misspelt end anchor, anchors in the wrong document, a section spanning two documents, start after end, a changed word, an extra member, a wrong `source_sha256`, a `word_count` off by one) and checker results `verify-check` must reject (a citation outside the section, a missing prompt, a stale hash, a pass without citation, an unjustified overall verdict, a failed check). It also writes two **checker fixtures**, each a draft and its prepared copy that `prep validate` passes but that carry one planted content fault only the AI checker can catch: `unsupported-answer` (prompt `c0001-p1`'s answer adds a detail that only the next section states; criterion `supported_by_section`) and `answer-given-away` (prompt `c0002-p1` names its own answer; criterion `does_not_give_away_answer`). They carry instructions version 1.0, fixed in the generator (`CHECKER_INSTRUCTIONS_VERSION`) so that a new instructions version leaves them byte-identical; `tests/test_loop.py` gives their draft the current version before running it through the loop to the checker. `negative/manifest.json` names, for each file, the exact checks and codes it must fail (`checker`: the prompt and criterion the checker must fail); `tests/test_validate.py` and `tests/test_verify_check.py` iterate it. **Draft fixtures** are drafts that build but whose exclusions `prep validate --draft` must reject: `excluded-chapter` (chapter 2 excluded as "not prepared"; check `exclusions`, all three codes). They are listed in their own `negative/draft-manifest.json` (format `rsvp-negative-drafts`), so that `manifest.json` stayed byte-identical when they were added. `python3 tools/prep/fixture/prepared.py --check` exits 1 if a file is stale.

`fixture.expected.json` is the ground truth other layers compare against, including the firmware's WordIndex tests (task R3). It is computed from the content model in `content.py`, not by parsing the EPUB, with the generator's own small text splitter. Before writing anything, the generator runs the word pass (`words.py`) on every XHTML document it produced and refuses if the two routes disagree; `tests/test_fixture.py` compares the committed file with the word pass, `prep extract`'s blocks and `prep build`'s anchors on `fixture.epub` again. An error in either route therefore shows up as a difference.

The file (format `rsvp-fixture-expected`, `version` 2, `rules_version` 1):

| Member | Content |
| --- | --- |
| `rules_version` | Version of the word rules (section 5 plus NOTE regions). Raised whenever a rule change can move a word index; the firmware's `.wix` `rulesVersion` should carry the same number |
| `rules` | What each part means, in prose |
| `documents[]` | Per spine document: `href`, `zip_path`, `word_count`, and the members below |
| `.words` | The whole word stream, texts only, in order. Indexes below are 0-based into it |
| `.flags` | Each flag (`PARA_END`, `LINE_END`, `NONPROSE`, `NOTEREF`, `HEADING`, `BYTE_SPLIT`) mapped to the `[start, end)` runs of word indexes that carry it. No `SENT_END`: the firmware decides sentence ends |
| `.markers` | In document order, `{kind, start, end}`: `IMAGE` (`start == end`, the index of the next word), `TABLE`, `CODE` and `NOTE` as `[start, end)` |
| `.blocks` | Every block `prep extract` numbers, main series then footnote series: `paragraph` (`d02-p004`), `id` (XHTML id), `kind`, `element`, `first_word`, `word_count`, plus the source data of figures (`image_href`, `alt`, `caption`), tables (`caption`, `header`, `rows`) and code (`lines`) |
| `.noterefs` | Each footnote mark: `note`, `mark` (its text), `form` (`noteref`, `sup-link` or `link-sup` markup), `href` (`#fn1`, or `endnotes.xhtml#fn2` for an endnote in another document), `block`, `word` (the word with its `NOTEREF` flag) |
| `.footnotes` (endnotes document) | The endnotes document (`text/endnotes.xhtml`, last in the spine) is one NOTE region over all its words, with no reading-stream block; its `footnotes` give each endnote's `[start, end)` inside that region. A firmware reader hands RsvpCore no word from it |
| `.footnotes` | Each footnote body: `id`, `mark`, its `NOTE` region `start`/`end`, its `paragraphs` |
| `.inline_elements`, `.page_breaks` | Where the `em`/`strong` words and the page-break marker sit |
| `sections[]` | Per draft section: `start` and `end` (the word indexes of its anchors), `word_count` (`end - start`, what `prep build` writes), first and last paragraph, anchor names, first and last word text |
| `token_cases[]` | One named case per word rule the book exercises, with the word, its index and its flags |

A firmware test can compare its word list with `words`, its flags with `flags` and its markers with `markers` for each chapter of `fixture.epub`, and the anchors of the prepared fixture (`prep build fixtures/books/fixture.epub fixtures/books/fixture.draft.json`) with `sections[].start` and `end`. Rules with no case in the book (the 255-byte split, U+FEFF, `rt`, `hidden`) are covered by `tests/test_words.py` only.

## EPUBCheck

```sh
python3 -m tools.prep.epubcheck fixtures/books/fixture.epub   # exit 0 when there are no errors
```

```python
from tools.prep.epubcheck import EpubcheckUnavailable, run_epubcheck

report = run_epubcheck("book.epub")   # Report(version, returncode, messages)
report.errors, report.warnings        # lists of Message(severity, id, message, locations)
```

`epubcheck.py` pins EPUBCheck 5.3.0 and its SHA-256, downloads the release zip from github.com once, verifies it and unpacks it into `$EPUBCHECK_CACHE_DIR`, else `$XDG_CACHE_HOME/eink-learner-aid`, else `~/.cache/eink-learner-aid` (outside the repository). It needs Java (`$JAVA_HOME/bin/java` or `java` on `PATH`). When Java is missing or the download is refused or fails verification it raises `EpubcheckUnavailable` with the host and error; the tests that need EPUBCheck then skip with that message.
