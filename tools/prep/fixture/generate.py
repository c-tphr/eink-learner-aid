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
``content.py``. Words are defined by ``tokenize`` below and by ``RULES``.
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

from tools.prep.fixture import content as C  # noqa: E402

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


# ---------------------------------------------------------------------------
# Word definition. Keep it in this one function so a later task can align it
# with the firmware's parser; RULES documents it for readers of expected.json.

WORD_RE = re.compile(r"[^ \t\n\r]+")


def tokenize(text: str) -> list[str]:
    """Split visible prose text into words.

    A word is a maximal run of characters other than the four XML white space
    characters (U+0020, U+0009, U+000A, U+000D). Punctuation stays attached;
    nothing is normalised, case-folded or stripped.
    """
    return WORD_RE.findall(text)


RULES = {
    "word": "A word is a maximal run of characters other than U+0020 SPACE, U+0009 TAB, U+000A LINE FEED and "
    "U+000D CARRIAGE RETURN, taken from the visible text of one prose block. Punctuation (. , ; : ? ! quotes, "
    "apostrophes, hyphens, em dashes) stays attached to the word it touches; an unspaced em dash therefore joins "
    "its neighbours into one word. No normalisation, case folding or stripping is applied; non-ASCII text is NFC "
    "as written.",
    "prose_blocks": "Prose blocks are h1/h2 headings (kind 'heading') and p elements (kind 'paragraph') that are "
    "direct children of body. A block boundary always ends a word.",
    "inline_elements": "Inline elements (em, strong) do not split or drop words: the text of a block is "
    "concatenated across inline tags before it is split into words.",
    "noterefs": "A footnote reference mark (sup > a[epub:type=noteref]) is not a word and contributes no "
    "characters to the prose text. Its position is 'after_word': the index of the word it follows.",
    "footnotes": "Footnote bodies (aside[epub:type=footnote] at the end of a chapter) are not part of the "
    "chapter's word stream; their words are listed separately under 'footnotes'.",
    "non_prose_blocks": "figure (kind 'image'), table (kind 'table') and pre (kind 'code') contribute no words. "
    "Their position is 'after_word', the index of the last prose word before them, and their inner text (caption, "
    "cells, code lines) is listed in the block entry only.",
    "indexing": "Word indices are 0-based over one document's whole word stream (not per block). 'after_word' is "
    "-1 when nothing precedes the item.",
    "sections": "A section's start anchor goes immediately before word 'first_word' and its end anchor "
    "immediately after word 'last_word' of the section's chapter.",
}


# ---------------------------------------------------------------------------
# Inline markup of content.py

INLINE_RE = re.compile(r"\*\*(?P<strong>[^*]+)\*\*|\*(?P<em>[^*]+)\*|\[\^(?P<note>[A-Za-z0-9_-]+)\]")


def parse_inline(source: str) -> list[tuple[str, str]]:
    """Split content markup into ("text"|"em"|"strong"|"note", value) segments."""
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
        if kind == "text" and ("*" in value or "[^" in value):
            raise ValueError(f"stray inline markup in {source!r}")
    return segments


def esc(text: str) -> str:
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def esc_attr(text: str) -> str:
    return esc(text).replace('"', "&quot;")


# ---------------------------------------------------------------------------
# Model: documents, blocks, words


def _documents() -> list[dict[str, Any]]:
    return [C.FRONT_MATTER, *C.CHAPTERS]


def build_model() -> list[dict[str, Any]]:
    """Number blocks, assign footnote marks and compute every document's word stream."""
    note_numbers: dict[str, int] = {}
    docs = []
    for src in _documents():
        doc: dict[str, Any] = {
            "href": src["href"],
            "role": src["role"],
            "prefix": src["prefix"],
            "title": src["title"],
            "blocks": [],
            "words": [],
            "noterefs": [],
            "inline": [],
            "notes": [],
        }
        words: list[str] = doc["words"]
        for number, (element, payload) in enumerate(src["blocks"], start=1):
            block_id = f"{src['prefix']}-p{number:03d}"
            if element in PROSE_ELEMENTS:
                segments = parse_inline(payload)
                text, marks, spans = _visible_text(segments, block_id)
                block_words = tokenize(text)
                if not block_words:
                    raise ValueError(f"{block_id}: prose block without words")
                first = len(words)
                block = {
                    "id": block_id,
                    "element": element,
                    "kind": PROSE_ELEMENTS[element],
                    "segments": segments,
                    "first_word": first,
                    "last_word": first + len(block_words) - 1,
                    "word_count": len(block_words),
                }
                if element.startswith("h"):
                    block["level"] = int(element[1:])
                words.extend(block_words)
                for note_id, offset in marks:
                    if note_id not in C.FOOTNOTES:
                        raise ValueError(f"{block_id}: unknown footnote {note_id!r}")
                    if note_id in note_numbers:
                        raise ValueError(f"{block_id}: footnote {note_id!r} referenced twice")
                    note_numbers[note_id] = len(note_numbers) + 1
                    doc["noterefs"].append(
                        {
                            "note": note_id,
                            "mark": str(note_numbers[note_id]),
                            "block": block_id,
                            "after_word": first + len(tokenize(text[:offset])) - 1,
                        }
                    )
                    doc["notes"].append(note_id)
                for element_name, start, end in spans:
                    inside = tokenize(text[start:end])
                    if len(inside) != 1 or inside[0] != text[start:end]:
                        raise ValueError(f"{block_id}: <{element_name}> must hold exactly one word")
                    # The word that contains the element's first character.
                    index = first + len(tokenize(text[: start + 1])) - 1
                    doc["inline"].append(
                        {"element": element_name, "block": block_id, "text": inside[0], "word_index": index}
                    )
            elif element in NON_PROSE_KINDS:
                block = {
                    "id": block_id,
                    "element": element,
                    "kind": NON_PROSE_KINDS[element],
                    "payload": payload,
                    "after_word": len(words) - 1,
                }
            else:
                raise ValueError(f"unknown block element {element!r}")
            doc["blocks"].append(block)
        docs.append(doc)
    unused = set(C.FOOTNOTES) - set(note_numbers)
    if unused:
        raise ValueError(f"footnotes never referenced: {sorted(unused)}")
    for doc in docs:
        doc["note_numbers"] = {n: note_numbers[n] for n in doc["notes"]}
    return docs


def _visible_text(segments, block_id):
    """Return (prose text, [(note id, char offset)], [(element, start, end)])."""
    text = ""
    marks = []
    spans = []
    for kind, value in segments:
        if kind == "note":
            offset = len(text)
            if offset == 0 or text[-1] in " \t\n\r":
                raise ValueError(f"{block_id}: a note mark must follow a word")
            marks.append((value, offset))
            continue
        if kind in ("em", "strong"):
            spans.append((kind, len(text), len(text) + len(value)))
        if marks and marks[-1][1] == len(text) and value[:1] not in ("", " "):
            raise ValueError(f"{block_id}: a note mark must be followed by a space or the block end")
        text += value
    if unicodedata.normalize("NFC", text) != text:
        raise ValueError(f"{block_id}: prose must be NFC")
    if "  " in text or text != text.strip(" "):
        raise ValueError(f"{block_id}: prose must use single spaces")
    return text, marks, spans


def _footnote_words(note_id: str) -> list[list[str]]:
    return [tokenize(p) for p in C.FOOTNOTES[note_id]]


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


def _inline_xhtml(segments, note_numbers) -> str:
    out = []
    for kind, value in segments:
        if kind == "text":
            out.append(esc(value))
        elif kind in ("em", "strong"):
            out.append(f"<{kind}>{esc(value)}</{kind}>")
        else:
            n = note_numbers[value]
            out.append(f'<sup><a epub:type="noteref" role="doc-noteref" href="#{value}">{n}</a></sup>')
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
    prose = [b for b in doc["blocks"][first : last + 1] if "first_word" in b]
    first_word, last_word = prose[0]["first_word"], prose[-1]["last_word"]
    return {
        "id": section["id"],
        "label": section["label"],
        "chapter_href": section["chapter_href"],
        "first_paragraph": section["first_paragraph"],
        "last_paragraph": section["last_paragraph"],
        "start_anchor": f"rsvp-{section['id']}-start",
        "end_anchor": f"rsvp-{section['id']}-end",
        "first_word": first_word,
        "last_word": last_word,
        "word_count": last_word - first_word + 1,
        "first_word_text": doc["words"][first_word],
        "last_word_text": doc["words"][last_word],
        "ends_chapter": last == len(doc["blocks"]) - 1,
    }


def build_draft(epub_sha256: str) -> dict[str, Any]:
    return {
        "format": "rsvp-draft",
        "version": 1,
        "book": {
            "title": C.TITLE,
            "language": C.LANGUAGE,
            "source_sha256": epub_sha256,
            "instructions_version": C.INSTRUCTIONS_VERSION,
        },
        "excluded": C.EXCLUDED,
        "sections": C.SECTIONS,
        "checkpoints": C.CHECKPOINTS,
    }


def _expected_block(block) -> dict[str, Any]:
    out = {"id": block["id"], "kind": block["kind"], "element": block["element"]}
    if "first_word" in block:
        if "level" in block:
            out["level"] = block["level"]
        out.update(first_word=block["first_word"], last_word=block["last_word"], word_count=block["word_count"])
        return out
    out["after_word"] = block["after_word"]
    p = block["payload"]
    if block["kind"] == "image":
        out.update(image_href=p["src"], alt=p["alt"], caption=p["caption"])
    elif block["kind"] == "table":
        out.update(caption=p["caption"], header=p["header"], rows=p["rows"])
    else:
        out["lines"] = p
    return out


def build_expected(docs, epub_sha256: str) -> dict[str, Any]:
    documents = []
    for doc in docs:
        footnotes = []
        note_index = 0
        for note_id in doc["notes"]:
            paragraphs = []
            for words in _footnote_words(note_id):
                note_index += 1
                paragraphs.append({"id": f"{doc['prefix']}-n{note_index:03d}", "words": words})
            footnotes.append({"id": note_id, "mark": str(doc["note_numbers"][note_id]), "paragraphs": paragraphs})
        documents.append(
            {
                "href": doc["href"],
                "zip_path": f"{OPF_DIR}/{doc['href']}",
                "role": doc["role"],
                "title": doc["title"],
                "word_count": len(doc["words"]),
                "blocks": [_expected_block(b) for b in doc["blocks"]],
                "noterefs": doc["noterefs"],
                "inline_elements": doc["inline"],
                "footnotes": footnotes,
                "words": doc["words"],
            }
        )
    by_id = {b["id"]: (doc, b) for doc in docs for b in doc["blocks"]}
    cases = []
    for case, block_id, token in C.TOKEN_CASES:
        doc, block = by_id[block_id]
        hits = [i for i in range(block["first_word"], block["last_word"] + 1) if doc["words"][i] == token]
        if not hits:
            raise ValueError(f"token case {case!r}: {token!r} is not a word of {block_id}")
        cases.append({"case": case, "href": doc["href"], "block": block_id, "word_index": hits[0], "word": token})
    return {
        "format": "rsvp-fixture-expected",
        "version": 1,
        "generated_by": "tools/prep/fixture/generate.py",
        "epub": EPUB_NAME,
        "epub_sha256": epub_sha256,
        "rules": RULES,
        "documents": documents,
        "sections": [section_words(docs, s) for s in C.SECTIONS],
        "token_cases": cases,
    }


# ---------------------------------------------------------------------------
# README


def build_readme(docs, expected) -> str:
    by_href = {d["href"]: d for d in docs}
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
        "as paragraph ranges, excluded front matter, checkpoints with cited paragraphs. Schema: "
        "`tools/prep/schema/draft.schema.json`, documented in `tools/prep/README.md`. |",
        "| `fixture.expected.json` | Ground truth for other layers, computed from the source data (not by parsing "
        "the EPUB): each spine document's word stream, block boundaries, non-prose blocks, footnote marks and "
        "bodies, and the first and last word of every draft section. Its `rules` member defines a word. |",
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
        "### Documents and word counts",
        "",
        "Word counts follow the rules in `fixture.expected.json` (headings included, footnote bodies and the "
        "inner text of figures, tables and code excluded).",
        "",
        "| Spine | File (relative to `OEBPS/`) | Role | Title | Blocks | Words |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for i, doc in enumerate(docs, start=1):
        out.append(
            f"| {i} | `{doc['href']}` | {doc['role']} | {doc['title']} | "
            f"{doc['blocks'][0]['id']} to {doc['blocks'][-1]['id']} | {len(doc['words'])} |"
        )
    out += [
        "",
        "### Sections and checkpoints (from the draft)",
        "",
        "| Section | Chapter | Paragraphs | Words (index range) | Ends chapter | Checkpoint |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for s in sections:
        cp = checkpoint_of.get(s["id"])
        cp_text = f"{cp['id']}, {len(cp['prompts'])} prompt{'s' if len(cp['prompts']) > 1 else ''}" if cp else "none"
        out.append(
            f"| {s['id']} {s['label']} | `{s['chapter_href']}` | {s['first_paragraph']} to {s['last_paragraph']} | "
            f"{s['word_count']} ({s['first_word']} to {s['last_word']}) | {'yes' if s['ends_chapter'] else 'no'} | "
            f"{cp_text} |"
        )
    out += [
        "",
        f"`{C.EXCLUDED[0]['chapter_href']}` ({C.EXCLUDED[0]['first_paragraph']} to "
        f"{C.EXCLUDED[0]['last_paragraph']}) is excluded front matter.",
        "",
        "### Content inventory",
        "",
        "| Content | Document | Block | Detail |",
        "| --- | --- | --- | --- |",
    ]
    for doc in docs:
        for block in doc["blocks"]:
            if block["kind"] == "image":
                detail = f"`<figure>` with `<img>` of `{block['payload']['src']}` (120x160 1-bit PNG, alt text) and `<figcaption>`"
            elif block["kind"] == "table":
                p = block["payload"]
                detail = f"`<table>` with caption, header row and {len(p['rows'])} body rows"
            elif block["kind"] == "code":
                detail = f"`<pre><code>`, {len(block['payload'])} lines"
            else:
                continue
            detail += f"; after word {block['after_word']}"
            out.append(f"| {block['kind'].capitalize()} | `{doc['href']}` | {block['id']} | {detail} |")
        for ref in doc["noterefs"]:
            out.append(
                f"| Footnote {ref['mark']} | `{doc['href']}` | {ref['block']} | "
                f"`<sup><a epub:type=\"noteref\" href=\"#{ref['note']}\">`, after word {ref['after_word']} "
                f"(\"{doc['words'][ref['after_word']]}\"); body in `<aside epub:type=\"footnote\" id=\"{ref['note']}\">` "
                "at chapter end |"
            )
        for item in doc["inline"]:
            out.append(
                f"| `<{item['element']}>` word | `{doc['href']}` | {item['block']} | "
                f"\"{item['text']}\" is word {item['word_index']} (\"{doc['words'][item['word_index']]}\") |"
            )
    out += [
        "",
        "### Tokenisation cases",
        "",
        "Each case is a word of the named block, exactly as it appears in `fixture.expected.json`. Everything "
        "else is plain ASCII with single spaces. Prose blocks with non-ASCII characters: "
        + ", ".join(
            f"`{b['id']}`"
            for d in docs
            for b in d["blocks"]
            if "first_word" in b and any(ord(ch) > 127 for w in d["words"][b["first_word"] : b["last_word"] + 1] for ch in w)
        )
        + ".",
        "",
        "| Case | Block | Word index | Word |",
        "| --- | --- | --- | --- |",
    ]
    for case in expected["token_cases"]:
        out.append(f"| {case['case']} | {case['block']} | {case['word_index']} | `{case['word']}` |")
    out += [
        "",
        "### Fixed values",
        "",
        f"- Identifier `{C.IDENTIFIER}`, `dcterms:modified` `{C.MODIFIED}`, language `{C.LANGUAGE}`.",
        f"- Zip entries in a fixed order, `mimetype` first, all stored, timestamp "
        f"{'-'.join(f'{v:02d}' for v in C.ZIP_DATE_TIME[:3])} 00:00:00.",
        "- Block ids `<prefix>-pNNN` number every heading, paragraph, figure, table and pre of a document in "
        "order (`fm`, `c1`, `c2`, `c3`); footnote paragraphs are `<prefix>-nNNN`.",
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
    epub = build_epub(docs)
    sha = hashlib.sha256(epub).hexdigest()
    expected = build_expected(docs, sha)
    return {
        EPUB_NAME: epub,
        DRAFT_NAME: _json_bytes(build_draft(sha)),
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
