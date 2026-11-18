#!/usr/bin/env python3
"""Generate the synthetic fixture book under ``fixtures/books/``.

    python3 tools/prep/fixture/generate.py            # write the files
    python3 tools/prep/fixture/generate.py --check    # exit 1 if the committed files are stale
    python3 tools/prep/fixture/generate.py --out DIR  # write (or check) somewhere else

Outputs, all byte-for-byte reproducible (standard library only, no
compression library involved, fixed timestamps, ids and file order):

- ``fixture.epub``           EPUB 3 with a nav document and an NCX
- ``fixture.draft.json``     the draft a drafter session would produce
                             (schema: tools/prep/schema/draft.schema.json)
- ``fixture.expected.json``  ground truth word streams for other layers
- ``README.md``              inventory and word counts

The book's text, the draft and the deliberate tokenisation cases live in
``content.py``.

Words follow the firmware's definition (docs/spikes/S1-word-stream.md
section 5, plus NOTE regions), implemented once for the tools in
``tools/prep/words.py``. The expected data is NOT produced by that word pass:
``build_model`` derives it from the content model below, with its own small
text splitter (``source_words``), because it knows what markup it writes.
``generate_files`` then runs the word pass on every XHTML document it wrote
and refuses to write anything if the two routes disagree;
``tests/test_fixture.py`` repeats the comparison through ``prep extract`` and
``prep build``.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
import tempfile
import unicodedata
import zlib
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parents[3]
if __package__ in (None, ""):
    sys.path.insert(0, str(REPO))

from tools.prep import refs  # noqa: E402
from tools.prep import words as word_pass  # noqa: E402
from tools.prep.fixture import content as C  # noqa: E402
from tools.prep.xmltree import parse as parse_xml  # noqa: E402

DEFAULT_OUT = REPO / "fixtures" / "books"
EPUB_NAME = "fixture.epub"
DRAFT_NAME = "fixture.draft.json"
EXPECTED_NAME = "fixture.expected.json"
README_NAME = "README.md"
OUTPUT_NAMES = (EPUB_NAME, DRAFT_NAME, EXPECTED_NAME, README_NAME)

OPF_DIR = "OEBPS"
PROSE_ELEMENTS = {"h1": "heading", "h2": "heading", "p": "paragraph"}
NON_PROSE_KINDS = {"figure": "image", "table": "table", "pre": "code"}
PROMPT_MAX_CHARS = 300  # docs/PLAN.md section 5, rule 5
ANSWER_MAX_CHARS = 600

# Flag names and marker kinds of the expected file: the firmware's (docs/spikes/S1-word-stream.md section 4.3),
# without SENT_END, which only the firmware decides.
FLAG_NAMES = ("PARA_END", "LINE_END", "NONPROSE", "NOTEREF", "HEADING", "BYTE_SPLIT")
RULES_VERSION = 1


# ---------------------------------------------------------------------------
# What fixture.expected.json means

RULES = {
    "source": "Words follow docs/spikes/S1-word-stream.md section 5 ('What a word is'), the contract between "
    "lib/WordIndex and the preparation tools, plus one decision beyond it: footnote and endnote bodies are NOTE "
    "regions. tools/prep/words.py implements the rules; this file is derived independently from the generator's "
    "content model and cross-checked against that word pass.",
    "rules_version": "rules_version counts changes to these word rules. Any change that can move a word index "
    "raises it, and the firmware's WordIndex rulesVersion should carry the same number.",
    "words": "Each spine document's 'words' is its whole word stream in order: every word in <body>, including "
    "headings, figure captions, table cells and captions, code in pre, and footnote bodies. Separators are U+0009, "
    "U+000A, U+000C, U+000D, U+0020, U+00A0, U+1680, U+2000-U+200B, U+2028, U+2029, U+202F, U+205F and U+3000. The "
    "start and end tags of block elements (p, h1-h6, figure, figcaption, table, caption, tr, th, td, pre, aside, "
    "...) and br and img end a word; inline elements (em, strong, a, sup, span) do not. A word also ends after an "
    "em dash or en dash that is followed by another non-separator. Soft hyphens and U+FEFF are removed, text is "
    "NFC-composed, and a word longer than 255 UTF-8 bytes is cut (BYTE_SPLIT). Punctuation stays attached.",
    "text_in_no_word": "Footnote reference marks (an a with epub:type noteref or role doc-noteref, or text inside "
    "both a sup and an internal '#' link, in either nesting order) and page-break markers are text in no word; "
    "they never split a word. The word a mark follows carries NOTEREF. Image alt text is never a word.",
    "indexing": "Word indexes are 0-based over one document's word stream. Every range is [start, end): start is "
    "the index of the first word inside, end is one past the last.",
    "flags": "'flags' maps each flag to the [start, end) runs of word indexes that carry it. PARA_END: last word "
    "before the start or end of a block element other than br and img, and the document's last word. LINE_END: "
    "last word before a br. NONPROSE: inside the outermost table or pre. HEADING: inside h1-h6. NOTEREF: see "
    "text_in_no_word. BYTE_SPLIT: cut at 255 bytes. There is no sentence-end flag: the firmware (lib/RsvpCore) "
    "decides sentence ends.",
    "markers": "'markers' in document order: IMAGE (an img outside any table or pre, zero words long, at the index "
    "of the next word), TABLE (outermost table), CODE (outermost pre) and NOTE (a footnote or endnote body: "
    "epub:type footnote, endnote or endnotes, or role doc-footnote, doc-endnote or doc-endnotes; RSVP passes over "
    "it, page view shows it), each as [start, end).",
    "blocks": "'blocks' lists every block prep extract numbers, in document order: 'paragraph' is the reference a "
    "draft cites (tools/prep/refs.py: 'd' and the 1-based spine position, '-p' and the block number; footnote "
    "paragraphs '-n' and their own count), 'id' the XHTML id, 'kind' as in structure.json, and 'first_word' and "
    "'word_count' the block's words [first_word, first_word + word_count).",
    "noterefs": "'noterefs' lists every footnote reference mark with its text ('mark'), its markup form and "
    "'word', the index of the word that carries its NOTEREF flag.",
    "sections": "A section's start anchor (<span id='rsvp-<id>-start'>) sits at word index 'start' (the number of "
    "words started before it) and its end anchor at 'end'; the section's words are [start, end) and 'word_count' "
    "is end - start, the number prep build writes to checkpoints.json. Footnote bodies that follow a chapter's "
    "last section are outside every section.",
}


# ---------------------------------------------------------------------------
# Inline markup of content.py

INLINE_RE = re.compile(
    r"\*\*(?P<strong>[^*]+)\*\*|\*(?P<em>[^*]+)\*|\[\^(?P<note>[A-Za-z0-9_-]+)\]|\[(?P<br>br)\]|\[pb:(?P<pb>[0-9]+)\]"
)
# Written as numeric character references so the XHTML shows where they are.
_REFERENCED = {" ", "­", "﻿"}


def parse_inline(source: str) -> list[tuple[str, str]]:
    """Split content markup into ("text"|"em"|"strong"|"note"|"br"|"pb", value) segments."""
    segments: list[tuple[str, str]] = []
    pos = 0
    for m in INLINE_RE.finditer(source):
        if m.start() > pos:
            segments.append(("text", source[pos : m.start()]))
        kind = m.lastgroup
        segments.append((kind, m.group(kind)))
        pos = m.end()
    if pos < len(source):
        segments.append(("text", source[pos:]))
    for kind, value in segments:
        if kind == "text" and ("*" in value or "[^" in value or "[br]" in value or "[pb:" in value):
            raise ValueError(f"stray inline markup in {source!r}")
    return segments


def esc(text: str) -> str:
    text = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    return "".join(f"&#{ord(ch)};" if ch in _REFERENCED or unicodedata.combining(ch) else ch for ch in text)


def esc_attr(text: str) -> str:
    return esc(text).replace('"', "&quot;")


# ---------------------------------------------------------------------------
# The generator's own text splitter (deliberately not tools/prep/words.py)

_SEPARATOR_RUN = re.compile("[\t\n\f\r    -​    　]+")
_DASH_PIECE = re.compile("[^–—]*[–—]|[^–—]+")


def source_words(text: str) -> list[str]:
    """The words of a plain run of content text (section 5's character rules, written as regular expressions)."""
    text = text.replace("­", "").replace("﻿", "")
    out = []
    for token in _SEPARATOR_RUN.split(text):
        for piece in _DASH_PIECE.findall(token):
            word = unicodedata.normalize("NFC", piece)
            if len(word.encode("utf-8")) > word_pass.MAX_WORD_BYTES:
                raise ValueError(f"word longer than {word_pass.MAX_WORD_BYTES} bytes in the content: {word[:20]}...")
            out.append(word)
    return out


# ---------------------------------------------------------------------------
# Model: documents, blocks, words


def _documents() -> list[dict[str, Any]]:
    return [C.FRONT_MATTER, *C.CHAPTERS]


class _Stream:
    """A document's expected words, per-word flag sets and markers, appended in document order."""

    def __init__(self, doc: dict[str, Any]):
        self.words: list[str] = doc["words"]
        self.flags: list[set[str]] = doc["word_flags"]
        self.markers: list[dict[str, Any]] = doc["markers"]

    def __len__(self) -> int:
        return len(self.words)

    def add(self, words: list[str], *flags: str) -> None:
        for word in words:
            self.words.append(word)
            self.flags.append(set(flags))

    def flag_last(self, flag: str) -> None:
        if self.flags:
            self.flags[-1].add(flag)

    def open(self, kind: str) -> dict[str, Any]:
        marker = {"kind": kind, "start": len(self), "end": len(self)}
        self.markers.append(marker)
        return marker

    def close(self, marker: dict[str, Any]) -> None:
        marker["end"] = len(self)


def build_model() -> list[dict[str, Any]]:
    """Number blocks, assign footnote marks and compute every document's expected word stream."""
    note_numbers: dict[str, int] = {}
    docs = []
    sources = _documents()
    for spine_index, src in enumerate(sources, start=1):
        doc: dict[str, Any] = {
            "spine_index": spine_index,
            "href": src["href"],
            "role": src["role"],
            "prefix": src["prefix"],
            "title": src["title"],
            "blocks": [],
            "note_blocks": [],
            "words": [],
            "word_flags": [],
            "markers": [],
            "noterefs": [],
            "inline": [],
            "page_breaks": [],
            "notes": [],
            "footnotes": [],
        }
        stream = _Stream(doc)
        for number, (element, payload) in enumerate(src["blocks"], start=1):
            block_id = f"{src['prefix']}-p{number:03d}"
            paragraph = refs.paragraph_ref(spine_index, len(sources), number, len(src["blocks"]))
            first = len(stream)
            block: dict[str, Any] = {"id": block_id, "paragraph": paragraph, "element": element}
            if element in PROSE_ELEMENTS:
                segments = parse_inline(payload)
                block.update(kind=PROSE_ELEMENTS[element], segments=segments)
                if element.startswith("h"):
                    block["level"] = int(element[1:])
                _prose_words(doc, stream, block, note_numbers)
                if len(stream) == first:
                    raise ValueError(f"{block_id}: prose block without words")
            elif element in NON_PROSE_KINDS:
                block.update(kind=NON_PROSE_KINDS[element], payload=payload)
                _non_prose_words(stream, element, payload, block_id)
            else:
                raise ValueError(f"unknown block element {element!r}")
            block.update(first_word=first, word_count=len(stream) - first)
            doc["blocks"].append(block)
        note_index = 0
        note_total = sum(len(C.FOOTNOTES[n]) for n in doc["notes"])
        for note_id in doc["notes"]:
            region = stream.open("NOTE")
            paragraphs = []
            for text in C.FOOTNOTES[note_id]:
                note_index += 1
                first = len(stream)
                stream.add(source_words(text))
                stream.flag_last("PARA_END")
                block = {
                    "id": f"{src['prefix']}-n{note_index:03d}",
                    "paragraph": refs.paragraph_ref(spine_index, len(sources), note_index, note_total, refs.NOTE),
                    "element": "p",
                    "kind": "footnote",
                    "note": note_id,
                    "first_word": first,
                    "word_count": len(stream) - first,
                }
                doc["note_blocks"].append(block)
                paragraphs.append(block["paragraph"])
            stream.close(region)
            doc["footnotes"].append({"id": note_id, "mark": str(note_numbers[note_id]), "start": region["start"],
                                     "end": region["end"], "paragraphs": paragraphs})
        docs.append(doc)
    unused = set(C.FOOTNOTES) - set(note_numbers)
    if unused:
        raise ValueError(f"footnotes never referenced: {sorted(unused)}")
    for doc in docs:
        doc["note_numbers"] = {n: note_numbers[n] for n in doc["notes"]}
    return docs


def _lines(segments, block_id):
    """Split a prose block at its line breaks.

    Returns (lines of visible text, [(kind, value, line, offset)] for marks and
    page breaks, [(element, line, start, end)] for em/strong). Marks and page
    breaks contribute no characters.
    """
    lines = [""]
    points = []
    spans = []
    for kind, value in segments:
        text = lines[-1]
        if kind == "br":
            if not text or text.endswith(" "):
                raise ValueError(f"{block_id}: a line break must follow a word")
            lines.append("")
            continue
        if kind in ("note", "pb"):
            if kind == "note" and (not text or text[-1] == " "):
                raise ValueError(f"{block_id}: a note mark must follow a word")
            points.append((kind, value, len(lines) - 1, len(text)))
            continue
        if points and points[-1][0] == "note" and points[-1][2] == len(lines) - 1 and points[-1][3] == len(text) \
                and value[:1] not in ("", " "):
            raise ValueError(f"{block_id}: a note mark must be followed by a space, a line break or the block end")
        if kind in ("em", "strong"):
            spans.append((kind, len(lines) - 1, len(text), len(text) + len(value)))
        lines[-1] = text + value
    for line in lines:
        if "  " in line or line != line.strip(" ") or not line:
            raise ValueError(f"{block_id}: prose must use single spaces and no empty lines")
    return lines, points, spans


def _prose_words(doc, stream: _Stream, block, note_numbers) -> None:
    block_id = block["id"]
    lines, points, spans = _lines(block["segments"], block_id)
    heading = ("HEADING",) if block["element"].startswith("h") else ()
    bases = []
    for n, line in enumerate(lines):
        bases.append(len(stream))
        stream.add(source_words(line), *heading)
        if n < len(lines) - 1:
            stream.flag_last("LINE_END")
    stream.flag_last("PARA_END")

    def word_at(line: int, offset: int) -> int:
        """The word that holds the character just before ``offset`` of ``line``."""
        return bases[line] + len(source_words(lines[line][:offset])) - 1

    for kind, value, line, offset in points:
        if kind == "pb":
            doc["page_breaks"].append({"id": f"page{value}", "text": value, "block": block_id,
                                       "word": word_at(line, offset)})
            continue
        if value not in C.FOOTNOTES:
            raise ValueError(f"{block_id}: unknown footnote {value!r}")
        if value in note_numbers:
            raise ValueError(f"{block_id}: footnote {value!r} referenced twice")
        note_numbers[value] = len(note_numbers) + 1
        word = word_at(line, offset)
        stream.flags[word].add("NOTEREF")
        doc["noterefs"].append({"note": value, "mark": str(note_numbers[value]), "form": C.NOTE_FORMS[value],
                                "block": block_id, "word": word})
        doc["notes"].append(value)
    for element, line, start, end in spans:
        inside = source_words(lines[line][start:end])
        if inside != [lines[line][start:end]]:
            raise ValueError(f"{block_id}: <{element}> must hold exactly one word")
        doc["inline"].append({"element": element, "block": block_id, "text": inside[0],
                              "word": word_at(line, start + 1)})


def _non_prose_words(stream: _Stream, element: str, payload, block_id: str) -> None:
    if element == "figure":
        stream.open("IMAGE")
        stream.add(source_words(payload["caption"]))
        stream.flag_last("PARA_END")
        return
    if element == "table":
        region = stream.open("TABLE")
        for cell in [payload["caption"], *payload["header"], *(c for row in payload["rows"] for c in row)]:
            words = source_words(cell)
            if not words:
                raise ValueError(f"{block_id}: empty table cell")
            stream.add(words, "NONPROSE")
            stream.flag_last("PARA_END")
        stream.close(region)
        return
    region = stream.open("CODE")
    stream.add(source_words("\n".join(payload)), "NONPROSE")
    stream.flag_last("PARA_END")
    stream.close(region)


def flag_ranges(word_flags: list[set[str]]) -> dict[str, list[list[int]]]:
    """Each flag name mapped to the [start, end) runs of word indexes that carry it."""
    out: dict[str, list[list[int]]] = {name: [] for name in FLAG_NAMES}
    for i, flags in enumerate(word_flags):
        for name in flags:
            runs = out[name]
            if runs and runs[-1][1] == i:
                runs[-1][1] = i + 1
            else:
                runs.append([i, i + 1])
    return out


# ---------------------------------------------------------------------------
# XHTML, OPF, nav, NCX

XHTML_HEAD = (
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    "<!DOCTYPE html>\n"
    '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" '
    f'xml:lang="{C.LANGUAGE}" lang="{C.LANGUAGE}">\n'
)


def _css_href(doc_href: str) -> str:
    return "../" * doc_href.count("/") + "style.css"


def _note_mark(note_id: str, number: int) -> str:
    form = C.NOTE_FORMS[note_id]
    if form == "noteref":
        return f'<sup><a epub:type="noteref" role="doc-noteref" href="#{note_id}">{number}</a></sup>'
    if form == "sup-link":
        return f'<sup><a href="#{note_id}">{number}</a></sup>'
    if form == "link-sup":
        return f'<a href="#{note_id}"><sup>{number}</sup></a>'
    raise ValueError(f"unknown footnote mark form {form!r}")


def _inline_xhtml(segments, note_numbers) -> str:
    out = []
    for kind, value in segments:
        if kind == "text":
            out.append(esc(value))
        elif kind in ("em", "strong"):
            out.append(f"<{kind}>{esc(value)}</{kind}>")
        elif kind == "br":
            out.append("<br/>")
        elif kind == "pb":
            out.append(f'<span epub:type="pagebreak" role="doc-pagebreak" id="page{value}" '
                       f'aria-label="{value}">{value}</span>')
        else:
            out.append(_note_mark(value, note_numbers[value]))
    return "".join(out)


def render_document(doc: dict[str, Any]) -> str:
    lines = [
        XHTML_HEAD.rstrip("\n"),
        "<head>",
        f"<title>{esc(doc['title'])}</title>",
        f'<link rel="stylesheet" type="text/css" href="{_css_href(doc["href"])}"/>',
        "</head>",
        "<body>",
    ]
    for block in doc["blocks"]:
        element, block_id = block["element"], block["id"]
        if element in PROSE_ELEMENTS:
            inner = _inline_xhtml(block["segments"], doc["note_numbers"])
            lines.append(f'<{element} id="{block_id}">{inner}</{element}>')
        elif element == "figure":
            p = block["payload"]
            src = "../" * doc["href"].count("/") + p["src"]
            lines += [
                f'<figure id="{block_id}">',
                f'<img src="{esc_attr(src)}" alt="{esc_attr(p["alt"])}"/>',
                f"<figcaption>{esc(p['caption'])}</figcaption>",
                "</figure>",
            ]
        elif element == "table":
            p = block["payload"]
            lines += [f'<table id="{block_id}">', f"<caption>{esc(p['caption'])}</caption>", "<thead>"]
            lines.append("<tr>" + "".join(f'<th scope="col">{esc(c)}</th>' for c in p["header"]) + "</tr>")
            lines += ["</thead>", "<tbody>"]
            for row in p["rows"]:
                lines.append("<tr>" + "".join(f"<td>{esc(c)}</td>" for c in row) + "</tr>")
            lines += ["</tbody>", "</table>"]
        elif element == "pre":
            code = "\n".join(esc(line) for line in block["payload"])
            lines.append(f'<pre id="{block_id}"><code>{code}</code></pre>')
    note_index = 0
    for note_id in doc["notes"]:
        lines.append(f'<aside epub:type="footnote" role="doc-footnote" id="{note_id}">')
        for paragraph in C.FOOTNOTES[note_id]:
            note_index += 1
            lines.append(f'<p id="{doc["prefix"]}-n{note_index:03d}">{esc(paragraph)}</p>')
        lines.append("</aside>")
    lines += ["</body>", "</html>", ""]
    return "\n".join(lines)


def _manifest_id(href: str) -> str:
    return href.rsplit("/", 1)[-1].rsplit(".", 1)[0]


def render_opf(docs) -> str:
    items = [
        '<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>',
        '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>',
        '<item id="css" href="style.css" media-type="text/css"/>',
    ]
    items += [
        f'<item id="{_manifest_id(d["href"])}" href="{d["href"]}" media-type="application/xhtml+xml"/>' for d in docs
    ]
    items.append(f'<item id="{_manifest_id(C.IMAGE_HREF)}" href="{C.IMAGE_HREF}" media-type="image/png"/>')
    spine = [f'<itemref idref="{_manifest_id(d["href"])}"/>' for d in docs]
    return "\n".join(
        [
            '<?xml version="1.0" encoding="UTF-8"?>',
            f'<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="book-id" '
            f'xml:lang="{C.LANGUAGE}">',
            '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">',
            f'<dc:identifier id="book-id">{C.IDENTIFIER}</dc:identifier>',
            f"<dc:title>{esc(C.TITLE)}</dc:title>",
            f"<dc:creator>{esc(C.AUTHOR)}</dc:creator>",
            f"<dc:language>{C.LANGUAGE}</dc:language>",
            f'<meta property="dcterms:modified">{C.MODIFIED}</meta>',
            "</metadata>",
            "<manifest>",
            *items,
            "</manifest>",
            '<spine toc="ncx">',
            *spine,
            "</spine>",
            "</package>",
            "",
        ]
    )


def render_nav(docs) -> str:
    chapters = [d for d in docs if d["role"] == "chapter"]
    toc = [f'<li><a href="{d["href"]}">{esc(d["title"])}</a></li>' for d in chapters]
    return "\n".join(
        [
            XHTML_HEAD.rstrip("\n"),
            "<head>",
            "<title>Contents</title>",
            "</head>",
            "<body>",
            '<nav epub:type="toc" id="toc">',
            "<h1>Contents</h1>",
            "<ol>",
            *toc,
            "</ol>",
            "</nav>",
            '<nav epub:type="landmarks" id="landmarks" hidden="hidden">',
            "<h2>Landmarks</h2>",
            "<ol>",
            f'<li><a epub:type="titlepage" href="{docs[0]["href"]}">Title page</a></li>',
            f'<li><a epub:type="bodymatter" href="{chapters[0]["href"]}">Start of content</a></li>',
            "</ol>",
            "</nav>",
            "</body>",
            "</html>",
            "",
        ]
    )


def render_ncx(docs) -> str:
    chapters = [d for d in docs if d["role"] == "chapter"]
    points = []
    for order, d in enumerate(chapters, start=1):
        points += [
            f'<navPoint id="nav-{order}" playOrder="{order}">',
            f"<navLabel><text>{esc(d['title'])}</text></navLabel>",
            f'<content src="{d["href"]}"/>',
            "</navPoint>",
        ]
    return "\n".join(
        [
            '<?xml version="1.0" encoding="UTF-8"?>',
            f'<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1" xml:lang="{C.LANGUAGE}">',
            "<head>",
            f'<meta name="dtb:uid" content="{C.IDENTIFIER}"/>',
            '<meta name="dtb:depth" content="1"/>',
            '<meta name="dtb:totalPageCount" content="0"/>',
            '<meta name="dtb:maxPageNumber" content="0"/>',
            "</head>",
            f"<docTitle><text>{esc(C.TITLE)}</text></docTitle>",
            "<navMap>",
            *points,
            "</navMap>",
            "</ncx>",
            "",
        ]
    )


CONTAINER_XML = (
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">\n'
    "<rootfiles>\n"
    f'<rootfile full-path="{OPF_DIR}/content.opf" media-type="application/oebps-package+xml"/>\n'
    "</rootfiles>\n"
    "</container>\n"
)

STYLE_CSS = """body { margin: 0 0.5em; }
h1, h2 { text-align: center; }
figure { margin: 1em 0; text-align: center; }
figcaption { font-style: italic; }
table { border-collapse: collapse; margin: 1em auto; }
th, td { border: 1px solid black; padding: 0.2em 0.4em; }
pre { font-size: 0.8em; white-space: pre-wrap; }
aside { font-size: 0.9em; }
"""


# ---------------------------------------------------------------------------
# PNG (1-bit grayscale, stored zlib stream: no compressor involved)


def _zlib_stored(data: bytes) -> bytes:
    out = bytearray(b"\x78\x01")
    chunks = [data[i : i + 65535] for i in range(0, len(data), 65535)] or [b""]
    for i, chunk in enumerate(chunks):
        out.append(1 if i == len(chunks) - 1 else 0)
        out += struct.pack("<HH", len(chunk), len(chunk) ^ 0xFFFF)
        out += chunk
    out += struct.pack(">I", zlib.adler32(data) & 0xFFFFFFFF)
    return bytes(out)


def _png_chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def lighthouse_png() -> bytes:
    """A 120x160 black-and-white line drawing of a lighthouse."""
    width, height = 120, 160
    ink = [[False] * width for _ in range(height)]

    def fill_rect(x0, y0, x1, y1, value=True):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                ink[y][x] = value

    def fill_poly(points):
        for y in range(height):
            for x in range(width):
                px, py = x + 0.5, y + 0.5
                inside = False
                for (ax, ay), (bx, by) in zip(points, points[1:] + points[:1]):
                    if (ay > py) != (by > py) and px < ax + (py - ay) * (bx - ax) / (by - ay):
                        inside = not inside
                if inside:
                    ink[y][x] = True

    def line(x0, y0, x1, y1):
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx + dy
        while True:
            ink[y0][x0] = True
            if (x0, y0) == (x1, y1):
                return
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    fill_poly([(8, 160), (20, 147), (44, 140), (80, 140), (100, 146), (112, 160)])  # rock
    for y in range(56, 141):  # tower: outline plus two black bands
        t = (y - 56) / 84
        left, right = round(50 - 8 * t), round(70 + 8 * t)
        for x in range(left, right + 1):
            ink[y][x] = x - left < 2 or right - x < 2 or 76 <= y <= 88 or 106 <= y <= 118
    fill_rect(56, 126, 63, 139)  # door
    fill_rect(44, 52, 76, 55)  # gallery
    fill_rect(50, 34, 70, 51)  # lamp room
    fill_rect(52, 36, 68, 49, False)
    fill_rect(57, 39, 63, 46)  # lamp
    fill_poly([(47, 35), (73, 35), (60, 21)])  # roof
    fill_rect(59, 15, 61, 21)  # finial
    for y in (26, 42, 58):  # light rays
        line(47, 42, 4, y)
        line(73, 42, 115, y)

    raw = bytearray()
    for row in ink:
        raw.append(0)  # filter: none
        for byte_start in range(0, width, 8):
            byte = 0
            for bit in range(8):
                x = byte_start + bit
                white = x >= width or not row[x]
                byte = (byte << 1) | (1 if white else 0)
            raw.append(byte)
    ihdr = struct.pack(">IIBBBBB", width, height, 1, 0, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + _png_chunk(b"IHDR", ihdr)
        + _png_chunk(b"IDAT", _zlib_stored(bytes(raw)))
        + _png_chunk(b"IEND", b"")
    )


# ---------------------------------------------------------------------------
# ZIP (every entry stored, fixed timestamp and attributes)


def build_zip(entries: list[tuple[str, bytes]]) -> bytes:
    y, mo, d, h, mi, s = C.ZIP_DATE_TIME
    dos_date = ((y - 1980) << 9) | (mo << 5) | d
    dos_time = (h << 11) | (mi << 5) | (s // 2)
    out = bytearray()
    central = bytearray()
    for name, data in entries:
        raw_name = name.encode("ascii")
        crc = zlib.crc32(data) & 0xFFFFFFFF
        offset = len(out)
        out += struct.pack(
            "<IHHHHHIIIHH", 0x04034B50, 20, 0, 0, dos_time, dos_date, crc, len(data), len(data), len(raw_name), 0
        )
        out += raw_name + data
        central += struct.pack(
            "<IHHHHHHIIIHHHHHII",
            0x02014B50,
            (3 << 8) | 20,  # made by: Unix, spec 2.0
            20,
            0,
            0,  # stored
            dos_time,
            dos_date,
            crc,
            len(data),
            len(data),
            len(raw_name),
            0,
            0,
            0,
            0,
            0o100644 << 16,
            offset,
        )
        central += raw_name
    cd_offset = len(out)
    out += central
    out += struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, len(entries), len(entries), len(central), cd_offset, 0)
    return bytes(out)


def build_epub(docs) -> bytes:
    entries = [
        ("mimetype", b"application/epub+zip"),
        ("META-INF/container.xml", CONTAINER_XML.encode()),
        (f"{OPF_DIR}/content.opf", render_opf(docs).encode()),
        (f"{OPF_DIR}/nav.xhtml", render_nav(docs).encode()),
        (f"{OPF_DIR}/toc.ncx", render_ncx(docs).encode()),
        (f"{OPF_DIR}/style.css", STYLE_CSS.encode()),
    ]
    entries += [(f"{OPF_DIR}/{d['href']}", render_document(d).encode("utf-8")) for d in docs]
    entries.append((f"{OPF_DIR}/{C.IMAGE_HREF}", lighthouse_png()))
    return build_zip(entries)


# ---------------------------------------------------------------------------
# Draft and expected data


def _block_index(docs) -> dict[str, tuple[dict, int]]:
    index = {}
    for doc in docs:
        for i, block in enumerate(doc["blocks"]):
            index[block["id"]] = (doc, i)
    return index


def _range(docs, entry, where) -> tuple[dict, int, int]:
    index = _block_index(docs)
    for key in ("first_paragraph", "last_paragraph"):
        if entry[key] not in index:
            raise ValueError(f"{where}: unknown paragraph {entry[key]!r}")
    doc, first = index[entry["first_paragraph"]]
    doc2, last = index[entry["last_paragraph"]]
    if doc is not doc2 or doc["href"] != entry["chapter_href"]:
        raise ValueError(f"{where}: range must lie inside {entry['chapter_href']}")
    if first > last:
        raise ValueError(f"{where}: range is reversed")
    return doc, first, last


def check_draft(docs) -> None:
    """Fail loudly if content.py's draft is inconsistent with the book."""
    covered: dict[str, set[int]] = {d["href"]: set() for d in docs}
    for where, entries in (("excluded", C.EXCLUDED), ("section", C.SECTIONS)):
        for entry in entries:
            label = f"{where} {entry.get('id', entry['chapter_href'])}"
            doc, first, last = _range(docs, entry, label)
            span = set(range(first, last + 1))
            if covered[doc["href"]] & span:
                raise ValueError(f"{label}: overlaps another range")
            covered[doc["href"]] |= span
    order = [(d["href"], i) for d in docs for i in range(len(d["blocks"]))]
    starts = [order.index((s["chapter_href"], _range(docs, s, s["id"])[1])) for s in C.SECTIONS]
    if starts != sorted(starts):
        raise ValueError("sections are not in reading order")
    if len({s["id"] for s in C.SECTIONS}) != len(C.SECTIONS):
        raise ValueError("duplicate section id")
    prompt_ids = [p["id"] for cp in C.CHECKPOINTS for p in cp["prompts"]]
    if len(set(prompt_ids)) != len(prompt_ids) or len({cp["id"] for cp in C.CHECKPOINTS}) != len(C.CHECKPOINTS):
        raise ValueError("duplicate checkpoint or prompt id")
    for doc in docs:
        if covered[doc["href"]] != set(range(len(doc["blocks"]))):
            raise ValueError(f"{doc['href']}: sections and exclusions do not cover every block")
    sections = {s["id"]: s for s in C.SECTIONS}
    seen = set()
    for cp in C.CHECKPOINTS:
        if cp["section"] not in sections or cp["section"] in seen:
            raise ValueError(f"checkpoint {cp['id']}: bad or repeated section")
        seen.add(cp["section"])
        doc, first, last = _range(docs, sections[cp["section"]], cp["id"])
        ids = {b["id"] for b in doc["blocks"][first : last + 1]}
        for prompt in cp["prompts"]:
            if not set(prompt["supporting_paragraphs"]) <= ids:
                raise ValueError(f"{prompt['id']}: support outside section {cp['section']}")
            if not 1 <= len(prompt["prompt"]) <= PROMPT_MAX_CHARS or not 1 <= len(prompt["answer"]) <= ANSWER_MAX_CHARS:
                raise ValueError(f"{prompt['id']}: prompt or answer length out of limits")


def section_words(docs, section) -> dict[str, Any]:
    doc, first, last = _range(docs, section, section["id"])
    first_block, last_block = doc["blocks"][first], doc["blocks"][last]
    # prep build puts the start anchor right after the first block's start tag (or before it) and the end anchor
    # right before the last block's end tag (or after it): at the block's first word and one past its last.
    start = first_block["first_word"]
    end = last_block["first_word"] + last_block["word_count"]
    if end <= start:
        raise ValueError(f"section {section['id']}: no words")
    return {
        "id": section["id"],
        "label": section["label"],
        "chapter_href": section["chapter_href"],
        "first_paragraph": first_block["paragraph"],
        "last_paragraph": last_block["paragraph"],
        "first_block_id": section["first_paragraph"],
        "last_block_id": section["last_paragraph"],
        "start_anchor": f"rsvp-{section['id']}-start",
        "end_anchor": f"rsvp-{section['id']}-end",
        "start": start,
        "end": end,
        "word_count": end - start,
        "first_word_text": doc["words"][start],
        "last_word_text": doc["words"][end - 1],
        "ends_chapter": last == len(doc["blocks"]) - 1,
    }


def _paragraph_refs(docs) -> dict[str, str]:
    """XHTML block id -> the paragraph reference ``prep extract`` gives that block."""
    return {b["id"]: b["paragraph"] for doc in docs for b in doc["blocks"]}


def _cite_refs(entries: list[dict[str, Any]], to_ref: dict[str, str]) -> list[dict[str, Any]]:
    """content.py ranges cite block ids; the draft cites paragraph references."""
    out = []
    for entry in entries:
        entry = dict(entry)
        for key in ("first_paragraph", "last_paragraph"):
            entry[key] = to_ref[entry[key]]
        out.append(entry)
    return out


def build_draft(docs, epub_sha256: str) -> dict[str, Any]:
    to_ref = _paragraph_refs(docs)
    checkpoints = [
        {
            **cp,
            "prompts": [
                {**p, "supporting_paragraphs": [to_ref[i] for i in p["supporting_paragraphs"]]} for p in cp["prompts"]
            ],
        }
        for cp in C.CHECKPOINTS
    ]
    return {
        "format": "rsvp-draft",
        "version": 1,
        "book": {
            "title": C.TITLE,
            "language": C.LANGUAGE,
            "source_sha256": epub_sha256,
            "instructions_version": C.INSTRUCTIONS_VERSION,
        },
        "excluded": _cite_refs(C.EXCLUDED, to_ref),
        "sections": _cite_refs(C.SECTIONS, to_ref),
        "checkpoints": checkpoints,
    }


def _expected_block(block) -> dict[str, Any]:
    out = {"paragraph": block["paragraph"], "id": block["id"], "kind": block["kind"], "element": block["element"],
           "first_word": block["first_word"], "word_count": block["word_count"]}
    if "level" in block:
        out["level"] = block["level"]
    p = block.get("payload")
    if block["kind"] == "image":
        out.update(image_href=p["src"], alt=p["alt"], caption=p["caption"])
    elif block["kind"] == "table":
        out.update(caption=p["caption"], header=p["header"], rows=p["rows"])
    elif block["kind"] == "code":
        out["lines"] = p
    elif block["kind"] == "footnote":
        out["note"] = block["note"]
    return out


def expected_document(doc) -> dict[str, Any]:
    """One spine document of fixture.expected.json, from the content model."""
    return {
        "spine_index": doc["spine_index"],
        "href": doc["href"],
        "zip_path": f"{OPF_DIR}/{doc['href']}",
        "role": doc["role"],
        "title": doc["title"],
        "word_count": len(doc["words"]),
        "words": doc["words"],
        "flags": flag_ranges(doc["word_flags"]),
        "markers": doc["markers"],
        "blocks": [_expected_block(b) for b in doc["blocks"] + doc["note_blocks"]],
        "noterefs": doc["noterefs"],
        "footnotes": doc["footnotes"],
        "inline_elements": doc["inline"],
        "page_breaks": doc["page_breaks"],
    }


def build_expected(docs, epub_sha256: str) -> dict[str, Any]:
    by_id = {b["id"]: (doc, b) for doc in docs for b in doc["blocks"]}
    cases = []
    for case, block_id, token, flags in C.TOKEN_CASES:
        doc, block = by_id[block_id]
        span = range(block["first_word"], block["first_word"] + block["word_count"])
        hits = [i for i in span if doc["words"][i] == token]
        if not hits:
            raise ValueError(f"token case {case!r}: {token!r} is not a word of {block_id}")
        missing = set(flags) - doc["word_flags"][hits[0]]
        if missing:
            raise ValueError(f"token case {case!r}: {token!r} lacks {sorted(missing)}")
        cases.append({"case": case, "href": doc["href"], "block": block_id, "word_index": hits[0], "word": token,
                      "flags": sorted(doc["word_flags"][hits[0]])})
    return {
        "format": "rsvp-fixture-expected",
        "version": 2,
        "rules_version": RULES_VERSION,
        "generated_by": "tools/prep/fixture/generate.py",
        "epub": EPUB_NAME,
        "epub_sha256": epub_sha256,
        "rules": RULES,
        "documents": [expected_document(doc) for doc in docs],
        "sections": [section_words(docs, s) for s in C.SECTIONS],
        "token_cases": cases,
    }


# ---------------------------------------------------------------------------
# The second route: the word pass on the XHTML this generator wrote


def stream_view(stream: word_pass.WordStream, document) -> dict[str, Any]:
    """What the word pass says about a document, in the expected file's terms (for comparison)."""
    ids = {}
    for el in document.root.iter():
        if el.get("id") is not None:
            ids[el.get("id")] = stream.range_of(el)
    return {
        "words": stream.texts,
        "flags": stream.flag_ranges(),
        "markers": [{"kind": m.kind, "start": m.start, "end": m.end} for m in stream.markers],
        "noterefs": [(r.mark, r.word) for r in stream.noterefs],
        "ids": ids,
    }


def cross_check(docs, xhtml: dict[str, bytes]) -> list[str]:
    """Differences between the content model and the word pass run on the generated XHTML (empty when they agree)."""
    problems = []
    for doc in docs:
        document = parse_xml(xhtml[doc["href"]], doc["href"])
        got = stream_view(word_pass.word_stream(document), document)
        want = expected_document(doc)
        for key in ("words", "flags", "markers"):
            if got[key] != want[key]:
                problems.append(f"{doc['href']}: {key} differ")
        if got["noterefs"] != [(n["mark"], n["word"]) for n in doc["noterefs"]]:
            problems.append(f"{doc['href']}: footnote marks differ")
        for block in doc["blocks"] + doc["note_blocks"]:
            if got["ids"].get(block["id"]) != (block["first_word"], block["first_word"] + block["word_count"]):
                problems.append(f"{doc['href']}: words of block {block['id']} differ")
    return problems


# ---------------------------------------------------------------------------
# README


def build_readme(docs, expected) -> str:
    sections = expected["sections"]
    checkpoint_of = {cp["section"]: cp for cp in C.CHECKPOINTS}
    out = [
        "# Test books",
        "",
        "<!-- Generated by tools/prep/fixture/generate.py from tools/prep/fixture/content.py. Do not edit by hand. -->",
        "",
        f"## Synthetic fixture: *{C.TITLE}*",
        "",
        f"A tiny made-up book by the made-up author {C.AUTHOR}, used by automated tests on every layer "
        "(docs/PLAN.md section 8, \"Test material\"). Its contents are known exactly, and every file below is "
        "regenerated byte for byte by the generator.",
        "",
        "| File | What it is |",
        "| --- | --- |",
        "| `fixture.epub` | The original (unprepared) EPUB 3: title page plus three chapters, nav document and NCX. "
        "Every zip entry is stored (no compression) with a fixed timestamp, so the bytes do not depend on the "
        "zlib build. |",
        "| `fixture.draft.json` | The draft a drafter session would write for it (plan section 8, step 2): sections "
        "as ranges of `prep extract` paragraph references, excluded front matter, checkpoints with cited "
        "paragraphs. Schema: "
        "`tools/prep/schema/draft.schema.json`, documented in `tools/prep/README.md`. |",
        "| `fixture.expected.json` | Ground truth for other layers, including the firmware's WordIndex tests, computed "
        "from the generator's content model (not by parsing the EPUB) and cross-checked against the preparation "
        "tools' word pass: each spine document's whole word stream, word flags, IMAGE/TABLE/CODE/NOTE markers, "
        "blocks with their paragraph references, footnote marks, and every draft section's `[start, end)` word "
        f"range. Word rules version {RULES_VERSION} (`docs/spikes/S1-word-stream.md` section 5 plus NOTE regions); "
        "its `rules` member explains each part. |",
        "| `README.md` | This file. |",
        "",
        "Regenerate after editing `tools/prep/fixture/content.py`:",
        "",
        "```sh",
        "python3 tools/prep/fixture/generate.py          # rewrite the files here",
        "python3 tools/prep/fixture/generate.py --check  # exit 1 if a committed file is stale",
        "python3 -m pytest tools/prep -q                 # includes tools/prep/tests/test_fixture.py",
        "```",
        "",
        f"`fixture.epub` SHA-256: `{expected['epub_sha256']}`.",
        "",
        "### Documents and word counts",
        "",
        "Words are counted as the firmware will count them: every word of the document, including headings, figure "
        "captions, table cells, code and footnote bodies. Prose words exclude the TABLE, CODE and NOTE regions.",
        "",
        "| Spine | File (relative to `OEBPS/`) | Role | Title | Paragraphs (XHTML ids) | Words | Prose words |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ]
    for i, doc in enumerate(docs, start=1):
        first, last = doc["blocks"][0], doc["blocks"][-1]
        skipped = {n for m in doc["markers"] if m["kind"] != "IMAGE" for n in range(m["start"], m["end"])}
        out.append(
            f"| {i} | `{doc['href']}` | {doc['role']} | {doc['title']} | "
            f"{first['paragraph']} to {last['paragraph']} ({first['id']} to {last['id']}) | {len(doc['words'])} | "
            f"{len(doc['words']) - len(skipped)} |"
        )
    out += [
        "",
        "### Sections and checkpoints (from the draft)",
        "",
        "| Section | Chapter | Paragraphs | Words `[start, end)` | Ends chapter | Checkpoint |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for s in sections:
        cp = checkpoint_of.get(s["id"])
        cp_text = f"{cp['id']}, {len(cp['prompts'])} prompt{'s' if len(cp['prompts']) > 1 else ''}" if cp else "none"
        out.append(
            f"| {s['id']} {s['label']} | `{s['chapter_href']}` | {s['first_paragraph']} to {s['last_paragraph']} | "
            f"{s['word_count']} `[{s['start']}, {s['end']})` | {'yes' if s['ends_chapter'] else 'no'} | {cp_text} |"
        )
    to_ref = _paragraph_refs(docs)
    out += [
        "",
        f"`{C.EXCLUDED[0]['chapter_href']}` ({to_ref[C.EXCLUDED[0]['first_paragraph']]} to "
        f"{to_ref[C.EXCLUDED[0]['last_paragraph']]}) is excluded front matter. Footnote bodies after a chapter's last "
        "section are in no section.",
        "",
        "### Content inventory",
        "",
        "| Content | Document | Block | Words or position |",
        "| --- | --- | --- | --- |",
    ]
    mark_markup = {
        "noteref": '`<sup><a epub:type="noteref" role="doc-noteref" href="#{n}">`',
        "sup-link": '`<sup><a href="#{n}">` (no epub:type)',
        "link-sup": '`<a href="#{n}"><sup>` (no epub:type)',
    }
    for doc in docs:
        by_block = {b["id"]: b for b in doc["blocks"]}
        for block in doc["blocks"]:
            if block["kind"] == "image":
                marker = next(m for m in doc["markers"] if m["kind"] == "IMAGE" and m["start"] == block["first_word"])
                detail = (f"`<figure>` with `<img>` of `{block['payload']['src']}` (120x160 1-bit PNG, alt text, never "
                          f"a word) and `<figcaption>` (prose words); IMAGE marker at word {marker['start']}")
            elif block["kind"] == "table":
                p = block["payload"]
                detail = f"`<table>` with caption, header row and {len(p['rows'])} body rows; TABLE region"
            elif block["kind"] == "code":
                detail = f"`<pre><code>`, {len(block['payload'])} lines; CODE region"
            else:
                continue
            first, end = block["first_word"], block["first_word"] + block["word_count"]
            out.append(f"| {block['kind'].capitalize()} | `{doc['href']}` | {block['paragraph']} ({block['id']}) | "
                       f"{detail} `[{first}, {end})` |")
        for ref in doc["noterefs"]:
            note = next(f for f in doc["footnotes"] if f["id"] == ref["note"])
            paragraph = by_block[ref["block"]]["paragraph"]
            out.append(
                f"| Footnote {ref['mark']} | `{doc['href']}` | {paragraph} ({ref['block']}) | "
                f"{mark_markup[ref['form']].format(n=ref['note'])}: NOTEREF on word {ref['word']} "
                f"(\"{doc['words'][ref['word']]}\"); body `<aside epub:type=\"footnote\" id=\"{ref['note']}\">` at "
                f"chapter end, NOTE region `[{note['start']}, {note['end']})` |"
            )
        for item in doc["inline"]:
            out.append(
                f"| `<{item['element']}>` word | `{doc['href']}` | {by_block[item['block']]['paragraph']} "
                f"({item['block']}) | \"{item['text']}\" is in word {item['word']} (\"{doc['words'][item['word']]}\") |"
            )
        for pb in doc["page_breaks"]:
            out.append(
                f"| Page-break marker | `{doc['href']}` | {by_block[pb['block']]['paragraph']} ({pb['block']}) | "
                f"`<span epub:type=\"pagebreak\" role=\"doc-pagebreak\" id=\"{pb['id']}\">{pb['text']}</span>` "
                f"inside word {pb['word']} (\"{doc['words'][pb['word']]}\"); its text is in no word |"
            )
    out += [
        "",
        "### Word rule cases",
        "",
        "Each case is a word of the named block, exactly as it appears in `fixture.expected.json`, with the flags "
        "it carries. The XHTML writes U+00A0, U+00AD and combining marks as numeric character references.",
        "",
        "| Case | Block | Word index | Word | Flags |",
        "| --- | --- | --- | --- | --- |",
    ]
    for case in expected["token_cases"]:
        out.append(f"| {case['case']} | {case['block']} | {case['word_index']} | `{case['word']}` | "
                   f"{', '.join(case['flags']) or '-'} |")
    out += [
        "",
        "### Fixed values",
        "",
        f"- Identifier `{C.IDENTIFIER}`, `dcterms:modified` `{C.MODIFIED}`, language `{C.LANGUAGE}`.",
        f"- Zip entries in a fixed order, `mimetype` first, all stored, timestamp "
        f"{'-'.join(f'{v:02d}' for v in C.ZIP_DATE_TIME[:3])} 00:00:00.",
        "- XHTML block ids `<prefix>-pNNN` number every heading, paragraph, figure, table and pre of a document in "
        "order (`fm`, `c1`, `c2`, `c3`); footnote paragraphs are `<prefix>-nNNN`. They exist only to show that "
        "`prep extract` records source ids: the draft cites paragraph references (`d02-p006`: spine document 2, "
        "block 6), which `prep extract` derives from spine position and block order alone "
        "(`tools/prep/refs.py`). `content.py` cites the XHTML ids and the generator translates them.",
        "",
    ]
    return "\n".join(out)


# ---------------------------------------------------------------------------
# Entry points


def _json_bytes(data: Any) -> bytes:
    return (json.dumps(data, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def generate_files() -> dict[str, bytes]:
    """Return every output file name mapped to its bytes."""
    docs = build_model()
    check_draft(docs)
    xhtml = {d["href"]: render_document(d).encode("utf-8") for d in docs}
    problems = cross_check(docs, xhtml)
    if problems:
        raise ValueError("the content model and the word pass disagree:\n" + "\n".join(problems))
    epub = build_epub(docs)
    sha = hashlib.sha256(epub).hexdigest()
    expected = build_expected(docs, sha)
    return {
        EPUB_NAME: epub,
        DRAFT_NAME: _json_bytes(build_draft(docs, sha)),
        EXPECTED_NAME: _json_bytes(expected),
        README_NAME: build_readme(docs, expected).encode("utf-8"),
    }


def write_files(out_dir: Path, files: dict[str, bytes]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        (out_dir / name).write_bytes(data)


def _show(path: Path) -> Path:
    """``path`` relative to the working directory when it lies below it."""
    try:
        return path.resolve().relative_to(Path.cwd().resolve())
    except ValueError:
        return path


def check(out_dir: Path) -> list[str]:
    """Regenerate into a temporary directory; return problems with the files in ``out_dir``."""
    problems = []
    with tempfile.TemporaryDirectory() as tmp:
        write_files(Path(tmp), generate_files())
        for name in OUTPUT_NAMES:
            fresh = (Path(tmp) / name).read_bytes()
            target = out_dir / name
            if not target.exists():
                problems.append(f"missing: {_show(target)}")
            elif target.read_bytes() != fresh:
                problems.append(f"differs: {_show(target)}")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true", help="exit 1 if the files in --out are not current")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help=f"output directory (default {DEFAULT_OUT})")
    args = parser.parse_args(argv)
    if args.check:
        problems = check(args.out)
        for problem in problems:
            print(problem)
        if problems:
            print(f"{len(problems)} of {len(OUTPUT_NAMES)} files are stale; run tools/prep/fixture/generate.py")
            return 1
        print(f"{_show(args.out)}: all {len(OUTPUT_NAMES)} generated files are current")
        return 0
    files = generate_files()
    write_files(args.out, files)
    for name, data in files.items():
        print(f"wrote {_show(args.out / name)} ({len(data)} bytes, sha256 {hashlib.sha256(data).hexdigest()})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
