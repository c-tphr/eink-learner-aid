"""The one word pass shared by every preparation tool.

A word is what the firmware's WordIndex will call a word. The contract is
``docs/spikes/S1-word-stream.md`` section 5 ("What a word is"); this module
implements it for Python, and its rules are not restated here in prose beyond
what the code needs. The firmware's implementation is ``lib/WordIndex``; the
throwaway C++ prototype of spike S1 was removed in task R3. Its worked example
is kept as ``fixtures/words/fixture_s1.xhtml``; ``tests/test_words.py`` feeds
it through this module and compares with the prototype's expected result.

``word_stream(document)`` walks one spine document (bytes or an
``xmltree.Document``) and returns a ``WordStream``:

- ``words``: every word of the chapter in order, each with its text and flags
  (``PARA_END``, ``LINE_END``, ``NONPROSE``, ``NOTEREF``, ``HEADING``,
  ``BYTE_SPLIT``; the bits are the firmware's). Words inside tables, ``pre``,
  figure captions and footnote bodies are in the stream like any other word.
  There is deliberately no sentence-end flag: sentence ends are decided once,
  by the firmware (``lib/RsvpCore``).
- ``markers``: ``IMAGE`` (zero words long, at the index of the next word),
  ``TABLE`` and ``CODE`` (outermost ``table`` / ``pre``) and ``NOTE`` (the
  outermost footnote or endnote body) as ``[start, end)`` word ranges.
- ``anchors``: every element whose ``id`` starts with ``rsvp-``, with its word
  index (the number of words started before its start tag) and whether it
  sits strictly inside a word (a preparation error).
- ``noterefs``: every footnote reference mark, with its text and the index of
  the word that carries its ``NOTEREF`` flag.
- ``ranges``: for every element, the number of words started before its start
  tag and before its end tag, so a block's or a section's words are a range.

``tokenize(text)`` splits one run of plain text with the same character rules
(separators, dash split, soft hyphen and U+FEFF removal, NFC, 255-byte cap).

Standard library only: the fixture generator imports it too.
"""

from __future__ import annotations

import unicodedata
from dataclasses import dataclass, field

from .xmltree import OPS_NS, Document, Element, parse

# --------------------------------------------------------------------------
# Section 5 constants

SEPARATORS = frozenset(
    chr(cp)
    for cp in (0x09, 0x0A, 0x0C, 0x0D, 0x20, 0xA0, 0x1680, *range(0x2000, 0x200C), 0x2028, 0x2029, 0x202F, 0x205F,
               0x3000)
)
DASHES = frozenset("–—")  # a boundary falls after these when a non-separator follows
REMOVED = frozenset("­﻿")  # soft hyphen, BOM / zero width no-break space: dropped, never a boundary
MAX_WORD_BYTES = 255

# Element boundaries (start and end tag). The first line is where the renderer itself breaks.
BOUNDARY_ELEMENTS = frozenset(
    "p li div br blockquote ul ol pre figure h1 h2 h3 h4 h5 h6 table tr td th hr img image".split()
    + "section article aside nav header footer main figcaption caption dl dt dd address hgroup details summary "
      "thead tbody tfoot body html".split()
)
# Boundaries that do not end a paragraph (no PARA_END).
NON_PARAGRAPH_BOUNDARIES = frozenset({"br", "img", "image"})
HEADINGS = frozenset({"h1", "h2", "h3", "h4", "h5", "h6"})
# Character data counts only inside <body> and outside these (lib/Epub/Epub/VisibleTextUtils.h).
NONVISIBLE = frozenset({"head", "style", "script", "title", "rp"})
EXTERNAL_SCHEMES = ("http:", "https:", "mailto:", "ftp:", "tel:", "javascript:")
# Footnote and endnote bodies (the lead's NOTE rule, beyond section 5).
NOTE_TYPES = frozenset({"footnote", "endnote", "endnotes"})
NOTE_ROLES = frozenset({"doc-footnote", "doc-endnote", "doc-endnotes"})
EPUB_TYPE = f"{{{OPS_NS}}}type"
_TOKEN_SPACE = " \t\n\r\f"

# Flags: the firmware's bit values (WordStreamSpike.h). Bit 0 is the firmware's SENT_END, never set here.
PARA_END = 1 << 1
LINE_END = 1 << 2
NONPROSE = 1 << 3
NOTEREF = 1 << 4
HEADING = 1 << 5
BYTE_SPLIT = 1 << 6
FLAGS = {"PARA_END": PARA_END, "LINE_END": LINE_END, "NONPROSE": NONPROSE, "NOTEREF": NOTEREF, "HEADING": HEADING,
         "BYTE_SPLIT": BYTE_SPLIT}

IMAGE, TABLE, CODE, NOTE = "IMAGE", "TABLE", "CODE", "NOTE"
MARKER_KINDS = (IMAGE, TABLE, CODE, NOTE)


# --------------------------------------------------------------------------
# Result


@dataclass(frozen=True)
class Word:
    text: str
    flags: int

    def flag_names(self) -> list[str]:
        return [name for name, bit in FLAGS.items() if self.flags & bit]


@dataclass
class Marker:
    kind: str  # IMAGE, TABLE, CODE or NOTE
    start: int  # first word inside the region; for IMAGE the index of the next word
    end: int  # one past the last word inside; == start for IMAGE


@dataclass
class Anchor:
    id: str
    index: int  # words started before the anchor's start tag
    inside_word: bool = False  # the word in progress at the anchor continued after it


@dataclass
class NoteRef:
    mark: str  # the mark's text (not a word)
    word: int  # index of the word that carries NOTEREF; -1 when no word precedes the mark


@dataclass
class WordStream:
    words: list[Word]
    markers: list[Marker]
    anchors: list[Anchor]
    noterefs: list[NoteRef]
    ranges: dict[int, tuple[int, int]] = field(repr=False)  # Element.order -> (start, end)

    @property
    def texts(self) -> list[str]:
        return [w.text for w in self.words]

    def range_of(self, el: Element) -> tuple[int, int]:
        """(words started before ``el``'s start tag, words started before its end tag)."""
        return self.ranges[el.order]

    def anchor(self, anchor_id: str) -> Anchor | None:
        return next((a for a in self.anchors if a.id == anchor_id), None)

    def flag_ranges(self) -> dict[str, list[list[int]]]:
        """Each flag name mapped to the ``[start, end)`` runs of word indexes that carry it."""
        out: dict[str, list[list[int]]] = {}
        for name, bit in FLAGS.items():
            runs: list[list[int]] = []
            for i, word in enumerate(self.words):
                if word.flags & bit:
                    if runs and runs[-1][1] == i:
                        runs[-1][1] = i + 1
                    else:
                        runs.append([i, i + 1])
            out[name] = runs
        return out


# --------------------------------------------------------------------------
# Element tests


def _tokens(value: str | None) -> list[str]:
    return (value or "").translate({ord(c): " " for c in _TOKEN_SPACE}).split(" ")


def is_internal_link(el: Element) -> bool:
    """An ``a`` whose ``href`` holds ``#`` and does not start with an external scheme (section 5)."""
    href = el.get("href") or ""
    return el.local.lower() == "a" and "#" in href and not href.startswith(EXTERNAL_SCHEMES)


def is_explicit_noteref(el: Element) -> bool:
    """An ``a`` with ``epub:type`` token noteref or ``role`` token doc-noteref."""
    return el.local.lower() == "a" and ("noteref" in _tokens(el.get(EPUB_TYPE))
                                        or "doc-noteref" in _tokens(el.get("role")))


def is_note_body(el: Element) -> bool:
    """A footnote or endnote body: ``epub:type`` footnote/endnote/endnotes, ``role`` doc-footnote/-endnote/-endnotes."""
    return bool(NOTE_TYPES.intersection(_tokens(el.get(EPUB_TYPE)))) or bool(
        NOTE_ROLES.intersection(_tokens(el.get("role"))))


def hides_text(el: Element) -> bool:
    """Its text is in no word: ``hidden``, ``role`` exactly doc-pagebreak, ``epub:type`` exactly pagebreak, ``rt``."""
    return (el.get("hidden") is not None or el.get("role") == "doc-pagebreak" or el.get(EPUB_TYPE) == "pagebreak"
            or el.local.lower() == "rt")


# --------------------------------------------------------------------------
# The pass


class _Pass:
    def __init__(self) -> None:
        self.texts: list[str] = []
        self.flags: list[int] = []
        self.markers: list[Marker] = []
        self.anchors: list[Anchor] = []
        self.noterefs: list[NoteRef] = []
        self.ranges: dict[int, tuple[int, int]] = {}
        self.cur: list[str] = []
        self.cur_bytes = 0
        self.cur_noteref = False
        self.last = ""
        self.pending: list[Anchor] = []  # anchors met while a word was in progress
        self.in_body = False
        self.nonvisible = 0
        self.hidden = 0
        self.heading = 0
        self.sup = 0
        self.link = 0
        self.explicit = 0
        self.nonprose = 0
        self.note = 0
        self.region: Marker | None = None
        self.note_region: Marker | None = None
        self.mark: list[str] | None = None
        self.mark_word = -1

    # -- words

    def started(self) -> int:
        return len(self.texts) + (1 if self.cur else 0)

    def push(self, text: str, flags: int) -> None:
        if self.nonprose:
            flags |= NONPROSE
        if self.heading:
            flags |= HEADING
        self.texts.append(unicodedata.normalize("NFC", text))
        self.flags.append(flags)

    def flush(self) -> None:
        self.pending.clear()
        if not self.cur:
            return
        self.push("".join(self.cur), NOTEREF if self.cur_noteref else 0)
        self.cur.clear()
        self.cur_bytes = 0
        self.cur_noteref = False
        self.last = ""

    def boundary(self, flag: int) -> None:
        self.flush()
        if self.flags:
            self.flags[-1] |= flag

    def append(self, ch: str) -> None:
        for anchor in self.pending:
            anchor.inside_word = True
        self.pending.clear()
        size = len(ch.encode("utf-8"))
        if self.cur and self.cur_bytes + size > MAX_WORD_BYTES:
            self.push("".join(self.cur), BYTE_SPLIT)
            self.cur.clear()
            self.cur_bytes = 0
        self.cur.append(ch)
        self.cur_bytes += size
        self.last = ch

    def noteref_active(self) -> bool:
        return self.explicit > 0 or (self.sup > 0 and self.link > 0)

    def text(self, s: str) -> None:
        if not self.in_body or self.nonvisible:
            return
        for ch in s:
            if self.hidden:
                continue
            if self.noteref_active():
                if self.mark is not None:
                    if not self.mark:
                        self.mark_word = len(self.texts) if self.cur else len(self.texts) - 1
                    self.mark.append(ch)
                if self.cur:
                    self.cur_noteref = True
                elif self.flags:
                    self.flags[-1] |= NOTEREF
                continue
            if ch in SEPARATORS:
                self.flush()
                continue
            if ch in REMOVED:
                continue
            if self.cur and self.last in DASHES:
                self.flush()
            self.append(ch)

    # -- elements

    def element(self, el: Element) -> None:
        local = el.local.lower()
        if local == "body":
            self.in_body = True
        entered_nonvisible = self.in_body and (self.nonvisible > 0 or local in NONVISIBLE)
        if entered_nonvisible:
            self.nonvisible += 1
        active = self.in_body and self.nonvisible == 0
        start = self.started()
        counters: list[str] = []
        region = note_region = False
        hidden = processed = False
        if active:
            anchor_id = el.get("id")
            if anchor_id is not None and anchor_id.startswith("rsvp-"):
                anchor = Anchor(anchor_id, start)
                self.anchors.append(anchor)
                if self.cur:
                    self.pending.append(anchor)
            # Hidden, page-break and rt content "never splits a word": the element's own tags and every
            # tag inside it are ignored entirely (no boundary, no flag, no marker), as in the prototype.
            if self.hidden == 0 and hides_text(el):
                hidden = True
                self.hidden += 1
            processed = self.hidden == 0
        if processed:
            if local == "br":
                self.boundary(LINE_END)
            elif local in BOUNDARY_ELEMENTS:
                if local in NON_PARAGRAPH_BOUNDARIES:
                    self.flush()
                else:
                    self.boundary(PARA_END)
            was_noteref = self.noteref_active()
            if local in HEADINGS:
                counters.append("heading")
            if local == "sup":
                counters.append("sup")
            if is_internal_link(el):
                counters.append("link")
            if is_explicit_noteref(el):
                counters.append("explicit")
            for name in counters:
                setattr(self, name, getattr(self, name) + 1)
            if not was_noteref and self.noteref_active():
                self.mark, self.mark_word = [], -1
            if local in ("table", "pre"):
                region = True
                if self.nonprose == 0:
                    self.region = Marker(TABLE if local == "table" else CODE, len(self.texts), len(self.texts))
                    self.markers.append(self.region)
                self.nonprose += 1
            if local in ("img", "image") and self.nonprose == 0:
                self.markers.append(Marker(IMAGE, len(self.texts), len(self.texts)))
            if is_note_body(el):
                note_region = True
                if self.note == 0:
                    at = self.started()
                    self.note_region = Marker(NOTE, at, at)
                    self.markers.append(self.note_region)
                self.note += 1

        for child in el.children:
            if isinstance(child, str):
                self.text(child)
            else:
                self.element(child)

        if entered_nonvisible:
            self.nonvisible -= 1
        if processed:
            if local in BOUNDARY_ELEMENTS:
                if local in NON_PARAGRAPH_BOUNDARIES:
                    self.flush()
                else:
                    self.boundary(PARA_END)
            was_noteref = self.noteref_active()
            for name in counters:
                setattr(self, name, getattr(self, name) - 1)
            if was_noteref and not self.noteref_active():
                if self.mark:
                    self.noterefs.append(NoteRef("".join(self.mark), self.mark_word))
                self.mark = None
            if region:
                self.nonprose -= 1
                if self.nonprose == 0 and self.region is not None:
                    self.region.end = len(self.texts)
            if note_region:
                self.note -= 1
                if self.note == 0 and self.note_region is not None:
                    self.note_region.end = self.started()
        if hidden:
            self.hidden -= 1
        self.ranges[el.order] = (start, self.started())
        if local == "body":
            self.boundary(PARA_END)
            self.in_body = False

    def result(self) -> WordStream:
        self.boundary(PARA_END)  # the chapter's last word
        words = [Word(t, f) for t, f in zip(self.texts, self.flags)]
        return WordStream(words, self.markers, self.anchors, self.noterefs, self.ranges)


def word_stream(source: bytes | Document, where: str = "document") -> WordStream:
    """The word stream of one XHTML spine document (docs/spikes/S1-word-stream.md section 5, plus NOTE regions)."""
    doc = source if isinstance(source, Document) else parse(source, where)
    p = _Pass()
    p.element(doc.root)
    return p.result()


def tokenize(text: str) -> list[str]:
    """The words of one run of plain text, under section 5's character rules (no markup involved)."""
    p = _Pass()
    p.in_body = True
    p.text(text)
    p.flush()
    return p.texts


def is_separator_only(text: str) -> bool:
    """True when ``text`` holds no word (empty, or only separators, soft hyphens and U+FEFF)."""
    return not tokenize(text)
