# RsvpCore

Portable RSVP logic for the reader: which words each display update shows, where the focus
letter is, and how long each update should ideally be held. No Arduino, ESP-IDF, font or display
code; host-tested in `test/rsvp_core` (plan section 4 and 6).

This part (task R1) is everything before the clock. Task R2 adds the scheduler that plays frames
against a clock and a display, and the delivered-WPM meter.

| File | What it holds |
| --- | --- |
| `WordSource.h` | `Token`, `TokenFlag`, the `WordSource` pull interface, `MAX_WORD_BYTES` |
| `WordClassifier.h/.cpp` | `classifyWord`, `WordClass`, `Pause`, the abbreviation table |
| `FocusLetter.h/.cpp` | `focusLetter`, `FOCUS_TABLE` |
| `Pacing.h/.cpp` | `PACING` (the weight table and normalisation constant), `rawWeight`, `Pacer` |
| `FrameBuilder.h/.cpp` | `FrameBuilder`, `Frame`, `StopReason`, the width callback `FitsFn` |
| `SentenceStep.h/.cpp` | `sentenceStart`, `previousSentenceStart`, `nextSentenceStart` |
| `RsvpText.h/.cpp` | UTF-8 decoding and character classes (internal) |

Everything is in namespace `rsvp`. English only (plan section 1).

## Data flow

```
WordSource (WordIndex adapter) --peek/advance--> FrameBuilder --Frame--> scheduler (R2) --> UI
                                                  |  classifyWord, focusLetter, Pacer
                                                  '- FitsFn (UI measures the text)
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
- After a jump, reposition itself and call `FrameBuilder::reset()`. The builder keeps no word state
  between frames, so the next `next()` starts from wherever the source now is.

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

## What the scheduler (R2) is expected to do

- Own one `FrameBuilder` per playback stream; call `next()` for each update.
- Draw the frame and use `holdMs` as the paced interval to the next update: start the next update
  at the later of the paced deadline and the end of the minimum dwell after the refresh, with no
  catch-up bursts (plan section 6).
- Act on `stops` after showing the frame: stop for a checkpoint, stop for a non-prose item, stop
  at the chapter end. A frame never extends past any of these.
- Use `lastWord.endsBlock()` and the section flags for the clearing-refresh policy.
- Count delivered words with `wordCount` for the delivered-WPM meter.
- Call `setWpm()` and `setWordsPerFrame()` only while paused.

## Constraints

C++20, no exceptions, RTTI, iostream, locale, regex or filesystem. No heap allocation: a `Frame`
is a fixed ~300-byte struct, and classification, focus and pacing work on views and stack values.
The fit callback is a function pointer and context, not `std::function`.
