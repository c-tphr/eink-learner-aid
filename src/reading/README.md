# src/reading

**ReadingSession** (`docs/PLAN.md` section 4): owns the reading position, the mode and the navigation source; decides when a checkpoint fires; saves and restores the per-book state. It connects `lib/RsvpCore`, `lib/CheckpointCore` and `lib/WordIndex` to the reader.

Filled by task C2 (#26), then extended by C4 (#28) and C5 (#29). Task R4a (#21) added the word index service and the position helpers that RSVP uses and C2 reuses; task R4b (#22) the per-book state file service, the pace settings and the rule for where RSVP opens; task C3a (#93) the checkpoint data loader.

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
| `CheckpointBook.h/.cpp` | A prepared book's checkpoint data (below): finds the manifest item `rsvp-checkpoints`, copies the file out of the EPUB once, opens `lib/CheckpointCore`'s index over the copy; `book_sha256`, lookups for C2 (checkpoint by section, a section's anchor ids, a section's spine index) | C3a |
| `CheckpointBookRules.h/.cpp` | The loader's pure decisions: what a parse result means for the book, the size limit, the copy's name | C3a |
| `OpfItemFinder.h/.cpp` | Finds one manifest item of the OPF by id, streaming the OPF through expat; resolves its zip path | C3a |
| `ReadingSession.h/.cpp` | The open book's session (below): its `CheckpointBook`, the section layout of the chapter being read, the mode, the page-view reading word; when a checkpoint fires in RSVP and in page view, and where reading goes on after it. `readingsession::` holds the reader's registration point (empty inline functions without the flag) | C2 |
| `ReadingSessionRules.h/.cpp` | Its pure decisions: a chapter's sections from the word index's anchors (`ChapterLayout`), the section-end flags of RSVP's word source, the step at an RSVP checkpoint stop, a page's words, the word a next-page action counts from, where page view resumes | C2 |

Task C2 removed the temporary start-up link checks (`RsvpLinkCheck.h/.cpp`, `displayprofile::runDisplayProfileLinkCheck()`) and their call in `src/main.cpp`: the reader now uses every library through ReadingSession, RSVP and the checkpoint screens.

Host tests of the pure parts: `test/checkpoint_activity` (`OpfItemFinderTest`; `CheckpointBookRulesTest` on every shared sample of `fixtures/checkpoints`), `test/rsvp_activity` (`ReadingHelpersTest`: index decisions on real and damaged indexes, page/word round trips, sentence starts against `fixtures/books/fixture.expected.json`, the entry rule; `RsvpSettingsTest`: steppers, the settings encoding, precedence, state files that are invalid, of another edition, or carry checkpoint members that a write must keep; `ReadingSessionTest`: sections from the prepared fixture's anchors, section-end flags against `stopAfterWord()`, groups of 2 words never crossing a section end, the RSVP stop step, page words and trigger rules 4, 5, 7 and 8, resume kinds, another edition's grades filtered with the open index).

## ReadingSession

`docs/PLAN.md` sections 4 and 7; `lib/CheckpointCore/README.md`, "Calling contract". The reader (`EpubReaderActivity`) makes it in `loadBook()` (`readingsession::open()`) and keeps it until the book closes; RSVP gets it through `rsvpentry::Entry::session`, the non-prose banner through `HandOff::session`. It never decides a trigger rule itself: it gathers the inputs and calls `lib/CheckpointCore`'s `rsvpStep()`, `evaluateMove()` and `pageAdvance()`.

**Opening a book.** `CheckpointBook::open()`: a prepared book gets its checkpoint data, any other book none (`[CPT] session open checkpoints=<0|1> sha=<book_sha256|none>`). Every read and write of the state file then goes with `identity()`: the checkpoint file's `book.source_sha256` and its index, or `NO_BOOK_SHA256` and none. `decodeForUse()` applies `checkpoint::applyBookChangeRule()` with the index as the file is read, so a file of another edition loses its position and checkpoint in progress and keeps only the grades of prompt ids the book still has; a file without a hash takes the book's; an ordinary book's file is read as before (no hash, every grade kept). Nothing is written at open: the next write saves the result.

**Sections.** For the chapter being read, `ChapterSections` (lib/CheckpointCore) loaded from the chapter's word index: each section of the chapter's `chapter_href` (found through `CheckpointBook::sectionSpine()`) gets its word range from its anchors `rsvp-<id>-start` / `rsvp-<id>-end` (`ChapterLayout`). RSVP loads them from its own index when it opens a chapter; page view from the session's own index. A chapter without sections needs no index. Sections whose anchors are missing are logged (`failed=`) and cannot fire.

**RSVP.** The word source (`IndexWordSource`) gets `ReadingSession::sectionEnds`, so the last word of a checkpoint's section carries `TOKEN_CHECKPOINT`: a frame never groups past it (RsvpCore's rule), and the scheduler stops once it has had its slot (`STOP_CHECKPOINT`) with nothing beyond drawn. `rsvpStop()` makes the Fire step (`rsvpCheckpointStep()`: `rsvpStep()` on the word on screen, or `evaluateMove()` over the note words between it and the next prose word). RSVP then shows CheckpointActivity over itself (`src/activities/rsvp/README.md`, "Checkpoints").

**Page view.** At the top of `EpubReaderActivity::pageTurn()`, a forward turn asks `readingsession::nextPageCheckpoint()` with the page being left (its start offset and the next page's, the next page laid out first if needed). `pageWords()` gives the page's words (those whose last character it shows, S1 section 4.1); the word counted from is the page's first, or the page-view reading word when it lies on that page; `pageAdvance()` decides. On Fire, CheckpointActivity is pushed and the page does not turn. A backward turn and a chapter skip clear the reading word and never fire (`[CPT] move source=previous-page|chapter-skip`).

**After the checkpoint** (plan section 7, point 6; S4 row 33). `endCheckpoint()` gives the place: the flow's `resume` for Finished and Skipped, otherwise the first word after the break in the mode it fired in. In page view `afterPageCheckpoint()` decides: the resume word on the page that was left (it stays; the reading word becomes the resume word, and the reader asks `nextPageCheckpoint()` again for the same page: trigger rule 7, a second section end on that page fires before reading continues); a later page or the next chapter's word 0 (the reader jumps to the page holding it, rule 8); the end of the book. RSVP: `src/activities/rsvp/README.md`, "Checkpoints".

| Outcome | What happens |
| --- | --- |
| Finished, Skipped | Reading resumes after the break, in the mode the checkpoint appeared in, and waits. The state file's `position` is the resume place (written by the activity) |
| RereadRsvp, RereadPage | **Task C4 replaces this** (`ReadingSession::endCheckpoint()`): the reread is not done yet. The grade is saved; reading resumes after the break as for Finished, the checkpoint in progress is removed from the file and `position` set to the resume place, and `[CPT] reread in <rsvp\|page> not implemented yet (task C4)` is logged |
| Failed (no memory, or the flow refused) | Nothing was saved; reading resumes after the break (logged). Page view stays on the page, RSVP pauses there |
| None (Home gesture, sleep while the checkpoint shows) | No result reaches the reader. The file holds the checkpoint in progress and, written when it started, the position before the break (`Request::before`: RSVP the frame on screen, page view the page's counted word); the reader's progress is that page. Reopening the book shows that place, and reading on fires the checkpoint again from its first prompt. Restoring the prompt itself is task C5 |

**Navigation sources.** Only ordinary forward reading reaches the trigger rules; every other route moves the position without asking them, so it never fires, and entering a section again by any of them re-arms its checkpoint (rule 5) with nothing stored.

| Route (reader or RSVP) | Code | Navigation source | Fires |
| --- | --- | --- | --- |
| Next page: side key, tap zone, swipe, a turn queued during a refresh, the Bluetooth remote's next page | `pageTurn(true)` | NextPage | yes |
| Automatic page turn (reader menu "Auto Turn") | `pageTurn(true)` from `loop()` | NextPage | yes; a checkpoint stops automatic turning, since nothing advances by itself after it |
| The non-prose banner left by a page-turn swipe | `pageTurn(±1)` | NextPage / PreviousPage | forward only |
| Previous page (the same inputs), end-of-book screen back | `pageTurn(false)`, `onReturnFromEndOfBook()` | PreviousPage | no |
| Chapter skip (long-press setting "Chapter skip", the remote's chapter keys) | `skipPages()` | chapter skip (as TableOfContents) | no |
| Table of contents: toolbar Contents, Select chapter, the toolbar's chapter scrub (Up/Down, scrub bar) | overlay `gotoSpine()`, chapter selection | TableOfContents | no |
| Footnote and link taps, footnote list, back from a footnote | `navigateToHref()`, `restoreSavedPosition()` | TableOfContents | no |
| Go to % | `jumpToPercent()` | GoToPercent | no |
| Bookmarks list, clippings list | `applyProgressChange()` | Bookmark | no |
| KOReader and plugin progress sync | `applyProgressChange()` | ProgressSync | no |
| Opening the book at its saved progress | `loadBook()` | OpenRestore | no |
| Relayout (font, margins, orientation) keeps the content offset | `pendingOffsetJump` | none (no move) | no |
| RSVP: Page view, Menu, Exit; the non-prose hand-over and Stay | `openSpeedRead()` result, `showSpeedReadItem()` | ModeSwitch | no |
| Checkpoint resume (jump to the page holding the resume word) | `resumeAfterCheckpoint()` | (after the break) | no; on the same page rule 7 asks again |
| RSVP playback, words and chapter changes | `Scheduler::step()` | RsvpPlayback | yes |
| RSVP sentence back / forward | `stepSentence()` | SentenceStep | no |
| RSVP entry at a page, a saved word, after the item (Continue) | `RsvpActivity::load()` | ModeSwitch / OpenRestore | no |

**Known limit.** The page-view reading word is cleared by every page turn and chapter skip, not by the other jumps; it is used only while it lies on the page being left. A jump away and back to exactly that page (a bookmark on it) therefore counts from the resume word, so the checkpoint of a section that ends before it on that page does not fire again on that visit.

**Log lines** (tag `CPT`): `session open ...`; `sections ch=<spine> words=<n> sections=<n> checkpoints=<n> failed=<n>`; `rsvp stop ch= last= next= step=<none|fire|reread-end>`; `page advance ch=<spine> first=<page's first word> from=<word counted from> last=<page's last word> step=<none|fire|reread-end>` (every next-page action in a chapter with a checkpoint); `fire source=<rsvp-playback|next-page> checkpoint=<id> section=<id> ch=<c> last_word=<w> resume_ch=<c> resume_word=<w> crossed=<n>`; `resume end=<finished|skipped|reread-rsvp|reread-page|failed|none> mode=<m> ch=<c> word=<w>`; `page resume <this-page|later-page|chapter|end-of-book> ch= word= offset=`; `move source=<previous-page|chapter-skip>`; `mode <rsvp|page> source=mode-switch`.

**Memory.** The session is one heap block of 56 bytes (simulator; pointers and the reading word) for every EPUB opened in an RSVP build. A prepared book adds, for as long as it is open: the `CheckpointBook` (72,544 bytes, C3a; allocated in `CheckpointBook::open()`) and the `Layout` (8,120 bytes in the simulator: `ChapterSections`, 6,156 bytes, room for `MAX_SECTIONS` spans of one chapter, plus page view's `ChapterWordIndex`, the 1.9 KB reader and one open file handle), allocated in `ReadingSession::attachLayout()`. The same sizes for the fixture (5 sections) and the Paine book (55 sections, 23 in Common Sense): both blocks are fixed-size. On the Picco, blocks of this size come from PSRAM. Per checkpoint: one `CheckpointRun` (`std::make_shared`, about 150 bytes) and an `Outcome`, kept by the result handler; CheckpointActivity's own memory is in `src/activities/checkpoint/README.md`. Nothing is allocated per word or per page turn: a next-page action reads two binary searches of the open index (`firstWordEndingAfter()`); the first action in a chapter with a checkpoint builds or opens its index (the build's working memory is transient, "Word index memory" below).

**For C4 and C5.** The session already keeps what both need: the open index, the sections of the chapter, the mode. C4 replaces the reread branch of `endCheckpoint()` (and the matching `RsvpActivity::returnFromCheckpoint()` / `EpubReaderActivity::resumeAfterCheckpoint()` paths) with the reread itself, passes `flow.rereadSection()` to the trigger calls (`rsvpStop()`, `nextPage()`), and reports moves during a reread (`rereadMoved()`); the page-view hook is the place for the reread bar's input. C5 adds `planRestore()` on open: with a `BookLayout` over the session's word indexes (`ChapterLayout` covers one chapter; restore needs any chapter's word count and a section's range), then `CheckpointFlow::restore()` and the activity opened with the restored flow instead of a Fire step. CheckpointActivity keeps its own `BookState` and flow today (78 KB while it shows); keeping one `BookState` in the session, as `lib/CheckpointCore/README.md` suggests, would let C5 share it.

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

`docs/PLAN.md` section 7 describes it as the "hash of the prepared file's source". `lib/CheckpointCore` stores the checkpoint file's `book.source_sha256` there, a value the preparation tool writes into the prepared EPUB, so the firmware reads it rather than hashing anything, and a book without checkpoint data has none (the member is not written). `CheckpointBook` reads it (task C3a); since task C2 every reader and writer of the file (RSVP, the banner, the checkpoint screens, the reread stub) passes the session's `identity()`, the hash and the open index, so a file without a hash takes the book's at its next write, and a file written for another edition keeps its overrides and the grades of prompt ids the book still has and loses its position and checkpoint in progress (`decodeForUse()` with `checkpoint::applyBookChangeRule()`).

## State file memory

Each `loadBookState()` or `editBookState()` call allocates one `checkpoint::BookState` (78 KB, nearly all of it the grade table) and a buffer the size of the file, and frees both before returning; the encoder streams to the file through a 128-byte buffer. On the Picco a block this size comes from PSRAM. When C5 keeps a `BookState` for the open book (as `lib/CheckpointCore/README.md` recommends), these calls can take that object instead of allocating their own.

## Word index memory

A build streams the chapter with bounded RAM, all of it freed when `open()` returns: the builder (4.1 KB, heap, `makeUniqueNoThrow`), expat's own allocations (parser, 1 KB input and character buffers, tag stack and attribute pool: measured 8.7 to 9.7 KB peak in `lib/WordIndex/README.md`), the EPUB's inflate stream (`ZipFile::readFileToStream`: a 32 KB ring buffer and two 1 KB chunk buffers), and about 1 KB of stack. An open index keeps the 1.9 KB reader (a member of its owner) and one file handle. The rebuild check reads the chapter's zip entry size and CRC-32 from the central directory: no read of the chapter.

Encrypted (LCP) chapters are indexed from their decrypted bytes, whose CRC-32 is not the zip entry's: their index is rebuilt on every entry, as for TXT books.
