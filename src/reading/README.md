# src/reading

**ReadingSession** (`docs/PLAN.md` section 4): owns the reading position, the mode and the navigation source; decides when a checkpoint fires; saves and restores the per-book state. It connects `lib/RsvpCore`, `lib/CheckpointCore` and `lib/WordIndex` to the reader.

Filled by task C2 (#26), then extended by C4 (#28) and C5 (#29). Task R4a (#21) added the word index service and the position helpers that RSVP uses and C2 reuses; task R4b (#22) the per-book state file service, the pace settings and the rule for where RSVP opens.

Everything here is compiled only with the RSVP feature flag: every file includes `RsvpFeature.h` and wraps its code in `#if CROSSPOINT_RSVP_ENABLED` (see `docs/ARCHITECTURE-RSVP.md`).

| File | What it holds | Task |
| --- | --- | --- |
| `ChapterWordIndex.h/.cpp` | A chapter's word index on demand: opens `/.crosspoint/epub_<hash>/rsvp/<spine>.wix` when it is valid for the chapter's current zip entry, otherwise builds it by streaming the chapter out of the EPUB through `WordIndexBuilder` (to `<spine>.wix.tmp`, renamed when the build succeeds), then opens it; `rebuild()` after a read error during use | R4a |
| `WordIndexCheck.h/.cpp` | The rebuild decision (`lib/WordIndex/README.md`, "Rebuild rules"): valid, missing, unreadable (truncated, unfinished, other format or rules, corrupt, I/O error), stale (other zip size or CRC-32), unverifiable (no zip entry: a TXT book; always rebuilt) | R4a |
| `RsvpPosition.h/.cpp` | Page view and RSVP hand-over (a page's first word; a word's visible offset), chapter percent, sentence stepping over a window of classified RSVP tokens; `chooseEntry()`, where RSVP opens (`src/activities/rsvp/README.md`, "Where RSVP opens"); `itemBefore()`, the image, table or code block a non-prose stop is for (outside the notes; the first of a run) | R4a, R4b, R5 |
| `UpdateTimeoutWatch.h` | Whether the display backend's update timeout count rose since Play or the last frame: RSVP then pauses (`src/activities/rsvp/README.md`, "Display timeout") | R5 |
| `BookStateFile.h/.cpp` | The per-book state file `<book file name>.rsvp-state.json` beside the book: `loadBookState()`, and `editBookState()`, which reads the file, applies one edit and writes it through `<file>.tmp` and a rename, keeping every member the edit does not touch (grades, the checkpoint in progress, `book_sha256`). An invalid or unreadable file reads as a fresh state and is moved to `<file>.bad` by the next save. The encoding is `lib/CheckpointCore`'s `StateCodec`; there is no second JSON format. C3 and C5 add their edits here | R4b |
| `BookStateRules.h/.cpp` | The pure part: what a decoded file means (missing, read, invalid; another edition) | R4b |
| `RsvpValues.h/.cpp` | WPM and words-per-update stepping and ranges, the global default as stored in the settings file (WPM in tens), and the precedence when RSVP opens (the book's override, else the global default) | R4b |
| `ReadingSession.cpp` | Placeholder | C2 |
| `RsvpLinkCheck.h/.cpp` | Temporary start-up check, called from `setup()` in `src/main.cpp`, that links every source file of RsvpCore and CheckpointCore into the firmware and logs one debug line; it also runs DisplayProfile's link check (`src/display/DisplayProfile.h`, task E3), which logs a second line. C2 removes both, and the call in `main.cpp`, when the real ReadingSession lands | F4, E3 |

Host tests of the pure parts: `test/rsvp_activity` (`ReadingHelpersTest`: index decisions on real and damaged indexes, page/word round trips, sentence starts against `fixtures/books/fixture.expected.json`, the entry rule; `RsvpSettingsTest`: steppers, the settings encoding, precedence, state files that are invalid, of another edition, or carry checkpoint members that a write must keep).

## The state file's `book_sha256`

`docs/PLAN.md` section 7 describes it as the "hash of the prepared file's source". `lib/CheckpointCore` stores the checkpoint file's `book.source_sha256` there, a value the preparation tool writes into the prepared EPUB, so the firmware reads it rather than hashing anything, and a book without checkpoint data has none (the member is not written). Reading the checkpoint file is task C2's detection of prepared books, so until then `BookStateFile` opens every book with `NO_BOOK_SHA256` (""): a hash already in a file is kept as it is and never compared. When C2 passes the real value, a file written for another edition keeps its overrides and grades and loses its position and checkpoint in progress (`decodeForUse()`; C2 should also apply `checkpoint::applyBookChangeRule()` with the open index, which filters the grades).

## State file memory

Each `loadBookState()` or `editBookState()` call allocates one `checkpoint::BookState` (78 KB, nearly all of it the grade table) and a buffer the size of the file, and frees both before returning; the encoder streams to the file through a 128-byte buffer. On the Picco a block this size comes from PSRAM. When C5 keeps a `BookState` for the open book (as `lib/CheckpointCore/README.md` recommends), these calls can take that object instead of allocating their own.

## Word index memory

A build streams the chapter with bounded RAM, all of it freed when `open()` returns: the builder (4.1 KB, heap, `makeUniqueNoThrow`), expat's own allocations (parser, 1 KB input and character buffers, tag stack and attribute pool: measured 8.7 to 9.7 KB peak in `lib/WordIndex/README.md`), the EPUB's inflate stream (`ZipFile::readFileToStream`: a 32 KB ring buffer and two 1 KB chunk buffers), and about 1 KB of stack. An open index keeps the 1.9 KB reader (a member of its owner) and one file handle. The rebuild check reads the chapter's zip entry size and CRC-32 from the central directory: no read of the chapter.

Encrypted (LCP) chapters are indexed from their decrypted bytes, whose CRC-32 is not the zip entry's: their index is rebuilt on every entry, as for TXT books.
