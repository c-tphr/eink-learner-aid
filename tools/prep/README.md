# tools/prep

Book preparation tools (docs/PLAN.md section 8). So far this package holds the checkpoint file schema and its validator, the draft file schema, the synthetic fixture book generator (`fixture/`) and an EPUBCheck runner (`epubcheck.py`). The `extract`, `build`, `validate` and `report` commands come in later tasks.

```sh
pip install -r tools/prep/requirements.txt
python3 -m pytest tools/prep -q
```

```python
from tools.prep.checkpoints import check_file, load_checkpoints, validate_checkpoints, verdict

issues = check_file("rsvp/checkpoints.json")   # list[Issue(code, path, message)]
verdict(issues)                                # "valid", "invalid" or "unsupported-version"
data = load_checkpoints("rsvp/checkpoints.json")  # parsed dict, or raises CheckpointFileError
```

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

`draft.json` is what the drafter session writes (plan section 8, step 2) and what `prep build` will read (step 3). Its JSON Schema is `schema/draft.schema.json` (draft 2020-12). The worked example is `fixtures/books/fixture.draft.json`. `prep build` may refine this format; until then it is the contract.

```json
{
  "format": "rsvp-draft",
  "version": 1,
  "book": { "title": "...", "language": "en", "source_sha256": "<hash of the original EPUB>", "instructions_version": "1.0" },
  "excluded": [
    { "chapter_href": "text/title.xhtml", "first_paragraph": "fm-p001", "last_paragraph": "fm-p003", "reason": "Front matter: title page." }
  ],
  "sections": [
    { "id": "s0001", "chapter_href": "text/chapter-1.xhtml", "first_paragraph": "c1-p001", "last_paragraph": "c1-p006", "label": "Arrival at Gannet Rock" }
  ],
  "checkpoints": [
    {
      "id": "c0001",
      "section": "s0001",
      "prompts": [
        { "id": "c0001-p1", "prompt": "...?", "answer": "...", "supporting_paragraphs": ["c1-p006"] }
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
| `sections` | At least one, in reading order. Each is a paragraph range plus `id` (id syntax) and a non-empty `label`. It becomes one section of `checkpoints.json`; `word_count` and the anchors are computed by `prep build` |
| `checkpoints` | Array, may be empty. `id`, `section` (a section id) and `prompts` (at least one, file order kept) |
| prompt | `id`, `prompt` (1 to 300 characters), `answer` (1 to 600 characters), `supporting_paragraphs` (at least one paragraph reference, no repeats) |

Definitions:

- **Paragraph range.** `chapter_href` is a spine document's href relative to the OPF, as in `checkpoints.json`. `first_paragraph` and `last_paragraph` are inclusive references to paragraph-level blocks of that document (headings, paragraphs, figures, tables, code blocks). A paragraph reference uses the id syntax. For the synthetic fixture it is the block's XHTML `id` (`c1-p003`); for other books it is the number `prep extract` assigns, a format that task fixes.
- **Ids** use the id syntax of `checkpoints.json`. Conventions (not rules): sections `s0001`, `s0002`, ...; checkpoints `c0001`, ... in order; prompts `<checkpoint id>-p1`, `-p2`, ...
- **Closed schema.** Unlike `checkpoints.json`, every object rejects unknown members, so a misspelt member in a drafter's output is caught at once. Members whose names start with `x_` are allowed and ignored.

Cross-reference rules (checked by `tests/test_fixture.py` for the fixture now, and by `prep build` later):

1. Section ids are unique, checkpoint ids are unique, prompt ids are unique across the file.
2. Every paragraph reference exists in its `chapter_href`. `last_paragraph` is not before `first_paragraph`, so a range lies inside one document (plan rule 1).
3. Ranges (sections and exclusions together) do not overlap, and every paragraph-level block of every spine document is in exactly one of them. Footnote bodies (`aside epub:type="footnote"`) are outside the reading stream and are neither covered nor excluded.
4. Sections appear in reading order.
5. A checkpoint names an existing section; at most one checkpoint per section (plan rule 2).
6. Every `supporting_paragraphs` entry lies inside the checkpoint's section.
7. `prompt` and `answer` are plain text and not blank, as defined for `checkpoints.json` above.

## Synthetic fixture book

`fixture/generate.py` writes `fixtures/books/fixture.epub`, `fixture.draft.json`, `fixture.expected.json` and `README.md` from the data in `fixture/content.py`, byte for byte reproducibly and with the standard library only. `fixtures/books/README.md` has the content inventory and word counts.

```sh
python3 tools/prep/fixture/generate.py           # regenerate after editing fixture/content.py
python3 tools/prep/fixture/generate.py --check   # exit 1 and name each stale file
```

`fixture.expected.json` is the ground truth other layers compare against. It is computed from the source data, not by parsing the EPUB. Its `rules` member states the word definition, which is implemented by the single function `tokenize` in `fixture/generate.py` so that a later task can align it with the firmware's parser:

- A word is a maximal run of characters other than U+0020, U+0009, U+000A and U+000D in the visible text of one prose block (`h1`, `h2`, `p`). Punctuation stays attached; an unspaced em dash joins its neighbours into one word. Nothing is normalised or stripped.
- Inline elements (`em`, `strong`) do not split or drop words.
- A footnote reference mark (`sup > a[epub:type=noteref]`) is not a word; its position is recorded as `after_word`. Footnote bodies are listed separately and are not in the chapter's word stream.
- Figures, tables and code blocks contribute no words; their position is `after_word` and their inner text is listed in their block entry.
- Indices are 0-based over a whole document. A section's start anchor belongs immediately before its `first_word`, its end anchor immediately after its `last_word`.

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
