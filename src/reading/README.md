# src/reading

**ReadingSession** (`docs/PLAN.md` section 4): owns the reading position, the mode and the navigation source; decides when a checkpoint fires; saves and restores the per-book state. It connects `lib/RsvpCore`, `lib/CheckpointCore` and `lib/WordIndex` to the reader.

Filled by task C2 (#26), then extended by C4 (#28) and C5 (#29). Task R4a (#21) added the word index service and the position helpers that RSVP uses and C2 reuses; task R4b (#22) the per-book state file service, the pace settings and the rule for where RSVP opens; task C3a (#93) the checkpoint data loader.

Everything here is compiled only with the RSVP feature flag: every file includes `RsvpFeature.h` and wraps its code in `#if CROSSPOINT_RSVP_ENABLED` (see `docs/ARCHITECTURE-RSVP.md`).

| File | What it holds | Task |
| --- | --- | --- |
| `ChapterWordIndex.h/.cpp` | A chapter's word index on demand: opens `/.crosspoint/epub_<hash>/rsvp/<spine>.wix` when it is valid for the chapter's current zip entry, otherwise builds it by streaming the chapter out of the EPUB through `WordIndexBuilder` (to `<spine>.wix.tmp`, renamed when the build succeeds), then opens it; `rebuild()` after a read error during use | R4a |
| `WordIndexCheck.h/.cpp` | The rebuild decision (`lib/WordIndex/README.md`, "Rebuild rules"): valid, missing, unreadable (truncated, unfinished, other format or rules, corrupt, I/O error), stale (other zip size or CRC-32), unverifiable (no zip entry: a TXT book; always rebuilt) | R4a |
| `RsvpPosition.h/.cpp` | Page view and RSVP hand-over (a page's first word; a word's visible offset), chapter percent, sentence stepping over a window of classified RSVP tokens; `chooseEntry()`, where RSVP opens (`src/activities/rsvp/README.md`, "Where RSVP opens") | R4a, R4b |
| `BookStateFile.h/.cpp` | The per-book state file `<book file name>.rsvp-state.json` beside the book: `loadBookState()`, and `editBookState()`, which reads the file, applies one edit and writes it through `<file>.tmp` and a rename, keeping every member the edit does not touch (grades, the checkpoint in progress, `book_sha256`). An invalid or unreadable file reads as a fresh state and is moved to `<file>.bad` by the next save. The encoding is `lib/CheckpointCore`'s `StateCodec`; there is no second JSON format. C3 and C5 add their edits here | R4b |
| `BookStateRules.h/.cpp` | The pure part: what a decoded file means (missing, read, invalid; another edition) | R4b |
| `RsvpValues.h/.cpp` | WPM and words-per-update stepping and ranges, the global default as stored in the settings file (WPM in tens), and the precedence when RSVP opens (the book's override, else the global default) | R4b |
| `CheckpointBook.h/.cpp` | A prepared book's checkpoint data (below): finds the manifest item `rsvp-checkpoints`, copies the file out of the EPUB once, opens `lib/CheckpointCore`'s index over the copy; `book_sha256`, lookups for C2 (checkpoint by section, a section's anchor ids, a section's spine index) | C3a |
| `CheckpointBookRules.h/.cpp` | The loader's pure decisions: what a parse result means for the book, the size limit, the copy's name | C3a |
| `OpfItemFinder.h/.cpp` | Finds one manifest item of the OPF by id, streaming the OPF through expat; resolves its zip path | C3a |
| `ReadingSession.cpp` | Placeholder | C2 |
| `RsvpLinkCheck.h/.cpp` | Temporary start-up check, called from `setup()` in `src/main.cpp`, that links every source file of RsvpCore and CheckpointCore into the firmware and logs one debug line; it also runs DisplayProfile's link check (`src/display/DisplayProfile.h`, task E3), which logs a second line. C2 removes both, and the call in `main.cpp`, when the real ReadingSession lands | F4, E3 |

Host tests of the pure parts: `test/checkpoint_activity` (`OpfItemFinderTest`; `CheckpointBookRulesTest` on every shared sample of `fixtures/checkpoints`), `test/rsvp_activity` (`ReadingHelpersTest`: index decisions on real and damaged indexes, page/word round trips, sentence starts against `fixtures/books/fixture.expected.json`, the entry rule; `RsvpSettingsTest`: steppers, the settings encoding, precedence, state files that are invalid, of another edition, or carry checkpoint members that a write must keep).

## Checkpoint data (`CheckpointBook`)

`docs/PLAN.md` section 5: "the firmware treats a book as prepared when that manifest item exists".
`CheckpointBook::open(epub)`:

1. Reads `META-INF/container.xml` (upstream's `ContainerParser`) for the OPF's path, then streams the
   OPF through `OpfItemFinder` until the manifest ends, looking for the item with id
   `rsvp-checkpoints`. `lib/Epub` keeps no manifest ids, so this needs no upstream change. No item: an
   ordinary book (`CheckpointData::None`, one DBG line).
2. Resolves the item's href against the OPF's folder (`rsvp/checkpoints.json` next to the OPF) and
   reads its inflated size from the zip directory. More than `checkpoint::MAX_FILE_BYTES` (1 MiB):
   rejected with issue code `limit` before anything is copied.
3. Copies it, once, to `/.crosspoint/epub_<hash>/rsvp/checkpoints-<crc-32>-<bytes>.json` (the zip
   entry's CRC-32 and size name the copy, so a changed book never reads an old copy), through a
   `.tmp` file and a rename. `lib/CheckpointCore` reads prompts and answers at stored offsets, and a
   deflated zip entry cannot seek, so its README asks for this copy. Later opens use the copy.
4. Opens the index over the copy (a `HalFile` with random reads). Valid: the book has checkpoints.
   A newer `version`: an ordinary book (`unsupported-version`, one INF line). Any other rejection: an
   ordinary book, logged with the library's issue code, path, offset and detail. A copy made on an
   earlier open that does not read back as valid is copied again once (it may be damaged).

Nothing here blocks reading: every failure returns `nullptr` and the book opens as any other.
`sourceSha256()` is `book.source_sha256`, which the state file stores as `book_sha256`.

**Memory.** The `CheckpointBook` is one heap object of 72,544 bytes in the simulator (the
fixed-size `CheckpointIndex`, which does not depend on the file, plus the file handle and the OPF
path; 4 bytes less for the index on the 32-bit ESP32), for as long as the book is open; on the Picco
a block this size comes from PSRAM. The fixture's file (2,805 bytes) and the Paine book's (91,475
bytes) cost the same RAM: the index keeps offsets, and text is read on demand into the caller's
buffers. `open()` adds, and frees before it returns: expat for container.xml and the OPF (about 1 KB
plus its 1 KB buffer), and on the first open the EPUB's inflate stream for the copy
(`Epub::extractItemToFile`: a 32 KB ring and two 8 KB buffers). No buffer holds the whole file.
Measured in the simulator: 2 ms (fixture) and 5 ms (Paine) for the first open with the copy.

Thread safety: none, as for the index. The checkpoint activity takes the RenderLock around every use.

## The state file's `book_sha256`

`docs/PLAN.md` section 7 describes it as the "hash of the prepared file's source". `lib/CheckpointCore` stores the checkpoint file's `book.source_sha256` there, a value the preparation tool writes into the prepared EPUB, so the firmware reads it rather than hashing anything, and a book without checkpoint data has none (the member is not written). `CheckpointBook` reads it (task C3a); the checkpoint screens pass it to `editBookState()`, and a file without a hash takes it with the screens' first write. RSVP still opens every book with `NO_BOOK_SHA256` (""): a hash already in a file is kept as it is and never compared. C2, which keeps the `CheckpointBook` of the open book, passes the real value everywhere. When C2 passes the real value, a file written for another edition keeps its overrides and grades and loses its position and checkpoint in progress (`decodeForUse()`; C2 should also apply `checkpoint::applyBookChangeRule()` with the open index, which filters the grades).

## State file memory

Each `loadBookState()` or `editBookState()` call allocates one `checkpoint::BookState` (78 KB, nearly all of it the grade table) and a buffer the size of the file, and frees both before returning; the encoder streams to the file through a 128-byte buffer. On the Picco a block this size comes from PSRAM. When C5 keeps a `BookState` for the open book (as `lib/CheckpointCore/README.md` recommends), these calls can take that object instead of allocating their own.

## Word index memory

A build streams the chapter with bounded RAM, all of it freed when `open()` returns: the builder (4.1 KB, heap, `makeUniqueNoThrow`), expat's own allocations (parser, 1 KB input and character buffers, tag stack and attribute pool: measured 8.7 to 9.7 KB peak in `lib/WordIndex/README.md`), the EPUB's inflate stream (`ZipFile::readFileToStream`: a 32 KB ring buffer and two 1 KB chunk buffers), and about 1 KB of stack. An open index keeps the 1.9 KB reader (a member of its owner) and one file handle. The rebuild check reads the chapter's zip entry size and CRC-32 from the central directory: no read of the chapter.

Encrypted (LCP) chapters are indexed from their decrypted bytes, whose CRC-32 is not the zip entry's: their index is rebuilt on every entry, as for TXT books.
