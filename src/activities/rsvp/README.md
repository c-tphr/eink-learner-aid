# src/activities/rsvp

**RsvpActivity** (`docs/PLAN.md` sections 4 and 6): RSVP playback and its pause screen. It drives
`rsvp::Scheduler` (`lib/RsvpCore`) over the chapter's word index (`lib/WordIndex`, through
`src/reading/ChapterWordIndex`) and draws through `DisplayProfile` (`src/display/`). Controls follow
`docs/spikes/S4-control-map.md` through the binding table in `src/controls/`.

Filled by task R4a (#21); R4b (#22) added the WPM and words-per-update steppers, saving them, and the
saved RSVP position; R5 (#23) the non-prose hand-over to page view and the pause on a display timeout.

| File | What it holds |
| --- | --- |
| `RsvpLaunch.h` | The reader's entry point: `rsvpentry::create()` with an `Entry` (the page on screen, or the item to continue after), `HandOff` (how RSVP returns, how it saves the reader's progress, and `ItemStop`, the session kept across a non-prose item), `openedInRsvp()`, `createItemBanner()`. Without the flag `create()` and `createItemBanner()` return nothing and `openedInRsvp()` false |
| `RsvpActivity.h/.cpp` | The activity: playback, pause screen, chapter changes, input, the hand-over at a non-prose item |
| `ItemBanner.h/.cpp` | The banner over page view at a non-prose item: Continue and Stay (S4 rows 15-16) |
| `WordBand.h/.cpp` | Where a frame is drawn in the word band (pure arithmetic, host-tested) |

Everything here is compiled only with the RSVP feature flag (`docs/ARCHITECTURE-RSVP.md`).

## Entering and leaving

The reader pushes the activity over itself (`EpubReaderActivity::openSpeedRead()`), from the first
row of its menu ("Speed read", also the first row of the toolbar's More panel) or from a 0.7 s hold
of a side key ("Page turn long press: Speed read", the default in RSVP builds). The reader keeps its
book and layout. RSVP opens on the pause screen with Play highlighted, at the word chosen by the
rule below; it never starts by itself.

When a book opens and its state file (`src/reading/BookStateFile.h`) says it was left in RSVP mode,
the reader opens RSVP by itself once its first page is shown (`rsvpentry::openedInRsvp()`,
`resumeSpeedRead` in the reader).

### Where RSVP opens

`reading::chooseEntry()` (`src/reading/RsvpPosition.h`, host-tested), with the state file's position
(the "saved word"):

1. **The book opened in RSVP mode** (the automatic entry above): the saved chapter and word, at most
   the chapter's end, whatever page the reader shows.
2. **Otherwise**, the saved word when it lies **on the page the reader shows**: the same chapter, not
   before the page's entry word, and starting before the next page's start offset (the reader passes
   it; on a chapter's last page there is no upper bound).
3. **Otherwise** the page's entry word: the first word whose end is past the page's start offset.

Why "on the page" and not "on or after the page's start": after Page view the reader shows the page
holding the saved word, so entering again from that page continues at that exact word (rule 2). Once
the reader has paged on, the saved word lies before the page and the page wins. If the reader has
paged back, the saved word lies after the page; "on or after" would then jump ahead of what the
reader is looking at, so the bound applies there too. Rule 1 does not look at the page, because after
a power loss the reader's own progress can be older than the state file (it is not written at every
pause; see below).

### Routes out, and what each saves

The position written is always "the next word": the word Play would show first.

| Route | State file (`position`) | Reader's own progress (`progress.bin`) |
| --- | --- | --- |
| Pause screen: Page view (or a left-edge Back swipe) | chapter, word, `page` | The reader jumps to the page holding the word; its render saves that page |
| Pause screen: Menu | chapter, word, `page` | The same, then the reader opens its menu |
| Pause screen: Exit | chapter, word, `rsvp` | The reader saves the page holding the word (`HandOff::saveReaderProgress`) and goes Home |
| Home gesture (bottom-edge swipe) on the pause screen | chapter, word, `rsvp` (in `onExit()`) | Saved by RSVP through `HandOff::saveReaderProgress` while the reader is still under it |
| Home gesture during playback | Nothing: it pauses (S4 section 6), and the pause saves | Unchanged |
| Sleep (power hold, auto-sleep cannot fire while playing) | chapter, word, `rsvp` (`prepareForSleep()`) | Saved the same way as for the Home gesture |
| Power-off | The firmware has no power-off hook besides sleep: the last pause or chapter change | The last route above, or the page RSVP was entered from |
| A pause (any reason: key, touch, end of book, display timeout) | chapter, word, `rsvp` | Unchanged; rule 1 restores from the state file |
| An image, table or code block ("Non-prose items" below) | chapter, the first prose word after the item, `rsvp` | The reader shows the item's page; its render saves that page |
| Banner: Continue | Unchanged (that word, `rsvp`) | Unchanged |
| Banner: Stay, Back, or a page-turn swipe | chapter, the same word, `page` | The item's page, or the page the swipe turns to |
| Home gesture, sleep or power loss while the banner shows | Unchanged (`rsvp`): reopening the book opens RSVP after the item | The item's page |
| A chapter change during playback | chapter, first word, `rsvp` | Unchanged |
| A checkpoint (task C2; "Checkpoints" below) | When it shows: the frame on screen, `rsvp`, with the checkpoint in progress (written by CheckpointActivity); when it ends: the first word after the break, `rsvp` | Unchanged; Home or sleep while it shows: the page holding the frame on screen |

How often during playback: never per word (SD wear, and a write would delay the next frame on the
render task). A write happens when playback pauses, which is every way playback ends, and once per
chapter change, so a power loss mid-chapter costs at most the part of the chapter read since the
last pause. A write that would repeat the place last written by this RSVP session is skipped.

## Pace settings

The WPM steppers move by 20 within 60 to 1000, the words-per-update steppers by 1 within 1 to 4
(`src/reading/RsvpValues.h`); a press at a limit changes nothing. Held on the key path, they repeat
every 0.4 s. A change takes effect at the next Play. Words per update above 1 group words through
`FrameBuilder` (a group never crosses a sentence or paragraph end).

- **Save as default** writes `rsvpWpmTens` (WPM / 10) and `rsvpWordsPerUpdate` to CrossPoint's
  settings file (`/.crosspoint/settings.json`) through upstream's settings list. They exist only in
  RSVP builds and are not shown in the Settings screens or the web settings page.
- **Save for this book** writes both values as `overrides` in the book's state file.
- The status line then starts with "Saved as default", "Saved for this book" or "Could not save"
  instead of "Paused", until the next stepper press, sentence step or Play.

When RSVP opens, each value is the book's override if the state file has one, else the global
default (which is 240 WPM and 1 word until a default is saved).

The state file is read when RSVP opens and when a book opens; an invalid one (not JSON, wrong
members, a newer version) or an unreadable one gives the defaults, and the next save moves it aside
to `<file>.bad` and writes a fresh one. A prepared book's file carries the checkpoint file's `book_sha256`
(`src/reading/README.md`, "The state file's `book_sha256`").

## Non-prose items

Plan section 1: images, tables (`<table>`) and code blocks (`<pre>`) pause RSVP and show in page view;
footnote reference marks are passed over. Controls: S4 rows 15 and 16 and its wireframe.

**The stop.** The word source flags the last prose word before an item (`TOKEN_NON_PROSE_FOLLOWS`),
and the scheduler stops once that word has had its full slot. RSVP then finds the item
(`reading::itemBefore()`: the first IMAGE, TABLE or CODE marker outside the notes that starts after the
last word shown and not after the next prose word), logs the `pause` line (the session's meter and
audit so far), saves the position (the first prose word after the item, mode `rsvp`), fills
`HandOff::item` and finishes with `Then::ShowItem`. An item before a chapter's first prose word, met
when playback moves into that chapter, is handed over the same way. On a trial display profile the
screen is cleared with a clearing refresh first (plan section 6, "Safety limits"); the stock profile
goes straight to the page.

**Which page.** The reader jumps to the item's start offset (`Marker::offsetStart`) with its own
offset jump. Once that page is drawn, it checks it against
`Section::getPageForVisibleTextOffset(offset, /*preferFirstAtOffset=*/true)`: an image takes no
offset, so an image-only page starts at the same offset as the page after it, and the first of them is
the image. If that is another page, the reader shows it first. A table or code block that spans pages
is shown at its first page. The reader's render saves that page as its progress.

**Runs of items.** The word source treats items with no prose between them (an image directly followed
by a table) as one gap, so one banner covers the run: it names and shows the run's first item, and
Continue goes to the first prose word after the whole run. Continue therefore always moves past at
least the item shown and never stops at the same item twice. The rest of a run, or of a table split
over pages, is reached by reading on in page view (Stay, or a page-turn swipe).

**Captions.** S1 section 5: an image's `figcaption` is prose and is read after the image (the fixture's
"Figure 1. ..."); a table's `caption` is inside the `<table>`, so it belongs to the item and is shown,
not read. Alt text is never a word.

**The banner** (`ItemBanner`) is a small activity the reader pushes once the item's page is on screen.
It draws only the bottom strip (y 632 to 800: a rule, "Image. Speed reading stopped here." or "Table."
or "Code block.", the two buttons, the key hint) over the page left in the frame buffer, with a fast
refresh; the item can be partly under it, as in S4's wireframe. If a render of the banner was not asked
for by the banner itself (another screen, such as the light panel, covered it and has closed, so the
frame buffer no longer holds the page), it finishes with `Then::ShowItem` and the reader draws the page
and opens the banner again.

| Input while the banner shows | What it does |
| --- | --- |
| Tap Continue (16,688,264,72), or hold a side key while Continue is highlighted (the default), or Confirm | Continue: RSVP opens again at the first prose word after the item, on the pause screen with Play highlighted |
| Tap Stay (304,688,160,72), or Down then hold, or a left-edge swipe (Back) | Stay: the banner goes, page view stays on the item's page, the state file's mode becomes `page` |
| A page-turn swipe (left or right, mid-screen) | Stay, and the page turns as it would have |
| Short Up or Down press | Moves the highlight (no wrap); does not turn the page |
| A tap anywhere else (the page, the centre tap for the toolbar, tap-mode page zones, links) | Nothing: the banner is modal (S4 section 6). The toolbar and the reader menu are reachable after Stay |
| Bottom-edge swipe (Home) | Goes Home as everywhere; the state file keeps the word after the item in RSVP mode, so reopening the book opens RSVP there |
| Top-edge swipe (light panel) | Opens the light panel over the banner; when it closes, the page is drawn again and the banner returns |
| Power button | Unchanged: a hold sleeps (the reopened book continues after the item), a short click does what its setting says |

**Continue keeps the session.** The banner sets `Then::Continue`; the reader calls `openSpeedRead()`
with the hand-off as `Entry::after`. The new RSVP activity opens at `ItemStop::nextWord` with the pace
that was in use (saved or not), without reading the state file, and carries the session on
(`rsvp::Scheduler::continueSession()`): the delivered-WPM meter and the word-order audit as they were
at the stop. Time on the banner is outside active playback, so it never counts in delivered WPM, and
the audit's open segment continues: after a stop it allows the one forward skip that `play()` at the
stop would have allowed, which covers the item's own word indexes (a table's or code block's words,
and any note words right after it; counted in `skipped=`). An image has no words, so nothing is
skipped. RSVP opens on its pause screen and waits for Play, as everywhere (plan section 6,
"Presentation"). S4 row 15 says Continue "starts playing"; the plan wins where they differ.

**Entering RSVP on an item's page** (the hold, or "Speed read") starts after the item: the page's
entry word is inside it or after it, and the word source moves to the first prose word after it
(S4 section 13). The saved word after Stay is used when it lies on the page shown ("Where RSVP
opens").

## Tasks and timing

All playback runs on the render task, inside `render()`: `Scheduler::step()`, the painter and the
display calls (`DisplayProfile.h`: the backend blocks until a refresh ends). One `render()` does at
most one step, so at most one display call. The main task never blocks on it while playing:
`loop()` reads input, calls `Scheduler::requestPause()` (an atomic flag) and requests the next
`render()` when the step's wake time has come (it polls every main-loop pass, about 10 ms). While
paused, `loop()` takes the RenderLock for its edits (focus, sentence steps), which waits for at most
one display call. A screen pushed over RSVP (the light panel) stops `render()` and `loop()` calls;
if `render()` runs after the main loop has not called `loop()` for 1.5 s, playback pauses.

`preventAutoSleep()` is true while playing. During playback any key press or touch-down pauses and
does nothing else: the touch contact is suppressed (its lift taps nothing; no edge swipe completes),
and the Home gesture pauses (`handleHomeGesture()`). A short power click set to "Force refresh"
runs a clearing refresh of the pause screen (`handleForcedRefresh()`).

## Screens

Every refresh goes through the display backend: frames by `update()` (in `Scheduler::step()`), the
pause screen by `clear()` when playback ends or RSVP opens and by `update()` for highlight moves and
sentence steps. Play first draws the playback screen (two ticks on the focus column, an empty band)
with `clear()`, then plays; the guides are not redrawn per frame, only the band (`paintBand()`
fills the band white and draws the frame; the panel still gets the whole frame buffer).

**Word band** (`WordBand.h`): portrait, band y 352-448, focus column at 2/5 of the width (x 192).
Font rule: the largest built-in reader font, NotoSans 18 bold; a frame that does not fit with its
focus letter on the column tries NotoSans 16, 14 and 12; failing that it is shifted into the band in
NotoSans 12 (the focus letter leaves the column), and a word wider than the band is split into lines
at code-point boundaries. Nothing is dropped. Grouping (`FrameBuilder`'s `FitsFn`) uses the first
rule with the renderer's real text widths. Black and white only.

**Pause screen** (S4 section 2, zones in `src/controls/ControlMap.h`): status (target WPM, delivered
WPM of the session, rounded), position (chapter title, chapter and book percent), Exit / Menu / Page
view, the words Play starts with, back one sentence, Play (default), forward one sentence, the WPM
and words-per-update values between their minus and plus steppers (drawn shapes, so the screen text of
a row is its value), Save as default and Save for this book, a one-line note, and the key hint. Notes: "End of the book", "This chapter cannot be speed
read" (no index could be built), "Paused: the display did not respond" (below).

**Chapters.** At a chapter end playback continues into the next spine item that has prose (its index
is built if needed; "Preparing..." is shown when the chapter's XHTML is 32 KB or more). Footnote and
endnote bodies are passed over (`lib/WordIndex`, NOTE regions), so a document made only of notes (the
fixture's endnotes) is skipped as a chapter without prose. An image, table or code block before that
chapter's first prose word is handed over to page view like any other ("Non-prose items"). Past the
last chapter, playback pauses with "End of the book".

**Display timeout.** The display backend counts a word update of 5 s or more as a timeout
(`stats().updates.timeouts`; `src/display/README.md`, "Timeouts"). After a frame whose update timed
out, playback pauses before the next word (`reading::UpdateTimeoutWatch`, armed at each Play), with
the note "Paused: the display did not respond" and `pause reason=display-timeout`. A stuck panel then
costs one long update (up to the SDK's 30 s BUSY limit), not one per word; Play tries again. The
simulator's modelled backend never classes a call as a timeout, so this is proven in
`test/rsvp_activity/DisplayTimeoutTest.cpp` through the device backend's timing logic.

**Checkpoints** (task C2; `src/reading/README.md`, "ReadingSession"). In a prepared book the word
source carries the chapter's section ends (`ReadingSession::sectionEnds`), so a frame never groups past
the last word of a section, and playback stops once the last word of a section with a checkpoint has
had its slot (`STOP_CHECKPOINT`): that word stays on screen and nothing after the break is drawn. RSVP
logs the `pause reason=checkpoint` line (the session's meter and audit so far) and `leave to=checkpoint`,
asks the session for the Fire step (`rsvpStop()`), and the main task pushes CheckpointActivity over RSVP
(RSVP stays alive under it, in phase Checkpoint). When it ends, RSVP takes the place
`ReadingSession::endCheckpoint()` gives and shows its pause screen there with Play highlighted and the
status line "Checkpoint done" (S4 row 33); nothing plays until Play:

- the section ended mid-chapter: the source already stands at the first word after the break, so the
  stop is resolved and the audit's segment carries on; Play continues at that word;
- the section ended its chapter: the next chapter with prose opens at word 0, a new audit segment as for
  any chapter change; the end of the book: "End of the book";
- a reread request (task C4, not done yet) resumes as Finished; a checkpoint that could not start
  resumes after the break with the status "Paused".

The meter is the same session's: time on the checkpoint screens is not playback time, because the
scheduler closed its active span at the stop and opens a new one at Play. While the checkpoint shows,
Home and sleep keep what CheckpointActivity wrote when it started (the position before the break, the
frame on screen, mode `rsvp`, and the checkpoint in progress) instead of saving the next word, and save
the reader's progress at the page holding that frame. A stop that the session does not confirm (a book
without checkpoint data never flags one) pauses with `reason=checkpoint` as before.

**Sentence steps** (`src/reading/RsvpPosition.h`): back goes to the start of the current sentence, or
of the previous one when already at a start; forward to the next sentence. Held (key path), they
repeat every 0.4 s. They stay within the chapter.

## Log lines

DBG lines, tag `RSVP` unless noted; stable for the scenarios and the device benchmark (D3a). Numbers
are decimal, times in ms.

| Line | When |
| --- | --- |
| `state <missing\|read\|invalid\|unreadable\|no-memory> book_wpm=<w> book_words=<k> default_wpm=<w> default_words=<k> wpm=<w> words_per_update=<k> position=<none\|rsvp\|page> ch=<c> word=<w>` | RSVP read the state file: the book's overrides (0: none), the global default, the values used, the saved position |
| `enter ch=<spine> page_offset=<o> word=<w> chapter_words=<n> activity_bytes=<b> from=<page\|saved\|resumed\|continue>` | RSVP opened at word `w`, by which rule ("Where RSVP opens"; `continue`: Continue on the banner) |
| `book state <read\|invalid\|unreadable\|no-memory> mode=<none\|rsvp\|page> ch=<c> word=<w>` | A book with a state file opened (`openedInRsvp()`) |
| `play ch=<spine> word=<w> wpm=<t> words_per_update=<k>` | Play |
| `frame ch=<spine> first=<i> last=<j> words=<k> hold=<ideal ms> ms=<update ms> text="<text>"` | After every frame shown; `ms` is the backend's measured update time |
| `clear ch=<spine> ms=<ms>` | A clearing refresh during playback (clearing policy) |
| `chapter ch=<spine> word=<w> chapter_words=<n>` | Playback moved into the next chapter |
| `pause reason=<pause\|non-prose\|end-of-book\|index-rebuilt\|display-timeout\|checkpoint> ch=<spine> word=<next word> target=<wpm> delivered=<wpm.d> words=<n> active_ms=<ms> updates=<n> clears=<n> mean_update_ms=<ms> max_update_ms=<ms> audit=<ok\|violated> drawn=<n> segments=<n> violations=<n> gaps=<n> repeats=<n> skipped=<n> declared=<n>` | Playback ended; meter (`DeliveredWpmMeter::stats()`) and word-order audit for the session. Followed by `[DSP] stats ...` |
| `pause screen focus=<Item> refresh=<clear\|update> status="<first line>"` | The pause screen was refreshed |
| `sentence <back\|forward> ch=<spine> from=<w> to=<w>` | A sentence step |
| `leave to=<page-view\|menu\|exit> ch=<spine> word=<w> offset=<visible offset>` | Leaving RSVP |
| `leave to=checkpoint ch=<spine> first=<w> last=<w>` | Stopped at a checkpoint: the frame on screen (its last word ends the section); CheckpointActivity follows |
| `leave to=item kind=<image\|table\|code> ch=<spine> word=<next word> offset=<item start offset>` | Handing over to page view at a non-prose item |
| `[ERR] [RSVP] display timeout ch=<spine> word=<next word>: pausing` | A word update timed out; the pause follows |
| `banner kind=<image\|table\|code> ch=<spine> word=<next word> item_offset=<o> activity_bytes=<b> hand_off_bytes=<b>` | The banner opened |
| `banner screen focus=<Continue\|Stay>` | The banner was drawn |
| `banner page redraw` | A screen that covered the banner closed: the reader draws the page and opens the banner again |
| `values wpm=<w> words_per_update=<k> changed=<0\|1>` | A stepper press (`changed=0` at a limit) |
| `saved default wpm=<w> words_per_update=<k> ok=<0\|1>`, `saved book ...` | Save as default, Save for this book |
| `position saved reason=<pause\|non-prose\|end-of-book\|index-rebuilt\|display-timeout\|checkpoint\|chapter\|page-view\|menu\|exit\|home\|sleep\|stay> ch=<spine> word=<w> mode=<rsvp\|page> ok=<0\|1>` | The state file's position was written (`stay`: by the banner) |
| `state <path> set aside as <path>.bad` | An invalid or unreadable state file was moved aside before a save |
| `Index built spine=<s> words=<n> bytes=<n> source=<n> ms=<ms>`, `Index open spine=<s> ...` | `src/reading/ChapterWordIndex` |
| `[CTL] Pause armed` | The pause screen accepts input (300 ms after it is shown and every key and finger up) |
| `[CTL] Pause focus=<Item>`, `[CTL] Pause select=<Item>` | Highlight moved, item activated (S4 section 9) |
| `[CTL] Playing pause=<key\|touch\|gesture\|home>` | Input paused playback |
| `[CTL] Banner armed`, `[CTL] Banner focus=<Item>`, `[CTL] Banner select=<Continue\|Stay>`, `[CTL] Banner swipe=<next\|previous>` | The banner's input (a swipe is followed by Stay's `position saved` line) |

## Memory

The activity is one heap object made when RSVP opens (`activity_bytes` in the enter line, 5,464
bytes in the simulator: the scheduler with its 300-byte frame, the 1.9 KB index reader, the 2.3 KB
sentence window, the band geometry; R4b's entry parameters, values, status and last saved place add
40 bytes; R5's `Entry::after`, timeout watch and two flags 24 bytes). The `HandOff` the reader makes
at each entry (one `make_shared`, as before) grows by `ItemStop`, the meter and audit kept across an
item: 176 bytes in the simulator (`hand_off_bytes` in the banner line). The banner is one more heap
object per item shown, 248 bytes in the simulator (`activity_bytes` in the banner line), freed when it
closes; Stay writes the state file as below. Playback allocates nothing per word: frames, text widths (a 257-byte stack copy) and the
band layout use fixed storage. An index build is transient (`src/reading/README.md`).

Reading or writing the state file allocates one `checkpoint::BookState` (78 KB; on the Picco the
allocator places a block this size in PSRAM) and a buffer the size of the file (about 100 to 200
bytes for a book without checkpoint data), both freed before the call returns
(`src/reading/BookStateFile.h`). That happens when a book opens, when RSVP opens, at each pause,
chapter change, save button and exit; never per word. If the allocation fails, RSVP opens with the
global defaults at the page's first word, and the save reports "Could not save" (buttons) or logs
`ok=0` (position) without touching the file.
