# RsvpCore

Portable RSVP logic for the reader: which words each display update shows, where the focus
letter is, how long each update should ideally be held, and the scheduler that plays them against
a clock and a display, with the delivered-WPM meter. No Arduino, ESP-IDF, font or display code;
host-tested in `test/rsvp_core` with a fake clock and a fake display (plan sections 4 and 6).

| File | What it holds |
| --- | --- |
| `WordSource.h` | `Token`, `TokenFlag`, the `WordSource` pull interface, `MAX_WORD_BYTES` |
| `WordClassifier.h/.cpp` | `classifyWord`, `WordClass`, `Pause`, the abbreviation table |
| `FocusLetter.h/.cpp` | `focusLetter`, `FOCUS_TABLE` |
| `Pacing.h/.cpp` | `PACING` (the weight table and normalisation constant), `rawWeight`, `Pacer` |
| `FrameBuilder.h/.cpp` | `FrameBuilder`, `Frame`, `StopReason`, the width callback `FitsFn` |
| `SentenceStep.h/.cpp` | `sentenceStart`, `previousSentenceStart`, `nextSentenceStart` |
| `Clock.h` | `Clock` (injected millisecond time), wrap-safe `timeBefore`, `timeLater`, `timeEarlier` |
| `DisplaySink.h` | `DisplaySink`: show a frame, run a clearing refresh (both blocking) |
| `Scheduler.h/.cpp` | `Scheduler`, `StepResult`, `StepEvent`, `PlaybackState`, `DEFAULT_MIN_DWELL_MS` |
| `ClearingPolicy.h/.cpp` | `ClearingPolicy`, `ClearingLimits` (60/120 by default) |
| `DeliveredWpmMeter.h/.cpp` | `DeliveredWpmMeter`, `MeterStats` |
| `WordOrderAudit.h/.cpp` | `WordOrderAudit`: every drawn word index once and in order |
| `RsvpText.h/.cpp` | UTF-8 decoding and character classes (internal) |

Everything is in namespace `rsvp`. English only (plan section 1).

## Data flow

```
WordSource (WordIndex adapter) --peek/advance--> FrameBuilder --Frame--> Scheduler --> DisplaySink
                                                  |  classifyWord,          |  Clock, ClearingPolicy,
                                                  |  focusLetter, Pacer     |  DeliveredWpmMeter, WordOrderAudit
                                                  '- FitsFn (UI measures)   '- StepResult --> activity loop
```

## Word input

A `Token` is one word as the source splits it: UTF-8 text with no whitespace and its punctuation
attached (`"The`, `weather."`, `Dr.`, `3.5`, `sea-worn`, `forgotten—`), its 0-based word index in
the chapter, and `TokenFlag` bits describing what follows the word:

| Flag | Meaning |
| --- | --- |
| `TOKEN_PARAGRAPH_END` | Last word of a paragraph, heading or other text block |
| `TOKEN_SECTION_END` | A section end anchor follows the word |
| `TOKEN_CHECKPOINT` | That section end carries a checkpoint (implies `TOKEN_SECTION_END`) |
| `TOKEN_NON_PROSE_FOLLOWS` | An image, table or code block follows the word |
| `TOKEN_CHAPTER_END` | Last word of the chapter (optional; see below) |

`WordSource` is pull style with one token of lookahead:

- `bool peek(Token&)` returns the next unconsumed token without consuming it; repeated calls return
  the same token. It returns false at the end of the chapter.
- `void advance()` consumes that token.
- A token's `text` is valid only until the next call on the source. RsvpCore copies what it keeps.

What the source must do:

- Split words, remove footnote reference marks, and set the flags. RsvpCore never parses EPUB.
- Keep words to at most `MAX_WORD_BYTES` (255) bytes, splitting longer ones at a code point
  boundary as WordIndex does. A longer token is cut and its frame is marked `truncated`.
- Signal the chapter end by returning false from `peek()`. It may also set `TOKEN_CHAPTER_END` on
  the last word; either is enough.
- Words inside a table or code block are not prose: the source flags the last prose word before
  them with `TOKEN_NON_PROSE_FOLLOWS` and does not hand them out as RSVP words. A non-prose block
  before the chapter's first word cannot be flagged; the activity handles that case.
- Words it passes over on purpose without a stop (a footnote or endnote body in mid-chapter) are
  declared on the next token: `Token::skippedBefore` is the number of word indexes skipped right
  before it (0 otherwise). A frame never joins words across such a skip: the group ends before the
  token, and the frame records the count in `Frame::skippedBefore`.
- After a jump, reposition itself; the owner then calls `Scheduler::reposition()` (or
  `FrameBuilder::reset()` when using a builder alone). The builder keeps no word state between
  frames, so the next `next()` starts from wherever the source now is.

## Classification

`classifyWord(word, tokenFlags, nextWord)` returns a `WordClass`: a `Pause` (`NONE`, `CLAUSE` or
`SENTENCE`), the token flags, and the letter count. Rules, in order:

1. Trailing closing punctuation is set aside: `" ' ) ] } ’ ” » ›` and invisible format characters
   (U+00AD, U+200B to U+200F, U+2060 to U+2064, U+FEFF). So `weather."` ends in `.`.
2. If the word then ends in `.`, `!`, `?` or `…` (which also covers `...`):
   - a `.` on an abbreviation gives no pause (see the table below);
   - otherwise it is a **sentence end** when the word ends a block (paragraph, section, non-prose
     or chapter end) or the next word, after any opening `" ' ( [ { ‘ “ « ‹`, starts with A to Z,
     0 to 9 or a Latin-1 capital (U+00C0 to U+00DE except ×);
   - otherwise it is a **clause pause**: `"Seaside!" said`, `then… nothing`, `etc. and`.
3. If the word ends in `,` `;` `:` `-` or a dash (U+2013, U+2014, U+2015), it is a **clause pause**.
4. A dash inside the word (U+2013, U+2014, U+2015 or `--`), as in `alone—two`, is a **clause pause**.
5. Otherwise no pause. A decimal number such as `3.5` or `0.25` has no terminal punctuation and
   takes this branch; `3.5.` at the end of a sentence does end it.

**Abbreviations** (`ABBREVIATIONS` in `WordClassifier.cpp`, the one list; matched case-insensitively
on the word without its opening and closing punctuation and final full stop):

`mr mrs ms dr st jr sr prof rev gen col capt lt sgt mt no vs e.g i.e cf fig vol pp ch`

plus any single letter followed by a full stop (initials: `J.`, `É.`). This is the list in the
WordIndex word rules (`docs/spikes/S1-word-stream.md`, section 5). `etc.` is deliberately not in it:
it ends a sentence only before a capital, by rule 2.

**Block end** (`WordClass::endsBlock()`): any of the paragraph, section, checkpoint, non-prose or
chapter-end flags. **Boundary** (`WordClass::isBoundary()`): a sentence end or a block end. A
boundary word is always the last word of its frame, and the next word starts a new sentence.

**Letters** (`countLetters`): letters and digits of any script. Not counted: ASCII punctuation,
Latin-1 symbols, general punctuation (quotes, dashes, ellipsis), symbols, emoji, and combining
marks (U+0300 to U+036F and the other combining blocks).

## Focus letter

`focusLetter(word)` returns the focus letter's index in code points, its byte offset, and its byte
length (the letter plus any combining marks after it). The letter is chosen by ordinal among the
word's letters, so leading and trailing punctuation, quotes, apostrophes, inner dashes and
combining marks are skipped automatically and never chosen:

| Letters in the word | Focus letter (0-based ordinal among the letters) |
| --- | --- |
| 1 | 0 |
| 2 to 5 | 1 |
| 6 to 9 | 2 |
| 10 to 13 | 3 |
| 14 or more | 4 |

A word with no letter at all (`—`, `&`) focuses on its first code point. The table is
`FOCUS_TABLE` in `FocusLetter.h`.

## Pacing

Base interval: 60,000 / target WPM milliseconds per word (250 ms at 240 WPM).

Each word gets a raw weight from `PACING` (`Pacing.h`), in units of `WEIGHT_ONE` = 1000:

| Word | Raw weight |
| --- | --- |
| No pause | 1000 |
| Clause pause | 1500 |
| Sentence end | 2000 |
| Block end (paragraph, section, non-prose, chapter end) | 2500 |
| Plus, 8 to 10 letters | +100 |
| Plus, 11 to 13 letters | +200 |
| Plus, 14 or more letters | +300 |

A word takes the highest of the first four rows that applies, plus at most one long-word extra.

The raw weight is divided by `PACING.normalisation`, the mean raw weight measured over the English
prose sample in `test/rsvp_core/prose_sample` (currently **1117**, measured 1116.76). So over typical
prose the mean normalised weight is 1.0 and the text plays at the target WPM with its holds
included: a plain word runs a little faster than the base interval and punctuation holds pay for
it. A test fails if the constant drifts more than 0.5% from the measurement.

`Pacer::intervalMs(rawWeight)` computes `60,000 * rawWeight / (wpm * normalisation)` in 64-bit
integers and carries the remainder to the next word. The same inputs give the same milliseconds on
the ESP32 and the host, and the sum of any run of intervals is exactly
`floor(60,000 * sum(rawWeight) / (wpm * normalisation))`: no drift, however long the run.
`setWpm()` and `reset()` clear the carried remainder.

## Grouping into frames

`FrameBuilder::next(Frame&)` builds one frame from the source. Settings: `setWordsPerFrame(1..4)`
(default 1, clamped to `MAX_WORDS_PER_FRAME`) and `setWpm()` (default 240). Rules:

- Consecutive words join a group until it holds N words.
- A boundary word (sentence end, paragraph end, section end, non-prose marker, chapter end) is
  always the last word of its group.
- The group must fit the screen. The UI supplies `FitsFn(context, text, focusByteOffset)`, called
  with each candidate group's joined text (words separated by one ASCII space) and the byte offset
  of its focus letter. When a candidate does not fit, the group ends before that word, which is
  left unconsumed for the next frame.
- A single word that does not fit is still shown alone, with `overWide` set so the UI can shrink
  or hyphenate it.
- The joined text is held in a fixed 256-byte buffer. A word that would overflow it ends the group
  like a width limit; a single word always fits.
- Nothing is ever dropped or reordered: every token the source hands out appears in exactly one
  frame, in order.

**Focus of a group**: the focus letter of the middle word, word `(wordCount - 1) / 2` (the first of
a pair, the second of three or four). The eye lands near the centre of the group, slightly left,
which is the same rule as for single words applied one level up. Grouping is meant for short,
common words, so the middle word is a stable anchor and the rest of the group stays within a
glance either side. The focus column is fixed, so the fit callback receives the focus offset to
check both sides of the column.

A `Frame` carries:

| Field | Meaning |
| --- | --- |
| `text`, `textLength`, `view()` | NUL-terminated joined UTF-8 text |
| `wordCount`, `wordOffset[]`, `wordLength[]`, `word(i)` | The member words within `text` |
| `firstWordIndex`, `lastWordIndex` | Chapter word indexes of the first and last word |
| `skippedBefore` | The first word's `Token::skippedBefore` (indexes passed over on purpose right before it) |
| `focusWord`, `focusByteOffset`, `focusByteLength` | The group's focus letter within `text` |
| `holdMs` | Ideal hold: exactly the sum of the member words' paced intervals |
| `lastWord` | `WordClass` of the last word (sentence, paragraph and section ends, for clearing refreshes) |
| `stops` | `STOP_CHECKPOINT`, `STOP_NON_PROSE`, `STOP_CHAPTER_END` bits: what follows this frame |
| `overWide`, `truncated` | Display hints described above |

## Sentence stepping

For "back one sentence" and "forward one sentence" on the pause screen. The caller classifies a
window of words around the position (`classifyWords` does a run of tokens; it needs the text of the
word after the window) and passes a `ClassWindow` (classes, count, chapter index of the first):

- `sentenceStart(window, i, out)`: start of the sentence containing word i.
- `previousSentenceStart(window, i, out)`: start of the sentence before that one.
- `nextSentenceStart(window, i, out)`: first word after the sentence containing word i. The result
  may be one past the window, or the chapter's word count; the caller checks it.

A sentence starts at word 0 and after every boundary word. Each function returns false when the
window is too small to decide (load a larger window and retry) or there is no such sentence.
A common back-step: go to `sentenceStart` if the position is past it, otherwise to
`previousSentenceStart`.

## Playback scheduler

`Scheduler` owns one `FrameBuilder` over the caller's `WordSource` and uses an injected `Clock` and
`DisplaySink`. It implements plan section 6, "Scheduler":

1. **Draw.** Build the next frame from the source and call `DisplaySink::showFrame()`, which
   returns when the panel reports the refresh finished. The clock is read just before (`start`)
   and just after (`end`) the call.
2. **Dwell.** The frame stays on screen at least `minDwellMs` after `end` (`DEFAULT_MIN_DWELL_MS`,
   60 ms; `setMinDwellMs()`).
3. **Next update** at `max(start + frame.holdMs, end + dwell)`. The deadline counts from this
   update's actual start, never from an absolute timetable, so a late update (slow refresh, a
   stall, a late call) is never repaid by shortening later frames: no catch-up bursts.
4. **Stops.** A frame never extends past a checkpoint section end, a non-prose marker or the
   chapter end (`FrameBuilder` ends the group there and only peeks the next token). When such a
   frame has had its full slot, the next `step()` reports `STOPPED` with the frame's `stops` bits,
   without building or consuming anything further: the last word before the break is on screen
   and the next word is still unconsumed in the source. The caller handles the stop (checkpoint,
   image in page view, next chapter); `play()` (or `resolveStop()` then `play()`) continues with
   the next word. Several bits can be set at once (a checkpoint section that ends the chapter):
   handle `STOP_CHECKPOINT` first.

Nothing sleeps. The scheduler is a state machine: `PAUSED` (initially, after a pause, after
`resolveStop()` or `reposition()`), `PLAYING` and `STOPPED`. `step()` does at most one blocking
display call and returns a `StepResult`: what it did (`NONE`, `SHOWED_FRAME`, `CLEARED`, `PAUSED`,
or `STOPPED` with the stop bits), and, while playing, the clock time at which it wants to be called
again (`wake`, `wakeAtMs`). It never builds a frame ahead, so a pause, a settings change or a seek
never strands a word that was taken from the source but not shown.

### Calling contract

- Call `step()` from one task: the task that may call the display (on the device the RSVP
  activity's render task; S2, section 5). `play()`, `reposition()`, the setters and the accessors
  belong to that task too, or are serialised with it (for example under the render lock).
- `requestPause()` is the one call that is safe from any task at any time (an atomic flag). It takes
  effect at the next `step()`, before any further frame is drawn. A request that arrives during a
  blocking `showFrame()` lets that frame finish, and `step()` then returns `wakeAtMs` = now so the
  next `step()` pauses at once. If the frame on screen ends at a stop, the stop is reported instead
  of the pause (the reader has reached the break).
- When `wake` is true, call `step()` again at `wakeAtMs` (calling earlier is harmless: it returns
  `NONE` with the same wake time). When `wake` is false, playback is paused or stopped: wait for
  input.
- `setWpm()` and `setWordsPerFrame()` may be called at any time; they take effect at the next
  `play()`, from the next unshown word. The pacing remainder is cleared only if the WPM changed.
- `reposition()` after moving the source (sentence step, reread, return from page view, next
  chapter): it pauses, drops a pending stop, clears the pacing remainder and the deadline, and
  opens a new audit segment. The next `play()` starts at once, waiting only for the rest of the
  dwell if the last refresh ended less than the dwell ago.
- **Pause and clearing: the caller clears.** On `PAUSED` the activity draws the pause screen, and
  on a trial profile that must be a clearing refresh (plan section 6, "Safety limits"). The
  scheduler does not run it because (a) whether a clear is needed depends on the display profile,
  which the scheduler does not know; (b) on the device the refresh that removes residue can be the
  one that draws the pause screen, one slow refresh instead of two; (c) it happens after the pause,
  outside active time, so the meter is the same either way. After any clearing refresh it runs
  itself (on pause, on entering RSVP, around a checkpoint or page view), the caller calls
  `noteClearingRefresh()` so the clearing count restarts.

```cpp
// RsvpActivity, on the render task. waitForInputUntil() returns early on a key or touch.
scheduler.play();
while (open) {
  const rsvp::StepResult r = scheduler.step();
  switch (r.event) {
    case rsvp::StepEvent::PAUSED:
      profile.clear();  // draws the pause screen with a clearing refresh (trial profiles: always)
      scheduler.noteClearingRefresh();
      drawPauseScreen(scheduler.wpm(), scheduler.meter().deliveredWpmTenths());
      break;
    case rsvp::StepEvent::STOPPED:
      handleStop(r.stops);  // checkpoint UI, image in page view, or next chapter + reposition()
      break;
    default:
      break;
  }
  if (r.wake) {
    waitForInputUntil(r.wakeAtMs);
  } else {
    waitForInput();
  }
  handleInput();  // may call requestPause(), play(), setWpm(), reposition() ...
}
```

The main task's input handler may instead call `scheduler.requestPause()` while the render task is
blocked in a refresh, then notify the render task so its wait ends.

### Display sink

`DisplaySink` (`DisplaySink.h`) is what `DisplayProfile` implements for RSVP:

- `showFrame(frame)`: draw the frame in the word band with the profile's word-update waveform and
  return when the panel reports the refresh finished (the BUSY wait inside `displayBuffer()` has
  ended). The full frame may be transferred; only the band changes.
- `clearingRefresh()`: run a clearing refresh (absolute HALF or FULL, trial LUT released) with the
  word band empty and the guides kept, and return when it finished.

Both are called only from inside `step()`, on the task that calls `step()`. The scheduler times
them with its own clock.

### Clearing policy

`ClearingPolicy` counts word updates since the last clearing refresh (plan section 6, "Clearing").
Before each update the scheduler asks `clearDue()`:

- due once at least `softLimit` updates have passed and the last update ended a block
  (`lastWord.endsBlock()`: a paragraph, section, non-prose or chapter end);
- due in any case once at least `hardLimit` updates have passed.

Defaults 60 and 120 (`DEFAULT_CLEAR_SOFT_LIMIT`, `DEFAULT_CLEAR_HARD_LIMIT`); trial profiles pass
lower ones with `setClearingLimits()` (45/90, 30/60). A limit of 0 turns that rule off. At most
`hardLimit` word updates run between two clears.

The clear runs when the next update is due: the paragraph-end frame keeps its full slot, the
clearing refresh runs, and the next frame follows as soon as it returns (no dwell: nothing is on
screen to read). Its time is display delay and counts as active time. No clear runs at a stop or at
the chapter end; if one is due when playback resumes, it runs before the first frame.

### Delivered-WPM meter

`DeliveredWpmMeter` gives the session's words shown divided by its active time. A session runs from
`resetSession()` (the activity calls it when RSVP opens); pauses, stops and seeks do not end it.

**Active time** is made of spans. Each span starts at the first display operation (a frame or a
clearing refresh) after a `play()` and runs without a break until playback stops being active:

- included: every refresh, dwell and hold, clearing refreshes, and any lateness while playing (a
  slow refresh, a stall, a late `step()` call);
- excluded: time before the first play; time from `play()` to the first display operation (the
  activity drawing its guides); paused time; time stopped at a checkpoint, a non-prose item or the
  chapter end; time after `reposition()` until the next play;
- **the last frame before a pause** counts its words, and its time up to the pause but never past
  the moment its successor was due (`max(start + hold, end + dwell)`). A pause mid-hold counts only
  what was on screen; a pause noticed late counts no idle time. The last frame before a **stop**
  always counts its full slot, because the stop is reported only when that slot ends.

So at an ideal display (refresh 0) each frame counts its ideal hold (or the dwell, if longer), and
a long run of prose reports the target WPM: the tests measure 240.1 on the prose sample at a
target of 240 and 600.1 at 600. With a refresh time `R`, a frame's slot is `max(hold, R + dwell)`,
plus clearing time: about 99 WPM at `R` = 540 ms, single words.

`deliveredWpmTenths()` is `round(words * 600,000 / activeMs)` in integers (2405 is 240.5 WPM), and
0 before any active time. `stats()` holds what a results file records: words shown, active ms,
word updates, clearing refreshes, total and maximum update time (`meanUpdateMs()`), total and
maximum clearing time. Active time is added as each display operation starts and when playback
stops, so during playback it lags by the frame on screen.

### Word-order audit

`WordOrderAudit` receives every frame the scheduler draws (`recordFrame(first, last, count)`) and
records whether each word index was drawn exactly once and in order since `reset()`:
`wordsDrawn()`, `inOrder()`, `violations()`, `gaps()`, `repeats()`, and the first violation's
expected and drawn index. A frame whose index range does not hold exactly its word count is itself
a violation.

- **Seeks** open a new segment: `reposition()` calls `beginSegment()`, and the next index is
  accepted whatever it is. Within a segment each index must be the previous plus 1. `segments()`
  counts them; a chapter played straight through, stops included, is one segment.
- **Non-prose items:** after a `STOP_NON_PROSE` stop, the next `play()` allows one forward jump
  (`allowForwardSkip()`), because a source may keep word indexes for a table's or code block's
  words without handing them out (S1, section 5). The jump is counted in `skippedWords()`. A
  repeat is never allowed.
- **Declared skips:** when a frame's first word has `skippedBefore` > 0, the scheduler calls
  `declareSkip(count)` before recording the frame, and the audit expects the next index exactly that
  much further on. The count goes to `declaredSkips()`, apart from `skippedWords()`. A wrong count
  is still a gap or a repeat; at the start of a segment the declaration is ignored.
- `resetSession()` resets it together with the meter.

## Constraints

C++20, no exceptions, RTTI, iostream, locale, regex, threads, sleeping or filesystem. No heap
allocation: a `Frame` is a fixed ~300-byte struct, classification, focus and pacing work on views
and stack values, and the scheduler holds one `Frame` and a few counters. The fit callback is a
function pointer and context, not `std::function`. `requestPause()` uses `std::atomic<bool>`.

Time is `uint32_t` milliseconds from the injected `Clock`, as `millis()` returns them. It wraps
every 49.7 days; every comparison is wrap-safe (`timeBefore`), which holds while the compared times
are less than 24.8 days apart. Accumulated active and update times are `uint64_t`.
