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
| `BookStateFile.h/.cpp` | The per-book state file `<book file name>.rsvp-state.json` beside the book: `loadBookState()`, and `editBookState()`, which reads the file, applies one edit and writes it through `<file>.tmp` and a rename, keeping every member the edit does not touch (grades, the checkpoint in progress, `book_sha256`). An invalid or unreadable file reads as a fresh state and is moved to `<file>.bad` by the next save. The encoding is `lib/CheckpointCore`'s `StateCodec`; there is no second JSON format. C3 and C5 add their edits here. Task C5: `writeBookState()` (the whole state, for the restore, which holds it already), `bookStateExists()`, and the read of a complete `<file>.tmp` that a power cut left between the old file's removal and the rename ("Saving: atomic, proven" below) | R4b, C5 |
| `BookStateRules.h/.cpp` | The pure part: what a decoded file means (missing, read, invalid; another edition) | R4b |
| `RsvpValues.h/.cpp` | WPM and words-per-update stepping and ranges, the global default as stored in the settings file (WPM in tens), and the precedence when RSVP opens (the book's override, else the global default) | R4b |
| `CheckpointBook.h/.cpp` | A prepared book's checkpoint data (below): finds the manifest item `rsvp-checkpoints`, copies the file out of the EPUB once, opens `lib/CheckpointCore`'s index over the copy; `book_sha256`, lookups for C2 (checkpoint by section, a section's anchor ids, a section's spine index) | C3a |
| `CheckpointBookRules.h/.cpp` | The loader's pure decisions: what a parse result means for the book, the size limit, the copy's name | C3a |
| `OpfItemFinder.h/.cpp` | Finds one manifest item of the OPF by id, streaming the OPF through expat; resolves its zip path | C3a |
| `ReadingSession.h/.cpp` | The open book's session (below): its `CheckpointBook`, the section layout of the chapter being read, the mode, the page-view reading word; when a checkpoint fires in RSVP and in page view, and where reading goes on after it; the checkpoint in progress (its flow and state) and the reread of its section ("Rereading"); the restore when the book opens ("Opening a book"). `readingsession::` holds the reader's registration point (empty inline functions without the flag) | C2, C4, C5 |
| `ReadingSessionRules.h/.cpp` | Its pure decisions: a chapter's sections from the word index's anchors (`ChapterLayout`), the section-end flags of RSVP's word source, the step at an RSVP checkpoint stop, a page's words, the word a next-page action counts from, where page view resumes; whether a page shown during a reread keeps it and where (`rereadPagePlace()`), and the section bounds a reread entry or sentence step keeps to; for the restore, a `BookLayout` over every chapter's word index (`BookIndexLayout`) and what the reader does with the plan (`openActionFor()`) | C2, C4, C5 |

Task C2 removed the temporary start-up link checks (`RsvpLinkCheck.h/.cpp`, `displayprofile::runDisplayProfileLinkCheck()`) and their call in `src/main.cpp`: the reader now uses every library through ReadingSession, RSVP and the checkpoint screens.

Host tests of the pure parts: `test/checkpoint_activity` (`OpfItemFinderTest`; `CheckpointBookRulesTest` on every shared sample of `fixtures/checkpoints`), `test/rsvp_activity` (`ReadingHelpersTest`: index decisions on real and damaged indexes, page/word round trips, sentence starts against `fixtures/books/fixture.expected.json`, the entry rule; `RsvpSettingsTest`: steppers, the settings encoding, precedence, state files that are invalid, of another edition, or carry checkpoint members that a write must keep; `ReadingSessionTest`: sections from the prepared fixture's anchors, section-end flags against `stopAfterWord()`, groups of 2 words never crossing a section end, the RSVP stop step, page words and trigger rules 4, 5, 7 and 8, resume kinds, another edition's grades filtered with the open index; task C4: the reread section's end in RSVP and page view, which pages keep a reread and where, sentence steps and entries inside the section, and the flow driven through a page-view reread; task C5 (`ReadingSessionRestore`): `planRestore()` over the word indexes of every chapter of the prepared fixture for a prompt, a reread in each mode, a reread outside its section, an unknown checkpoint, a prompt index out of range, a resume outside the book and another edition, and what the reader does with each plan). `test/book_state_file` (task C5): `BookStateFile` on an SD card in memory whose power is cut after any number of changes.

## ReadingSession

`docs/PLAN.md` sections 4 and 7; `lib/CheckpointCore/README.md`, "Calling contract". The reader (`EpubReaderActivity`) makes it in `loadBook()` (`readingsession::open()`) and keeps it until the book closes; RSVP gets it through `rsvpentry::Entry::session`, the non-prose banner through `HandOff::session`. It never decides a trigger rule itself: it gathers the inputs and calls `lib/CheckpointCore`'s `rsvpStep()`, `evaluateMove()` and `pageAdvance()`.

**Opening a book.** `CheckpointBook::open()`: a prepared book gets its checkpoint data, any other book none (`[CPT] session open checkpoints=<0|1> sha=<book_sha256|none>`). Every read and write of the state file then goes with `identity()`: the checkpoint file's `book.source_sha256` and its index, or `NO_BOOK_SHA256` and none. `decodeForUse()` applies `checkpoint::applyBookChangeRule()` with the index as the file is read, so a file of another edition loses its position and checkpoint in progress and keeps only the grades of prompt ids the book still has; a file without a hash takes the book's; an ordinary book's file is read as before (no hash, every grade kept). Then the reader calls `readingsession::restoreOnOpen()` (task C5, "Opening a book" below): a checkpoint or reread in progress is restored, and a file of another edition, or one with something that cannot be restored, is written back cleaned before anything shows.

**Sections.** For the chapter being read, `ChapterSections` (lib/CheckpointCore) loaded from the chapter's word index: each section of the chapter's `chapter_href` (found through `CheckpointBook::sectionSpine()`) gets its word range from its anchors `rsvp-<id>-start` / `rsvp-<id>-end` (`ChapterLayout`). RSVP loads them from its own index when it opens a chapter; page view from the session's own index. A chapter without sections needs no index. Sections whose anchors are missing are logged (`failed=`) and cannot fire.

**RSVP.** The word source (`IndexWordSource`) gets `ReadingSession::sectionEnds`, so the last word of a checkpoint's section carries `TOKEN_CHECKPOINT`: a frame never groups past it (RsvpCore's rule), and the scheduler stops once it has had its slot (`STOP_CHECKPOINT`) with nothing beyond drawn. `rsvpStop()` makes the Fire step (`rsvpCheckpointStep()`: `rsvpStep()` on the word on screen, or `evaluateMove()` over the note words between it and the next prose word). RSVP then shows CheckpointActivity over itself (`src/activities/rsvp/README.md`, "Checkpoints").

**Page view.** At the top of `EpubReaderActivity::pageTurn()`, a forward turn asks `readingsession::nextPageCheckpoint()` with the page being left (its start offset and the next page's, the next page laid out first if needed). `pageWords()` gives the page's words (those whose last character it shows, S1 section 4.1); the word counted from is the page's first, or the page-view reading word when it lies on that page; `pageAdvance()` decides. On Fire, CheckpointActivity is pushed and the page does not turn. A backward turn and a chapter skip clear the reading word and never fire (`[CPT] move source=previous-page|chapter-skip`).

**After the checkpoint** (plan section 7, point 6; S4 row 33). `endCheckpoint()` gives the place: the flow's `resume` for Finished and Skipped, otherwise the first word after the break in the mode it fired in. In page view `afterPageCheckpoint()` decides: the resume word on the page that was left (it stays; the reading word becomes the resume word, and the reader asks `nextPageCheckpoint()` again for the same page: trigger rule 7, a second section end on that page fires before reading continues); a later page or the next chapter's word 0 (the reader jumps to the page holding it, rule 8); the end of the book. RSVP: `src/activities/rsvp/README.md`, "Checkpoints".

| Outcome | What happens |
| --- | --- |
| Finished, Skipped | Reading resumes after the break, in the mode the checkpoint appeared in, and waits. The state file's `position` is the resume place (written by the activity). After a reread the view on top may be the other mode: it changes to the original one ("Rereading", below) |
| RereadRsvp, RereadPage | The reread starts (task C4; "Rereading", below): `ReadingSession::startReread()` gives the section's first word in the chosen mode. The checkpoint stays in progress |
| Failed (no memory, or the flow refused) | Nothing was saved; reading resumes after the break (logged). Page view stays on the page, RSVP pauses there |
| None (Home gesture, sleep while the checkpoint shows) | No result reaches the reader. The file holds the checkpoint in progress and, written when it started, the position before the break (`Request::before`: RSVP the frame on screen, page view the page's counted word); the reader's progress is that page. Reopening the book shows the same prompt with its answer hidden (task C5, "Opening a book") |

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

**Log lines** (tag `CPT`): `session open ...`; `sections ch=<spine> words=<n> sections=<n> checkpoints=<n> failed=<n>`; `rsvp stop ch= last= next= step=<none|fire|reread-end> reread=<section id being reread|->`; `page advance ch=<spine> first=<page's first word> from=<word counted from> last=<page's last word> step=<none|fire|reread-end> reread=<section id|->` (every next-page action in a chapter with a checkpoint); `fire source=<rsvp-playback|next-page> checkpoint=<id> section=<id> ch=<c> last_word=<w> resume_ch=<c> resume_word=<w> crossed=<n>`; `resume end=<finished|skipped|failed|none> mode=<m> ch=<c> word=<w> reread=<0|1>`; `page resume <this-page|later-page|chapter|end-of-book|reread-page|reread-rsvp|rsvp> ch= word= offset=`; `move source=<previous-page|chapter-skip>`; `mode <rsvp|page> source=mode-switch`; the reread's lines in "Rereading" below.

**Memory.** The session is one heap block of 128 bytes (simulator; pointers, the reading word, since task C4 the checkpoint in progress: two shared pointers, a flag and a place, and since task C5 two flags and the renderer pointer the restore's "Preparing..." uses) for every EPUB opened in an RSVP build. A prepared book adds, for as long as it is open: the `CheckpointBook` (72,544 bytes, C3a; allocated in `CheckpointBook::open()`) and the `Layout` (8,120 bytes in the simulator: `ChapterSections`, 6,156 bytes, room for `MAX_SECTIONS` spans of one chapter, plus page view's `ChapterWordIndex`, the 1.9 KB reader and one open file handle), allocated in `ReadingSession::attachLayout()`. The same sizes for the fixture (5 sections) and the Paine book (55 sections, 23 in Common Sense): both blocks are fixed-size. On the Picco, blocks of this size come from PSRAM. Per checkpoint: one `CheckpointRun` (`std::make_shared`, about 170 bytes) and an `Outcome` (one more per return to the prompt after a reread), kept by the session and the result handler, and the checkpoint's flow and state (`checkpointentry::Progress`, 78,056 bytes, nearly all of it the grade table; PSRAM on the Picco) from the Fire step until the checkpoint ends ("Rereading", "Memory" there); CheckpointActivity's own memory is in `src/activities/checkpoint/README.md`. Nothing is allocated per word or per page turn: a next-page action reads two binary searches of the open index (`firstWordEndingAfter()`); the first action in a chapter with a checkpoint builds or opens its index (the build's working memory is transient, "Word index memory" below).

## Rereading

Task C4 (#28); `docs/PLAN.md` section 7 ("Trigger rules" sixth bullet, "Per-prompt flow" points 4-6, "Grade rules", "State file"); S4 rows 25-33; PRD criterion 2. After Incorrect the reader rereads the prompt's section in RSVP or in page view, comes back to the same prompt with its answer hidden, and retries or goes on. The first grade never changes. The rules are `lib/CheckpointCore`'s (`CheckpointFlow`'s Rereading state, trigger rule 6); this code gathers their inputs.

**The checkpoint in progress lives in the session.** `startCheckpoint()` allocates the flow and the state it changes (`checkpointentry::Progress`: `BookState` and `CheckpointFlow`, 78,056 bytes) and keeps it with the `CheckpointRun` until the checkpoint ends (`endCheckpoint()` frees both). CheckpointActivity drives the session's flow (`Request::progress`) instead of allocating its own, so the flow survives while the screens are closed for the reread, and the activity opens again on the same flow for the prompt after it. Whoever is on top shows the prompt when one waits (`promptDue()`).

**Start** (`startReread()`). The after-Incorrect screen's Reread in RSVP or Reread in page view writes `active_checkpoint` with `reread` at the section's first word in the chosen mode (`resume` unchanged) before the screens close; then:

| Fired in \ reread in | RSVP | Page view |
| --- | --- | --- |
| RSVP (RSVP under the screens) | RSVP seeks to the section's first word: its pause screen, Play highlighted, status "Rereading", with "Back to the question" | RSVP closes to page view (`leave to=reread-page`) at the page holding that word, with the bar |
| Page view (the reader under the screens) | The reader opens RSVP (`Go::SpeedRead`), which takes the reread place from the session (`rsvpStart()`) | The reader goes to the page holding that word, with the bar below the text |

**During the reread** `rereadSection()` goes to `rsvpStop()` and `nextPage()`, so no checkpoint fires and the section's own end gives `RereadEnd`: RSVP stops with the section's last word on screen (`pause reason=reread-end`), page view on the next-page action from the page that shows it, before the next page. Both go back to the prompt by themselves.

**The page-view bar.** After each page render the reader calls `pageShown()` with where its text area ends (its screen height minus its bottom margin: the display inset, the screen margin and the status-bar lane) and one line of its text. When the page shows a word of the section (`rereadPagePlace()`: the section's first page counts from its first word, a later page from its reading word or first word), the flow records it as the reread place (`rereadMoved()`, saved when it changed) and the bar is drawn with a second, fast refresh over the reader's status-bar row, which a reread does not need (battery, title, page count): a white strip from the text area's bottom to the screen's bottom edge with a 1 px rule along its top, the label at the left and the "Back to the question" button at the right (`controls::rereadBarLayout()`; this replaces S4 row 31's top bar, which hid the page's first two lines). The texts take the UI font whose line fits the strip, UI 12 or UI 10; the label is "Rereading this section", or "Rereading" when that does not fit beside the button. A tap on the button is checked in the reader's `loop()` before links, the menu tap and the page-turn zones (S4 section 8); a tap is told apart from the bottom-edge Home swipe as everywhere (a swipe moves, a tap does not; R5's banner sits on the same edge). A side-key hold, which normally enters RSVP, opens a list instead, whatever the long-press setting says (S4 section 8): **Back to the question** (the default), Speed read from here, Keep reading; the Back gesture is Keep reading. With the long-press setting Off a hold is impossible (keys turn pages on press); paging to the section's end still returns to the prompt.

Where the bar is, measured in the simulator (`[CPT] reread bar metrics` and `reread bar shown` log lines; `ControlMapTest`):

| Reader setting | Text area ends at | Strip | Button (x,y,w,h) | Covers text |
| --- | --- | --- | --- | --- |
| Defaults (portrait 480x800) | y 773 | 773-800 | 216,774,248,26; label "Rereading" | No |
| Progress bar shown | y 768 | 768-800 | 216,769,248,31 | No |
| Screen margin 40 | y 738 | 738-800 | 216,739,248,61 | No |
| Landscape (800x480) | y 453 | 453-480 | 536,454,248,26; label "Rereading this section" | No |
| Status bar hidden | y 792 | 776-800 | 216,777,248,23 (UI 10) | The bottom 16 px of the text area: at most part of the last line |

When less than 24 px (`REREAD_BAR_MIN_H`) are free below the text area, the strip grows upward by at most one line of the reader's text, so it can cover the bottom margin and the last line, never more and never the top of the page. With the default settings the button is 26 px high, below the 56 px aimed for: the reader's status-bar row is 27 px high, and anything taller would cover text. The toolbar reader menu (touch boards) is drawn over the page including the bar and owns all input while open; the bar is back when it closes.

**Every way out of a reread, and what happens:**

| Way out | Mode | What happens |
| --- | --- | --- |
| Play reaches the section's last word | RSVP | Back to the prompt (`RereadReachedEnd`), answer hidden |
| Next page from the page showing the section's last word (key, swipe, tap zone, automatic turn) | Page view | Back to the prompt before the next page shows |
| "Back to the question" on the pause screen (tap 16,128,448,64; keys: any key pauses, Up x2, hold) | RSVP | Back to the prompt (`ReturnEarly`) |
| The bar's button (tap 216,774,248,26 with the default settings), or hold, then hold on the list's default row | Page view | Back to the prompt (`ReturnEarly`) |
| Previous page to a page with no word of the section; table of contents, chapter skip, go to %, bookmarks, links and footnotes, progress sync to a page outside it | Page view | The page shows briefly, then back to the prompt as Return early (`leftSection`; `planRestore()` treats a reread outside its section the same way). Finishing the checkpoint then resumes after its original break, not where the jump went |
| Sentence step across the section's start or end | RSVP | Refused: the position does not move (`rereadStepStays()`), so a step never ends the reread |
| Page view on the pause screen (or the Back swipe), Menu | RSVP to page view | Allowed: the reread goes on in page view at RSVP's word (`reread moved ... source=mode-switch`, saved) |
| Speed read from here (the list), the reader menu's Speed read | Page view to RSVP | Allowed: RSVP opens at the reread place, inside the section |
| An image, table or code block inside the section | RSVP | Page view shows it with the banner (task R5) and the bar; Continue opens RSVP again inside the reread; Stay keeps the reread in page view |
| Exit on the pause screen, Home, sleep, power off | Either | The reread stays in the state file (below); reopening the book restores it in its mode at its place (task C5, "Opening a book") |
| Skip checkpoint | (only on the prompt screens) | The flow accepts it while rereading, but no reread screen offers it |

Mode switches are allowed because the reread place and its mode live in the flow, which every view reports to: whichever view shows a position of the section during a reread records it, and each has a visible way back to the question by touch and by keys.

**Back at the prompt** (S4 row 32). The same prompt, answer hidden, "Recorded earlier: Incorrect" (the stored first grade, as on a later pass); Reveal is the retry, Skip question is Continue. Correct moves on and the file keeps Incorrect; Incorrect again offers the reread again.

**When the checkpoint ends** (finished or skipped) reading resumes at the original resume place in the mode the checkpoint fired in, whatever view is on top: RSVP on top of a page-view checkpoint closes (`Then::CheckpointDone`) and the reader resumes as in C2/C3, at the page holding the resume word (never "this page": the page that fired is no longer shown); the reader on top of an RSVP checkpoint opens RSVP at the resume word (`page resume rsvp`, then `enter ... from=checkpoint`), paused with "Checkpoint done".

**RSVP's meter and audit.** A reread in RSVP is more playback in the same session: the delivered-WPM meter counts its words and time (the display delivered them; time on the screens and the pause screen stays outside, as before). The move back to the section's start, and the move to the resume word when the reread was left early, are `Scheduler::reposition()` calls, each opening a new segment of the word-order audit, so the jump back is never a violation of the main reading. When the section's end is reached in RSVP and the checkpoint then finishes, the source stands at the resume word and the stop is resolved, so the audit's segment carries on in order. An RSVP opened by the reader (a reread from page view, or the resume of an RSVP checkpoint after a page-view reread) starts a new session, as every RSVP entry from page view does (status "-- actual" until Play).

**State file** (`docs/PLAN.md` section 7). `position` keeps the place before the break written when the checkpoint fired; during a reread RSVP and page view write `active_checkpoint.reread` (mode, chapter, word) instead, through the flow: at RSVP's pauses (before the pause screen is drawn, task C5), mode switches, Exit, Home and sleep, and for every page shown in page view (before the page is drawn, task C5: `pageShowing()`), only when it changed. `prompt_index` and `resume` stay as they were. Back at the prompt `reread` is removed.

**Closing during a reread.** The file holds `active_checkpoint` with `prompt_index`, `resume` and `reread`, and `position` before the break in the mode the checkpoint fired in. Reopening the book restores the reread in its mode at its place (task C5, "Opening a book"; `cp_kill_in_reread`, `cp_sleep_restore`).

**Log lines** (tag `CPT`): `reread start mode=<rsvp|page> checkpoint=<id> section=<id> prompt=<i> ch=<c> start=<w> last_word=<w>`; `page resume reread-<page|rsvp> ch= word= offset=`; `reread page ch=<spine> first=<w> last=<w> any=<0|1> inside=<0|1> word=<w>` (every page shown during a reread), `reread bar metrics screen=<w>x<h> text_bottom=<y> line=<px>` and `reread bar shown top=<y> button=<x,y,w,h> clear_of_text=<0|1> label="<text>"`; `reread moved by=<page|pause|exit|home|sleep|enter|stay|...> source=<nav source> mode=<m> ch=<c> word=<w> state=<flow state> left_section=<0|1>`; `reread end by=<rsvp-playback|next-page|pause-screen|bar|list> event=<reread-reached-end|return-early> accepted=<0|1> state=<flow state>`; `saved after=<reread-moved|reread-reached-end|return-early|reread-left-section> ... reread=<ch:word:mode|none>` (CheckpointActivity's line); `reread list focus=<Item>`; `[CTL] RereadBar select=BackToQuestion`, `[CTL] RereadBar hold: the list`, `[CTL] RereadList armed|focus=|select=`. RSVP's own lines are in `src/activities/rsvp/README.md`.

**Memory.** Unchanged at its peak: the 78,056-byte flow and state that CheckpointActivity allocated while it showed is now allocated by the session when the checkpoint fires and kept until it ends, a reread included, and the activity no longer allocates one. While a checkpoint shows: 78,056 (flow and state) + 4,056 (the activity, 4,016 before: it holds a shared pointer instead of a unique one) + up to 78 KB and the file during a state file write, as before. During a reread with the screens closed: the same 78,056 bytes, plus RSVP (5,512 bytes) or the reader, plus the transient `BookState` of a state file read or write; less than while the screens show. The bookkeeping adds 56 bytes to every session (above) and about 20 bytes per checkpoint. The page-view list is a small activity (one per hold), freed when it closes.

## Opening a book

Task C5 (#29); `docs/PLAN.md` section 7 (opening paragraph, "Trigger rules" fifth bullet, "Grade rules", "State file"); PRD criterion 4; `lib/CheckpointCore/README.md`, "Opening a book". The library decides what can be restored (`checkpoint::planRestore()`, `CheckpointFlow::restore()`); this code gathers its inputs and shows the result.

**Where it runs.** `EpubReaderActivity::loadBook()`, on the main task inside the reader's `onEnter()`, after `readingsession::open()`: `readingsession::restoreOnOpen()`. Every way a book opens goes through it: Home's card, the file browser, a power loss or crash (the next open), and the wake from sleep (the firmware reopens the book).

1. **No state file** (`bookStateExists()`: the file, or a complete `.tmp` a power cut left): nothing is read or allocated; the reader opens at its own progress. A book without checkpoint data goes on to `rsvpentry::openedInRsvp()` as before (it has the same early return).
2. **A prepared book with a state file**: the checkpoint block (`checkpointentry::Progress`, 78,056 bytes) is allocated and the file is read straight into its `BookState`. A file of another edition was cleaned as it was read (`applyBookChangeRule()`): it is written back now (`[CPT] restore dropped reason=other-edition`).
3. **No checkpoint in progress**: the open mode is the saved position's, as before (RSVP opens over the first page when it is `rsvp`); the block is freed. `planRestore()` is not called, so opening a book never builds a word index just to check a plain position (RSVP clamps its own entry).
4. **A checkpoint in progress**: `planRestore()` over `BookIndexLayout`, a `BookLayout` over every chapter's word index, opened one at a time through page view's `ChapterWordIndex` (built if missing, with "Preparing..." on screen through `GUI.drawPopup()` when the chapter is large, `ChapterWordIndex::NOTICE_SOURCE_BYTES`). It needs the section's chapter and the resume's chapter. Then, in the plan's order:

| Plan | What the owner sees | How |
| --- | --- | --- |
| `Reread`, page view | The page holding the saved reread word, with the reread bar | The reader opens at that page (`OpenRestore::jump`, the word's offset), the page-view reading word is the reread word, and `pageShowing()` keeps the reread (the place does not change, so nothing is written) |
| `Reread`, RSVP | One refresh of the page holding the reread word, then RSVP's pause screen at that word: "Rereading", "Back to the question", Play highlighted, nothing playing | The reader jumps to that page and sets `resumeSpeedRead`; RSVP takes the place from `rsvpStart()`. That first page is marked "under RSVP" (`setPageUnderRsvp()`), so it is not taken as the reread's place in page view |
| `Prompt` | The prompt, answer hidden, "question N of M" as saved, the stored grade as "Recorded earlier"; no page of the book first | `promptWaiting`; `loadBook()` calls `openRereadPrompt()`, so CheckpointActivity is pushed inside the reader's `onEnter()`, before the activity manager hands the first render to the reader. `APP_STATE.openEpubPath` is set then (a sleep during the prompt reopens the book), since `rememberBookOnceRendered()` waits for a page |
| `Position` after a dropped checkpoint | Page view at the page holding the place (normally the first word after the dropped checkpoint's break), or RSVP there | `jump` in page view; RSVP by its own rule (the cleaned file's position) |
| `BookStart` | The reader's own progress | Nothing to do |

For `Reread` and `Prompt` the session keeps the block as the checkpoint in progress, as `startCheckpoint()` does, with a `CheckpointRun` rebuilt from the plan: `fire` (the checkpoint, its section range, the saved `resume` as the first word after the break), `mode` = the mode the checkpoint fired in (`active.resume.mode`), and `reread = true` (no view shows the page it fired on). From there every path is C4's: the way back to the prompt, Reveal, the grades, the reread in either mode, and the end at `resume` in the mode it fired in (page view: the page holding the resume word; RSVP: its pause screen there with "Checkpoint done"). When the checkpoint ends after a restored prompt the reader has no page yet: RSVP opens once the page holding its word is drawn (`resumeAfterCheckpoint()` sets `resumeSpeedRead`; `PageResume::offset` is filled for `SpeedRead` too).

**What cannot be restored.** `planRestore()` removes it from the state and says why; the session writes the cleaned file at once (`writeBookState()`, the block's own state: no second one) and logs one line. Nothing loops: the cleaned file has nothing left to restore. No grade is ever dropped (another edition keeps the grades of prompt ids the book still has, as before).

| Saved | Opens at | `notes` in the log |
| --- | --- | --- |
| A checkpoint id the book does not have | After its break (`resume`), in its mode | `unknown-checkpoint` |
| A prompt index past the checkpoint's prompts | After its break, in its mode | `prompt-out-of-range` |
| Section anchors not found | After its break, in its mode | `section-unresolved` |
| `resume` outside the book | The position before the break, else the reader's progress | `resume-outside-book` |
| A reread outside its section | The prompt, answer hidden; the reread is removed | `reread-outside-section` |
| Another edition (`book_sha256`) | The reader's progress; position and checkpoint dropped as the file was read | `restore dropped reason=other-edition` |
| No memory for the 78 KB block, or no section layout | The reader's progress; the file is not touched | `OOM: restore` / `restore skipped` |

**Every transition saves first.** Each transition of the checkpoint flow and of a reread is written before the screen changes; whether the answer is revealed is not saved (plan section 7), so a restart always shows it hidden.

| Transition | Written by | Before the screen changes | Proven by |
| --- | --- | --- | --- |
| Start (a Fire step) | `CheckpointActivity::start()`: the checkpoint at prompt 0, `resume`, and `position` before the break | Yes: before the first prompt is drawn | `cp_fire_page`, `cp_skip_grades`, `cp_kill_in_prompt` |
| Reveal | Nothing (`Save::No`) | Not saved by design | `cp_kill_in_prompt` (kill with the answer shown: restored hidden) |
| Correct, Incorrect (first grade) | `CheckpointActivity::apply()` → `persist()` | Yes | `cp_kill_in_prompt`, `cp_skip_grades` |
| Correct, Incorrect (grade already stored) | Correct moves on (`Save::Now`, prompt index only); Incorrect writes nothing | Yes / nothing to write | `cp_second_pass_grades`, `cp_kill_in_reread` |
| Next prompt (Correct, Continue, Skip question) | `persist()`: `prompt_index` | Yes | `cp_skip_grades`, `cp_kill_in_prompt` |
| Finish, Skip checkpoint | `persist()`: `position` = `resume`, `active_checkpoint` removed | Yes, before the activity closes | `cp_kill_in_prompt`, `cp_skip_grades` |
| Reread start (RSVP or page view) | `persist()`: `reread` at the section's first word | Yes, before the screens close | `cp_kill_in_reread`, `cp_reread_rsvp_end` |
| Reread place, RSVP pause | `RsvpActivity::enterPause()` → `savePosition()` → `rereadMoved()` | Yes since C5 (before: after the pause screen) | `cp_kill_in_reread` pass 2 |
| Reread place, RSVP Exit, Home, sleep, Page view, Menu | `savePosition()` on the way out | Yes | `cp_sleep_restore` run 3 |
| Reread place, page view page turn | `pageShowing()` → `rereadMoved()` | Yes since C5 (before: after the page was drawn) | `cp_kill_in_reread` pass 1, `cp_reread_page_early` |
| Back to the question (bar, list, pause screen), reread reached its end, a page out of the section | `endReread()` / `rereadMoved()` | Yes, before the prompt opens | `cp_kill_in_reread`, `cp_sleep_restore`, `cp_reread_rsvp_end` |
| Restore that dropped something | `restoreActive()` → `writeBookState()` | Yes, before any screen | `cp_restore_damaged` |

**Saving: atomic, proven.** Every write goes to `<file>.tmp`, which is closed, then the old file is removed and the `.tmp` renamed (SdFat's rename does not replace). A power cut before the removal leaves the old file; after it, only the new file, complete, as `.tmp`. Before task C5 that second case read as "missing", so the next save started from a fresh state and every grade was lost; `loadBookState()` now reads a valid `.tmp` when the file is missing, and the next write renames it into place. A `.tmp` that does not decode (a first write cut short) is ignored. The simulator's kill can only land between log lines, not inside one write, so this is proven in `test/book_state_file` (`BookStateFileTest`): an SD card in memory whose power is cut after each of the write's changes in turn (open, every 128-byte write, removal, rename), with and without an old `.tmp`; after every cut the file reads back as the state before the write or the state after it, and the next write completes. The test failed (cut after the removal: "missing") before the fix. The scenarios kill the simulator right after a screen appears and check the file each time: `cp_kill_in_prompt`, `cp_kill_in_reread`, `cp_skip_grades` (four kills), each with no `.tmp` or `.bad` left.

**A later pass** (plan section 7, "Trigger rules" fifth bullet). Reading through the section again fires its checkpoint again from its first prompt; each prompt shows its stored first grade as "Recorded earlier"; a different answer changes nothing in the file. This was already so (C2, C3, C3a); `cp_second_pass_grades` proves it after a restart, in page view by touch and in RSVP by keys, with the file checked after each run.

**Memory.**

| Moment | Allocated | Notes |
| --- | --- | --- |
| A book without a state file opens | Nothing new | `bookStateExists()` is two `exists()` calls; `openedInRsvp()` keeps its early return |
| A book with a state file opens, nothing in progress | 78,056 bytes and the file's size (a few hundred bytes), freed before `loadBook()` returns | Before C5 `openedInRsvp()` allocated the same `BookState` (78,020) for the same moment |
| A book opens with a checkpoint in progress | The same block, kept as the checkpoint in progress (`kept_bytes` in the `restore open` line: the block, the run and its outcome), plus, only for a chapter whose word index is missing, a build's transient memory ("Word index memory" below) | No second `BookState`: the plan, the cleaned write and the flow use the block |
| While a restored prompt shows | `CheckpointBook` 72,544 + layout 8,120 + session 128 + the kept block and run + CheckpointActivity 4,056 | The same as a prompt fired by reading, minus the reader's chapter layout (no page was drawn yet). A grade's write adds `editBookState()`'s 78 KB and the file for its duration, as before |

The session grew by 16 bytes (two flags and a pointer). On the Picco the 78 KB blocks come from PSRAM.

**Log lines** (tag `CPT` unless noted): `[RSVP] book state <source> mode= ch= word=` (the file as read); `restore view=<reread|prompt|position|book-start> checkpoint=<saved id> prompt=<saved index + 1>/<prompts, or - when dropped> place=<ch:word:mode> resume=<ch:word:mode|none> notes=<none|names joined by +> progress_bytes=<b>` (what `planRestore()` returned); `restore dropped checkpoint=<id> prompt=<n> reason=<notes> saved=<0|1>` and `restore dropped reason=other-edition grades=<n> saved=<0|1>` (the cleaned file); `restore open show=<reading|prompt|reread> speed_read=<0|1> jump=<0|1> ch=<c> offset=<o> kept_bytes=<b>` (what the reader does); `reread page ch=<c> under rsvp: not the reread place`; `[RSVP] state <path> recovered from <path>.tmp` (`BookStateFile`).

**Limits.** A reread in page view started from a page whose next page is not laid out yet treats the page as running to the chapter's end (it only keeps the reread on, never ends it). The bar follows the reader's orientation and metrics ("Rereading" above). After a restored prompt the reader's first page is drawn only when the checkpoint ends or a reread starts.

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

Each `loadBookState()` or `editBookState()` call allocates one `checkpoint::BookState` (78 KB, nearly all of it the grade table) and a buffer the size of the file, and frees both before returning; the encoder streams to the file through a 128-byte buffer. On the Picco a block this size comes from PSRAM. The restore at open (task C5) reads into the checkpoint block it keeps and writes it with `writeBookState()`, so it allocates no second state; the edits during a checkpoint still allocate their own for the duration of the write, as before.

## Word index memory

A build streams the chapter with bounded RAM, all of it freed when `open()` returns: the builder (4.1 KB, heap, `makeUniqueNoThrow`), expat's own allocations (parser, 1 KB input and character buffers, tag stack and attribute pool: measured 8.7 to 9.7 KB peak in `lib/WordIndex/README.md`), the EPUB's inflate stream (`ZipFile::readFileToStream`: a 32 KB ring buffer and two 1 KB chunk buffers), and about 1 KB of stack. An open index keeps the 1.9 KB reader (a member of its owner) and one file handle. The rebuild check reads the chapter's zip entry size and CRC-32 from the central directory: no read of the chapter.

Encrypted (LCP) chapters are indexed from their decrypted bytes, whose CRC-32 is not the zip entry's: their index is rebuilt on every entry, as for TXT books.
