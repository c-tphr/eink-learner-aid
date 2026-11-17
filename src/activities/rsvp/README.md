# src/activities/rsvp

**RsvpActivity** (`docs/PLAN.md` sections 4 and 6): RSVP playback and its pause screen. It drives
`rsvp::Scheduler` (`lib/RsvpCore`) over the chapter's word index (`lib/WordIndex`, through
`src/reading/ChapterWordIndex`) and draws through `DisplayProfile` (`src/display/`). Controls follow
`docs/spikes/S4-control-map.md` through the binding table in `src/controls/`.

Filled by task R4a (#21); R4b (#22) adds the WPM and words-per-update steppers, saving them, and the
saved RSVP position; R5 (#23) the non-prose hand-off to page view.

| File | What it holds |
| --- | --- |
| `RsvpLaunch.h` | The reader's entry point: `rsvpentry::create()` and `HandOff` (how RSVP returns). Without the flag `create()` returns nothing |
| `RsvpActivity.h/.cpp` | The activity: playback, pause screen, chapter changes, input |
| `WordBand.h/.cpp` | Where a frame is drawn in the word band (pure arithmetic, host-tested) |

Everything here is compiled only with the RSVP feature flag (`docs/ARCHITECTURE-RSVP.md`).

## Entering and leaving

The reader pushes the activity over itself (`EpubReaderActivity::openSpeedRead()`), from the first
row of its menu ("Speed read", also the first row of the toolbar's More panel) or from a 0.7 s hold
of a side key ("Page turn long press: Speed read", the default in RSVP builds). The reader keeps its
book and layout. RSVP opens on the pause screen at the first word whose end is past the page's start
offset, with Play highlighted; it never starts by itself.

Leaving fills `HandOff` and finishes; the reader's result handler acts on it:

| Pause-screen item | Reader does |
| --- | --- |
| Page view (or a left-edge Back swipe) | Jumps to the page holding the next word (`applyProgressChange` with its visible offset); that render saves the reader's progress |
| Menu | The same, then opens its menu (toolbar or list, per the setting) |
| Exit | Saves that page as its progress and goes Home, like the menu's "Go Home" |

The Home gesture on the pause screen goes Home as everywhere else (the reader's progress is then the
page RSVP was entered from; R4b saves the RSVP position).

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
and words-per-update values (R4b adds their steppers in these rows, and the two save buttons below),
a one-line note, and the key hint. Notes: "An image, table or code block was skipped" after a
non-prose stop (Play continues after the item), "End of the book", "This chapter cannot be speed
read" (no index could be built).

**Chapters.** At a chapter end playback continues into the next spine item that has prose (its index
is built if needed; "Preparing..." is shown when the chapter's XHTML is 32 KB or more). An image,
table or code block before that chapter's first prose word pauses with the note. Past the last
chapter, playback pauses with "End of the book".

**Sentence steps** (`src/reading/RsvpPosition.h`): back goes to the start of the current sentence, or
of the previous one when already at a start; forward to the next sentence. Held (key path), they
repeat every 0.4 s. They stay within the chapter.

## Log lines

DBG lines, tag `RSVP` unless noted; stable for the scenarios and the device benchmark (D3a). Numbers
are decimal, times in ms.

| Line | When |
| --- | --- |
| `enter ch=<spine> page_offset=<o> word=<w> chapter_words=<n> activity_bytes=<b>` | RSVP opened at word `w` |
| `play ch=<spine> word=<w> wpm=<t> words_per_update=<k>` | Play |
| `frame ch=<spine> first=<i> last=<j> words=<k> hold=<ideal ms> ms=<update ms> text="<text>"` | After every frame shown; `ms` is the backend's measured update time |
| `clear ch=<spine> ms=<ms>` | A clearing refresh during playback (clearing policy) |
| `chapter ch=<spine> word=<w> chapter_words=<n>` | Playback moved into the next chapter |
| `pause reason=<pause\|non-prose\|end-of-book\|index-rebuilt> ch=<spine> word=<next word> target=<wpm> delivered=<wpm.d> words=<n> active_ms=<ms> updates=<n> clears=<n> mean_update_ms=<ms> max_update_ms=<ms> audit=<ok\|violated> drawn=<n> segments=<n> violations=<n> gaps=<n> repeats=<n> skipped=<n> declared=<n>` | Playback ended; meter (`DeliveredWpmMeter::stats()`) and word-order audit for the session. Followed by `[DSP] stats ...` |
| `pause screen focus=<Item> refresh=<clear\|update> status="<first line>"` | The pause screen was refreshed |
| `sentence <back\|forward> ch=<spine> from=<w> to=<w>` | A sentence step |
| `leave to=<page-view\|menu\|exit> ch=<spine> word=<w> offset=<visible offset>` | Leaving RSVP |
| `Index built spine=<s> words=<n> bytes=<n> source=<n> ms=<ms>`, `Index open spine=<s> ...` | `src/reading/ChapterWordIndex` |
| `[CTL] Pause armed` | The pause screen accepts input (300 ms after it is shown and every key and finger up) |
| `[CTL] Pause focus=<Item>`, `[CTL] Pause select=<Item>` | Highlight moved, item activated (S4 section 9) |
| `[CTL] Playing pause=<key\|touch\|gesture\|home>` | Input paused playback |

## Memory

The activity is one heap object made when RSVP opens (`activity_bytes` in the enter line, about
5.4 KB in the simulator: the scheduler with its 300-byte frame, the 1.9 KB index reader, the 2.3 KB sentence window,
the band geometry). Playback allocates nothing per word: frames, text widths (a 257-byte stack copy)
and the band layout use fixed storage. An index build is transient (`src/reading/README.md`).
