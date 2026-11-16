# Spike S1: word stream and page-to-word mapping

Issue #9. Plan references: `docs/PLAN.md` section 4 ("Position model", "Constraints", "Where data is stored"), section 5 (anchors), section 6 (grouping), section 13 (risk "Word and page mapping ... harder than assumed").

## Answer in short

CrossPoint already has a content position that does not depend on layout: the count of visible Unicode codepoints in a chapter's `<body>`. The page renderer records it for the first character of every page and stores it in the section cache. Reading progress, bookmarks and KOReader sync already use it. WordIndex should give every word its start and end in this same count. With that:

- the word stream is built by a separate, light expat pass over the chapter XHTML in `lib/WordIndex`, with no hook in the parser;
- page to first word is a binary search of the page's start offset in WordIndex, and word to page is the existing `Section::getPageForVisibleTextOffset`. No page table is written and no layout code changes;
- a font or margin change rebuilds the section cache (new page offsets) and leaves word indexes untouched.

The prototype runs the real `ChapterHtmlSlimParser` on the host at two layouts, one with hyphenation and Focus Reading. It checks every rendered token against the word stream. The fixture and a 3,051-word chapter from `test/epubs` show 0 mismatches and 0 word-to-page errors. The plan's fallback (approximate, paragraph-level mapping) is not needed. The design needs no edit to `lib/Epub`. The upstream edits the feature still needs are the registration points listed in "Upstream edits".

## How to read this document

- Every `path:line` refers to the pinned upstream commit `490813ff` (`docs/BASE.md`). `lib/`, `src/`, `test/` and `platformio.ini` on `main` are byte-identical to it (`git diff --stat 490813ff HEAD -- lib src test platformio.ini` prints nothing).
- **Verified by running**: the host prototype (`test/spike_word_stream/`, section 7) and the 924 existing host tests. **Read only**: everything about the device (SD timing, heap, expat memory), the reader activity's behaviour at run time, and the partial-build paths, which the prototype does not exercise.

## 1. How an EPUB chapter becomes pages today

### 1.1 Pipeline

| Step | Where | What happens |
| --- | --- | --- |
| Open book | `src/activities/reader/EpubReaderActivity.cpp:251` | `Epub(bookPath, "/.crosspoint")`. Cache folder is `/.crosspoint/epub_<std::hash of the book path>` (`lib/Epub/Epub.cpp:28-30`). |
| Load or build a chapter | `EpubReaderActivity.cpp:1749-1910` | `Section(epub, spineIndex)` tries `loadSectionFile(spec)`. Otherwise it builds incrementally: `startBuild`, then `buildSomeMore(BUILD_PAGES_PER_CHUNK)` until the target page, anchor or content offset exists (`:1854-1868`). |
| Inflate XHTML | `lib/Epub/Epub/Section.cpp:331-403` | The spine item is unzipped once to `<cache>/html/<spine>.html` and kept across layout changes (`:342-347`). |
| Parser setup | `Section.cpp:432-448, 468-478` | A `ChapterHtmlSlimParser` is created with the render spec. The CSS parser is passed only when "Book's style" (`embeddedStyle`) is on. The page callback appends `{fileOffset, paragraphIndex, listItemIndex, visibleTextOffset}` to an in-RAM LUT (`Section.h:29-34`). |
| Parse | `ChapterHtmlSlimParser.cpp:2850-2951` | Expat SAX in 1 KB steps (`:29`, `:2924-2939`). Named HTML entities go through `XML_SetDefaultHandlerExpand` and `lookupHtmlEntity` (`:2892-2894`, `:2591-2605`). |
| Words to blocks | `ChapterHtmlSlimParser.cpp:2317-2589`, `:688-763` | `characterData` splits on ASCII whitespace (`:62`, `:2451-2460`). It cuts words at 200 bytes (`:25`, `:2537-2566`) and turns U+00A0/U+202F into glued space tokens (`:2462-2516`). `flushPartWordBuffer` calls `ParsedText::addWord` with the word's visible offset (`:750-751`). Inline tags glue fragments with `nextWordContinues` (`:2682-2700`). |
| Tokenize for layout | `lib/Epub/Epub/ParsedText.cpp:441-660` | `addWord` composes to NFC (`:456-462`), then splits into layout tokens: CJK break points (`:540-568`), explicit hyphens (`:580-602`), Focus Reading segments (`:611-`). Every token keeps a source offset (`:490`). |
| Line breaking | `ParsedText.cpp:1380`, `:1480`, `:1866-1880` | Hyphenation splits a token. The remainder's offset is the start plus the prefix length (`:1380`). Each line reports the offset of its first logical token (`:1480`). Each emitted word stores a `[start, end)` source range (`:1871-1880`). |
| Pages | `ChapterHtmlSlimParser.cpp:3165-3222`, `:3096-3163`, `:349-360` | `addLineToPage` breaks pages, carrying orphan or widow lines. `setCurrentPageVisibleOffset` keeps the first offset placed on a page, and page 0 is always 0 (`:679-685`). `emitCurrentPage` calls `completePageFn(page, paragraphIndex, listItemIndex, currentPageVisibleOffset)` (`:356`). |
| Persist | `Section.cpp:124-144`, `:671-753` | Each page is serialized as it completes. At commit, the page LUT, anchor map, paragraph LUT, list-item LUT and visible-offset LUT are appended, the header is patched, and the version byte is written last as the commit point (`:735-738`). |

The upstream architecture note says the same: "`sections/*.bin` contains rendered pages plus anchor, paragraph, list-item, and page-start visible-text-offset lookup tables. The offset table makes reading positions content-based" (`docs/contributing/architecture.md:157-161`).

### 1.2 The section cache

- **Location**: `/.crosspoint/epub_<hash>/sections/<spineIndex>.bin` (`Section.cpp:117`). Builds write `<file>.part` and rename it on commit (`Section.h:75-77`).
- **Format**: a header that is also the cache key, then pages, then LUTs (`Section.cpp:146-182`; layout in `docs/file-formats.md:91-652`). The visible-offset LUT holds one `uint32` per page (`Section.cpp:716-719`). Each `TextBlock` word carries an 8-byte source range (`docs/file-formats.md:270-276`; `lib/Epub/Epub/blocks/TextBlock.h:47-50, 134`).
- **Versioning**: `SECTION_FILE_VERSION = 75` (`Section.cpp:86`). `docs/file-formats.md` lags and describes v74. A partial (suspended) build uses a derived sentinel version (`Section.cpp:87-103`). Unknown versions are deleted and rebuilt (`:194-199`).
- **Invalidation**: the section is discarded and rebuilt when any header field differs from the current spec. The fields are font id, line compression, paragraph spacing and indent, alignment, viewport, hyphenation, embedded style, image rendering, Focus Reading, character and word spacing (`Section.cpp:229-240`). A CSS cache change deletes the whole `sections/` folder (`Epub.cpp:532`, `:639`). `Epub::clearCache` deletes the whole book folder (`Epub.cpp:729-742`). The unzipped `html/` copy survives layout invalidation (`Section.cpp:342-347`).
- **Partial sections**: leaving a chapter mid-build persists the pages built so far (`Section.cpp:792-829`). The reader extends the build later (`EpubReaderActivity.cpp:1913-1948`). Offsets are answered from the live build or from the file (`Section.cpp:1130-1133`, `:1183-1189`).

### 1.3 Page turns and progress in the reader

- Forward and back turns change `section->currentPage` or move to the next or previous spine (`EpubReaderActivity::pageTurn`, `EpubReaderActivity.cpp:1511-1552`). Chapter skip is `skipPages` (`:1554-1576`).
- Each render whose spine, page or page count changed saves `progress.bin` (`:2024-2032`). The file holds spine, page, page count and the page's visible offset (`:2125-2133`; `src/activities/reader/EpubReaderUtils.h:15-42`). It is written atomically through a temp file (`src/activities/reader/ProgressFile.h`).
- On open, the saved offset is read back (`EpubReaderActivity.cpp:286-309`). The build runs until it reaches that content offset (`:1854-1857`, `Section.h:169-177`), and the page comes from `getPageForVisibleTextOffset` (`EpubReaderActivity.cpp:1884-1889`). After a relayout, `applyDeferredReposition` maps the old offset to the new page (`:2089-2118`). Sync results and any external offset use the same path (`applyProgressChange`, `:1160-1176`).

So upstream already stores and restores position by content offset, not by page. That is exactly what (chapter, word) needs underneath.

## 2. What the parser sees

### 2.1 Element ids and `rsvp-` anchors

- The parser reads every `id` attribute (`ChapterHtmlSlimParser.cpp:1387-1409`). It records an id only when the element is not a `<span>` or the id is a TOC anchor (`:1397-1399`; `isNonNavigableInlineElement` at `:124-130`), with a cap of 1,024 per chapter (`:47`). **`<span id="rsvp-s0001-end"></span>` is therefore dropped and never reaches the anchor map.**
- A recorded id maps to a page, not to an offset. It is held as `pendingAnchorId` and written when the next text block starts (`:331-347`, `:794`, `:814`), so the page is that of the next block start. For an id in the middle of a long paragraph this is not exact (read, not run). Switching the anchors to `<a id>` would not give a usable word position either.
- Conclusion: WordIndex reads anchor ids itself in its own pass and ignores CrossPoint's anchor map. Keeping anchors as `<span>` means they add nothing to upstream's anchor map or its cap.

### 2.2 How existing features locate positions

- **Footnote and TOC links**: id to page through the anchor map (`Section.cpp:690-703`; `Section.h:130-146`). `navigateToHref` sets `pendingAnchor` and the render loop resolves it (`EpubReaderActivity.cpp:3310-3350`, `:1895-1902`). TOC anchors also force a page break (`Section.cpp:450-461`; `ChapterHtmlSlimParser.cpp:335-342`).
- **KOReader sync**: CrossPoint positions are `(spineIndex, visibleTextOffset)` (`lib/KOReaderSync/ProgressMapper.h:20-30`). `ChapterXPathResolver` is a **separate expat pass** over the chapter, fed by `Epub::readItemContentsToStream` (`lib/KOReaderSync/ChapterXPathResolver.cpp:92-200`, `:606`). It "count[s] every body codepoint the way ChapterHtmlSlimParser does" (`ChapterXPathResolver.h:22-35`). Both share the non-visible element list (`lib/Epub/Epub/VisibleTextUtils.h:17-20`). This is the upstream precedent for the design below.
- **Bookmarks and clippings**: bookmarks store a visible offset (`EpubReaderActivity.cpp:3468`). Clippings use per-word source ranges (`docs/file-formats.md:256-276`).

### 2.3 Images

- `<img>` and `<image>` (`ChapterHtmlSlimParser.cpp:61`, `:1648-1971`). The parser probes the size without extracting the image (`:1696-1707`), places it on the current page or a new one (`:1866-1887`), and sets the page offset at the image if it is the page's first content (`:1923`). An image page's start offset is the visible offset at the `<img>` tag.
- If decoding fails or the "placeholder" image setting is on, the alt text is laid out as `[Image: alt]`. It is marked synthetic and never counted in offsets (`:1950-1964`, `:2320`). Setting 2 suppresses the image entirely (`:1667-1672`).

### 2.4 Tables

- `<table>` flushes the current block, pre-scans the markup to plan column widths (`:1495-1548`), and lays simple rows out as a grid (`:1550-1639`). Rows with block content, images, links or more than 160 words or 2 KB fall back to stacked full-width flow (`ChapterHtmlSlimParser.h:156-159`; `ChapterHtmlSlimParser.cpp:1641-1646`, `:2036-2038`). Nested tables are flattened (`:1497-1505`, `:2671-2680`). Grid rows take their page offsets from per-line source offsets (`:1260-1290`).
- The renderer has no "table" marker in its output beyond `PageBorderBox` frames, so the page cache cannot tell a reader where a table is.

### 2.5 `<pre>` and `<code>`

- `<pre>` is a block tag (`:56`) and turns on preserved whitespace (`:1458-1461`). Each space becomes a token and `\n` breaks the line (`:2431-2449`).
- `<code>` has no special handling. It falls into the generic inline branch (`:2247-2311`). `<pre>` is the only code-block signal in the markup that the renderer itself knows.

### 2.6 Footnote references

- Any internal `<a href>` is treated as a possible footnote (`:2018-2068`). It is underlined, its text becomes ordinary words (with SUP style inside `<sup>`), and its number is collected with brackets stripped (`:2373-2399`). On close it becomes a `FootnoteEntry` (`:2719-2733`), attached to the page that holds it (`:3190-3197`). The link rectangle is stored for touch (`:3201-3207`).
- `<aside epub:type="footnote">` bodies are "rendered as normal content without special handling" (`:2019-2020`).

### 2.7 Hidden text

- `hidden` attribute (`:1463-1467`), `role="doc-pagebreak"` and `epub:type="pagebreak"` (`:2006-2016`): skipped in layout.
- CSS `display:none`, including inline `style` (`:1425-1436`, `:1487-1492`), is honoured only when the CSS parser exists, which means only with "Book's style" on (`Section.cpp:432-448`).
- Offsets count **all** body character data except inside `head`, `style`, `script`, `title` and `rp`. Skipped and hidden text is counted too, because the counting in `characterData` runs before the skip check (`ChapterHtmlSlimParser.cpp:2320-2339`). The offset coordinate is therefore a function of the XHTML alone. It does not depend on CSS or any reader setting. This is the property the design relies on.

## 3. Does the plan's assumption hold?

The assumption: WordIndex is "built once per chapter, on first open, by a pass over the same parsed content the page renderer uses" (`docs/PLAN.md:108`).

**In substance yes, literally no.**

- What holds: the same chapter XHTML, the same offset rules, and therefore the same coordinate system as the renderer's page table. The prototype checks this token by token (section 7).
- What does not: the renderer's parsed content is not a usable word stream, and a pass "over" it would mean hooking the layout. Its tokens depend on settings: Focus Reading segments (`ParsedText.cpp:611-`), hyphen and CJK splits (`:540-602`), hyphenation remainders (`:1380`), glued no-break-space tokens (`ChapterHtmlSlimParser.cpp:2462-2516`), drop caps that move letters out of words (`:2413-2429`, `:576-645`) and synthetic list bullets (`:2146-2162`). It exists only for one layout at a time, and a build may stop part-way (`Section.cpp:792-829`). Span ids are dropped (section 2.1).

So "a pass over the same XHTML, in the renderer's offset coordinate" is what to build. It is still "built once per chapter" and never per layout.

## 4. Recommended design for `lib/WordIndex`

### 4.1 Idea

A position is (chapter, word index). WordIndex stores, per chapter, each word's text, flags, `start` and `end` visible offsets, plus anchors and non-prose regions. Page view keeps working in pages and offsets as it does today. Conversion is always through offsets.

| Need | How |
| --- | --- |
| Page p → first word | `w = first word with end > Section::getVisibleTextOffsetForPage(p)` (`Section.h:160-162`). It returns the word a page starts inside when hyphenation split it across the break. |
| Word w → page | `Section::getPageForVisibleTextOffset(start[w])` (`Section.h:164-167`, `Section.cpp:1167-1243`). |
| Page that holds a section's end (checkpoint in page view) | `getPageForVisibleTextOffset(end[E-1] - 1)`, where `E` is the end anchor's word index. That is the page showing the last character of the section's last word, even when that word is hyphenated across two pages. |
| Non-prose marker → page (show it in page view) | `getPageForVisibleTextOffset(marker.offsetStart, preferFirstAtOffset = true)`: lands on an image-only page. |
| Open page view at word w | Hand the reader the offset `start[w]`. The reader already builds to a content offset and lands on its page (`EpubReaderActivity.cpp:1762-1767`, `:1854-1857`, `:1884-1889`). Writing `progress.bin` with that offset before opening the reader reaches the same path with no code change (`:286-309`). |
| RSVP from the current page | `progress.bin` already holds the page's start offset (`:2125-2133`). Map it to the first word as above. |
| Page view checkpoint test on a forward turn from page p | Fire if `start(p) <= end[E-1] - 1 < start(p+1)`. This compares offsets only, with no word lookup. |

### 4.2 Hook points

- **(i) Emitting the word stream with markers: no hook.** `WordIndexBuilder` is its own expat SAX handler, a `Print` sink like `ChapterXPathResolver`'s counter (`ChapterXPathResolver.cpp:92-138`). It reads `<cache>/html/<spine>.html` when present (`Section.cpp:332-333`; keeping that path in step is a convention, not an API). Otherwise it reads `Epub::readItemContentsToStream(href, sink, 1024)` (`lib/Epub/Epub.h:84-85`). It must mirror these rules exactly:
  - body start, case-insensitive, and end (`ChapterHtmlSlimParser.cpp:1352-1357`, `:2840-2842`);
  - the non-visible depth counter (`:1358-1360`, `:2610-2612`, `VisibleTextUtils.h:17-20`);
  - per-codepoint counting (`:2320-2329`);
  - entity expansion (`:2591-2605`, `:2894`).
- **(ii) Recording page → first word at layout time: no hook.** The renderer already records each page's start offset through `completePageFn` (`ChapterHtmlSlimParser.cpp:356`, `Section.cpp:472-476`) and persists it (`Section.cpp:716-719`). The first word is derived on demand.

### 4.3 On-SD format

One file per chapter: `/.crosspoint/epub_<hash>/rsvp/<spineIndex>.wix`. It lives beside `sections/`, not inside it, so CSS-driven `sections/` wipes do not delete it. All integers are little-endian, as in `Serialization.h`.

| Part | Content | Size |
| --- | --- | --- |
| Header | magic `CPWI`, `u8 formatVersion`, `u8 rulesVersion`, `u32 sourceBytes` (inflated XHTML size), `u32 visibleCodepoints`, `u32 wordCount`, `u32 blockCount`, `u32 directoryOffset`, `u16 anchorCount`, `u32 anchorOffset`, `u16 markerCount`, `u32 markerOffset`, reserved; commit byte written last | 64 B |
| Word blocks | Up to 64 words and at most 2,048 text bytes per block. Block header `u32 baseOffset, u32 firstWord, u16 wordCount, u16 textBytes`. Per word `u16 startDelta, u8 span (end-start, capped 255), u8 flags, u16 textOffset`. Then the NUL-terminated texts. A block also closes early if `startDelta` would pass 65,535. | 12 B + 6 B/word + text |
| Block directory | `u32 firstWord, u32 fileOffset, u32 baseOffset` per block. It is sorted on both keys, so word → block and offset → block are binary searches read from SD. | 12 B/block |
| Anchors | `u32 wordIndex, u32 offset, u8 idLength, id` for every id starting `rsvp-` | 9 B + id |
| Markers | `u8 kind (image, table, code), u32 wordStart, u32 wordEnd, u32 offsetStart, u32 offsetEnd` | 17 B |
| Page table | **None.** The section cache's visible-offset LUT (4 B/page, already written by upstream) is the page table. | 0 B |

Flags (1 byte): `SENT_END`, `PARA_END`, `LINE_END`, `NONPROSE`, `NOTEREF`, `HEADING`, `BYTE_SPLIT` (section 5).

Measured with the prototype's size model (`estimateFormatSize`, `test/spike_word_stream/WordStreamSpike.cpp:385-`):

| Chapter | Words | File | Per word | Blocks (largest) |
| --- | --- | --- | --- | --- |
| `fixture_s1.xhtml` | 84 | 1,233 B | 14.7 B | 2 (760 B) |
| `test_reader_menu_progress.epub` chapter 1 (21 KB XHTML) | 3,051 | 40,717 B | 13.3 B | 48 (867 B) |

A typical 5,000-word chapter is therefore about 67 KB. An 80,000-word book is about 1.1 MB on the card. The 13.3 B/word figure is measured on that chapter's English prose; other books will differ somewhat.

Writing uses the upstream commit pattern: blocks are streamed to `<file>.tmp` as the parse goes, the directory, anchors and markers are appended at the end, the header and commit byte are written last, then the file is renamed. This is the same pattern as `Section::commitBuildFile` (`Section.cpp:671-753`) and `ProgressFile::writeAtomic`.

### 4.4 Layout changes and rebuild rules

- Font size, margins, spacing, alignment, hyphenation, Focus Reading, embedded style, image mode and orientation change only the section cache (`Section.cpp:229-240`). Word indexes do not change. Saved (chapter, word) positions convert to the new page through the new LUT, exactly as upstream's own offset-based restore does (`EpubReaderActivity.cpp:2089-2118`).
- Rebuild the `.wix` file when it is missing or incomplete (no commit byte), when `formatVersion` or `rulesVersion` differs (bump `rulesVersion` whenever a word rule changes, because indexes then shift), or when `sourceBytes` differs from the chapter's inflated size (`Epub::getItemSize`, `Epub.h:88`). It is deleted with the book cache (`Epub.cpp:729-742`).
- Build lazily: on the first RSVP use of a chapter, or when a prepared book's checkpoints need the anchors of the chapter being read. It is one streaming pass, and the cost on the device is not measured. The whole prototype test, which parses a 21 KB chapter and lays it out twice, runs in about 5 ms on the host.

### 4.5 RAM estimate (not measured on the device)

- **Build**:
  - expat parser and its 1 KB input buffer: same as the existing parser, size not measured;
  - one open block, at most about 2.5 KB (867 B was the largest measured);
  - current word, 256 B;
  - open-element stack: 1 byte per element, bounded (for example 64 deep; deeper elements are treated as plain);
  - anchors and markers: tens of bytes each;
  - directory: 12 B per block, about 0.6 KB per 3,000 words. For a giant single-spine book it goes to a second temp file instead of RAM.

  Expected peak: under about 12 KB plus expat.
- **RSVP playback**: header, current and next block (at most 5 KB, typically under 2 KB), and the current chapter's anchors and markers (under 1 KB for prose chapters). The directory stays on SD.
- **Page mapping**: one directory binary search (about 12 small reads) plus one block read per lookup. Lookups happen only at mode switches and position saves; the page-turn checkpoint test uses offsets only.

### 4.6 Upstream edits

**For the word stream and page mapping (the subject of S1): none.** No change to `lib/Epub` (parser, `Section`, `ParsedText`, file formats).

The whole feature, for the tasks that follow, needs these registration points. None is made in this spike.

| # | File, function | Why | Task |
| --- | --- | --- | --- |
| 1 | `platformio.ini`, `[env:picco*]` and the simulator env `build_flags` | One feature flag; other devices unaffected. `lib/WordIndex` is compiled only where included. | F4 |
| 2 | `src/activities/reader/EpubReaderActivity.cpp`, `pageTurn()` (`:1511-1552`) | On a forward turn, ask ReadingSession whether the page being left holds an armed end anchor, and show the checkpoint instead of turning. This is the only place where "ordinary forward reading" in page view is distinguishable from jumps (`skipPages` `:1554`, `applyProgressChange` `:1160`, `navigateToHref` `:3310`, percent jump `:1120`). | C2 |
| 3 | `EpubReaderMenuActivity` (`MenuAction`) and `EpubReaderActivity::onReaderMenuConfirm` (`:1218`) | Menu entry "RSVP". | R4 |
| 4 | `src/activities/reader/ReaderActivity.*` | Route to and from `RsvpActivity` / `CheckpointActivity`. | R4, C3 |
| 5 | `CrossPointSettings` / `SettingsList` | Global RSVP defaults (plan section 4, "Where data is stored"). | R4 |
| (6) | `test/CMakeLists.txt`: one `add_subdirectory` line | Host tests for `lib/WordIndex`. The spike avoided it by building standalone. | R3 |

Position hand-off between modes needs no further edit if it goes through `progress.bin` (section 4.1). A small setter on `EpubReaderActivity` taking an initial offset would be cleaner and would count as edit 7.

### 4.7 Rejected alternative: a hook inside the parser

This would add an observer to `ChapterHtmlSlimParser` and call it from:

- `flushPartWordBuffer` (`:688-763`) for words;
- the `id` branch (`:1387-1409`) for anchors;
- `startElement` image, table and `pre` branches for markers;
- `emitCurrentPage` (`:349-360`) for page → word;

and install it in `Section::startBuild` (`Section.cpp:468-478`). That is two upstream files and about six call sites in the busiest code of the firmware.

It would still need its own word rules, because the parser's tokens depend on settings (section 3). The word stream would exist only after a full, non-partial layout build. It would be re-emitted on every relayout. It would also depend on `embeddedStyle` for `display:none`. All of this buys nothing the offset coordinate does not already give.

### 4.8 The plan's fallback

Section 13 of the plan offers "a separate, simpler word pass with approximate page mapping at paragraph level" (`docs/PLAN.md:614`).

- The recommendation keeps the first half (a separate pass).
- It replaces the second half with an exact, codepoint-level mapping through the visible-offset LUT that upstream added for KOReader sync (`docs/file-formats.md:368-369`).
- The paragraph LUT that a paragraph-level fallback would use counts only `<p>` elements (`ChapterHtmlSlimParser.cpp:1368-1373`; `Section.h:151-158`). It would misplace headings, lists and `div` text, and checkpoints would fire up to a page early or late.

The fallback is not needed.

## 5. What a word is

This is the contract between `lib/WordIndex` and the Python preparation tool. Only the word sequence (texts, in order) must match. Flags are firmware-defined and listed for task R3's expected-output file. The prototype implements these rules (`test/spike_word_stream/WordStreamSpike.cpp`), and `fixture_s1.xhtml` with its expected list in `SpikeWordStreamTest.cpp:274-` is a worked example.

**Input text**

- The chapter XHTML parsed as XML. Character data counts only inside `<body>`, excluding descendants of `head`, `style`, `script`, `title` and `rp`.
- Entities: XML entities and numeric references as XML defines them. Named HTML entities that expat hands over undeclared, for example `&nbsp;` under an XHTML 1.1 doctype, expand through the firmware's table (`lib/Epub/Epub/htmlEntities.cpp`). Unknown names stay literal (`&foo;`). With `<!DOCTYPE html>` an undeclared named entity is an expat error for the renderer and WordIndex alike.

**Text that is in the text but in no word** (it still counts in offsets and never splits a word):

1. content of an element with a `hidden` attribute, `role="doc-pagebreak"`, `epub:type="pagebreak"` (exact values, as the renderer tests them), or `<rt>`;
2. footnote reference marks. These are text inside an `<a>` whose `epub:type` has the token `noteref` or whose `role` has the token `doc-noteref`, or text that is inside both a `<sup>` and an `<a href>` whose href contains `#` and is not `http:`, `https:`, `mailto:`, `ftp:`, `tel:` or `javascript:`, in either nesting order. The word the mark sits in or follows gets the `NOTEREF` flag. So `recall.<sup><a href="#fn1">1</a></sup>` gives the single word `recall.`.

CSS, including `display:none` and inline `style`, is ignored on purpose (section 6).

**Word boundaries**

- **Separators**: U+0009, U+000A, U+000C, U+000D, U+0020, U+00A0, U+1680, U+2000–U+200B, U+2028, U+2029, U+202F, U+205F, U+3000. `Mr.&nbsp;Darcy` is two words.
- **Element boundaries** (start and end tag):
  - `p li div br blockquote ul ol pre figure h1–h6 table tr td th hr img image`, which are the elements where the renderer itself breaks;
  - `section article aside nav header footer main figcaption caption dl dt dd address hgroup details summary thead tbody tfoot body html`.
  
  Any other tag (`b`, `i`, `em`, `span`, `a`, `sup`, `small`, ...) does not split: `<b>bold</b>face` is `boldface`.
- **Dashes**: a boundary falls after U+2014 (em dash) or U+2013 (en dash) when another non-separator follows. `forgotten—which` is `forgotten—` and `which`. Hyphen-minus and U+2010 do not split: `well-known` is one word.

**Inside a word**

- U+00AD (soft hyphen) and U+FEFF are removed and never split. `hy&#173;phens` is `hyphens`.
- Text is NFC-composed (`utf8ComposeNfc`; Python `unicodedata.normalize("NFC", ...)`).
- A word longer than 255 UTF-8 bytes is cut at the last codepoint boundary before 255 bytes, and the rest continues as the next word (flag `BYTE_SPLIT`).
- Punctuation-only tokens are words (`—`, `{`, `<`). Hyphenated line breaks are invisible here because they are layout.

**Flags** (firmware only)

- `PARA_END`: last word before the start or end of any boundary element except `br`, `img` and `image`, and the chapter's last word.
- `LINE_END`: last word before `<br>`.
- `NONPROSE`: the word is inside an outermost `<table>` or `<pre>`.
- `HEADING`: the word is inside `h1`–`h6`.
- `SENT_END`: after stripping trailing `" ' ) ] } ’ ” » ›`, the word ends in `.`, `!`, `?` or `…`, **and** the word is `PARA_END`/`LINE_END` or the next word starts (after `" ' ( [ { ‘ “ « ‹`) with A–Z, 0–9 or Latin-1 capitals, **and**, for `.` only, it is not an abbreviation. Abbreviations are a single letter followed by `.`, or one of: mr. mrs. ms. dr. st. jr. sr. prof. rev. gen. col. capt. lt. sgt. mt. no. vs. e.g. i.e. cf. fig. vol. pp. ch. (case-insensitive). Commas, semicolons and colons are left to RsvpCore's pacing table, which reads them from the text.

**Anchors and markers**

- **Anchor**: any element in the visible body whose `id` starts with `rsvp-`. Its word index is the number of words that started before its start tag. `here.<span id="rsvp-s0001-end"></span>` gives the index after `here.`, and `<span id="rsvp-s0002-start"></span>A` gives the index of `A`. Section `[start, end)` is `[index(start anchor), index(end anchor))`. An anchor strictly inside a word is a preparation error.
- **Markers**:
  - `IMAGE`: an `img` or `image` outside any table or `pre` region, at the index of the next word, zero words long. Alt text is never a word.
  - `TABLE`: the outermost `<table>`.
  - `CODE`: the outermost `<pre>`.
  
  Region markers span `[wordStart, wordEnd)`. Inline `<code>` is prose.

## 6. What stays hard or uncertain

| Topic | Status | Meaning for RSVP and checkpoints in English v1 |
| --- | --- | --- |
| Offset-rule drift between WordIndex and the renderer | The only real coupling. Verified identical on the two prototype inputs. Upstream's own KOReader resolver lives with the same duplication (`ChapterXPathResolver.h:22-35`). | If the rules diverge, page mapping drifts by the difference. R3 must port the prototype's token-by-token parity test into the host suite. The base is pinned, so upstream cannot change the rules underneath us in v1. |
| Hyphenated line breaks; a word split across pages | Handled and **verified**: in layout B, 2 of the chapter's 61 pages start inside a hyphenated word (real English patterns), with 0 mapping errors. | Page → word gives the split word, so RSVP starts with the whole word. The end-anchor page uses the word's last character, so the checkpoint never fires before the end of the section's last word is visible. |
| Text after the end anchor on the same page | Inherent (plan section 7 already says so). | Unchanged. |
| CSS `display:none` | WordIndex ignores CSS; the renderer honours it only with "Book's style" on (`Section.cpp:432-448`). | RSVP may show words page view hides. Mapping stays exact because offsets count hidden text either way. The validator could warn on `display:none` inside prepared sections. |
| Images as pages | An image page starts at the `<img>` offset. **Verified** for the fixture image (layout B, page 1). | Image marker → page uses `preferFirstAtOffset`. Two images with nothing between share an offset; the tie goes to the first page. |
| Tables split across pages in grid layout | Grid rows take offsets from per-line cell offsets (`ChapterHtmlSlimParser.cpp:1260-1290`). **Not exercised across a page break.** | The table is non-prose, so RSVP pauses before it and resumes after it. An error would only move a checkpoint inside a table, which the validator should forbid. |
| Footnote and endnote bodies | `<aside epub:type="footnote">` and endnote lists are prose in the stream. | RSVP reads them at the chapter end. Decide in R5: a fourth marker kind `NOTES` (skip silently) is a small addition. |
| Noteref detection | Explicit `noteref`/`doc-noteref`, or `<sup>` plus an internal `#` link. | A bare `[1]` link without either is read as a word. This is acceptable for v1. The prep tool sees the same rule. |
| Sentence ends, dash split | English heuristics. | Affects pacing and grouping only, never positions. |
| RTL, CJK, ruby | Not tested. Logical order is kept, and offsets follow source order. | Out of scope for English v1. |
| Entities and expat differences | The host prototype links system expat 2.6.1. The firmware uses `lib/expat`. Both passes use the same library on any one build. | The Python side must expand named entities the same way. Prepared books should keep the original's entities unchanged. |
| Book file replaced at the same path | The cache key is the path hash (`Epub.cpp:28-30`). Upstream's `html/` and `sections/` have the same exposure. Not verified how upstream detects a replaced file. | The `.wix` header's `sourceBytes` catches most changes. Re-preparing a book under the same file name should also clear its cache (`Epub::clearCache`). |
| Device cost | Not measured: build time per chapter, SD write time, expat heap. | First RSVP use of a chapter costs one extra parse. Measure in the simulator (R3) and on the device. |

## 7. Prototype

**Throwaway**, in `test/spike_word_stream/`. It is a standalone CMake project and is not wired into `test/CMakeLists.txt`, so it changes no upstream file and adds nothing to the suite. Contents:

- `WordStreamSpike.{h,cpp}`: the word pass.
- `SpikeLinkStubs.cpp`: link stubs adapted from `test/chapter_html_slim_parser/ParserLinkStubs.cpp`, with real entities and hyphenation.
- `fixture_s1.xhtml`: two sections with start and end anchors, a footnote mark, an image, a table, a code block, a page-break marker, `&nbsp;`, `&mdash;`, soft hyphens, and a word split by `<b>`.
- `SpikeWordStreamTest.cpp`: the tests.

It compiles upstream's `ChapterHtmlSlimParser`, `ParsedText`, `TextBlock`, `Page` and the real English hyphenation unmodified, against the existing host stubs in `test/chapter_html_slim_parser/stubs`.

What it does:

1. It builds the word stream, asserts the full expected word list, anchors and markers for the fixture, and prints the stream.
2. It lays the same file out with the real parser at layout A (480×800, 16 px lines) and layout B (240×300, 24 px lines, hyphenation and Focus Reading on). The page start offsets come from the parser's `completePageFn`, exactly what `Section` stores.
3. It prints page → first word, anchor → page and marker → page for each layout.
4. Parity: every token on every page must fall inside a stream word and be a substring of it. Allowed exceptions are counted: noteref and hidden text, synthetic bullets, and no-break-space tokens.
5. Round trip: for every word, `pageForOffset(start)` (a copy of `Section::getPageForVisibleTextOffset`'s rule) must equal the page where the renderer first drew that word.
6. It does the same for `OEBPS/chapter1.xhtml` of `test/epubs/test_reader_menu_progress.epub`, extracted at configure time.

What the host cannot show: real fonts (the renderer stub uses fixed 8 px glyphs, so layouts are varied by viewport and line height), image decoding (a GIF header probe stands in), and SD or heap behaviour. `Section` itself is not run because its stub `Epub` lacks the needed API, so the prototype keeps the parser's page offsets in a vector and applies `Section`'s lookup rule to it.

Command, from a clean checkout:

```sh
cmake -S test/spike_word_stream -B build/spike_word_stream
cmake --build build/spike_word_stream -j4
build/spike_word_stream/SpikeWordStreamTest
```

Output (run on 2026-11-17; long listings trimmed where marked `[...]`):

```text
[ RUN      ] SpikeWordStream.FixtureWordsAnchorsAndMarkers

== Word stream: fixture_s1.xhtml
   84 words, 4 anchors, 3 markers, 490 visible codepoints
   w0    @3    +7  "Chapter"              HEAD
   w1    @11   +3  "One"                  PARA,HEAD
   ANCHOR rsvp-s0001-start -> before w2 (offset 17)
   w2    @17   +7  "Spacing"
   [...]
   w8    @59   +7  "recall."              SENT,NOTEREF
   w9    @68   +2  "It"
   [...]
   w14   @93   +10 "forgotten—"
   w15   @103  +5  "which"
   w16   @109  +2  "is"
   w17   @112  +7  "useful."              SENT
   w18   @120  +3  "Mr."
   w19   @124  +5  "Darcy"
   w20   @130  +7  "agreed,"
   w21   @138  +4  "e.g."
   [...]
   w24   @150  +10 "well-known"
   w25   @161  +7  "letter."              SENT,PARA
   w26   @171  +4  "Soft"
   w27   @176  +8  "hyphens"
   [...]
   w33   @212  +8  "boldface"
   [...]
   w37   @241  +5  "tags."                SENT,PARA
   MARKER IMAGE words [38,38) offsets [249,249)
   w38   @249  +6  "Figure"
   w39   @256  +2  "1."                   SENT
   [...]
   w42   @271  +5  "time."                SENT,PARA
   MARKER TABLE words [43,48) offsets [279,301)
   w43   @279  +8  "Interval"             PARA,NONPROSE
   [...]
   w47   @298  +3  "80%"                  PARA,NONPROSE
   MARKER CODE words [48,60) offsets [304,354)
   w48   @304  +3  "for"                  NONPROSE
   [...]
   w58   @336  +16 "review(card[i]);"     NONPROSE
   w59   @353  +1  "}"                    PARA,NONPROSE
   w60   @357  +3  "The"
   [...]
   w67   @395  +5  "here."                SENT,PARA
   ANCHOR rsvp-s0001-end -> before w68 (offset 400)
   ANCHOR rsvp-s0002-start -> before w68 (offset 403)
   w68   @403  +1  "A"
   [...]
   w71   @417  +6  "begins"
   w72   @426  +4  "with"
   [...]
   w79   @459  +7  "middle."              SENT,PARA
   ANCHOR rsvp-s0002-end -> before w80 (offset 466)
   w80   @469  +2  "1."                   SENT
   w81   @472  +3  "See"
   w82   @476  +3  "the"
   w83   @480  +9  "appendix."            SENT,PARA

== Proposed word file size, fixture_s1.xhtml: 1233 bytes total (84 words, 2 blocks, max block 760 bytes; header 64, directory 24, blocks 998, anchors 96, markers 51)
[       OK ] SpikeWordStream.FixtureWordsAnchorsAndMarkers (0 ms)
[ RUN      ] SpikeWordStream.FixturePagesMapToWordsAtTwoLayouts

== Layout A 480x800 line16, fixture_s1.xhtml: 1 pages
   page 0   offset 0      -> first word w0     "Chapter"
   pages starting inside a word: 0
   anchor rsvp-s0001-start   word 2     -> page 0
   anchor rsvp-s0001-end     word 68    -> page 0
   anchor rsvp-s0002-start   word 68    -> page 0
   anchor rsvp-s0002-end     word 80    -> page 0
   marker IMAGE at word 38    -> page 0
   marker TABLE at word 43    -> page 0
   marker CODE  at word 48    -> page 0
   parity: 86 tokens matched, 1 excluded (noteref/hidden), 0 synthetic, 12 nbsp, 0 mismatched
   word -> page (Section::getPageForVisibleTextOffset rule): 84 words checked, 0 wrong, 0 not drawn

== Layout B 240x300 line24 hyph+focus, fixture_s1.xhtml: 3 pages
   page 0   offset 0      -> first word w0     "Chapter"
   page 1   offset 249    -> first word w38    "Figure"
   page 2   offset 353    -> first word w59    "}"
   pages starting inside a word: 0
   anchor rsvp-s0001-start   word 2     -> page 0
   anchor rsvp-s0001-end     word 68    -> page 2
   anchor rsvp-s0002-start   word 68    -> page 2
   anchor rsvp-s0002-end     word 80    -> page 2
   marker IMAGE at word 38    -> page 1
   marker TABLE at word 43    -> page 1
   marker CODE  at word 48    -> page 1
   parity: 113 tokens matched, 1 excluded (noteref/hidden), 0 synthetic, 14 nbsp, 0 mismatched
   word -> page (Section::getPageForVisibleTextOffset rule): 84 words checked, 0 wrong, 0 not drawn
[       OK ] SpikeWordStream.FixturePagesMapToWordsAtTwoLayouts (0 ms)
[ RUN      ] SpikeWordStream.EpubChapterPagesMapToWordsAtTwoLayouts

== Word stream: test_reader_menu_progress.epub OEBPS/chapter1.xhtml
   3051 words, 0 anchors, 0 markers, 21200 visible codepoints
   [...]

== Proposed word file size, test_reader_menu_progress.epub chapter1: 40717 bytes total (3051 words, 48 blocks, max block 867 bytes; header 64, directory 576, blocks 40077, anchors 0, markers 0)

== Layout A 480x800 line16, test_reader_menu_progress.epub chapter1: 8 pages
   page 0   offset 0      -> first word w0     "Long"
   page 1   offset 2869   -> first word w422   "total"
   [...]
   page 7   offset 20142  -> first word w2901  "battery"
   pages starting inside a word: 0
   parity: 3051 tokens matched, 0 excluded (noteref/hidden), 0 synthetic, 0 nbsp, 0 mismatched
   word -> page (Section::getPageForVisibleTextOffset rule): 3051 words checked, 0 wrong, 0 not drawn

== Layout B 240x300 line24 hyph+focus, test_reader_menu_progress.epub chapter1: 61 pages
   page 0   offset 0      -> first word w0     "Long"
   page 1   offset 346    -> first word w59    "Percent"
   page 2   offset 675    -> first word w106   "Total"
   [...]
   page 7   offset 2473   -> first word w365   "glyph"
   page 22  offset 7718   -> first word w1120  "fallback"  (page starts inside this word)
   page 48  offset 16779  -> first word w2416  "section."  (page starts inside this word)
   ... (53 pages not shown)
   pages starting inside a word: 2
   parity: 3481 tokens matched, 0 excluded (noteref/hidden), 0 synthetic, 0 nbsp, 0 mismatched
   word -> page (Section::getPageForVisibleTextOffset rule): 3051 words checked, 0 wrong, 0 not drawn
[       OK ] SpikeWordStream.EpubChapterPagesMapToWordsAtTwoLayouts (5 ms)
[  PASSED  ] 3 tests.
```

The same word indexes map to 1 and 3 fixture pages, and to 8 and 61 chapter pages. The code block in layout B is split across pages 1 and 2, so page 2 starts at word 59 (`}`), still inside the CODE region.

## 8. Proposed changes to docs/PLAN.md

These go in by separate pull request, as section 11, rule 8 asks.

1. Section 4, "Position model", replace the first two bullets with:

   > - WordIndex is built once per chapter, on first use, by its own light pass over the chapter XHTML the page renderer parses. Each word records where it sits in CrossPoint's existing content coordinate: the count of visible codepoints in the chapter body. The page renderer records the same count for every page, and reading progress already uses it. WordIndex does not depend on font or layout settings, is stored in the book's existing cache folder and can always be rebuilt (spike S1, `docs/spikes/S1-word-stream.md`).
   > - Page view maps pages and words through that coordinate: the section cache already stores each page's start offset, so a page's first word is a binary search in WordIndex, and a word's page is CrossPoint's existing offset-to-page lookup. No page table is written and no layout code is changed. A font or margin change re-lays out pages without changing any word index.

2. Section 4, "Position model", third bullet, replace with:

   > - Elements whose id starts with `rsvp-` become anchors in the stream (section 5). Images, tables (`<table>`) and code blocks (`<pre>`) become non-prose markers that pause RSVP. Footnote reference marks are left out of the word text. The exact word rules are in `docs/spikes/S1-word-stream.md`, section 5, and the preparation tool follows them.

3. Section 4, "Where data is stored", replace the row `| WordIndex and page-to-word tables | The book's folder under /.crosspoint/ | No, rebuilt on demand |` with:

   > `| WordIndex (words, anchors, non-prose markers per chapter) | rsvp/ in the book's folder under /.crosspoint/ | No, rebuilt on demand. Pages map to words through the section cache CrossPoint already keeps |`

4. Section 5, "Layout", replace the "Anchors" bullet with:

   > - Anchors: empty `<span>` elements, for example `<span id="rsvp-s0007-end"></span>`, inserted between words (never inside a word), with ids `rsvp-<section id>-start` and `rsvp-<section id>-end`. They render as nothing, and CrossPoint's own anchor map ignores ids on `<span>`.

5. Section 7, "Trigger rules", fourth bullet, replace "the page holding the end anchor" with "the page that shows the last character of the section's last word".

6. Section 8, "What the validator script checks", add a bullet:

   > - Every anchor sits at a word boundary as the firmware splits words, and no anchor is inside a table or code block.

7. Section 13, risk "Word and page mapping in CrossPoint's engine is harder than assumed":
   - set Likelihood to "Low (spike S1)";
   - replace the Mitigation with: "Spike S1 found an exact mapping through CrossPoint's visible-text offsets with no layout changes, so the paragraph-level fallback is not needed. The remaining risk is the word pass counting offsets differently from the renderer, guarded by a host parity test (R3)."

## 9. Consequences for issues R3, R4a, R5, C2

- **R3**: Build `lib/WordIndex` as a standalone expat pass with the rules in section 5 and the `.wix` format in section 4.3, with zero upstream edits. Port the prototype's token-by-token parity test (real parser, two layouts) into the host suite, plus one `add_subdirectory` line.
- **R4a**: Mode switches pass a visible offset. Page view lands on it through the reader's existing offset jump, and RSVP starts at the first word whose end is past the page's saved offset. No layout data or page table is involved.
- **R5**: Pauses come from WordIndex markers (`IMAGE` at a word index; `TABLE` and `CODE` as word ranges). Footnote marks are already absent from the word text (flag `NOTEREF`). Decide whether footnote and endnote bodies get a skip marker.
- **C2**: Page-view triggers need one hook in `EpubReaderActivity::pageTurn()`. That hook compares the end anchor's last-word offset with the start offsets of the page being left and the next page. RSVP triggers are word-index comparisons. Jumps never pass through `pageTurn()`.
