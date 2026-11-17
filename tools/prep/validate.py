"""``prep validate``: prove a prepared EPUB is a faithful, well-formed copy of its original.

    python3 -m tools.prep validate ORIGINAL.epub PREPARED.epub [--draft DRAFT.json | --work-dir DIR]
                                   [--json OUT] [--timestamp ISO]

Everything a script can verify about a prepared book is verified here
(docs/PLAN.md section 8, "What the validator script checks"). Each check is
named, and each failure has a stable ``code``, a location and a message; the
checks and codes are listed in tools/prep/README.md ("prep validate"). The
result is pass only when every check has status ``pass``: a check that
could not run (EPUBCheck unavailable, say) or ran on only part of the book
because an earlier check failed is not a pass.

Exclusions (front and back matter in no section) are not in the checkpoint
file. They are read from the draft the book was built from (``--draft``, or
``draft.json`` in ``--work-dir``); without a draft the coverage check runs
in "implicit" mode: a spine document with no section at all counts as
excluded as a whole, and every other document must be covered completely.
"""

from __future__ import annotations

import datetime as _dt
import hashlib
import json
import os
import posixpath
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

from . import checkpoints as cp
from . import draft as draft_mod
from . import epubcheck as epubcheck_mod
from . import matter
from .build import CHECKPOINTS_HREF, CHECKPOINTS_ID, CHECKPOINTS_MEDIA_TYPE
from .epub import OPF_NS, XHTML_MEDIA_TYPE, Book, EpubError, SpineDoc, load_book_bytes
from .epubzip import ZipFormatError
from .limits import check_limits
from .version import TOOL_NAME, TOOL_VERSION, WORD_RULES_VERSION
from .words import FLAGS, NOTE, Anchor
from .xmltree import XML_NS, XHTML_NS, Element, XmlError
from .xmltree import parse as parse_xml

FORMAT = "rsvp-validation"
VERSION = 1
SCHEMA_PATH = Path(__file__).resolve().parent / "schema" / "validation.schema.json"

PASS, FAIL, INCOMPLETE, NOT_RUN = "pass", "fail", "incomplete", "not-run"
STATUSES = (PASS, FAIL, INCOMPLETE, NOT_RUN)

# (name, what it confirms), in the order the checks run and are reported.
CHECKS = [
    ("open", "Both files open as EPUBs and every spine document parses"),
    ("source-hash", "The checkpoint file names the original's SHA-256, and the original was not changed"),
    ("checkpoint-item", "The package declares rsvp/checkpoints.json exactly once, as application/json"),
    ("checkpoint-file", "The checkpoint file passes its schema and cross-reference rules (unique ids, plain text)"),
    ("firmware-limits", "The checkpoint file is within the firmware's limits"),
    ("anchors", "Every section's two anchors exist once, in its chapter, as empty spans, in order, without overlap"),
    ("word-boundaries", "Every anchor sits between two words, outside tables and code blocks"),
    ("coverage", "Sections cover the body text; everything else is footnotes or listed excluded matter"),
    ("exclusions", "Excluded text is front, back or editorial matter, not the body of the book"),
    ("word-counts", "Every section's word_count equals the words between its anchors"),
    ("text-unchanged", "Every spine document has the original's words, flags, markers and footnote marks"),
    ("archive-unchanged", "Only the anchors, one manifest line and the checkpoint file differ from the original"),
    ("epubcheck", "EPUBCheck reports no errors"),
]
TITLES = dict(CHECKS)

EpubcheckRunner = Callable[[Path], "epubcheck_mod.Report"]


# --------------------------------------------------------------------------
# Results


@dataclass(frozen=True)
class Failure:
    code: str
    location: str
    message: str

    def as_json(self) -> dict[str, str]:
        return {"code": self.code, "location": self.location, "message": self.message}


@dataclass
class Check:
    name: str
    failures: list[Failure] = field(default_factory=list)
    details: list[str] = field(default_factory=list)
    status: str | None = None  # set by finish(); NOT_RUN may be set early
    incomplete: bool = False

    def fail(self, code: str, location: str, message: str) -> None:
        self.failures.append(Failure(code, location, message))

    def not_run(self, reason: str) -> None:
        self.status = NOT_RUN
        self.details.append(reason)

    def finish(self) -> None:
        if self.status == NOT_RUN:
            return
        self.status = FAIL if self.failures else (INCOMPLETE if self.incomplete else PASS)

    def as_json(self) -> dict[str, Any]:
        return {"name": self.name, "title": TITLES[self.name], "status": self.status,
                "failures": [f.as_json() for f in self.failures], "details": self.details}


@dataclass
class Resolved:
    """A section whose anchors were found once each, in its own chapter, in order."""

    index: int
    id: str
    doc: SpineDoc
    start_el: Element
    end_el: Element
    start_anchor: Anchor
    end_anchor: Anchor

    @property
    def start(self) -> int:
        return self.start_anchor.index

    @property
    def end(self) -> int:
        return self.end_anchor.index


@dataclass
class Outcome:
    """What ``validate_bytes`` found, for callers that need more than the JSON (verify-check, report)."""

    result: dict[str, Any]
    checks: dict[str, Check]
    prepared: Book | None = None
    original: Book | None = None
    checkpoints: dict[str, Any] | None = None
    resolved: dict[str, Resolved] = field(default_factory=dict)

    @property
    def passed(self) -> bool:
        return bool(self.result["passed"])


def timestamp(value: str | None = None) -> str:
    """The time recorded in results: ``value`` (ISO 8601), else ``$SOURCE_DATE_EPOCH``, else now (UTC)."""
    if value:
        try:
            parsed = _dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
        except ValueError as exc:
            raise ValueError(f"not an ISO 8601 time: {value!r}") from exc
        if parsed.tzinfo is None:
            parsed = parsed.replace(tzinfo=_dt.timezone.utc)
        moment = parsed.astimezone(_dt.timezone.utc)
    elif os.environ.get("SOURCE_DATE_EPOCH"):
        moment = _dt.datetime.fromtimestamp(int(os.environ["SOURCE_DATE_EPOCH"]), _dt.timezone.utc)
    else:
        moment = _dt.datetime.now(_dt.timezone.utc)
    return moment.replace(microsecond=0).strftime("%Y-%m-%dT%H:%M:%SZ")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _words(texts: list[str], start: int, end: int, limit: int = 8) -> str:
    part = texts[max(0, start):min(len(texts), end)]
    shown = " ".join(part[:limit]) + (" ..." if len(part) > limit else "")
    return f"'{shown}'"


def _rsvp_ids(el: Element) -> list[str]:
    return [v for v in (el.get("id"), el.get(f"{{{XML_NS}}}id")) if v is not None and v.startswith("rsvp-")]


def checkpoint_zip_path(book: Book) -> str:
    return posixpath.join(book.opf_dir, CHECKPOINTS_HREF) if book.opf_dir else CHECKPOINTS_HREF


# --------------------------------------------------------------------------
# The checks


def _check_item(book: Book, check: Check) -> bytes | None:
    """``checkpoint-item``: the manifest item and the member. Returns the checkpoint file's bytes."""
    target = checkpoint_zip_path(book)
    items = [m for m in book.manifest if m.id == CHECKPOINTS_ID]
    if not items:
        check.fail("checkpoint-item-missing", book.opf_path,
                   f"the manifest has no item with id {CHECKPOINTS_ID!r}; the firmware would open the book as an "
                   "ordinary EPUB")
    elif len(items) > 1:
        check.fail("checkpoint-item-repeated", book.opf_path,
                   f"the manifest has {len(items)} items with id {CHECKPOINTS_ID!r}")
    for item in items:
        if item.media_type != CHECKPOINTS_MEDIA_TYPE:
            check.fail("checkpoint-item-media-type", book.opf_path,
                       f"item {CHECKPOINTS_ID!r} has media type {item.media_type!r}, not {CHECKPOINTS_MEDIA_TYPE!r}")
        if item.zip_path != target:
            check.fail("checkpoint-item-href", book.opf_path,
                       f"item {CHECKPOINTS_ID!r} points to {item.href!r} ({item.zip_path}), not {CHECKPOINTS_HREF!r} "
                       f"beside the package document ({target})")
    others = [m for m in book.manifest if m.id != CHECKPOINTS_ID and m.zip_path == target]
    for item in others:
        check.fail("checkpoint-item-repeated", book.opf_path,
                   f"item {item.id!r} also declares {target}")
    if target not in book.archive.by_name:
        check.fail("checkpoint-file-missing", target, f"the archive has no member {target}")
        return None
    try:
        return book.archive.read(target)
    except ZipFormatError as exc:
        check.fail("checkpoint-file-missing", target, f"cannot read {target}: {exc}")
        return None


def _resolve_anchors(book: Book, data: dict[str, Any], check: Check, cp_path: str) -> tuple[dict[str, Resolved], set[str]]:
    """``anchors``: find each section's anchors. Returns the resolved sections and the documents a broken
    section touches (coverage cannot be judged there)."""
    occurrences: dict[str, list[tuple[SpineDoc | None, str, Element]]] = {}
    xhtml_docs = [sd for sd in book.spine if sd.is_xhtml and sd.doc is not None]
    for sd in xhtml_docs:
        assert sd.doc is not None
        for el in sd.doc.root.iter():
            for value in _rsvp_ids(el):
                occurrences.setdefault(value, []).append((sd, sd.zip_path, el))
    spine_paths = {sd.zip_path for sd in book.spine}
    for item in book.manifest:  # XHTML outside the spine (the nav document, say) must not carry anchors either
        if item.media_type != XHTML_MEDIA_TYPE or item.zip_path in spine_paths or item.zip_path not in book.archive.by_name:
            continue
        try:
            doc = parse_xml(book.archive.read(item.zip_path), item.zip_path)
        except (XmlError, ZipFormatError):
            continue  # EPUBCheck reports a broken document
        for el in doc.root.iter():
            for value in _rsvp_ids(el):
                occurrences.setdefault(value, []).append((None, item.zip_path, el))

    sections = data["sections"]
    linked = {}
    for c in data["checkpoints"]:
        linked.setdefault(c["section"], c["id"])
    expected_ids: set[str] = set()
    resolved: dict[str, Resolved] = {}
    broken_docs: set[str] = set()
    for i, section in enumerate(sections):
        sid = section["id"]
        names = (f"rsvp-{sid}-start", f"rsvp-{sid}-end")
        expected_ids.update(names)
        base = f"{cp_path}#/sections/{i}"
        sd = book.doc_by_href(section["chapter_href"])
        if sd is None or not sd.is_xhtml or sd.doc is None:
            known = ", ".join(d.href for d in xhtml_docs)
            check.fail("unknown-document", f"{base}/chapter_href",
                       f"section {sid}: chapter_href {section['chapter_href']!r} is not an XHTML spine document "
                       f"(spine documents: {known})")
            for name in names:
                for other, _, _ in occurrences.get(name, []):
                    if other is not None:
                        broken_docs.add(other.href)
            continue
        found: list[tuple[SpineDoc, Element] | None] = []
        homes: list[str | None] = []
        touched = {sd.href}
        for side, name in zip(("start", "end"), names):
            places = occurrences.get(name, [])
            here = [(d, el) for d, path, el in places if d is sd]
            elsewhere = [path for d, path, el in places if d is not sd]
            touched.update(d.href for d, _, _ in places if d is not None)
            if not places:
                check.fail("anchor-missing", f"{sd.zip_path}#{name}",
                           f"section {sid}: the {side} anchor {name!r} is not in {sd.href} or any other document"
                           + (f"; checkpoint {linked[sid]} links to this section and could never fire"
                              if side == "end" and sid in linked else ""))
                found.append(None)
                homes.append(None)
                continue
            if len(places) > 1:
                where = ", ".join(sorted({p for _, p, _ in places}))
                check.fail("anchor-repeated", f"{sd.zip_path}#{name}",
                           f"section {sid}: the {side} anchor {name!r} occurs {len(places)} times ({where}); it must "
                           "occur exactly once")
            if not here:
                check.fail("anchor-wrong-document", f"{elsewhere[0]}#{name}",
                           f"section {sid}: the {side} anchor {name!r} is in {', '.join(sorted(set(elsewhere)))}, "
                           f"not in its chapter {sd.href}")
                found.append(None)
                homes.append(elsewhere[0])
                continue
            homes.append(sd.zip_path)
            found.append(here[0] if len(places) == 1 else None)
        if homes[0] is not None and homes[1] is not None and homes[0] != homes[1]:
            check.fail("section-spans-documents", f"{base}",
                       f"section {sid} starts in {homes[0]} and ends in {homes[1]}; a section lies inside one "
                       "chapter (plan rule 1)")
        if None in found:
            broken_docs.update(touched)
            continue
        (_, start_el), (_, end_el) = found  # type: ignore[misc]
        ok = True
        assert sd.stream is not None
        anchors = []
        for side, name, el in (("start", names[0], start_el), ("end", names[1], end_el)):
            problems = []
            if not (el.ns == XHTML_NS and el.local == "span"):
                problems.append(f"is a <{el.local}>, not an XHTML <span>")
            if el.children:
                problems.append("has content")
            extra = sorted(k for k in el.attrib if k != "id")
            if extra:
                problems.append(f"has attributes besides id ({', '.join(extra)})")
            if problems:
                check.fail("anchor-element", f"{sd.zip_path}#{name}",
                           f"section {sid}: the {side} anchor {' and '.join(problems)}; an anchor is an empty "
                           "<span id=...></span> that renders nothing")
            anchor = next((a for a in sd.stream.anchors if a.id == name), None)
            if anchor is None:
                check.fail("anchor-not-in-body", f"{sd.zip_path}#{name}",
                           f"section {sid}: the {side} anchor is outside the visible body text, where the "
                           "firmware does not look for anchors")
                ok = False
            anchors.append(anchor)
        if not ok:
            broken_docs.update(touched)
            continue
        if not (start_el.order < end_el.order and anchors[0].index <= anchors[1].index):
            check.fail("anchor-order", f"{sd.zip_path}#{names[0]}",
                       f"section {sid}: the start anchor (word {anchors[0].index}) comes after the end anchor "
                       f"(word {anchors[1].index})")
            broken_docs.update(touched)
            continue
        resolved[sid] = Resolved(i, sid, sd, start_el, end_el, anchors[0], anchors[1])

    # Order and overlap of the sections that resolved, in file order.
    ordered = sorted(resolved.values(), key=lambda r: r.index)
    for a_pos, a in enumerate(ordered):
        for b in ordered[a_pos + 1:]:
            if a.doc is not b.doc:
                continue
            if b.start_el.order > a.end_el.order:
                continue
            if b.end_el.order < a.start_el.order:
                check.fail("section-order", f"{cp_path}#/sections/{b.index}",
                           f"section {b.id} comes before section {a.id} in {a.doc.href} but is listed after it; "
                           "list sections in reading order")
            else:
                check.fail("section-overlap", f"{cp_path}#/sections/{b.index}",
                           f"sections {a.id} (words {a.start}-{a.end}) and {b.id} (words {b.start}-{b.end}) overlap "
                           f"in {a.doc.href}")
    for prev, cur in zip(ordered, ordered[1:]):
        if cur.doc.index < prev.doc.index:
            check.fail("section-order", f"{cp_path}#/sections/{cur.index}",
                       f"section {cur.id} is in {cur.doc.href}, which comes before {prev.doc.href} (section "
                       f"{prev.id}) in the spine; list sections in reading order")

    for anchor_id in sorted(set(occurrences) - expected_ids):
        where = ", ".join(sorted({p for _, p, _ in occurrences[anchor_id]}))
        check.fail("unaccounted-anchor", f"{where}#{anchor_id}",
                   f"id {anchor_id!r} starts with 'rsvp-' but is no anchor of a section in the checkpoint file; "
                   "the firmware treats every rsvp- id as an anchor")
    if len(resolved) < len(sections):
        check.incomplete = True
    check.details.append(f"{len(resolved)} of {len(sections)} sections resolved to two anchors in their chapter")
    return resolved, broken_docs


def _check_boundaries(resolved: dict[str, Resolved], total: int, check: Check) -> None:
    """``word-boundaries``: no anchor strictly inside a word, inside a table or inside a code block."""
    for r in resolved.values():
        for side, el, anchor in (("start", r.start_el, r.start_anchor), ("end", r.end_el, r.end_anchor)):
            where = f"{r.doc.zip_path}#{anchor.id}"
            if anchor.inside_word:
                words = r.doc.words
                around = words[anchor.index - 1] if 0 < anchor.index <= len(words) else "?"
                check.fail("anchor-inside-word", where,
                           f"section {r.id}: the {side} anchor splits the word {around!r}; the firmware would put "
                           "the section boundary somewhere else")
            node = el.parent
            while node is not None:
                local = node.local.lower()
                if local == "table":
                    check.fail("anchor-in-table", where, f"section {r.id}: the {side} anchor is inside a table")
                    break
                if local == "pre":
                    check.fail("anchor-in-code", where, f"section {r.id}: the {side} anchor is inside a code block "
                                                        "(<pre>)")
                    break
                node = node.parent
    if len(resolved) < total:
        check.incomplete = True
    check.details.append(f"{2 * len(resolved)} anchors checked")


def _exclusions(draft_data: dict[str, Any], original: Book, check: Check) -> dict[str, list[tuple[int, int, str]]]:
    """The draft's excluded ranges as word ranges of the original's documents, by href."""
    resolver = draft_mod._Resolver(original)
    out: dict[str, list[tuple[int, int, str]]] = {}
    for i, entry in enumerate(draft_data["excluded"]):
        issues: list[draft_mod.Issue] = []
        r = resolver.range(entry, f"/excluded/{i}", issues)
        for issue in issues:
            check.fail("draft-exclusion", f"draft.json#{issue.path}", f"excluded range {i}: {issue.message}")
        if r is None:
            continue
        sd, first, last = r
        out.setdefault(sd.href, []).append((sd.blocks[first].start, sd.blocks[last].end, entry["reason"]))
    return out


def _load_draft(raw: bytes, original: Book | None, data: dict[str, Any] | None, check: Check) -> dict[str, Any] | None:
    parsed, issues = cp.parse_checkpoints(raw)
    if issues:
        for issue in issues:
            check.fail("draft-unreadable", f"draft.json#{issue.path}", issue.message)
        return None
    schema_issues = draft_mod.validate_schema(parsed)
    for issue in schema_issues:
        check.fail("draft-unreadable", f"draft.json#{issue.path}", issue.message)
    if schema_issues:
        return None
    if original is not None and parsed["book"]["source_sha256"] != original.sha256:
        check.fail("draft-mismatch", "draft.json#/book/source_sha256",
                   f"the draft was made from an EPUB with SHA-256 {parsed['book']['source_sha256']}, not the "
                   f"original given ({original.sha256})")
    if data is not None:
        mine = [(s["id"], s["chapter_href"], s["label"]) for s in parsed["sections"]]
        theirs = [(s["id"], s["chapter_href"], s["label"]) for s in data["sections"]]
        if mine != theirs:
            check.fail("draft-mismatch", "draft.json#/sections",
                       "the draft's sections (ids, chapters, labels) are not the checkpoint file's; give the draft "
                       "this file was built from")
    return parsed


def _note_mask(sd: SpineDoc) -> list[bool]:
    """For each word of the document: is it in a footnote or endnote body (a NOTE region)?"""
    n = len(sd.words)
    note = [False] * n
    for marker in sd.stream.markers if sd.stream is not None else []:
        if marker.kind == NOTE:
            for k in range(marker.start, min(marker.end, n)):
                note[k] = True
    return note


Exclusions = dict[str, list[tuple[int, int, str]]]


def _check_coverage(original: Book | None, prepared: Book, data: dict[str, Any], resolved: dict[str, Resolved],
                    broken_docs: set[str], draft_raw: bytes | None,
                    check: Check) -> tuple[str, dict[str, Any] | None, Exclusions]:
    """``coverage``. Returns the mode used ('draft' or 'implicit'), the usable draft and its excluded ranges."""
    excluded: Exclusions = {}
    mode = "implicit"
    draft_data = None
    if draft_raw is not None:
        mode = "draft"
        draft_data = _load_draft(draft_raw, original, data, check)
        if draft_data is None or original is None:
            check.details.append("coverage not judged: the draft cannot be used")
            check.incomplete = True
            return mode, None, excluded
        excluded = _exclusions(draft_data, original, check)
    section_docs = {s["chapter_href"] for s in data["sections"]}
    for sd in prepared.spine:
        if not sd.is_xhtml or sd.stream is None:
            continue
        if sd.href in broken_docs:
            check.incomplete = True
            check.details.append(f"{sd.href}: not judged, a section's anchors there did not resolve")
            continue
        n = len(sd.words)
        note = _note_mask(sd)
        secs = sorted((r for r in resolved.values() if r.doc is sd), key=lambda r: r.start)
        if mode == "implicit" and not secs and sd.href not in section_docs:
            body = n - sum(note)
            check.details.append(f"{sd.href}: no section; treated as excluded matter ({body} words outside "
                                 "footnotes)" if body else f"{sd.href}: no section and no words outside footnotes")
            continue
        covered = [False] * n
        for r in secs:
            for k in range(r.start, min(r.end, n)):
                covered[k] = True
        out = [False] * n
        for start, end, reason in excluded.get(sd.href, []):
            overlap = [k for k in range(start, min(end, n)) if covered[k]]
            if overlap:
                check.fail("excluded-in-section", f"{sd.zip_path} words {overlap[0]}-{overlap[-1]}",
                           f"{len(overlap)} words are both in a section and in an excluded range ({reason!r})")
            for k in range(start, min(end, n)):
                out[k] = True
            span = f"words {start}-{end - 1}" if end > start else "a range with no words"
            check.details.append(f"{sd.href}: {span} excluded by the draft: {reason}")
        k = 0
        while k < n:
            if covered[k] or out[k] or note[k]:
                k += 1
                continue
            run_end = k
            while run_end < n and not (covered[run_end] or out[run_end] or note[run_end]):
                run_end += 1
            check.fail("uncovered-words", f"{sd.zip_path} words {k}-{run_end - 1}",
                       f"{run_end - k} words are in no section and not excluded: {_words(sd.words, k, run_end)}")
            k = run_end
        check.details.append(f"{sd.href}: {sum(covered)} of {n} words in sections, {sum(note)} in footnotes")
    return mode, draft_data, excluded


# What may be excluded (INSTRUCTIONS.md section 3; tools/prep/README.md, "The exclusions check").
EXCLUSION_CATEGORIES = ("Front matter", "Back matter", "Editorial matter")
MAX_EXCLUDED_BODY_PERCENT = 10


def exclusion_category(reason: str) -> str | None:
    """The category a reason starts with ('Front matter: title page.' -> 'Front matter'), else None."""
    text = reason.strip().lower()
    for name in EXCLUSION_CATEGORIES:
        if text.startswith(name.lower()) and (len(text) == len(name) or text[len(name)] in ": "):
            return name
    return None


def _check_exclusions(prepared: Book, data: dict[str, Any], resolved: dict[str, Resolved], mode: str,
                      draft_data: dict[str, Any] | None, excluded: Exclusions, check: Check) -> None:
    """``exclusions``: the excluded text is what INSTRUCTIONS.md section 3 allows to leave out.

    Which documents the EPUB marks as front, back or body matter comes from ``matter.py``. Words are
    counted outside NOTE regions. In implicit mode a document without a section counts as excluded.
    """
    if mode == "draft" and draft_data is None:
        check.details.append("not judged: the draft cannot be used (see coverage)")
        check.incomplete = True
        return
    if draft_data is not None:
        for i, entry in enumerate(draft_data["excluded"]):
            if exclusion_category(entry["reason"]) is None:
                check.fail("exclusion-category", f"draft.json#/excluded/{i}/reason",
                           f"the reason {entry['reason']!r} does not start with one of the categories "
                           f"{', '.join(EXCLUSION_CATEGORIES)}; nothing else may be excluded")
    if not resolved or len(resolved) != len(data["sections"]):
        check.details.append("the excluded words are not judged: a section's anchors did not resolve")
        check.incomplete = True
        return
    roles = matter.document_roles(prepared)
    first_start = min((r.doc.index, r.start) for r in resolved.values())
    last_end = max((r.doc.index, r.end) for r in resolved.values())
    section_docs = {r.doc.index for r in resolved.values()}
    section_hrefs = {s["chapter_href"] for s in data["sections"]}
    total = 0
    counted: list[tuple[str, int, str]] = []
    for sd in prepared.spine:
        if not sd.is_xhtml or sd.stream is None:
            continue
        n = len(sd.words)
        note = _note_mask(sd)
        words_here = n - sum(note)
        total += words_here
        if mode == "draft":
            ranges = [(start, min(end, n)) for start, end, _ in excluded.get(sd.href, [])]
        elif sd.index not in section_docs and sd.href not in section_hrefs:
            ranges = [(0, n)]
        else:
            ranges = []
        out = [False] * n
        for start, end in ranges:
            for k in range(start, end):
                out[k] = not note[k]
        excluded_here = sum(out)
        if not excluded_here:
            continue
        role = roles[sd.zip_path]
        check.details.append(f"{sd.href}: {excluded_here} words excluded; the EPUB marks it as {role.kind} "
                             f"({role.evidence})")
        if role.kind == matter.FRONT_BACK:
            continue
        # Body matter always counts; an unmarked document counts only between the first and last section.
        count = sum(sum(out[start:end]) for start, end in ranges
                    if role.kind == matter.BODY or first_start < (sd.index, start) and (sd.index, end) <= last_end)
        if count:
            counted.append((sd.href, count, role.kind))
        between = any(i < sd.index for i in section_docs) and any(i > sd.index for i in section_docs)
        if excluded_here == words_here and between:
            check.fail("excluded-body-document", sd.zip_path,
                       f"the whole of {sd.href} ({excluded_here} words) is excluded, between documents that hold "
                       f"sections, and the EPUB does not mark it as front or back matter ({role.evidence}); body "
                       "text belongs in sections")
    share = sum(count for _, count, _ in counted)
    check.details.append(f"excluded words that count against the limit: {share} of the book's {total} words outside "
                         f"footnotes (limit {MAX_EXCLUDED_BODY_PERCENT}%)")
    if total and share * 100 > MAX_EXCLUDED_BODY_PERCENT * total:
        listed = "; ".join(f"{href}: {count} words ({kind})" for href, count, kind in counted)
        check.fail("excluded-body-text", "draft.json#/excluded" if mode == "draft" else prepared.source_name,
                   f"{share} excluded words ({100 * share / total:.1f}% of the book's {total} words) are in body "
                   f"matter or between sections, more than the {MAX_EXCLUDED_BODY_PERCENT}% allowed for editorial "
                   f"matter: {listed}")


def _check_word_counts(data: dict[str, Any], resolved: dict[str, Resolved], cp_path: str, check: Check) -> None:
    for r in sorted(resolved.values(), key=lambda r: r.index):
        stated = data["sections"][r.index]["word_count"]
        actual = r.end - r.start
        if stated != actual:
            check.fail("word-count", f"{cp_path}#/sections/{r.index}/word_count",
                       f"section {r.id}: word_count is {stated}, but there are {actual} words between its anchors "
                       f"(words {r.start} to {r.end - 1} of {r.doc.href})")
    if len(resolved) < len(data["sections"]):
        check.incomplete = True
    check.details.append(f"{len(resolved)} sections counted")


def _flag_names(flags: int) -> str:
    return "+".join(n for n, bit in FLAGS.items() if flags & bit) or "none"


def _check_text(original: Book, prepared: Book, check: Check) -> None:
    """``text-unchanged``: the shared word pass gives the same stream for every spine document."""
    before = [(sd.href, sd.media_type) for sd in original.spine]
    after = [(sd.href, sd.media_type) for sd in prepared.spine]
    if before != after:
        check.fail("spine-changed", prepared.opf_path,
                   f"the spine differs from the original's ({len(before)} items before, {len(after)} after)")
    by_href = {sd.href: sd for sd in prepared.spine}
    compared = 0
    for a in original.spine:
        b = by_href.get(a.href)
        if not a.is_xhtml or b is None or a.stream is None or b.stream is None:
            continue
        compared += 1
        ta, tb = a.stream.texts, b.stream.texts
        if ta != tb:
            i = next((k for k, (x, y) in enumerate(zip(ta, tb)) if x != y), min(len(ta), len(tb)))
            check.fail("words-changed", f"{b.zip_path} word {i}",
                       f"the words differ from the original from word {i}: original {_words(ta, i, i + 5)}, "
                       f"prepared {_words(tb, i, i + 5)} ({len(ta)} words before, {len(tb)} after)")
            continue
        fa = [w.flags for w in a.stream.words]
        fb = [w.flags for w in b.stream.words]
        if fa != fb:
            i = next(k for k, (x, y) in enumerate(zip(fa, fb)) if x != y)
            check.fail("flags-changed", f"{b.zip_path} word {i}",
                       f"word {i} ({ta[i]!r}) has flags {_flag_names(fb[i])}; the original has {_flag_names(fa[i])}")
        if a.stream.markers != b.stream.markers:
            check.fail("markers-changed", b.zip_path,
                       "the image, table, code or footnote markers differ from the original's: "
                       f"{[(m.kind, m.start, m.end) for m in a.stream.markers]} before, "
                       f"{[(m.kind, m.start, m.end) for m in b.stream.markers]} after")
        if a.stream.noterefs != b.stream.noterefs:
            check.fail("noterefs-changed", b.zip_path, "the footnote marks differ from the original's")
    check.details.append(f"{compared} spine documents compared word for word")


def _strip_anchors(data: bytes, root: Element) -> bytes:
    """``data`` without every empty element whose id starts with 'rsvp-' (build's anchor spans)."""
    cuts = sorted((el.start, el.end_end) for el in root.iter() if _rsvp_ids(el) and not el.children)
    out = bytearray()
    pos = 0
    for start, end in cuts:
        if start < pos:
            continue
        out += data[pos:start]
        pos = end
    out += data[pos:]
    return bytes(out)


def _opf_without_item(prepared: Book) -> list[bytes]:
    """The package document with the checkpoint item removed: as an element, and as a line of its own."""
    data = prepared.opf.data
    items = [el for el in prepared.opf.root.iter() if el.ns == OPF_NS and el.local == "item"
             and el.get("id") == CHECKPOINTS_ID]
    if len(items) != 1:
        return [data]
    el = items[0]
    out = [data[:el.start] + data[el.end_end:]]
    line_start = data.rfind(b"\n", 0, el.start) + 1
    line_end = data.find(b"\n", el.end_end)
    if line_end >= 0 and data[line_start:el.start].strip(b" \t") == b"" and data[el.end_end:line_end].strip(b" \t\r") == b"":
        out.append(data[:line_start] + data[line_end + 1:])
    return out


def _check_archive(original: Book, prepared: Book, check: Check) -> None:
    """``archive-unchanged``: members, and the bytes of every member."""
    target = checkpoint_zip_path(prepared)
    names_a = set(original.archive.names())
    names_b = set(prepared.archive.names())
    for name in sorted(names_b - names_a - {target}):
        check.fail("member-added", name, f"{name} is not in the original; the only new member may be {target}")
    for name in sorted(names_a - names_b):
        check.fail("member-removed", name, f"{name} from the original is missing")
    docs = {sd.zip_path: sd for sd in prepared.spine if sd.is_xhtml and sd.doc is not None}
    changed = []
    for name in original.archive.names():
        if name not in names_b:
            continue
        try:
            a = original.archive.read(name)
            b = prepared.archive.read(name)
        except ZipFormatError as exc:
            check.fail("member-unreadable", name, str(exc))
            continue
        if a == b:
            continue
        changed.append(name)
        if name == prepared.opf_path:
            if a not in _opf_without_item(prepared):
                check.fail("opf-changed", name, "the package document differs from the original by more than the "
                                                f"one {CHECKPOINTS_ID!r} manifest item")
        elif name in docs:
            doc = docs[name].doc
            assert doc is not None
            if _strip_anchors(b, doc.root) != a:
                check.fail("document-changed", name, "removing the rsvp- anchor elements does not restore the "
                                                     "original bytes; something else in the document changed")
        else:
            check.fail("member-changed", name, f"{name} is not byte-identical to the original")
    check.details.append(f"{len(names_a)} original members; changed: {', '.join(changed) or 'none'}")


def _check_epubcheck(prepared_bytes: bytes, name: str, runner: EpubcheckRunner, check: Check) -> str | None:
    with tempfile.TemporaryDirectory(prefix="prep-validate-") as tmp:
        path = Path(tmp) / (name or "prepared.epub")
        path.write_bytes(prepared_bytes)
        try:
            report = runner(path)
        except epubcheck_mod.EpubcheckUnavailable as exc:
            check.fail("epubcheck-unavailable", name, f"EPUBCheck could not be run ({exc}); an unchecked file is not "
                                                      "a pass")
            check.status = NOT_RUN
            return None
        except (RuntimeError, OSError) as exc:
            check.fail("epubcheck-unavailable", name, f"EPUBCheck failed to produce a report: {exc}")
            check.status = NOT_RUN
            return None
    for m in report.errors:
        check.fail("epubcheck-error", ", ".join(m.locations) or name, f"{m.severity} {m.id}: {m.message}")
    for m in report.warnings:
        check.details.append(f"warning {m.id}: {m.message} ({', '.join(m.locations)})")
    check.details.append(f"EPUBCheck {report.version}: {len(report.errors)} errors, {len(report.warnings)} warnings")
    return report.version


# --------------------------------------------------------------------------
# Entry points


def validate_bytes(original_bytes: bytes | None, original_name: str, prepared_bytes: bytes | None, prepared_name: str,
                   draft_raw: bytes | None = None, draft_name: str | None = None,
                   epubcheck: EpubcheckRunner | None = None, created: str | None = None,
                   original_after: Callable[[], bytes | None] | None = None) -> Outcome:
    """Run every check. ``original_after`` re-reads the original at the end (to prove it did not change)."""
    runner = epubcheck if epubcheck is not None else (lambda p: epubcheck_mod.run_epubcheck(p))
    checks = {name: Check(name) for name, _ in CHECKS}
    outcome = Outcome({}, checks)
    original_sha = sha256(original_bytes) if original_bytes is not None else None
    prepared_sha = sha256(prepared_bytes) if prepared_bytes is not None else None

    # open
    c = checks["open"]
    original = prepared = None
    if original_bytes is None:
        c.fail("unreadable-original", original_name, f"cannot read {original_name}")
    else:
        try:
            original = load_book_bytes(original_bytes, original_name)
        except EpubError as exc:
            c.fail("unreadable-original", original_name, str(exc))
    if prepared_bytes is None:
        c.fail("unreadable-prepared", prepared_name, f"cannot read {prepared_name}")
    else:
        try:
            prepared = load_book_bytes(prepared_bytes, prepared_name)
        except EpubError as exc:
            c.fail("unreadable-prepared", prepared_name, str(exc))
    outcome.original, outcome.prepared = original, prepared

    data: dict[str, Any] | None = None
    schema_ok = False
    cp_path = checkpoint_zip_path(prepared) if prepared is not None else CHECKPOINTS_HREF
    raw = None
    if prepared is None:
        for name in ("checkpoint-item", "checkpoint-file", "firmware-limits"):
            checks[name].not_run("the prepared file does not open")
    else:
        raw = _check_item(prepared, checks["checkpoint-item"])
        if raw is None:
            checks["checkpoint-file"].not_run("there is no checkpoint file to read")
            checks["firmware-limits"].not_run("there is no checkpoint file to read")
        else:
            parsed, parse_issues = cp.parse_checkpoints(raw)
            issues = parse_issues or cp.validate_checkpoints(parsed)
            for issue in issues:
                checks["checkpoint-file"].fail(issue.code, f"{cp_path}#{issue.path}", issue.message)
            schema_ok = not parse_issues and not any(i.layer in ("version", "schema") for i in issues)
            if schema_ok:
                data = parsed
                checks["checkpoint-file"].details.append(
                    f"{len(parsed['sections'])} sections, {len(parsed['checkpoints'])} checkpoints, "
                    f"{sum(len(x['prompts']) for x in parsed['checkpoints'])} prompts")
            for limit in check_limits(raw, None if parse_issues else parsed):
                checks["firmware-limits"].fail("limit", f"{cp_path}#{limit.path}", f"{limit.name}: {limit.message}")
            if not parse_issues:
                checks["firmware-limits"].details.append(f"{len(raw)} bytes")
    outcome.checkpoints = data

    # source-hash (the "unchanged" half is finished at the end)
    c = checks["source-hash"]
    if data is not None and original_sha is not None:
        if data["book"]["source_sha256"] != original_sha:
            c.fail("source-mismatch", f"{cp_path}#/book/source_sha256",
                   f"the checkpoint file names source SHA-256 {data['book']['source_sha256']}, but the original given "
                   f"has {original_sha}")
        else:
            c.details.append(f"source_sha256 matches the original ({original_sha})")
    elif original_sha is None:
        c.not_run("the original cannot be read")
    else:
        c.not_run("the checkpoint file cannot be read")

    structural = ("anchors", "word-boundaries", "coverage", "exclusions", "word-counts")
    if prepared is None or data is None:
        for name in structural:
            checks[name].not_run("the checkpoint file is missing or fails its schema")
    else:
        resolved, broken = _resolve_anchors(prepared, data, checks["anchors"], cp_path)
        outcome.resolved = resolved
        total = len(data["sections"])
        _check_boundaries(resolved, total, checks["word-boundaries"])
        mode, draft_data, excluded = _check_coverage(original, prepared, data, resolved, broken, draft_raw,
                                                     checks["coverage"])
        checks["coverage"].details.insert(0, "exclusions from the draft" if mode == "draft" else
                                          "no draft given: a spine document without a section counts as excluded")
        outcome.result["coverage_mode"] = mode
        _check_exclusions(prepared, data, resolved, mode, draft_data, excluded, checks["exclusions"])
        _check_word_counts(data, resolved, cp_path, checks["word-counts"])

    if original is None or prepared is None:
        checks["text-unchanged"].not_run("both files must open")
        checks["archive-unchanged"].not_run("both files must open")
    else:
        _check_text(original, prepared, checks["text-unchanged"])
        _check_archive(original, prepared, checks["archive-unchanged"])

    epubcheck_version = None
    if prepared_bytes is None or prepared is None:
        checks["epubcheck"].not_run("the prepared file does not open")
    else:
        epubcheck_version = _check_epubcheck(prepared_bytes, prepared_name, runner, checks["epubcheck"])

    c = checks["source-hash"]
    if original_after is not None and original_sha is not None:
        after = original_after()
        after_sha = sha256(after) if after is not None else None
        if after_sha != original_sha:
            c.fail("original-changed", original_name,
                   f"the original's SHA-256 was {original_sha} before validation and {after_sha} after")
        else:
            c.details.append("the original is byte-identical before and after validation")

    for check in checks.values():
        check.finish()
    passed = all(check.status == PASS for check in checks.values())
    outcome.result = {
        "format": FORMAT,
        "version": VERSION,
        "tool": {"name": TOOL_NAME, "version": TOOL_VERSION, "word_rules_version": WORD_RULES_VERSION,
                 "epubcheck_version": epubcheck_version},
        "created": created or timestamp(),
        "original": {"file": original_name, "sha256": original_sha},
        "prepared": {"file": prepared_name, "sha256": prepared_sha},
        "draft": ({"file": draft_name or "draft.json", "sha256": sha256(draft_raw)} if draft_raw is not None else None),
        "coverage_mode": outcome.result.get("coverage_mode"),
        "passed": passed,
        "checks": [checks[name].as_json() for name, _ in CHECKS],
    }
    return outcome


def _read(path: Path) -> bytes | None:
    try:
        return path.read_bytes()
    except OSError:
        return None


def find_draft(draft: Path | None, work_dir: Path | None) -> Path | None:
    if draft is not None:
        return draft
    if work_dir is not None and (work_dir / "draft.json").is_file():
        return work_dir / "draft.json"
    return None


def validate_files(original: Path, prepared: Path, draft: Path | None = None,
                   epubcheck: EpubcheckRunner | None = None, created: str | None = None) -> Outcome:
    """Validate files on disk. The original is only ever read."""
    draft_raw = None
    if draft is not None:
        draft_raw = draft.read_bytes()
    return validate_bytes(_read(original), original.name, _read(prepared), prepared.name, draft_raw,
                          draft.name if draft is not None else None, epubcheck, created,
                          original_after=lambda: _read(original))


_validator: Any = None


def validate_result_schema(result: Any) -> list[str]:
    """Problems with a validation result against ``schema/validation.schema.json`` (empty when valid)."""
    global _validator
    if _validator is None:
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        cp.Draft202012Validator.check_schema(schema)
        _validator = cp.StrictValidator(schema)
    return [f"{cp.json_pointer(e.absolute_path) or '/'}: {cp._shorten(e.message)}"
            for e in sorted(_validator.iter_errors(result), key=lambda e: cp.json_pointer(e.absolute_path))]


def result_bytes(result: dict[str, Any]) -> bytes:
    return (json.dumps(result, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def format_table(result: dict[str, Any]) -> str:
    rows = [("Check", "Status", "Failures", "What it confirms")]
    for c in result["checks"]:
        rows.append((c["name"], c["status"], str(len(c["failures"])), c["title"]))
    widths = [max(len(r[k]) for r in rows) for k in range(3)]
    lines = []
    for r in rows:
        lines.append("  ".join(r[k].ljust(widths[k]) for k in range(3)) + "  " + r[3])
    lines.insert(1, "  ".join("-" * w for w in widths) + "  " + "-" * 16)
    return "\n".join(lines)
