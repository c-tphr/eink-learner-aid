"""Front, body and back matter as the EPUB itself marks it (for the validator's ``exclusions`` check).

Only the book's structural markup is read, never its words, so the result
does not depend on the book's language:

1. ``epub:type`` on a spine document's ``html`` and ``body`` elements, and on
   the body's only element child when it has just one (a wrapping
   ``section``);
2. the types the nav document's ``landmarks`` and the EPUB 2 ``guide`` give
   a link to the document;
3. the document's place relative to the landmarks: before the first
   document a ``bodymatter`` landmark (or the guide's ``text`` reference)
   points to is front matter; from there up to the first document marked
   front or back matter by rules 1 or 2 is body matter; after that, back
   matter.

The first rule that says anything decides. A document is ``front-back``
(front or back matter), ``body`` (body matter), or ``unmarked`` (the EPUB
says nothing). A preface or introduction marked only as such is not
classified, because it may carry the author's own text
(``INSTRUCTIONS.md`` section 3).
"""

from __future__ import annotations

import posixpath
from dataclasses import dataclass

from .epub import EPUB_TYPE, OPF_NS, Book, EpubError, SpineDoc, _parse_member, _resolve
from .xmltree import XHTML_NS, Element

FRONT_BACK, BODY, UNMARKED = "front-back", "body", "unmarked"

# epub:type values (EPUB Structural Semantics Vocabulary) that name front or back matter.
FRONT_BACK_TYPES = frozenset({
    "frontmatter", "backmatter", "cover", "frontispiece", "titlepage", "halftitlepage", "seriespage",
    "copyright-page", "imprint", "imprimatur", "colophon", "dedication", "epigraph", "toc", "landmarks", "loi",
    "lot", "lov", "index", "bibliography", "glossary", "endnotes", "footnotes", "rearnotes", "acknowledgments",
    "contributors", "other-credits", "errata", "credits",
})
BODY_TYPES = frozenset({"bodymatter", "chapter", "part", "division", "volume"})
# EPUB 2 guide reference types for front or back matter; "text" marks the start of the body.
GUIDE_FRONT_BACK_TYPES = frozenset({
    "cover", "title-page", "copyright-page", "colophon", "dedication", "epigraph", "toc", "loi", "lot", "index",
    "glossary", "bibliography", "notes", "acknowledgements",
})


@dataclass(frozen=True)
class Role:
    kind: str  # FRONT_BACK, BODY or UNMARKED
    evidence: str  # what in the EPUB said so, for the check's details


def _tokens(value: str | None) -> set[str]:
    return set((value or "").split())


def _own_types(sd: SpineDoc) -> set[str]:
    if sd.doc is None:
        return set()
    root = sd.doc.root
    found = _tokens(root.get(EPUB_TYPE))
    body = root.first(XHTML_NS, "body")
    if body is not None:
        found |= _tokens(body.get(EPUB_TYPE))
        children = list(body.elements())
        if len(children) == 1:
            found |= _tokens(children[0].get(EPUB_TYPE))
    return found


def _links(book: Book) -> dict[str, list[tuple[str, set[str]]]]:
    """Zip path -> [(source, types)] from the nav landmarks and the EPUB 2 guide."""
    out: dict[str, list[tuple[str, set[str]]]] = {}
    nav = next((m for m in book.manifest if "nav" in m.properties), None)
    if nav is not None:
        try:
            doc = _parse_member(book.archive, nav.zip_path)
        except EpubError:
            doc = None
        landmarks = None if doc is None else next(
            (n for n in doc.root.find_all(XHTML_NS, "nav") if "landmarks" in _tokens(n.get(EPUB_TYPE))), None)
        if landmarks is not None:
            for a in landmarks.find_all(XHTML_NS, "a"):
                _add(out, posixpath.dirname(nav.zip_path), a, "landmarks", _tokens(a.get(EPUB_TYPE)))
    guide = book.opf.root.first(OPF_NS, "guide")
    if guide is not None:
        for ref in guide.elements():
            if ref.ns == OPF_NS and ref.local == "reference":
                _add(out, book.opf_dir, ref, "guide", _tokens(ref.get("type")))
    return out


def _add(out: dict[str, list[tuple[str, set[str]]]], base: str, el: Element, source: str, types: set[str]) -> None:
    href = el.get("href")
    if not href or not types:
        return
    try:
        target = _resolve(base, href, source)
    except EpubError:
        return
    out.setdefault(target, []).append((source, types))


def _linked(links: list[tuple[str, set[str]]]) -> Role | None:
    for source, types in links:
        hits = (types & GUIDE_FRONT_BACK_TYPES) if source == "guide" else (types & FRONT_BACK_TYPES)
        if hits:
            return Role(FRONT_BACK, f"{source} type {' '.join(sorted(hits))}")
    for source, types in links:
        hits = types & ({"text"} if source == "guide" else {"bodymatter"})
        if hits:
            return Role(BODY, f"{source} type {' '.join(sorted(hits))}")
    return None


def document_roles(book: Book) -> dict[str, Role]:
    """The role of every XHTML spine document, by zip path."""
    links = _links(book)
    direct: dict[str, Role | None] = {}
    for sd in book.spine:
        own = _own_types(sd)
        if own & BODY_TYPES:
            direct[sd.zip_path] = Role(BODY, f"epub:type {' '.join(sorted(own & BODY_TYPES))}")
        elif own & FRONT_BACK_TYPES:
            direct[sd.zip_path] = Role(FRONT_BACK, f"epub:type {' '.join(sorted(own & FRONT_BACK_TYPES))}")
        else:
            direct[sd.zip_path] = _linked(links.get(sd.zip_path, []))
    start = next((i for i, sd in enumerate(book.spine)
                  if any(t & ({"text"} if s == "guide" else {"bodymatter"}) for s, t in links.get(sd.zip_path, []))),
                 None)
    roles: dict[str, Role] = {}
    after_body = False
    for i, sd in enumerate(book.spine):
        role = direct[sd.zip_path]
        if start is not None and i > start and role is not None and role.kind == FRONT_BACK:
            after_body = True
        if role is None:
            if start is None:
                role = Role(UNMARKED, "no structural markup")
            elif i < start:
                role = Role(FRONT_BACK, "before the landmarks' start of body matter")
            elif after_body:
                role = Role(FRONT_BACK, "after the first front or back matter document that follows the body")
            else:
                role = Role(BODY, "inside the landmarks' body matter")
        roles[sd.zip_path] = role
    return roles
