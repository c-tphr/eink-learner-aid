# CheckpointCore

Portable C++20 reader for `rsvp/checkpoints.json`, the checkpoint file of a prepared book (docs/PLAN.md section 5). The rules it enforces are the ones in `tools/prep/README.md`, implemented in Python by `tools/prep/checkpoints.py`. The two readers must agree on every file, and the shared samples in `fixtures/checkpoints/` prove it. Later tasks add the checkpoint state machine, trigger and grade rules here (task C1b).

The library has no hardware dependencies: no Arduino, ESP-IDF, SdFat, iostream, exceptions, RTTI, `<filesystem>`, `<regex>` or locale functions, and no heap allocation. It is not part of any firmware build yet; a later task adds the build flag and registration.

| File | Contents |
| --- | --- |
| `CheckpointLimits.h` | Firmware limits and buffer sizes |
| `CheckpointSource.h/.cpp` | Byte-source interface the caller implements, plus `MemoryCheckpointSource` |
| `CheckpointIndex.h/.cpp` | Public API: `open()`, book fields, section, chapter, checkpoint and prompt queries |
| `CheckpointParser.h/.cpp` | Internal: the validating pass |
| `JsonText.h/.cpp` | Internal: strict JSON string decoding, buffered reader, FNV-1a hash |

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

These are named constants in `CheckpointLimits.h`. A file that exceeds one is rejected with code `limit`, which the Python validator does not have. "Characters" are code points after unescaping, as in the schema's `maxLength`, so each limit can be copied into the Python schema as it is.

| Constant | Value | Applies to | In the Python schema today |
| --- | --- | --- | --- |
| `MAX_FILE_BYTES` | 1,048,576 | File size in bytes | none |
| `MAX_SECTIONS` | 512 | Entries in `sections` | none |
| `MAX_CHECKPOINTS` | 512 | Entries in `checkpoints` | none |
| `MAX_PROMPTS` | 1,024 | Prompts in the whole file | none |
| `MAX_PROMPTS_PER_CHECKPOINT` | 32 | `prompts` of one checkpoint | none |
| `MAX_CHAPTERS` | 256 | Distinct `chapter_href` values | none |
| `STRING_POOL_BYTES` | 16,384 | Sum over section ids, checkpoint ids, checkpoint `section` values and distinct chapter hrefs of (UTF-8 bytes + 1) | none |
| `MAX_ID_CHARS` | 64 | Every id | same (64) |
| `MAX_PROMPT_CHARS` | 300 | `prompt` | same (300) |
| `MAX_ANSWER_CHARS` | 600 | `answer` | same (600) |
| `MAX_HREF_CHARS` | 255 | `chapter_href` | none |
| `MAX_LABEL_CHARS` | 200 | `label` | none |
| `MAX_TITLE_CHARS` | 300 | `book.title` | none |
| `MAX_LANGUAGE_CHARS` | 35 | `book.language` (RFC 5646 section 4.4.1) | none |
| `MAX_INSTRUCTIONS_VERSION_CHARS` | 32 | `book.instructions_version` | none |
| `MAX_DEPTH` | 32 | Nesting of objects and arrays anywhere, including unknown members (the top-level object counts as 1) | none (Python fails with RecursionError at about 1,000) |
| `MAX_OPEN_MEMBERS` | 128 | Members of all objects that are open at one time, anywhere in the file | none |
| `MAX_NUMBER_CHARS` | 64 | Characters in one number, anywhere in the file | none (Python rejects integers over 4,300 digits as `parse`) |

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

- `limit`: a file the reference accepts can be rejected for exceeding a firmware limit (table above).
- `io`: the source failed to read.

Paths match the reference for every sample. For `schema`, the parser reports the issue whose path sorts first, as the Python validator sorts them; it does not report every schema issue. For the cross-reference layer it reports the first issue in the reference's order, not the full list.
