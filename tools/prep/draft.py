"""Load ``draft.json`` and check it against the book it was drafted from.

``check_draft(raw, book)`` returns ``(issues, plan)``. ``issues`` lists every
problem found (``Issue(code, path, message)``, ``path`` an RFC 6901 pointer
into the draft); ``plan`` is the resolved list of sections that ``build``
needs, and is only meaningful when ``issues`` is empty.

Checks run in three stages; a stage runs only when the previous one found
nothing, because later checks are noise on a draft that failed earlier:

1. parse (as for checkpoints.json: UTF-8, one JSON value, no duplicate keys)
   and ``schema/draft.schema.json``;
2. ``source-mismatch``: ``book.source_sha256`` is the SHA-256 of this EPUB;
3. cross-reference rules (tools/prep/README.md, "The draft file"), all
   reported together.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from . import checkpoints as cp
from .epub import Block, Book, SpineDoc

SCHEMA_PATH = Path(__file__).resolve().parent / "schema" / "draft.schema.json"

# Issue codes (besides parse, duplicate-key and schema, shared with checkpoints.py).
SOURCE_MISMATCH = "source-mismatch"
UNKNOWN_DOCUMENT = "unknown-document"
UNKNOWN_PARAGRAPH = "unknown-paragraph"
WRONG_DOCUMENT = "wrong-document"
RANGE_ACROSS_DOCUMENTS = "range-across-documents"
REVERSED_RANGE = "reversed-range"
OVERLAPPING_RANGES = "overlapping-ranges"
UNCOVERED_PARAGRAPHS = "uncovered-paragraphs"
SECTION_ORDER = "section-order"
EMPTY_SECTION = "empty-section"
SUPPORT_OUTSIDE_SECTION = "support-outside-section"
ANCHOR_PLACEMENT = "anchor-placement"
ANCHOR_ID_CONFLICT = "anchor-id-conflict"


@dataclass(frozen=True)
class Issue:
    code: str
    path: str
    message: str

    def __str__(self) -> str:
        return f"{self.code} at {self.path or '/'}: {self.message}"


@dataclass
class PlannedSection:
    """A draft section resolved against the book."""

    index: int  # position in draft["sections"]
    id: str
    label: str
    doc: SpineDoc
    first: int  # index into doc.blocks
    last: int

    @property
    def blocks(self) -> list[Block]:
        return self.doc.blocks[self.first : self.last + 1]

    @property
    def start(self) -> int:
        """Word index of the start anchor: words started before the first block's start tag."""
        return self.blocks[0].start

    @property
    def end(self) -> int:
        """Word index of the end anchor: words started before the last block's end tag."""
        return self.blocks[-1].end

    @property
    def word_count(self) -> int:
        """Words in ``[start, end)``, as the firmware counts them: every word between the anchors, including
        caption, table, code and footnote-body words that fall inside."""
        return self.end - self.start

    @property
    def start_anchor(self) -> str:
        return f"rsvp-{self.id}-start"

    @property
    def end_anchor(self) -> str:
        return f"rsvp-{self.id}-end"


_validator: Any = None


def _get_validator() -> Any:
    global _validator
    if _validator is None:
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        cp.Draft202012Validator.check_schema(schema)
        _validator = cp.StrictValidator(schema)
    return _validator


def _schema_message(error: Any) -> str:
    where = error.absolute_path[-1] if error.absolute_path else "value"
    if error.validator == "maxLength" and isinstance(error.instance, str):
        return f"{where} has {len(error.instance)} characters; the limit is {error.validator_value}"
    if error.validator == "pattern" and "-p[0-9]" in str(error.validator_value):
        return (f"{error.instance!r} is not a paragraph reference such as 'd02-p006'; cite the references "
                "that prep extract wrote to text/*.txt and structure.json")
    return cp._shorten(error.message)


def validate_schema(data: Any) -> list[Issue]:
    errors = sorted(_get_validator().iter_errors(data), key=lambda e: (cp.json_pointer(e.absolute_path), e.message))
    return [Issue(cp.SCHEMA, cp.json_pointer(e.absolute_path), _schema_message(e)) for e in errors]


def _runs(refs: list[str]) -> str:
    """'d02-p001 to d02-p003, d02-p007' from consecutive references of one document."""
    return ", ".join(refs) if len(refs) <= 2 else f"{refs[0]} to {refs[-1]} ({len(refs)} paragraphs)"


class _Resolver:
    def __init__(self, book: Book):
        self.book = book
        self.where: dict[str, tuple[SpineDoc, int | None]] = {}
        for sd in book.spine:
            for i, b in enumerate(sd.blocks):
                self.where[b.ref] = (sd, i)
            for b in sd.notes:
                self.where[b.ref] = (sd, None)

    def range(self, entry: dict[str, Any], base: str, issues: list[Issue]) -> tuple[SpineDoc, int, int] | None:
        href = entry["chapter_href"]
        sd = self.book.doc_by_href(href)
        if sd is None or not sd.is_xhtml:
            known = ", ".join(d.href for d in self.book.spine if d.is_xhtml)
            issues.append(Issue(UNKNOWN_DOCUMENT, f"{base}/chapter_href",
                                f"{href!r} is not an XHTML spine document (spine documents: {known})"))
            return None
        found = []
        for key in ("first_paragraph", "last_paragraph"):
            ref = entry[key]
            hit = self.where.get(ref)
            if hit is None:
                span = f"{sd.blocks[0].ref} to {sd.blocks[-1].ref}" if sd.blocks else "no paragraphs"
                issues.append(Issue(UNKNOWN_PARAGRAPH, f"{base}/{key}",
                                    f"paragraph {ref!r} does not exist ({href} has {span})"))
                found.append(None)
            elif hit[1] is None:
                issues.append(Issue(UNKNOWN_PARAGRAPH, f"{base}/{key}",
                                    f"{ref!r} is a footnote body, which is outside the reading stream"))
                found.append(None)
            else:
                found.append(hit)
        if None in found:
            return None
        (doc_a, first), (doc_b, last) = found  # type: ignore[misc]
        if doc_a is not doc_b:
            issues.append(Issue(RANGE_ACROSS_DOCUMENTS, base,
                                f"{entry['first_paragraph']} is in {doc_a.href} but {entry['last_paragraph']} is in "
                                f"{doc_b.href}; a range lies inside one document (plan rule 1)"))
            return None
        if doc_a is not sd:
            issues.append(Issue(WRONG_DOCUMENT, f"{base}/chapter_href",
                                f"paragraphs {entry['first_paragraph']} and {entry['last_paragraph']} are in "
                                f"{doc_a.href}, not {href}"))
            return None
        if first > last:
            issues.append(Issue(REVERSED_RANGE, base,
                                f"last_paragraph {entry['last_paragraph']} comes before first_paragraph "
                                f"{entry['first_paragraph']}"))
            return None
        return sd, first, last  # type: ignore[return-value]


def check_cross_references(data: dict[str, Any], book: Book) -> tuple[list[Issue], list[PlannedSection]]:
    issues: list[Issue] = []
    resolver = _Resolver(book)
    owner: dict[tuple[int, int], str] = {}
    plan: list[PlannedSection] = []

    def claim(sd: SpineDoc, first: int, last: int, base: str, name: str) -> None:
        clashes: dict[str, list[str]] = {}
        for i in range(first, last + 1):
            key = (sd.index, i)
            if key in owner:
                clashes.setdefault(owner[key], []).append(sd.blocks[i].ref)
            else:
                owner[key] = name
        for other, refs in clashes.items():
            issues.append(Issue(OVERLAPPING_RANGES, base, f"{_runs(refs)} also belong to {other}"))

    for i, entry in enumerate(data["excluded"]):
        base = f"/excluded/{i}"
        r = resolver.range(entry, base, issues)
        if r:
            claim(*r, base, f"excluded[{i}]")

    seen_sections: set[str] = set()
    previous: tuple[int, int] | None = None
    for i, entry in enumerate(data["sections"]):
        base = f"/sections/{i}"
        sid = entry["id"]
        if sid in seen_sections:
            issues.append(Issue(cp.DUPLICATE_ID, f"{base}/id", f"section id {sid!r} is used more than once"))
        seen_sections.add(sid)
        r = resolver.range(entry, base, issues)
        if not r:
            continue
        sd, first, last = r
        claim(sd, first, last, base, f"section {sid}")
        position = (sd.index, first)
        if previous is not None and position <= previous:
            issues.append(Issue(SECTION_ORDER, base, f"section {sid} starts before the section listed before it; "
                                                    "list sections in reading order"))
        previous = position
        section = PlannedSection(i, sid, entry["label"], sd, first, last)
        if section.word_count == 0:
            issues.append(Issue(EMPTY_SECTION, base, f"section {sid} has no words between its anchors"))
        for block, side in ((section.blocks[0], "start"), (section.blocks[-1], "end")):
            if block.placement == "none":
                issues.append(Issue(ANCHOR_PLACEMENT, base,
                                    f"no valid place for the {side} anchor at {block.ref} (<{block.element.local}> "
                                    f"inside <{block.element.parent.local if block.element.parent else '?'}>)"))
        assert sd.doc is not None
        for anchor in (section.start_anchor, section.end_anchor):
            if anchor in sd.doc.ids:
                issues.append(Issue(ANCHOR_ID_CONFLICT, f"{base}/id",
                                    f"{sd.href} already has an element with id {anchor!r}"))
        plan.append(section)

    for sd in book.spine:
        runs: list[list[str]] = []
        last_index = None
        for i, b in enumerate(sd.blocks):
            if (sd.index, i) in owner:
                continue
            if last_index is not None and i == last_index + 1:
                runs[-1].append(b.ref)
            else:
                runs.append([b.ref])
            last_index = i
        for run in runs:
            issues.append(Issue(UNCOVERED_PARAGRAPHS, "",
                                f"{sd.href}: {_runs(run)} belong to no section and are not excluded"))

    by_id = {s.id: s for s in plan}
    checkpoint_ids: set[str] = set()
    prompt_ids: set[str] = set()
    with_checkpoint: set[str] = set()
    for i, checkpoint in enumerate(data["checkpoints"]):
        base = f"/checkpoints/{i}"
        cid = checkpoint["id"]
        if cid in checkpoint_ids:
            issues.append(Issue(cp.DUPLICATE_ID, f"{base}/id", f"checkpoint id {cid!r} is used more than once"))
        checkpoint_ids.add(cid)
        target = checkpoint["section"]
        if target not in seen_sections:
            issues.append(Issue(cp.UNKNOWN_SECTION, f"{base}/section",
                                f"checkpoint names section {target!r}, which does not exist"))
        elif target in with_checkpoint:
            issues.append(Issue(cp.MULTIPLE_CHECKPOINTS_FOR_SECTION, f"{base}/section",
                                f"section {target!r} already has a checkpoint"))
        with_checkpoint.add(target)
        section = by_id.get(target)
        inside = {b.ref for b in section.blocks} if section else None
        for j, prompt in enumerate(checkpoint["prompts"]):
            pbase = f"{base}/prompts/{j}"
            pid = prompt["id"]
            if pid in prompt_ids:
                issues.append(Issue(cp.DUPLICATE_ID, f"{pbase}/id", f"prompt id {pid!r} is used more than once"))
            prompt_ids.add(pid)
            for field in ("prompt", "answer"):
                problem = cp.plain_text_problem(prompt[field])
                if problem:
                    issues.append(Issue(cp.NOT_PLAIN_TEXT, f"{pbase}/{field}", f"{field} {problem}"))
                if cp.is_blank(prompt[field]):
                    issues.append(Issue(cp.BLANK_TEXT, f"{pbase}/{field}", f"{field} contains only white space"))
            for k, ref in enumerate(prompt["supporting_paragraphs"]):
                if ref not in resolver.where:
                    issues.append(Issue(UNKNOWN_PARAGRAPH, f"{pbase}/supporting_paragraphs/{k}",
                                        f"paragraph {ref!r} does not exist"))
                elif inside is not None and ref not in inside:
                    issues.append(Issue(SUPPORT_OUTSIDE_SECTION, f"{pbase}/supporting_paragraphs/{k}",
                                        f"{ref} is not inside section {target} ({section.blocks[0].ref} to "
                                        f"{section.blocks[-1].ref} in {section.doc.href})"))
    return issues, plan


def check_draft(raw: bytes, book: Book) -> tuple[Any, list[Issue], list[PlannedSection]]:
    """Return (draft data, issues, resolved sections)."""
    data, parse_issues = cp.parse_checkpoints(raw)
    if parse_issues:
        return None, [Issue(i.code, i.path, i.message) for i in parse_issues], []
    issues = validate_schema(data)
    if issues:
        return data, issues, []
    if data["book"]["source_sha256"] != book.sha256:
        return data, [Issue(SOURCE_MISMATCH, "/book/source_sha256",
                            f"the draft was made from an EPUB with SHA-256 {data['book']['source_sha256']}, "
                            f"but {book.source_name} has {book.sha256}; re-run prep extract on this file and "
                            "redraft")], []
    issues, plan = check_cross_references(data, book)
    return data, issues, plan
