# tools/prep

Book preparation tools (docs/PLAN.md section 8). This package holds the `extract` and `build` commands, the EPUB reading library they share, the checkpoint file schema and its validator, the draft file schema, the synthetic fixture book generator (`fixture/`) and an EPUBCheck runner (`epubcheck.py`). The `validate` and `report` commands come in a later task.

```sh
pip install -r tools/prep/requirements.txt
python3 -m pytest tools/prep -q

python3 -m tools.prep extract BOOK.epub WORKDIR                        # step 1: work folder for the drafter
python3 -m tools.prep build BOOK.epub DRAFT.json -o BOOK.prepared.epub  # step 3: prepared EPUB
```

Exit status of every command: 0 success, 1 a problem with the input (every problem is listed on stderr), 2 a usage error.

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

## Synthetic fixture book

`fixture/generate.py` writes `fixtures/books/fixture.epub`, `fixture.draft.json`, `fixture.expected.json` and `README.md` from the data in `fixture/content.py`, byte for byte reproducibly and with the standard library only. `fixtures/books/README.md` has the content inventory and word counts.

```sh
python3 tools/prep/fixture/generate.py           # regenerate after editing fixture/content.py
python3 tools/prep/fixture/generate.py --check   # exit 1 and name each stale file
```

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
| `.noterefs` | Each footnote mark: `note`, `mark` (its text), `form` (`noteref`, `sup-link` or `link-sup` markup), `block`, `word` (the word with its `NOTEREF` flag) |
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
