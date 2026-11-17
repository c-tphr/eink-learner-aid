"""Read an EPUB into the model every ``prep`` command shares.

``load_book(path)`` returns a ``Book``: package metadata, the spine in reading
order, the nav/NCX table of contents and, for every XHTML spine document, its
blocks with their paragraph references (``tools/prep/refs.py``), kinds and
words (``tools/prep/words.py``). ``extract`` writes this model to the work
folder; ``build`` uses it to find where anchors go; ``validate`` (next task) is
meant to load the original and the prepared file with it and compare.

Block rules (also in tools/prep/README.md, "Paragraph identity"):

1. Only the ``body`` of an XHTML spine document is read. ``script``,
   ``style``, ``template``, ``title``, ``noscript``, ``rt``, ``rp``, elements
   with a ``hidden`` attribute and page-break markers (``role`` exactly
   doc-pagebreak or ``epub:type`` exactly pagebreak) hold no words.
2. Atomic (non-prose) blocks: ``table`` (kind ``table``), ``pre`` (``code``),
   ``img``/``svg``/``picture`` (``image``), ``math`` (``math``),
   ``video``/``audio``/``object``/``iframe``/``embed``/``canvas`` (``media``),
   and ``figure`` when it holds one of those (kind taken from what it holds).
   They count no words; their text is shown only as a placeholder.
3. Footnote containers (``epub:type`` containing footnote, endnote,
   rearnote, note, footnotes, endnotes or rearnotes; or ``role`` doc-footnote,
   doc-endnote, doc-endnotes) are outside the reading stream. Their blocks are
   numbered separately (``dNN-nNNN``, kind ``footnote``).
4. Any other element that has text of its own (a non-blank text node, or an
   inline child with words) is one prose block, including everything inside
   it. Otherwise its element children are visited in order.
5. A prose block with no words is not a block (empty spacer paragraphs).
6. Block numbers count reading-stream blocks of one document in document
   order from 1; footnote blocks from 1 in their own series.

Words of a prose block: the concatenated text of its descendants (inline
elements do not split words), with ``br`` and block-level descendants acting
as a space, minus footnote reference marks and minus atomic descendants;
then ``tokenize``. A footnote reference mark is an ``a`` with ``epub:type``
noteref or ``role`` doc-noteref, or text inside both a ``sup`` and an
``a href`` to an internal ``#`` target, in either nesting order (the
firmware's rule, docs/spikes/S1-word-stream.md section 5).
"""

from __future__ import annotations

import hashlib
import posixpath
import urllib.parse
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterator

from . import refs
from .epubzip import Archive, ZipFormatError
from .words import tokenize
from .xmltree import OPS_NS, XHTML_NS, Document, Element, XmlError, parse

CONTAINER_PATH = "META-INF/container.xml"
CONTAINER_NS = "urn:oasis:names:tc:opendocument:xmlns:container"
OPF_NS = "http://www.idpf.org/2007/opf"
DC_NS = "http://purl.org/dc/elements/1.1/"
NCX_NS = "http://www.daisy.org/z3986/2005/ncx/"
SVG_NS = "http://www.w3.org/2000/svg"
MATHML_NS = "http://www.w3.org/1998/Math/MathML"
XHTML_MEDIA_TYPE = "application/xhtml+xml"

INVISIBLE = {"script", "style", "template", "head", "title", "noscript", "rt", "rp"}
EXTERNAL_SCHEMES = ("http:", "https:", "mailto:", "ftp:", "tel:", "javascript:")
BLOCK_LEVEL = {
    "address", "article", "aside", "blockquote", "body", "details", "dialog", "dd", "div", "dl", "dt", "fieldset",
    "figcaption", "figure", "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "header", "hgroup", "hr", "li",
    "main", "nav", "ol", "p", "pre", "section", "table", "ul", "summary", "menu", "caption", "tr", "td", "th",
    "thead", "tbody", "tfoot",
}
ATOMIC_KIND = {
    "table": "table", "pre": "code", "img": "image", "picture": "image", "math": "math", "video": "media",
    "audio": "media", "object": "media", "iframe": "media", "embed": "media", "canvas": "media",
}
HEADINGS = {"h1", "h2", "h3", "h4", "h5", "h6"}
LIST_ITEMS = {"li", "dt", "dd"}
FOOTNOTE_TYPES = {"footnote", "endnote", "rearnote", "note", "footnotes", "endnotes", "rearnotes"}
FOOTNOTE_ROLES = {"doc-footnote", "doc-endnote", "doc-endnotes"}
# Parents in which an empty <span> may not stand as a child (HTML content models).
NO_PHRASING_CHILD = {
    "html", "head", "table", "thead", "tbody", "tfoot", "tr", "colgroup", "ul", "ol", "dl", "menu", "select",
    "optgroup", "datalist", "hgroup", "picture", "video", "audio", "object", "figure", "ruby", "svg", "math",
}

EPUB_TYPE = f"{{{OPS_NS}}}type"


class EpubError(ValueError):
    """A problem with the input book (the commands exit with status 1)."""


# --------------------------------------------------------------------------
# Model


@dataclass
class Block:
    ref: str
    series: str  # refs.MAIN or refs.NOTE
    number: int
    kind: str  # heading, paragraph, list-item, blockquote, aside, footnote, image, table, code, math, media
    element: Element
    words: list[str]
    first_word: int | None  # index in the document's reading stream (prose reading-stream blocks only)
    after_word: int  # index of the last reading-stream word before the block (-1 if none)
    placeholder: str | None  # for atomic blocks: a one-line description
    note_id: str | None = None  # for footnote blocks: id of the footnote container

    @property
    def word_count(self) -> int:
        return len(self.words)

    @property
    def atomic(self) -> bool:
        return self.placeholder is not None

    @property
    def source_id(self) -> str | None:
        return self.element.get("id")

    @property
    def placement(self) -> str:
        """Where anchors go: 'inside' the block element, 'around' it (as siblings), or 'none' possible."""
        el = self.element
        if not self.atomic and el.local not in NO_PHRASING_CHILD and not el.is_empty_tag:
            return "inside"
        if el.parent is not None and el.parent.local not in NO_PHRASING_CHILD:
            return "around"
        return "none"

    def summary(self) -> dict[str, Any]:
        out: dict[str, Any] = {
            "ref": self.ref,
            "number": self.number,
            "kind": self.kind,
            "element": self.element.local,
            "source_id": self.source_id,
            "word_count": self.word_count,
        }
        if self.kind == "heading" and self.element.local in HEADINGS:
            out["level"] = int(self.element.local[1])
        if self.first_word is not None:
            out["first_word"] = self.first_word
            out["last_word"] = self.first_word + self.word_count - 1
        elif self.series == refs.MAIN:
            out["after_word"] = self.after_word
        if self.placeholder is not None:
            out["placeholder"] = self.placeholder
        if self.note_id is not None:
            out["note_id"] = self.note_id
        out["path"] = self.element.path()
        out["line"] = self.element.line
        if self.series == refs.MAIN:
            out["anchor_placement"] = self.placement
        return out


@dataclass
class SpineDoc:
    index: int  # 1-based spine position
    idref: str
    href: str  # manifest href, verbatim
    media_type: str
    linear: bool
    zip_path: str
    prefix: str
    title: str | None = None
    doc: Document | None = None
    blocks: list[Block] = field(default_factory=list)  # reading stream
    notes: list[Block] = field(default_factory=list)  # footnote bodies
    words: list[str] = field(default_factory=list)  # reading-stream words

    @property
    def is_xhtml(self) -> bool:
        return self.media_type == XHTML_MEDIA_TYPE

    def block_by_ref(self) -> dict[str, Block]:
        return {b.ref: b for b in self.blocks + self.notes}

    def block_for_element(self, el: Element) -> Block | None:
        """The reading-stream block that contains ``el``, else the first one after it."""
        for b in self.blocks:
            if b.element.contains(el) or b.element.order > el.order:
                return b
        return None


@dataclass
class ManifestItem:
    id: str
    href: str
    media_type: str
    properties: list[str]
    zip_path: str
    element: Element


@dataclass
class TocEntry:
    label: str
    href: str  # relative to the OPF directory, with fragment
    depth: int
    source: str  # "nav" or "ncx"
    ref: str | None


@dataclass
class Book:
    source_name: str
    sha256: str
    archive: Archive
    opf_path: str
    opf_dir: str
    opf: Document
    title: str | None
    language: str | None
    manifest: list[ManifestItem]
    spine: list[SpineDoc]
    toc: list[TocEntry]

    def doc_by_href(self, href: str) -> SpineDoc | None:
        for d in self.spine:
            if d.href == href:
                return d
        return None

    def all_refs(self) -> dict[str, tuple[SpineDoc, Block]]:
        return {b.ref: (d, b) for d in self.spine for b in d.blocks + d.notes}


# --------------------------------------------------------------------------
# Helpers


def _tokens(value: str | None) -> set[str]:
    return set((value or "").split())


def is_footnote_container(el: Element) -> bool:
    return bool(_tokens(el.get(EPUB_TYPE)) & FOOTNOTE_TYPES) or bool(_tokens(el.get("role")) & FOOTNOTE_ROLES)


def is_noteref(el: Element) -> bool:
    """An ``a`` explicitly marked as a footnote reference (``epub:type`` noteref or ``role`` doc-noteref)."""
    return el.local == "a" and ("noteref" in _tokens(el.get(EPUB_TYPE)) or "doc-noteref" in _tokens(el.get("role")))


def is_internal_link(el: Element) -> bool:
    href = el.get("href") or ""
    return el.local == "a" and "#" in href and not href.lower().startswith(EXTERNAL_SCHEMES)


def _mark_flags(el: Element) -> tuple[bool, bool]:
    """(inside a sup, inside an internal link) for ``el`` itself and its ancestors."""
    sup = link = False
    node: Element | None = el
    while node is not None:
        sup = sup or node.local == "sup"
        link = link or is_internal_link(node)
        node = node.parent
    return sup, link


def is_invisible(el: Element) -> bool:
    """Not part of any word (spike S1, section 5): script/style/..., ``hidden``, page breaks, ``rt``/``rp``."""
    return (
        (el.ns == XHTML_NS and el.local in INVISIBLE)
        or el.get("hidden") is not None
        or el.get("role") == "doc-pagebreak"
        or el.get(EPUB_TYPE) == "pagebreak"
    )


def atomic_kind(el: Element) -> str | None:
    if el.ns == SVG_NS and el.local == "svg":
        return "image"
    if el.ns == MATHML_NS and el.local == "math":
        return "math"
    if el.ns != XHTML_NS:
        return None
    if el.local == "figure":
        inner = [atomic_kind(d) for d in el.iter() if d is not el]
        for kind in ("image", "table", "code", "math", "media"):
            if kind in inner:
                return kind
        return None  # a figure of prose (a quotation, say) is a container
    return ATOMIC_KIND.get(el.local)


def _is_block_level(el: Element) -> bool:
    return el.ns == XHTML_NS and el.local in BLOCK_LEVEL


def prose_text(el: Element, show_noterefs: bool = False) -> str:
    """Visible reading-stream text of ``el`` (see the module docstring).

    With ``show_noterefs`` a footnote mark is written as ``[^mark]`` instead
    of being dropped (for the work folder's text files).
    """
    out: list[str] = []

    def walk(node: Element, in_sup: bool, in_link: bool) -> None:
        for child in node.children:
            if isinstance(child, str):
                out.append(child)
                continue
            if is_invisible(child) or is_footnote_container(child):
                continue
            sup = in_sup or child.local == "sup"
            link = in_link or is_internal_link(child)
            if is_noteref(child) or (sup and link):
                # A footnote mark: an explicit noteref, or text inside both a <sup> and an
                # internal link (in either nesting order), as the firmware's word pass decides.
                if show_noterefs:
                    out.append(f"[^{' '.join(tokenize(child.text())) or '*'}]")
                continue
            if atomic_kind(child) is not None:
                if _is_block_level(child):
                    out.append(" ")
                continue
            if child.ns == XHTML_NS and child.local == "br":
                out.append(" ")
                continue
            block = _is_block_level(child)
            if block:
                out.append(" ")
            walk(child, sup, link)
            if block:
                out.append(" ")

    if is_invisible(el) or is_noteref(el) or all(_mark_flags(el)):
        return ""
    walk(el, *_mark_flags(el))
    return "".join(out)


def all_text(el: Element) -> str:
    """Every visible character of ``el``, including note marks, captions, cells and code.

    Block-level boundaries and ``br`` count as a space. Used to compare a
    document before and after preparation: an inserted empty element changes
    nothing here.
    """
    out: list[str] = []

    def walk(node: Element) -> None:
        for child in node.children:
            if isinstance(child, str):
                out.append(child)
                continue
            if is_invisible(child):
                continue
            if child.ns == XHTML_NS and child.local == "br":
                out.append(" ")
                continue
            block = _is_block_level(child)
            if block:
                out.append(" ")
            walk(child)
            if block:
                out.append(" ")

    walk(el)
    return "".join(out)


def _has_own_text(el: Element) -> bool:
    for child in el.children:
        if isinstance(child, str):
            if tokenize(child):
                return True
        elif not (is_invisible(child) or _is_block_level(child) or atomic_kind(child) is not None
                  or is_footnote_container(child) or is_noteref(child)):
            if tokenize(prose_text(child)):
                return True
    return False


def _clip(text: str, limit: int = 120) -> str:
    text = " ".join(tokenize(text))
    return text if len(text) <= limit else text[: limit - 3].rstrip() + "..."


def placeholder_for(el: Element, kind: str) -> str:
    parts: list[str] = []
    if kind == "image":
        alts = [i.get("alt") for i in el.iter() if i.local == "img" and i.get("alt")]
        if alts:
            parts.append(f'alt "{_clip(alts[0])}"')
    if kind == "table":
        rows = [r for r in el.iter() if r.local == "tr"]
        cols = max((len([c for c in r.elements() if c.local in ("td", "th")]) for r in rows), default=0)
        parts.append(f"{len(rows)} rows x {cols} columns")
    if kind == "code":
        lines = el.text().strip("\n").split("\n")
        parts.append(f"{len(lines)} lines")
    captions = [c for c in el.iter() if c.local in ("figcaption", "caption")]
    if captions and tokenize(captions[0].text()):
        parts.append(f'caption "{_clip(captions[0].text())}"')
    label = {"image": "Image", "table": "Table", "code": "Code", "math": "Math", "media": "Media"}[kind]
    return f"[{label}: <{el.local}>" + ("; " + "; ".join(parts) if parts else "") + "]"


def _prose_kind(el: Element) -> str:
    if el.local in HEADINGS:
        return "heading"
    if el.local in LIST_ITEMS:
        return "list-item"
    if el.local == "blockquote":
        return "blockquote"
    node = el.parent
    while node is not None and node.local != "body":
        if node.local in LIST_ITEMS:
            return "list-item"
        if node.local == "blockquote":
            return "blockquote"
        if node.local == "aside":
            return "aside"
        node = node.parent
    return "paragraph"


# --------------------------------------------------------------------------
# Segmentation


@dataclass
class _Raw:
    element: Element
    kind: str
    words: list[str]
    placeholder: str | None
    note_id: str | None


def _segment(body: Element) -> tuple[list[_Raw], list[_Raw]]:
    main: list[_Raw] = []
    notes: list[_Raw] = []

    def visit(el: Element, note: Element | None) -> None:
        if is_invisible(el) or is_noteref(el):
            return
        target = notes if note is not None else main
        if note is None and is_footnote_container(el):
            note = el
            target = notes
        note_id = note.get("id") if note is not None else None
        kind = atomic_kind(el)
        if kind is not None:
            target.append(_Raw(el, "footnote" if note is not None else kind, [], placeholder_for(el, kind), note_id))
            return
        if _has_own_text(el):
            words = tokenize(prose_text(el))
            if words:
                target.append(_Raw(el, "footnote" if note is not None else _prose_kind(el), words, None, note_id))
            return
        for child in el.elements():
            visit(child, note)

    visit(body, None)
    return main, notes


def segment_document(sd: SpineDoc, spine_count: int) -> None:
    """Fill ``sd.blocks``, ``sd.notes`` and ``sd.words`` from ``sd.doc``."""
    assert sd.doc is not None
    root = sd.doc.root
    body = next((c for c in root.elements() if c.ns == XHTML_NS and c.local == "body"), None)
    head = next((c for c in root.elements() if c.ns == XHTML_NS and c.local == "head"), None)
    if head is not None:
        title = head.first(XHTML_NS, "title")
        if title is not None and tokenize(title.text()):
            sd.title = " ".join(tokenize(title.text()))
    if body is None:
        return
    main, notes = _segment(body)
    words: list[str] = []
    for number, raw in enumerate(main, start=1):
        ref = refs.paragraph_ref(sd.index, spine_count, number, len(main), refs.MAIN)
        first = len(words) if raw.words else None
        sd.blocks.append(Block(ref, refs.MAIN, number, raw.kind, raw.element, raw.words, first, len(words) - 1,
                               raw.placeholder, raw.note_id))
        words.extend(raw.words)
    sd.words = words
    for number, raw in enumerate(notes, start=1):
        ref = refs.paragraph_ref(sd.index, spine_count, number, len(notes), refs.NOTE)
        sd.notes.append(Block(ref, refs.NOTE, number, raw.kind, raw.element, raw.words, None, -1, raw.placeholder,
                              raw.note_id))


# --------------------------------------------------------------------------
# Package


def _resolve(base_dir: str, href: str, where: str) -> str:
    path = urllib.parse.unquote(urllib.parse.urldefrag(href)[0])
    joined = posixpath.normpath(posixpath.join(base_dir, path)) if path else base_dir
    if joined.startswith("../") or joined == ".." or posixpath.isabs(joined):
        raise EpubError(f"{where}: {href!r} points outside the container")
    return joined


def _parse_member(archive: Archive, name: str) -> Document:
    try:
        return parse(archive.read(name), name)
    except KeyError:
        raise EpubError(f"{name} is missing from the archive") from None
    except (XmlError, ZipFormatError) as exc:
        raise EpubError(str(exc)) from exc


def _text_of_first(root: Element, ns: str, local: str) -> str | None:
    el = root.first(ns, local)
    if el is None:
        return None
    words = tokenize(el.text())
    return " ".join(words) if words else None


def _rel_to_opf(opf_dir: str, zip_path: str, fragment: str) -> str:
    rel = posixpath.relpath(zip_path, opf_dir or ".")
    return rel + (f"#{fragment}" if fragment else "")


def _nav_entries(nav_path: str, doc: Document) -> list[tuple[str, str, int]]:
    """(label, target zip path#fragment, depth) of the nav document's toc."""
    nav_dir = posixpath.dirname(nav_path)
    toc = next((n for n in doc.root.find_all(XHTML_NS, "nav") if "toc" in _tokens(n.get(EPUB_TYPE))), None)
    out: list[tuple[str, str, int]] = []
    if toc is None:
        return out

    def walk_list(ol: Element, depth: int) -> None:
        for li in ol.elements():
            if li.local != "li":
                continue
            label_el = next((c for c in li.elements() if c.local in ("a", "span")), None)
            if label_el is not None:
                href = label_el.get("href") or ""
                target = ""
                if href:
                    target = _resolve(nav_dir, href, nav_path) + (
                        "#" + urllib.parse.urldefrag(href)[1] if urllib.parse.urldefrag(href)[1] else "")
                out.append((" ".join(tokenize(label_el.text())), target, depth))
            for sub in li.elements():
                if sub.local == "ol":
                    walk_list(sub, depth + 1)

    for ol in toc.elements():
        if ol.local == "ol":
            walk_list(ol, 1)
    return out


def _ncx_entries(ncx_path: str, doc: Document) -> list[tuple[str, str, int]]:
    ncx_dir = posixpath.dirname(ncx_path)
    out: list[tuple[str, str, int]] = []
    nav_map = doc.root.first(NCX_NS, "navMap")
    if nav_map is None:
        return out

    def walk(parent: Element, depth: int) -> None:
        for point in parent.elements():
            if point.local != "navPoint":
                continue
            label = point.first(NCX_NS, "text")
            content = next((c for c in point.elements() if c.local == "content"), None)
            src = content.get("src") if content is not None else ""
            target = ""
            if src:
                frag = urllib.parse.urldefrag(src)[1]
                target = _resolve(ncx_dir, src, ncx_path) + (f"#{frag}" if frag else "")
            out.append((" ".join(tokenize(label.text())) if label is not None else "", target, depth))
            walk(point, depth + 1)

    walk(nav_map, 1)
    return out


def load_book(path: str | Path) -> Book:
    """Read and segment an EPUB. Raises ``EpubError`` for any problem with the file."""
    path = Path(path)
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise EpubError(f"cannot read {path}: {exc.strerror}") from exc
    return load_book_bytes(data, path.name)


def load_book_bytes(data: bytes, source_name: str) -> Book:
    try:
        archive = Archive(data)
    except ZipFormatError as exc:
        raise EpubError(f"{source_name}: {exc}") from exc
    container = _parse_member(archive, CONTAINER_PATH)
    rootfile = next(
        (r for r in container.root.find_all(CONTAINER_NS, "rootfile")
         if r.get("media-type") == "application/oebps-package+xml"),
        None,
    )
    if rootfile is None or not rootfile.get("full-path"):
        raise EpubError(f"{CONTAINER_PATH}: no rootfile for the package document")
    opf_path = rootfile.get("full-path") or ""
    opf_dir = posixpath.dirname(opf_path)
    opf = _parse_member(archive, opf_path)
    package = opf.root
    if package.ns != OPF_NS or package.local != "package":
        raise EpubError(f"{opf_path}: root element is not an OPF package")

    title = _text_of_first(package, DC_NS, "title")
    language = _text_of_first(package, DC_NS, "language")
    manifest_el = package.first(OPF_NS, "manifest")
    spine_el = package.first(OPF_NS, "spine")
    if manifest_el is None or spine_el is None:
        raise EpubError(f"{opf_path}: missing manifest or spine")
    manifest: list[ManifestItem] = []
    by_id: dict[str, ManifestItem] = {}
    for item in manifest_el.elements():
        if item.ns != OPF_NS or item.local != "item":
            continue
        href = item.get("href") or ""
        mi = ManifestItem(item.get("id") or "", href, item.get("media-type") or "", (item.get("properties") or "").split(),
                          _resolve(opf_dir, href, opf_path) if href else "", item)
        manifest.append(mi)
        by_id[mi.id] = mi

    itemrefs = [r for r in spine_el.elements() if r.ns == OPF_NS and r.local == "itemref"]
    spine: list[SpineDoc] = []
    for index, ref_el in enumerate(itemrefs, start=1):
        idref = ref_el.get("idref") or ""
        mi = by_id.get(idref)
        if mi is None:
            raise EpubError(f"{opf_path}: spine itemref {idref!r} names no manifest item")
        spine.append(SpineDoc(index, idref, mi.href, mi.media_type, ref_el.get("linear", "yes") != "no", mi.zip_path,
                              refs.document_prefix(index, len(itemrefs))))
    for sd in spine:
        if sd.is_xhtml:
            sd.doc = _parse_member(archive, sd.zip_path)
            segment_document(sd, len(spine))

    toc_raw: list[tuple[str, str, int]] = []
    source = ""
    nav = next((m for m in manifest if "nav" in m.properties), None)
    if nav is not None:
        toc_raw = _nav_entries(nav.zip_path, _parse_member(archive, nav.zip_path))
        source = "nav"
    if not toc_raw:
        ncx_id = spine_el.get("toc")
        ncx = by_id.get(ncx_id or "") or next((m for m in manifest if m.media_type == "application/x-dtbncx+xml"), None)
        if ncx is not None:
            toc_raw = _ncx_entries(ncx.zip_path, _parse_member(archive, ncx.zip_path))
            source = "ncx"
    by_zip = {sd.zip_path: sd for sd in spine}
    toc: list[TocEntry] = []
    for label, target, depth in toc_raw:
        zip_path, _, fragment = target.partition("#")
        sd = by_zip.get(zip_path)
        ref = None
        if sd is not None and sd.doc is not None:
            el = sd.doc.ids.get(fragment) if fragment else None
            block = sd.block_for_element(el) if el is not None else (sd.blocks[0] if sd.blocks else None)
            ref = block.ref if block is not None else None
        toc.append(TocEntry(label, _rel_to_opf(opf_dir, zip_path, fragment) if zip_path else "", depth, source, ref))

    return Book(source_name, hashlib.sha256(data).hexdigest(), archive, opf_path, opf_dir, opf, title, language,
                manifest, spine, toc)


def iter_blocks(book: Book) -> Iterator[tuple[SpineDoc, Block]]:
    for sd in book.spine:
        for b in sd.blocks:
            yield sd, b
