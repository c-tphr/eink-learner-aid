# WordIndex

Per-chapter word index for RSVP and checkpoints (plan section 4, "Position model"; spike S1,
`docs/spikes/S1-word-stream.md`). One streaming expat pass over a chapter's XHTML writes a `.wix`
file holding every word with its visible-text offsets and flags, the `rsvp-` anchors, and the
IMAGE/TABLE/CODE/NOTE markers. A reader streams words back from the SD card with a fixed, small
RAM footprint, maps pages to words and words to pages through CrossPoint's visible-offset
coordinate, and feeds RsvpCore. Portable C++20, no Arduino, ESP-IDF or SdFat code; host-tested in
`test/word_index`.

| File | What it holds |
| --- | --- |
| `WordIndexFormat.h/.cpp` | Format constants, `WordFlag`, `MarkerKind`, `Header`, header encoding, CRC-32, little-endian helpers |
| `WordIndexIo.h` | `ChapterInput`, `IndexSink`, `IndexFile`: the byte interfaces the caller implements |
| `WordIndexBuilder.h/.cpp` | `WordIndexBuilder`, `BuildStatus`, `BuildReport` |
| `WordIndexReader.h/.cpp` | `WordIndexReader`, `ReadStatus`, `WordInfo`, `Marker`, `Anchor`, `AnchorCursor` |
| `IndexWordSource.h/.cpp` | `IndexWordSource` (`rsvp::WordSource` over a reader), `SectionEndFn`, `SectionEndTable` |

Everything is in namespace `wordindex`. Dependencies: expat (the firmware's `lib/expat`, or system
expat on the host), `lib/Utf8` (`utf8NextCodepoint`, `utf8ComposeNfcInPlace`), `lib/Epub` headers
`Epub/htmlEntities.h` (the named-entity table) and `Epub/VisibleTextUtils.h` (the non-visible
element list), `lib/XmlParserUtils`, and for the adapter only `lib/RsvpCore/WordSource.h`. Nothing
here depends on `lib/CheckpointCore`.

## What a word is

The rules are **`docs/spikes/S1-word-stream.md` section 5**, plus the NOTE region decision recorded
in `tools/prep/README.md` ("Words"), and `tools/prep/words.py` is the Python implementation of the
same rules. This library implements them exactly, with these choices where the text leaves room:

- **Offsets** are counted exactly as `ChapterHtmlSlimParser` counts `visibleTextOffset`: every code
  point of character data and expanded entities once `<body>` has started (matched like the
  renderer: case-insensitive qualified name at the start tag, exact `body` at the end tag), outside
  `head`, `style`, `script`, `title` and `rp` (the renderer's depth counter, qualified names).
  Hidden, page-break, `rt` and footnote-mark text is counted but is in no word.
- **Element rules** (boundaries, headings, `sup`, `a`, `table`, `pre`, `img`, `rt`, note bodies)
  match the lowercased local name, as `words.py` does. Attributes are matched by their qualified
  name (`id`, `role`, `epub:type`, `href`, `hidden`), as the renderer does.
- **Hidden content** (a `hidden` attribute, `role="doc-pagebreak"`, `epub:type="pagebreak"`, `rt`):
  its text is in no word and every tag inside it is ignored (no boundary, flag or marker). An
  `rsvp-` anchor inside it is still recorded.
- **A word's span** `[start, end)` runs from its first kept code point to after its last one; a
  soft hyphen or U+FEFF after the last kept code point extends `end`. Hidden or footnote-mark text
  inside a word is inside its span. Words never overlap.
- **Composition** uses the firmware's own composer (`utf8ComposeNfcInPlace`, the allocation-free
  form of `utf8ComposeNfc` over the same table), after the 255-byte split, piece by piece, as
  `words.py` composes each piece.
- **NOTE**: a word gets the `NOTE` flag when it starts inside a note body (an element whose
  `epub:type` has the token `footnote`, `endnote` or `endnotes`, or whose `role` has `doc-footnote`,
  `doc-endnote` or `doc-endnotes`); the NOTE marker is the outermost such element, `[words started
  before its start tag, words started before its end tag)`. Markers that open inside a note body
  carry `MARKER_IN_NOTE`.
- **Sentence ends are not computed**: `rsvp::classifyWord` is the single rule set. Bit 0 of the flags
  is reserved and never set.
- **After `</html>`** anything that does not parse is ignored, as by the renderer.

## File format (`.wix`, format version 1, rules version 1)

All integers are little-endian. The file is a sequence of 512-byte **slots**, then three tables.
Slot 0 holds the header; slots 1 to `slotCount` hold word blocks and, in large chapters, spill
slots. Byte offset of slot `i` = `i * 512`.

### Header (slot 0, 96 bytes used, the rest zero)

| Offset | Type | Field |
| --- | --- | --- |
| 0 | `char[4]` | magic `CPWI` |
| 4 | `u8` | `formatVersion` (1) |
| 5 | `u8` | `rulesVersion` (1; equals `rules_version` of `fixtures/books/fixture.expected.json`) |
| 6 | `u16` | header bytes (96) |
| 8 | `u16` | slot bytes (512) |
| 10 | `u16` | reserved, 0 |
| 12 | `u32` | `sourceBytes`: XHTML bytes read (the inflated zip entry size) |
| 16 | `u32` | `sourceCrc32`: CRC-32 of those bytes (equals the zip entry's CRC-32) |
| 20 | `u32` | `visibleCodepoints`: the renderer's visible-text count for the whole chapter |
| 24 | `u32` | `wordCount` |
| 28 | `u32` | `slotCount` (slots after slot 0) |
| 32 | `u32` | `blockCount` (word blocks) |
| 36 | `u32` | `directoryOffset` = `(1 + slotCount) * 512` |
| 40 | `u32` | `anchorCount` |
| 44 | `u32` | `anchorOffset` = `directoryOffset + 12 * blockCount` |
| 48 | `u32` | `anchorBytes` |
| 52 | `u32` | `markerCount` |
| 56 | `u32` | `markerOffset` = `anchorOffset + anchorBytes` |
| 60 | `u32` | `fileBytes` = `markerOffset + 20 * markerCount` |
| 64 | `u32` | `tablesCrc32`: CRC-32 of bytes `[directoryOffset, fileBytes)` |
| 68 | 24 bytes | reserved, 0 |
| 92 | `u32` | CRC-32 of header bytes `[0, 92)` |

The builder writes slot 0 as zeros first and the header last, so an interrupted build never has a
valid magic.

### Word block (a slot of kind 1)

| Offset | Type | Field |
| --- | --- | --- |
| 0 | `u8` | kind = 1 |
| 1 | `u8` | `n`, words in the block (1 to 62) |
| 2 | `u16` | used bytes (`16 + 7n + text bytes`); the rest of the slot is zero |
| 4 | `u32` | index of the block's first word |
| 8 | `u32` | `base`: start offset of the block's first word |
| 12 | `u32` | CRC-32 of bytes `[0, 12)` followed by `[16, used)` |
| 16 | `n` x 7 bytes | per word: `u16 startDelta` (start = base + startDelta), `u24 span` (end - start), `u8 flags`, `u8 textBytes` |
| 16 + 7n | | the words' UTF-8 texts, concatenated in order, not NUL-terminated |

A block closes when the next word's record and text would not fit the slot, or when its start
would be more than 65,535 code points past `base`. Words of a block are consecutive, and blocks
follow each other in word order.

### Spill slot (kind 2)

Builder scratch: a 16-byte header (`u8 kind = 2`, `u8 table`, `u16 used`, `u32 next spill slot of
the same table`, `u32 records`, `u32 0`) and up to 496 bytes of table records. The builder keeps
each table (directory, anchors, markers) in one 496-byte RAM buffer and writes a full buffer to the
next slot; at the end it copies the spilled records into the final tables. Spill slots are dead space
afterwards (one per 41 blocks, about 2 % of a large chapter's file) and no reader reads them.

### Tables (after the slots, in this order, covered by `tablesCrc32`)

| Table | Record |
| --- | --- |
| Directory | per word block, in order: `u32 firstWord`, `u32 base`, `u32 slot` (12 bytes). First words strictly increase from 0, bases do not decrease, slots increase |
| Anchors | per element whose `id` starts with `rsvp-`, in document order: `u32 wordIndex` (words started before its start tag), `u32 offset` (visible offset there), `u8 idLength`, the id bytes |
| Markers | in document order of their start (`wordStart` never decreases): `u8 kind` (1 IMAGE, 2 TABLE, 3 CODE, 4 NOTE), `u8 flags` (bit 0 `MARKER_IN_NOTE`), `u16 0`, `u32 wordStart`, `u32 wordEnd` (== wordStart for IMAGE), `u32 offsetStart`, `u32 offsetEnd` (20 bytes) |

### Changes from S1's proposal (section 4.3), and why

- **Fixed 512-byte slots instead of variable-size blocks**: block `k` is one aligned SD sector, the
  reader's block buffers are fixed arrays, and the directory needs no file offset arithmetic.
- **The header carries the source's CRC-32 as well as its size**: the zip central directory already
  holds both, so the rebuild check costs no read of the chapter and catches a book replaced by one
  with a chapter of the same size.
- **Per word `u16 startDelta, u24 span, u8 flags, u8 textBytes`** instead of `u8 span` (capped at
  255) and `u16 textOffset`: a span is exact even when hidden text or a footnote mark sits inside a
  word, and the text offsets are computed once when a block is loaded.
- **Spill slots**: S1 kept the directory, anchors and markers in RAM until the end of the build. This
  builder's RAM does not grow with the chapter.
- **No commit byte**: the header is written last, and the header CRC, `fileBytes` and the table and
  block checksums detect an incomplete or damaged file.
- **CRC-32 on the header, on each block and on the tables**, and a NOTE marker kind with an
  in-note flag.

## Robust reading

`open()` reads the header and checks, in order: size at least one slot, magic, format version,
header CRC, rules version, file size equal to `fileBytes`, the layout equations above, the tables'
CRC, and the structure of every directory entry, anchor and marker record. A word block is checked
when first read: kind, counts, used bytes, its CRC, its first word and base against the directory,
non-overlapping spans inside the chapter, texts filling the used bytes exactly. Any failure sets
`status()` to `Corrupt` (or `IoError`), which sticks: every later call returns false. No read goes
past `fileBytes`, and no buffer is indexed by an unchecked value. The tests prove it under
AddressSanitizer and UndefinedBehaviorSanitizer for every truncation and every single-byte change of
an index, and 1,500 random multi-byte edits: everything outside padding and spill slots is
detected.

## RAM and file size (measured, `test/word_index`, FormatTest)

**Reader**: `sizeof(WordIndexReader)` = 1,920 bytes, the same for every chapter:

    header (68) + one directory page (43 entries x 12 = 516) + 2 x block cache (512 + 62 x 2 + 24) + counters

so about 1.9 KB for a 10,000-word chapter as for a 20-word one. Stack: at most about 0.6 KB in a call
(a directory page is decoded from a 516-byte local buffer). Reads: one per block when iterating
(10,000 words: 274 reads), and per random lookup a binary search over directory page heads
(`ceil(log2(pages))` reads of 12 bytes, pages = blocks / 42), one page read (up to 516 bytes) and one
block read (512 bytes).

**Builder**: `sizeof(WordIndexBuilder)` = 4,120 bytes (element stack of 256 x 2 bytes, the word in
progress and the pending word, one open block and its text, three 496-byte table buffers), plus up
to about 0.8 KB of stack, plus expat's own heap. Expat allocates its parser structure, one 1 KB input
buffer (`XML_GetBuffer`, `XML_CONTEXT_BYTES=0`), a 1 KB character-data buffer, a tag structure and
name buffer per open element (bounded by `MAX_ELEMENT_DEPTH` = 256), attribute arrays and a string
pool sized by the largest start tag, and DTD hash tables (sized by any internal DTD subset). Measured
peak with the firmware's flags: 9.7 KB (fixture chapter 2), 8.7 KB (the 3,051-word chapter), 9.2 KB
(a 106 KB synthetic chapter with 800 anchors). Nothing else grows with the chapter.

**File size**:

| Chapter | Words | XHTML bytes | `.wix` bytes | Bytes per word | Blocks | Spill slots |
| --- | --- | --- | --- | --- | --- | --- |
| fixture `title.xhtml` | 20 | 481 | 1,036 | 51.8 | 1 | 0 |
| fixture `chapter-1.xhtml` | 451 | 3,177 | 6,316 | 14.0 | 11 | 0 |
| fixture `chapter-2.xhtml` | 608 | 4,081 | 7,868 | 12.9 | 14 | 0 |
| fixture `chapter-3.xhtml` | 321 | 2,321 | 4,724 | 14.7 | 8 | 0 |
| fixture `endnotes.xhtml` | 34 | 697 | 1,056 | 31.1 | 1 | 0 |
| `test_reader_menu_progress.epub` chapter 1 | 3,051 | 21,696 | 43,468 | 14.2 | 81 | 1 |
| 10,000 words (that chapter x3 + 847) | 10,000 | 70,694 | 142,444 | 14.2 | 265 | 6 |

A chapter costs at least 1 KB (header slot and one block); English prose costs about 14 bytes per
word, so an 80,000-word book takes about 1.1 MB on the card.

## API

```cpp
// Build: from a pull source, or pushed from a Print-style stream.
wordindex::WordIndexBuilder builder;                // optional XML_Memory_Handling_Suite* for expat
wordindex::BuildStatus s = builder.build(input, sink);   // or begin(sink); feed(data, n)...; end()
builder.report();   // status, expat error and line, anchor id for AnchorInsideWord, header, sizes

// Read.
wordindex::WordIndexReader reader;
reader.open(file);                                   // ReadStatus::Ok, or why to rebuild
reader.matchesSource(zipEntrySize, zipEntryCrc32);   // the rebuild check
reader.word(i, info);           // info.text (valid until the next call), start, end, flags
reader.firstWordEndingAfter(pageStartOffset, w);     // page -> word
reader.wordStart(w, offset);    // word -> page via Section::getPageForVisibleTextOffset(offset)
reader.findAnchor("rsvp-s0007-end", anchor);         // anchor.wordIndex
reader.nextAnchor(cursor, anchor);                   // enumerate, in document order
reader.imageAt(w); reader.regionContaining(w, kinds, m); reader.nextMarker(w, kinds, m);

// RSVP.
wordindex::SectionEnd ends[] = {{endAnchorWordIndex, /*checkpoint=*/true}};
wordindex::SectionEndTable table{ends, 1};
wordindex::IndexWordSource source(reader, wordindex::SectionEndTable::lookup, &table);
source.seek(w);  // then Scheduler::reposition()
```

Page view mappings (S1 section 4.1): a page's first word is `firstWordEndingAfter(start(p))`; a
word's page is `getPageForVisibleTextOffset(start[w])`; the page showing a section's end is
`getPageForVisibleTextOffset(end[E-1] - 1)` for end anchor index `E`; an image's page is
`getPageForVisibleTextOffset(marker.offsetStart, /*preferFirstAtOffset=*/true)`. A section's words
are `[findAnchor(start).wordIndex, findAnchor(end).wordIndex)`.

### RsvpCore adapter

`IndexWordSource` hands RsvpCore the prose words only, with:

- `TOKEN_PARAGRAPH_END` when the word has `PARA_END` or `LINE_END`;
- `TOKEN_NON_PROSE_FOLLOWS` on the last prose word before an IMAGE marker (outside notes) or a
  TABLE/CODE region; the region's words are never handed out, and the next token is the first prose
  word after it (the audit accepts the jump after the non-prose stop);
- `TOKEN_SECTION_END` / `TOKEN_CHECKPOINT` from the caller's `SectionEndFn`, asked for the end anchors
  between this token and the next prose word (`SectionEndTable` is a ready-made one over an array);
- `TOKEN_CHAPTER_END` on the last prose word;
- NOTE-region words are passed over with no flag and no stop; the next token's
  `Token::skippedBefore` says how many, so the scheduler declares them to the word-order audit
  (`lib/RsvpCore/README.md`, "Declared skips").

A chapter whose every word is in a note (an endnotes document) hands out nothing: `peek()` is false at
once. A table, code block or image before the chapter's first prose word cannot be flagged
(RsvpCore's rule); the activity checks `reader.nextMarker(0, ...)` itself.

## Rebuild rules

The `.wix` file must be (re)built when:

1. it does not exist;
2. `open()` returns anything but `Ok`: `Truncated`, `BadMagic` (also an interrupted build),
   `OtherFormat` (format version changed), `OtherRules` (rules version changed: word indexes may have
   moved), `Corrupt` (a checksum or consistency check failed), `IoError` (after one retry);
3. `matchesSource(size, crc32)` is false for the chapter's zip entry (the book was replaced or
   re-prepared);
4. a later call fails with `status()` `Corrupt` (a damaged block): close, delete, rebuild.

Raise `RULES_VERSION` (and `rules_version` in the preparation tools' expected file) whenever a word
rule changes in a way that can move a word index; raise `FORMAT_VERSION` when the bytes change.
Font, margin, spacing, hyphenation, Focus Reading and orientation changes never invalidate it.

## For the integration tasks

- **Where**: `/.crosspoint/epub_<hash>/rsvp/<spineIndex>.wix`, beside `sections/` in the book's cache
  folder, so a CSS-driven wipe of `sections/` keeps it and `Epub::clearCache` deletes it.
- **Who and when**: built lazily, on the first RSVP use of a chapter (R4), and when the checkpoint
  code needs the anchors of a prepared book's chapter (C2/C3). One pass, no layout.
- **How**: write to `<spineIndex>.wix.tmp` through an `IndexSink` over an `FsFile` opened
  `O_RDWR | O_CREAT | O_TRUNC` (the builder appends, patches the header at offset 0 last, and reads
  back its own spill slots), then rename on `BuildStatus::Ok`; delete the temporary file otherwise.
  Feed the XHTML either from `<cache>/html/<spineIndex>.html` through a `ChapterInput`, or push it with
  `begin`/`feed`/`end` from `Epub::readItemContentsToStream`. Build where the parser's ~10 KB heap
  and ~5 KB of builder state are affordable (the builder object can live on the heap or in PSRAM).
- **Reading**: an `IndexFile` over an `FsFile` opened for reading; keep the reader open for the
  chapter being read. Check `matchesSource` with the zip entry's size and CRC-32 at open.
- `AnchorInsideWord` means the prepared book is broken (the preparation tool refuses to write such a
  book); treat the chapter's checkpoints as unresolved and report the anchor id.
- **Known limits**: offsets mirror the pinned renderer; a renderer change to its visible-offset rules
  must be mirrored here (the parity test fails first). In grid-laid-out tables, a row split across
  pages is drawn line by line across its cells, so a table word's page can differ from the page its
  offset gives; table words are never RSVP positions and anchors never sit inside tables.
