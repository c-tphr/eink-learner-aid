# CheckpointCore

Portable C++20 logic for retrieval-practice checkpoints (docs/PLAN.md sections 5 and 7):

- a reader for `rsvp/checkpoints.json`, the checkpoint file of a prepared book. The rules it enforces are the ones in `tools/prep/README.md`, implemented in Python by `tools/prep/checkpoints.py`. The two readers must agree on every file, and the shared samples in `fixtures/checkpoints/` prove it;
- the checkpoint flow (task C1b): trigger rules, the checkpoint and reread state machine, grade rules, and the byte encoding of the per-book state file, described in [Checkpoint flow](#checkpoint-flow) below.

The library has no hardware dependencies: no Arduino, ESP-IDF, SdFat, iostream, exceptions, RTTI, `<filesystem>`, `<regex>` or locale functions, and no heap allocation. It does no file I/O: callers read and write files and hand it bytes. It is not part of any firmware build yet; a later task adds the build flag and registration.

| File | Contents |
| --- | --- |
| `CheckpointLimits.h` | Firmware limits and buffer sizes |
| `CheckpointSource.h/.cpp` | Byte-source interface the caller implements, plus `MemoryCheckpointSource` |
| `CheckpointIndex.h/.cpp` | Public API: `open()`, book fields, section, chapter, checkpoint and prompt queries |
| `CheckpointParser.h/.cpp` | Internal: the validating pass |
| `JsonText.h/.cpp` | Internal: strict JSON string decoding, buffered reader, FNV-1a hash |
| `CheckpointTypes.h/.cpp` | Positions, reading modes, navigation sources, grades, `SectionRange`, the `BookLayout` interface the caller implements |
| `CheckpointTrigger.h/.cpp` | `ChapterSections` (one chapter's section layout) and the trigger rules |
| `CheckpointFlow.h/.cpp` | The checkpoint and reread state machine |
| `GradeTable.h/.cpp` | First grades by prompt id, grade times, id syntax |
| `BookState.h/.cpp` | The per-book state: overrides, position, grades, active checkpoint |
| `StateCodec.h/.cpp` | Encoding and decoding of the state file, version 1 |
| `StateRestore.h/.cpp` | What to show on open, and the rule for a state from another edition |

Host tests are in `test/checkpoint_core/`. To build and run only them: `cmake --build build/test --target CheckpointCoreTest && build/test/checkpoint_core/CheckpointCoreTest`.

## Design

`CheckpointIndex::open(source)` reads the file once, front to back, through a 256-byte buffer. It is an iterative state machine with an explicit stack of at most `MAX_DEPTH` frames, so it does no recursion. During that pass it:

1. **Checks the JSON (layer `parse`).** Strict UTF-8 (no overlong forms, no encoded surrogates, nothing above U+10FFFF), no byte order mark, RFC 8259 grammar (no NaN/Infinity, no trailing commas, no trailing data), no raw control characters in strings, and only valid escapes. It finds duplicate member names in every object, including objects nested inside unknown members. Member names are compared after unescaping, so `"id"` and `"id"` are duplicates. It also finds unpaired surrogate escapes.
2. **Records the version inputs (layer `version`):** whether the top level is an object, whether `format` equals `"rsvp-checkpoints"`, and whether `version` is an integer above 1.
3. **Checks the schema (layer `schema`)** for every known member, whatever order the members come in. Unknown members are skipped at every level, whatever they contain. Lengths are counted in code points after unescaping. An "integer" is a number written without a fraction or exponent, so `1.0` and `1e2` are not integers.
4. **Fills the index** and notes the first anchor-name problem and the first plain-text or blank-text problem. These are checked while each string streams past, so no text is kept.

After the pass, the result is chosen in the reference's layer order: `parse` (syntax and UTF-8 errors, then duplicate names, then surrogates, which is the order Python reports them in), then `unsupported-version`, then a firmware `limit`, then `schema`, then the cross-reference rules. The cross-reference rules run over the index in the same order as `validate_cross_references()`: each section's duplicate id and anchors, then each checkpoint's duplicate id, section reference and prompts.

### What is kept in RAM, and what is read on demand

| Kept in the index | Read from the file when asked |
| --- | --- |
| Section: id (in the string pool) and its hash, chapter index, word count, checkpoint index | Section label |
| Checkpoint: id and its hash, section index, first prompt, prompt count | Book title |
| Prompt: hash of its id, file offsets and raw lengths of its id, prompt and answer | Prompt id, prompt text, answer text |
| Distinct chapter hrefs (in the string pool) and their hashes | |
| Book: language, instructions version, source SHA-256 | |

Anchor names are not stored: they must be `rsvp-<id>-start` and `rsvp-<id>-end`, so `findSectionByEndAnchor()` strips the prefix and suffix and looks up the id.

`load*()` decodes one stored string into a caller buffer: it seeks to the stored offset, unescapes, and writes NUL-terminated UTF-8. The decoder checks everything again, so a source that changed after `open()` gives `false`, not bad text. A string that contains `\u0000` (allowed in a label or title, which are not checked for plain text) ends at that character in the returned C string.

### Uniqueness without a set

Ids and member names are compared by a 32-bit FNV-1a hash first. Every hash match is then confirmed against the text, so a hash collision never produces a false duplicate. The tests use the colliding pairs `costarring`/`liquid` and `panc4v`/`pbha0a` to prove this.

- **Section and checkpoint ids** are in RAM, so a match is confirmed with `strcmp`.
- **Prompt ids** are not in RAM. A match is confirmed by decoding both ids from the file.
- **Member names** are held as (hash, offset) on a stack covering the objects that are open at the moment. A match is confirmed from the file.

The duplicate scans compare every pair, which is O(n²), but they compare 32-bit hashes in RAM. At the limits that is about 0.5 million comparisons, and only real duplicates or collisions cause file reads. A hostile file built to collide many hashes can make `open()` slow, but it cannot make it fail or overrun memory.

## Limits

These are named constants in `CheckpointLimits.h`. A file that exceeds one is rejected with code `limit`. "Characters" are code points after unescaping, as in the schema's `maxLength`. The preparation tools mirror every limit: `tools/prep/schema/checkpoints.schema.json` has the ones a JSON Schema can express (`maxItems`, `maxLength`), and `tools/prep/limits.py` checks all of them by their C++ names, so `prep validate` rejects a file that breaks one with code `limit` (check `firmware-limits`; `tools/prep/README.md`, "Firmware limits"). `tools/prep/tests/test_limits.py` fails if a number in `limits.py` differs from this header. The last column says where the schema has the limit.

| Constant | Value | Applies to | In the Python schema |
| --- | --- | --- | --- |
| `MAX_FILE_BYTES` | 1,048,576 | File size in bytes | none (`limits.py` only) |
| `MAX_SECTIONS` | 512 | Entries in `sections` | `maxItems` 512 |
| `MAX_CHECKPOINTS` | 512 | Entries in `checkpoints` | `maxItems` 512 |
| `MAX_PROMPTS` | 1,024 | Prompts in the whole file | none (`limits.py` only) |
| `MAX_PROMPTS_PER_CHECKPOINT` | 32 | `prompts` of one checkpoint | `maxItems` 32 |
| `MAX_CHAPTERS` | 256 | Distinct `chapter_href` values | none (`limits.py` only) |
| `STRING_POOL_BYTES` | 16,384 | Sum over section ids, checkpoint ids, checkpoint `section` values and distinct chapter hrefs of (UTF-8 bytes + 1) | none (`limits.py` only) |
| `MAX_ID_CHARS` | 64 | Every id | `maxLength` 64 |
| `MAX_PROMPT_CHARS` | 300 | `prompt` | `maxLength` 300 |
| `MAX_ANSWER_CHARS` | 600 | `answer` | `maxLength` 600 |
| `MAX_HREF_CHARS` | 255 | `chapter_href` | `maxLength` 255 |
| `MAX_LABEL_CHARS` | 200 | `label` | `maxLength` 200 |
| `MAX_TITLE_CHARS` | 300 | `book.title` | `maxLength` 300 |
| `MAX_LANGUAGE_CHARS` | 35 | `book.language` (RFC 5646 section 4.4.1) | `maxLength` 35 |
| `MAX_INSTRUCTIONS_VERSION_CHARS` | 32 | `book.instructions_version` | `maxLength` 32 |
| `MAX_DEPTH` | 32 | Nesting of objects and arrays anywhere, including unknown members (the top-level object counts as 1) | none (`limits.py` only; Python's own JSON parser fails with RecursionError at about 1,000) |
| `MAX_OPEN_MEMBERS` | 128 | Members of all objects that are open at one time, anywhere in the file | none (`limits.py` only) |
| `MAX_NUMBER_CHARS` | 64 | Characters in one number, anywhere in the file | none (`limits.py` only; Python's own JSON parser rejects integers over 4,300 digits as `parse`) |

For the limits on counts, text lengths and the string pool, the scan continues: a `parse` problem anywhere in the file, or a newer `version`, still wins over the limit, and the limit wins over `schema` and cross-reference problems. The four structural limits (`MAX_FILE_BYTES`, `MAX_DEPTH`, `MAX_OPEN_MEMBERS`, `MAX_NUMBER_CHARS`) are different. They stop the scan where they are found, because the rest of the file cannot be read within the memory bound. A version 2 file that breaks one of them is therefore reported as `limit`, not `unsupported-version`.

Buffer sizes that always fit a field of an accepted file are `ID_BUFFER_BYTES` (65), `PROMPT_BUFFER_BYTES` (1,201), `ANSWER_BUFFER_BYTES` (2,401), `LABEL_BUFFER_BYTES` (801) and `TITLE_BUFFER_BYTES` (1,201).

## RAM

`CheckpointIndex` is one fixed-size object with no heap allocation, so its size does not depend on the file. Its size is 72,480 bytes on a 64-bit host (4 bytes less on the 32-bit ESP32, because it holds one pointer):

```
sizeof(CheckpointIndex) = 24·MAX_SECTIONS + 28·MAX_CHECKPOINTS + 24·MAX_PROMPTS + 8·MAX_CHAPTERS
                        + STRING_POOL_BYTES + 2,480 (parse scratch) + 335 (book fields) + ~30
                        = 12,288 + 14,336 + 24,576 + 2,048 + 16,384 + 2,480 + 335 + 33 = 72,480 bytes
```

At peak, `open()` adds less than 1 KiB of stack: the parser object, one string state, a 64-byte number buffer, and two 32-byte string cursors while it confirms a hash match. A `load*()` call adds about 100 bytes of stack plus the caller's buffer, at most 2,401 bytes for an answer. Nothing else is allocated.

**Design point: 300 sections and 600 prompts.** If arrays were sized to exactly this book, with ids like `s0001` and ten sections per chapter, it would need 24·300 + 28·300 + 24·600 + 8·30 + about 6,100 bytes of pool + 2,480 + 370 ≈ 39 KB. The fixed object is 71 KiB, which leaves 1.7× headroom on sections and prompts and 2.7× on the string pool. The test `CheckpointIndex.DesignPointBook` builds this book: the file is 418 KB with 150-character prompts and 400-character answers, and `open()` takes 32 ms on the host with sanitizers on. On the device, the caller should allocate the index on the heap when a prepared book opens and free it when the book closes, so it is placed in PSRAM.

## API

```cpp
#include <CheckpointIndex.h>

class CachedFileSource : public checkpoint::CheckpointSource { /* size(), read(offset, out, n) */ };

auto index = makeUniqueNoThrow<checkpoint::CheckpointIndex>();   // 71 KiB, once per open book
checkpoint::OpenResult r = index->open(source);
switch (r.verdict) {
  case checkpoint::Verdict::Valid: break;
  case checkpoint::Verdict::UnsupportedVersion: /* open as an ordinary EPUB, one-line notice */ break;
  case checkpoint::Verdict::Invalid: /* log issueCodeName(r.code), r.path, r.offset, r.detail */ break;
}

int s = index->findSectionByEndAnchor("rsvp-s0007-end");      // section whose end anchor this is
int c = index->sectionCheckpoint(s);                           // -1 when none
char text[checkpoint::PROMPT_BUFFER_BYTES];
for (uint16_t p = 0; p < index->promptCount(c); ++p) index->loadPromptText(c, p, text, sizeof(text));
```

- `open()` returns an `OpenResult`: the verdict (`Valid`, `Invalid`, `UnsupportedVersion`), the first issue's code (`IssueCode`, spelled as in the manifest by `issueCodeName()`), the byte offset of the value or byte at fault, a static `detail` string for logs, and an RFC 6901 `path` as the Python validator reports it. Paths longer than 127 bytes are cut and end in `...`. Unless the verdict is `Valid`, the index stays closed and every query returns its empty value (0, -1, `NO_INDEX`, `nullptr`, `""` or `false`).
- Book: `language()`, `sourceSha256()`, `instructionsVersion()`, `loadTitle()`.
- Sections in file order: `sectionCount()`, `findSection(id)`, `findSectionByEndAnchor(anchorId)`, `findSectionByStartAnchor(anchorId)`, `sectionId()`, `sectionChapter()`, `sectionChapterHref()`, `sectionWordCount()`, `sectionCheckpoint()`, `loadSectionLabel()`.
- Chapters, meaning the distinct hrefs in order of first use: `chapterCount()`, `findChapter(href)`, `chapterHref()`, `nextSectionInChapter(chapter, after)`. The last one lists the sections of a chapter in file order: start with `after = -1` and stop when it returns -1. Hrefs are compared byte for byte, exactly as written in the file.
- Checkpoints in file order: `checkpointCount()`, `findCheckpoint(id)`, `checkpointId()`, `checkpointSection()`, `promptCount()`, `loadPromptId()`, `loadPromptText()`, `loadAnswerText()`. Prompts are numbered from 0 in file order. A load returns `false` and writes `""` on a bad index, a read error, or a buffer that is too small.

**Thread safety.** There is none. Use an index from one task at a time. The `load*()` calls read the source, so they also need exclusive use of the source; a `std::ifstream` or SD file has a position that moves.

**Lifetime.** `open()` keeps a pointer to the source. The source must stay valid until `close()`, the next `open()`, or the index is destroyed. Pointers returned by the index (ids, hrefs, book fields) are valid until the same moment. Copying an index is disabled.

**The source on the device.** Loading a prompt seeks to a stored offset, so the source needs cheap random reads. A deflated zip entry cannot seek. The firmware adapter should therefore extract `rsvp/checkpoints.json` once into the book's cache folder and read that file. The source reports a short read as a read error, which gives code `io`.

## How the shared samples are used

`test/checkpoint_core/CheckpointManifestTest.cpp` reads `fixtures/checkpoints/manifest.json` with a small DOM reader used only by the test. The test finds the fixture folder through the compile definition `CHECKPOINT_FIXTURES_DIR`. The test:

1. Fails if any file in `valid/` or `invalid/` is missing from the manifest.
2. Opens every sample through a `std::ifstream` source. For a valid sample, it also loads every prompt id, prompt and answer.
3. Prints a table with the expected verdict, code and path, the parser's result, and whether they match. It then checks the agreement rule: the verdict always matches, and for the `version` and `cross-reference` layers the code and path match exactly. For the `parse` and `schema` layers, a rejection with a code from the same layer is enough, but today all 107 samples match exactly, paths included.

`CheckpointRobustnessTest.cpp` feeds the parser these inputs: every truncation of every valid sample, 24,000 random mutations (fixed seed), random bytes, deep nesting (200,000 levels), very long strings and member names, and huge numbers. `CheckpointIndexTest.cpp` covers the API, each rule's edge cases, each limit and read errors. The test target is built with AddressSanitizer and UndefinedBehaviorSanitizer (`-fno-sanitize-recover=all`; turn them off with `-DCHECKPOINT_CORE_SANITIZE=OFF`). A separate object target, `CheckpointCoreNoRttiCheck`, compiles the library with `-fno-exceptions -fno-rtti -Wconversion -Wshadow -Werror`.

## Differences from the Python reference

There are none in verdicts or codes, apart from the firmware-only codes:

- `limit`: a file that exceeds a firmware limit (table above). Where the schema has the limit, the reference rejects the file too, with code `schema`; for the other limits the reference (`validate_checkpoints()`) accepts it, and `prep validate` rejects it through `limits.py` with code `limit`.
- `io`: the source failed to read.

Paths match the reference for every sample. For `schema`, the parser reports the issue whose path sorts first, as the Python validator sorts them; it does not report every schema issue. For the cross-reference layer it reports the first issue in the reference's order, not the full list.

## Checkpoint flow

Everything in this part is decided here, in portable code, so that `ReadingSession` (the reader integration) and `CheckpointActivity` (the screens) only call it and never decide a rule themselves. The tests in `test/checkpoint_core/` name each rule of docs/PLAN.md section 7 (`TriggerRule1..6`, `FlowStep1..6`, `GradeRule1..4`, `RestoreOrder_*`) and replay the PRD's checkpoint acceptance criteria on the synthetic book (`CheckpointScenario.Prd1..4`). `test/checkpoint_core/StateTestSupport.h` has `ReaderSim`, a small stand-in for `ReadingSession` that follows the calling contract below.

### Positions

A position is (reader chapter, word index) (`Position`), the reader chapter being the spine index. A `Place` is a position plus a `ReadingMode` (`Rsvp` or `Page`). In RSVP the position is the word on screen. In page view it is the page's first word, or a later word on that page after a checkpoint resumed mid-page. A section is the word range `[start, end)` of one chapter given by its anchors (docs/spikes/S1-word-stream.md, section 5); the caller resolves anchors through `BookLayout::sectionRange()`.

`NavSource` says why the position moved: `RsvpPlayback`, `NextPage`, `PreviousPage`, `TableOfContents` (also footnote links), `GoToPercent`, `Bookmark`, `ProgressSync`, `SentenceStep`, `ModeSwitch`, `OpenRestore`, `RereadStart`, `RereadReturn`. Only `RsvpPlayback` and `NextPage` are ordinary forward reading (`isOrdinaryForward()`).

### Trigger rules

`ChapterSections` holds the sections of the chapter being read, sorted, with their checkpoints. `load()` fills it from the index and `BookLayout`; `add()` refuses an empty range, a range past the chapter's end and an overlap, since such a checkpoint could never fire (`load()` counts them in `failed`). When the chapter's word count is unknown, no section is loaded and nothing fires in that chapter.

1. **General rule** (`evaluateMove(layout, from, to, source, rereadSection)`). A move fires when `source` is ordinary forward reading and the words it leaves behind, `[from.word, to.word)`, or to the end of the chapter when `to` is in a later chapter, contain the last word of a section with a checkpoint. A checkpoint is therefore armed while the position is at or before its section's last word and fires when reading carries the position past it. The first such section fires; `Step::crossed` counts all of them.
2. **Other sources never fire**, however many section ends they pass. Neither does a backward move or a move that starts outside `layout`'s chapter.
3. **RSVP** (`rsvpStep(layout, shownWord)`, `stopAfterWord()`): asked with the last word on screen before the next word or group is drawn. Fire means playback stops with the section's last word on screen and draws nothing beyond. A group never crosses a section end (RsvpCore's grouping rule), so asking for the group's last word is enough. `stopAfterWord()` is the scheduler's stop-at-checkpoint flag.
4. **Page view** (`pageAdvance(layout, fromWord, pageLastWord)`): asked on a next-page action. A page's words are those whose last character it shows: `fromWord` is the first word whose end lies past the page's start offset (or the reading position, if it is later on that page) and `pageLastWord` is the next page's first word minus one, or the chapter's last word. A word hyphenated across two pages therefore belongs to the second page, which is the page showing the last character of the section's last word (S1 section 4.1: fire if `start(p) <= end[E-1] - 1 < start(p+1)`). Text after the break on that page was already visible. A page with no word of its own (an image page) fires nothing.
5. **Reading through again fires again.** Nothing remembers that a checkpoint fired. After it, reading resumes just after the break, outside the section. Going back into the section by any means re-arms it.
6. **During a reread** (`rereadSection` = `CheckpointFlow::rereadSection()`), no checkpoint fires; crossing the reread section's own end gives `StepKind::RereadEnd`, which goes back to the prompt.
7. **Two section ends on one page.** One next-page action presents every checkpoint whose end it crosses, in text order, before reading continues. The step names the first; when it finishes in page view, the resume word is on the same page, so the caller calls `pageAdvance(layout, resume.word, pageLastWord)` for that page and starts the next checkpoint before drawing the page. This needs no extra saved state: after a close between the two, the same call on reopen presents the second. In RSVP each end stops playback in turn.
8. **A section that ends its chapter.** RSVP stops with the chapter's last word on screen; page view fires on leaving the chapter's last page, before the next chapter is shown. The resume position is the next chapter's word 0 (`ChapterSections::after()`), or `(chapter, word count)` when it is the last chapter of the book.

`Step` carries the checkpoint, its `SectionRange` and the resume position (the first word after the break), which `CheckpointFlow::start()` takes.

### State machine

```text
                 start(fire)                 Reveal
 Idle/Finished -------------> PromptHidden ----------> AnswerShown
      ^                        ^   ^   |                 |     |
      |                        |   |   +-- Correct ------+     | Incorrect
      |                        |   |       (next prompt)       v
      |                        |   |                     IncorrectChoice
      |                        |   |                       |   |    |
      |                        |   +--- Continue / Skip ---+   |    |
      |                        |        prompt (next prompt)   |    |
      |   Return early,        |                               |    |
      |   reread reached end   +--------- Rereading <----------+    |
      |                                   Reread RSVP / page        |
      +---- last prompt done, or Skip checkpoint from any state ----+
            but Idle and Finished: Finished, resume after the break

 restore(plan): Idle/Finished -> PromptHidden (plan Prompt) or Rereading (plan Reread)
```

| State \ event | Reveal | Correct | Incorrect | Skip prompt | Skip checkpoint | Reread RSVP / page | Continue | Return early, reread reached end | `rereadMoved()` |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Idle | - | - | - | - | - | - | - | - | - |
| PromptHidden | AnswerShown (save No) | - | - | next | Finished | - | - | - | - |
| AnswerShown | - | record, next | record, IncorrectChoice (save Now if recorded, else No) | next | Finished | - | - | - | - |
| IncorrectChoice | - | - | - | next | Finished | Rereading at the section start | next | - | - |
| Rereading | - | - | - | - | Finished | - | - | PromptHidden, same prompt | inside the section: Rereading (save Position, or Now on a mode change); forward past the end, or out of the section: PromptHidden |
| Finished | - | - | - | - | - | - | - | - | - |

"next" is PromptHidden on the next prompt, or Finished after the last one. Every accepted event not marked otherwise returns `Save::Now`. "-" is refused with `Refusal::InvalidInState` and changes nothing. `start()` and `restore()` are accepted only in Idle and Finished. `CheckpointFlow::accepts(state, event)` is this table, for the UI to offer only valid actions.

What the machine remembers, and where:

| Data | Kept in | Saved |
| --- | --- | --- |
| Checkpoint id, prompt index | `BookState::active` | yes |
| Resume place: first word after the break, mode active when the checkpoint appeared | `active.resume` | yes; set by `start()` only, so a reread never changes it |
| Reread place and mode | `active.reread` while `active.rereading` | yes |
| Answer revealed, choice after Incorrect | `CheckpointFlow` | no: reopening always shows the prompt with its answer hidden |
| Section range, checkpoint number | `CheckpointFlow` | no: recomputed from the index on restore |

On finish (last prompt done, Continue or Skip prompt on the last prompt, or Skip checkpoint) the transition has `finished` set and `resume` filled; `state.position` becomes `resume` and `active` is cleared. The caller shows `resume` in its mode and waits: RSVP paused until Play, page view until the next action. Nothing advances on its own.

`Save::Now` means: encode the state and write the file (temporary file, rename) before showing the next screen. The plan's "every transition saves state first" is met this way: the flow has already changed `BookState` when it returns, and nothing is shown until the caller has written it. `Save::Position` is a reread position update, saved at the reader's normal position cadence.

During a reread, `rereadMoved(to, source)` reports each move. Reading forward past the section's last word returns to the prompt as "reread reached end" does. Any other move that leaves the section (previous page before its start, a jump elsewhere) returns to the prompt as Return early does, with `leftSection` set: a reread is confined to its section, so its saved place is always inside it.

### Grade rules

- `GradeTable::recordFirst(promptId, grade, at)` stores Correct or Incorrect for a prompt id once. A later call for the same id returns `AlreadyGraded` and changes nothing, whatever the grade, so retries, rereads, later sessions and later passes cannot overwrite it.
- Skipping records nothing; a skipped prompt stays ungraded until it is first graded.
- After Incorrect, a retry can be marked Correct: the flow moves on, and the stored grade stays Incorrect (`Transition::gradeRecorded` is false).
- A later pass presents all prompts from the first; `CheckpointFlow::storedGrade()` gives the stored grade to show as already recorded.
- Grades are keyed by prompt id, never by checkpoint or position, so a re-prepared file that reorders or moves prompts keeps them.
- `at` is the caller's clock as `YYYY-MM-DDTHH:MM:SS` (local time, no zone, years 2000 to 2135). Text that does not parse, or no clock, records the grade without a time rather than losing it; the file then has no `at` for it.
- If the table is full, the flow first drops grades for ids the current checkpoint file does not have (`retainPromptsOf()`), then retries. A prompt id that cannot be read from the file refuses the grade event (`PromptIdUnreadable`) and changes nothing, so the reader can try again or skip.

### State file, version 1

`encodeState(state, sink)` writes the plan's JSON shape with the same member order and layout, so the plan's example (with a real hash) re-encodes byte for byte:

```json
{
  "version": 1,
  "book_sha256": "<64 lowercase hex digits: the checkpoint file's book.source_sha256>",
  "overrides": { "wpm": 240, "words_per_update": 1 },
  "position": { "chapter": 3, "word": 1542, "mode": "rsvp" },
  "grades": {
    "c0007-p1": { "first": "correct", "at": "2026-12-01T19:04:00" }
  },
  "active_checkpoint": {
    "id": "c0007",
    "prompt_index": 1,
    "resume": { "chapter": 3, "word": 1543, "mode": "rsvp" },
    "reread": { "mode": "page", "chapter": 3, "word": 910 }
  }
}
```

| Member | Written | Values accepted by `decodeState()` |
| --- | --- | --- |
| `version` | always | integer: 1, or above 1 for `UnsupportedVersion` |
| `book_sha256` | when set (books with checkpoint data) | 64 lowercase hex digits |
| `overrides` | always, possibly `{}` | `wpm` 1 to 65,535 and `words_per_update` 1 to 255, each optional; clamping to what RSVP supports is the reader's job |
| `position` | when known | `chapter` 0 to 65,534, `word` 0 to 4,294,967,295, `mode` `"rsvp"` or `"page"`, all required |
| `grades` | when `book_sha256` is set or there are grades | member name: a prompt id (1 to 64 of `[A-Za-z0-9_-]`, not starting with `_` or `-`); `first` `"correct"` or `"incorrect"` (required); `at` as above (optional) |
| `active_checkpoint` | while a checkpoint is in progress | `id` (a checkpoint id), `prompt_index` 0 to 31, `resume` (a place) required; `reread` (a place) optional |

A book without checkpoint data writes only `version`, `overrides` and `position`. Whether the answer is revealed is not written.

`decodeState(bytes, size, openedBookSha256, out)` checks strict JSON (UTF-8, RFC 8259, no byte order mark, no lone surrogates, nothing after the value) and then, in this order: a `version` above 1 gives `UnsupportedVersion` whatever the rest holds; a missing, non-integer or zero version gives `Schema`; more than `GRADE_CAPACITY` grades or nesting deeper than `MAX_DEPTH` gives `Limit`; any known member that is missing, repeated, of the wrong type or out of range gives `Schema`. Unknown members are skipped at every level, whatever they contain (repeated unknown members are not detected). On any status but `Ok`, `out` is a fresh state; the caller keeps the bad file aside (for example renamed with a `.bad` suffix) and continues with that. Input larger than `STATE_DECODE_MAX_BYTES` gives `TooLarge`. `DecodeResult::shaMismatch` is set when the file's `book_sha256` and the open book's are both set and differ. Decoding recurses at most four fixed levels, skips unknown values iteratively, and allocates nothing.

Capacities:

| Constant | Value | Meaning |
| --- | --- | --- |
| `GRADE_CAPACITY` | 1,024 (`MAX_PROMPTS`) | grades in one state |
| `GRADE_ID_POOL_BYTES` | 65,536 (`MAX_PROMPTS` x `MAX_ID_CHARS`) | prompt id text, stored without padding or NUL |
| `STATE_ENCODED_MAX_BYTES` | 130,575 | exact size of the longest encoding (every member at its widest, 1,024 grades with 64-character ids and times): the size of the caller's write buffer |
| `STATE_DECODE_MAX_BYTES` | 262,144 | largest file `decodeState()` reads |

### RAM

| Object | Bytes (64-bit host) | Notes |
| --- | --- | --- |
| `GradeTable` | 77,832 | 12 bytes per grade (id hash, packed time, id offset, id length, grade) x 1,024, plus 65,536 bytes of id text |
| `BookState` | 78,020 | the grade table plus about 190 bytes |
| `ChapterSections` | 6,156 | 12 bytes per section x `MAX_SECTIONS` |
| `CheckpointFlow` | 32 | references and the transient state |

Times are packed into 4 bytes (seconds since 2000) instead of 19 characters, and ids are stored at their own length. The id text is still the bulk, because a valid book may have 1,024 prompts with 64-character ids; the fixture book's 4 prompts use 32 bytes of it. Keying grades by prompt number in the open index instead would cost about 5 KB, but grades would then depend on the checkpoint file being present and unchanged, and a book that opened without it would lose them on the next save. Allocate `BookState` and `ChapterSections` once per open book on the heap (PSRAM on the Picco), like `CheckpointIndex`.

### Opening a book

1. Read the state file into a buffer and call `decodeState()` with the index's `sourceSha256()` (or `""` for a book without checkpoint data). On failure, set the file aside and use the fresh state.
2. `applyBookChangeRule(state, index)` (`index` null when the book has no valid checkpoint data):
   - no checkpoint data: nothing changes, so grades survive a book that opens without its checkpoint file;
   - the state has no `book_sha256`: it takes the book's;
   - same hash: nothing changes, and grades for ids the file no longer has are kept;
   - different hash (another source EPUB): grades for prompt ids the book still has are kept and the rest dropped; the position and any checkpoint in progress are dropped, because word positions may differ; overrides are kept.
3. `planRestore(state, index, layout)` returns the `OpenView`, in the plan's order: an active reread whose place is inside its section (`Reread`), else the active prompt with its answer hidden (`Prompt`), else the saved position (`Position`), else the start of the book (`BookStart`). It removes from `state` what cannot be restored and says why in `notes`: no checkpoint data, unknown checkpoint id, prompt index out of range, section anchors not found, resume position outside the book, reread outside its section (the prompt is shown instead), position outside the book. A dropped checkpoint whose resume position is in the book becomes the reading position, so reading continues after its break. A position is in the book when its chapter exists and its word index is at most the chapter's word count.
4. For `Reread` and `Prompt`, call `CheckpointFlow::restore(plan)`.

### Calling contract

`ReadingSession`:

- Owns `BookState`, `CheckpointFlow`, the open `CheckpointIndex`, and a `ChapterSections` for the current chapter, reloaded with `load()` on every chapter change (reader chapter to href through the spine, then `CheckpointIndex::findChapter()`).
- Tags every position change with its `NavSource` and updates `state.position` while the flow is in Idle or Finished.
- RSVP: before drawing the word after the one on screen, calls `rsvpStep(layout, shownWord, flow.rereadSection())`. Fire: stop playback, `flow.start(step, ReadingMode::Rsvp)`. RereadEnd: stop, `flow.handle(FlowEvent::RereadReachedEnd)`.
- Page view, in `EpubReaderActivity::pageTurn()` on a forward turn: `pageAdvance(layout, fromWord, pageLastWord, flow.rereadSection())`, with the same two outcomes, before the next page is drawn. Every other navigation either goes through `evaluateMove()`, which never fires for it, or does not ask at all.
- During a reread: after each move, `flow.rereadMoved(place, source)`; leaves reading mode when the flow leaves Rereading.
- After every transition: if `save == Save::Now`, `encodeState()` into a `STATE_ENCODED_MAX_BYTES` buffer and write the file atomically before showing anything; if `finished`, show `resume` in its mode, waiting, and in page view apply trigger rule 7 first.

`CheckpointActivity`:

- Shows prompt `flow.promptIndex()` of `flow.checkpoint()` (`loadPromptText()`, `loadAnswerText()`), the answer only in AnswerShown, and `flow.storedGrade()` as "already recorded".
- Offers exactly the events `CheckpointFlow::accepts(flow.state(), event)` allows and sends them with `flow.handle(event, now)`; in Rereading it hands control to the reader at `state.active.reread`.
- Back at a prompt after a reread, "retry" is Reveal and "continue" is Skip prompt.
