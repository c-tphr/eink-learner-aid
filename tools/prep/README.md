# tools/prep

Book preparation tools (docs/PLAN.md section 8). So far this package holds the checkpoint file schema and its validator. The `extract`, `build`, `validate` and `report` commands come in later tasks.

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
