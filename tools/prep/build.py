"""``prep build``: original EPUB + draft.json -> prepared EPUB.

The prepared copy differs from the original in exactly three ways:

1. Each section gets two empty anchor elements in its chapter document,
   spliced into the original bytes (nothing is re-serialised):
   ``<span id="rsvp-<id>-start"></span>`` and ``...-end``. See
   ``anchor_insertions`` for where they go.
2. The package document gains one manifest line declaring
   ``rsvp/checkpoints.json`` (inserted before ``</manifest>``).
3. ``rsvp/checkpoints.json`` is added next to the package document.

Every other zip entry is copied byte for byte (``epubzip.Archive.rebuild``).
"""

from __future__ import annotations

import json
import os
import posixpath
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from . import checkpoints as cp
from .draft import ANCHOR_PLACEMENT, Issue, PlannedSection, check_draft
from .epub import OPF_NS, Book, EpubError, all_text, load_book, load_book_bytes
from .words import WordStream, tokenize, word_stream
from .xmltree import Document, XmlError

CHECKPOINTS_HREF = "rsvp/checkpoints.json"
CHECKPOINTS_ID = "rsvp-checkpoints"
CHECKPOINTS_MEDIA_TYPE = "application/json"


class BuildError(Exception):
    """The input cannot be built; ``issues`` lists every problem."""

    def __init__(self, issues: list[Issue]):
        self.issues = issues
        super().__init__("\n".join(str(i) for i in issues))


@dataclass
class BuildResult:
    data: bytes
    checkpoints: dict[str, Any]
    touched: list[str]  # zip paths of rewritten entries
    added: str  # zip path of the checkpoint file


def checkpoints_zip_path(book: Book) -> str:
    return posixpath.join(book.opf_dir, CHECKPOINTS_HREF) if book.opf_dir else CHECKPOINTS_HREF


def _span(qname: str, anchor: str) -> bytes:
    return f'<{qname} id="{anchor}"></{qname}>'.encode("ascii")


def anchor_insertions(section: PlannedSection) -> list[tuple[int, int, bytes]]:
    """(byte offset, order at that offset, bytes) for a section's two anchors.

    - Prose block (``placement == 'inside'``): the start anchor is the first
      child of the section's first block (right after its start tag) and the
      end anchor the last child of its last block (right before its end tag).
    - Non-prose block (figure, table, pre, img, svg, ...: ``'around'``): the
      anchor is a sibling, right before the block's start tag or right after
      its end tag, because a span is not allowed directly inside table,
      figure (next to figcaption) and the like, and inside pre it would sit
      in preformatted text.
    - When an end anchor and the next section's start anchor fall on the same
      offset, the end anchor comes first (order 0 before 1).
    """
    first, last = section.blocks[0], section.blocks[-1]
    out = []
    el = first.element
    if first.placement == "inside":
        out.append((el.start_end, 1, _span(el.qname("span"), section.start_anchor)))
    else:
        assert el.parent is not None
        out.append((el.start, 1, _span(el.parent.qname("span"), section.start_anchor)))
    el = last.element
    if last.placement == "inside":
        out.append((el.end, 0, _span(el.qname("span"), section.end_anchor)))
    else:
        assert el.parent is not None
        out.append((el.end_end, 0, _span(el.parent.qname("span"), section.end_anchor)))
    return out


def splice(data: bytes, insertions: list[tuple[int, int, bytes]]) -> bytes:
    out = bytearray()
    pos = 0
    for offset, _, chunk in sorted(insertions, key=lambda t: (t[0], t[1])):
        out += data[pos:offset]
        out += chunk
        pos = offset
    out += data[pos:]
    return bytes(out)


def add_manifest_item(opf: Document) -> bytes:
    """The package document with one ``<item>`` added at the end of the manifest.

    When ``</manifest>`` starts its own line, the item becomes a new line just
    before it, indented like the manifest's last item and ended with the
    file's own line break; otherwise it is inserted right before
    ``</manifest>``.
    """
    data = opf.data
    manifest = opf.root.first(OPF_NS, "manifest")
    assert manifest is not None and not manifest.is_empty_tag
    q = manifest.qname("item")
    item = f'<{q} id="{CHECKPOINTS_ID}" href="{CHECKPOINTS_HREF}" media-type="{CHECKPOINTS_MEDIA_TYPE}"/>'.encode(
        "ascii"
    )
    pos = manifest.end
    line_start = data.rfind(b"\n", 0, pos) + 1
    if line_start > 0 and data[line_start:pos].strip(b" \t") == b"":
        indent = b""
        items = list(manifest.elements())
        if items:
            ls = data.rfind(b"\n", 0, items[-1].start) + 1
            if data[ls : items[-1].start].strip(b" \t") == b"":
                indent = data[ls : items[-1].start]
        newline = b"\r\n" if data[line_start - 2 : line_start] == b"\r\n" else b"\n"
        return data[:line_start] + indent + item + newline + data[line_start:]
    return data[:pos] + item + data[pos:]


def checkpoint_document(draft: dict[str, Any], plan: list[PlannedSection]) -> dict[str, Any]:
    book = draft["book"]
    return {
        "format": cp.FORMAT,
        "version": cp.SUPPORTED_VERSION,
        "book": {
            "title": book["title"],
            "language": book["language"],
            "source_sha256": book["source_sha256"],
            "instructions_version": book["instructions_version"],
        },
        "sections": [
            {
                "id": s.id,
                "chapter_href": s.doc.href,
                "start_anchor": s.start_anchor,
                "end_anchor": s.end_anchor,
                "label": s.label,
                "word_count": s.word_count,
            }
            for s in plan
        ],
        "checkpoints": [
            {
                "id": c["id"],
                "section": c["section"],
                "prompts": [{"id": p["id"], "prompt": p["prompt"], "answer": p["answer"]} for p in c["prompts"]],
            }
            for c in draft["checkpoints"]
        ],
    }


def checkpoint_bytes(doc: dict[str, Any]) -> bytes:
    return (json.dumps(doc, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def _preflight(book: Book) -> list[Issue]:
    issues = []
    target = checkpoints_zip_path(book)
    if target in book.archive.by_name:
        issues.append(Issue("already-prepared", "", f"{book.source_name} already contains {target}"))
    for item in book.manifest:
        if item.id == CHECKPOINTS_ID or item.zip_path == target:
            issues.append(Issue("already-prepared", "", f"{book.opf_path} already declares {item.href!r} "
                                                        f"(id {item.id!r})"))
    return issues


def build_bytes(book: Book, draft_raw: bytes) -> BuildResult:
    """Build the prepared EPUB in memory. Raises ``BuildError`` listing every problem."""
    issues = _preflight(book)
    draft, draft_issues, plan = check_draft(draft_raw, book)
    issues += draft_issues
    if issues:
        raise BuildError(issues)

    by_doc: dict[str, list[tuple[int, int, bytes]]] = {}
    for section in plan:
        by_doc.setdefault(section.doc.zip_path, []).extend(anchor_insertions(section))
    replace: dict[str, bytes] = {}
    for sd in book.spine:
        if sd.zip_path in by_doc:
            assert sd.doc is not None
            replace[sd.zip_path] = splice(sd.doc.data, by_doc[sd.zip_path])
    issues = anchors_inside_words(plan, replace)
    if issues:
        raise BuildError(issues)
    replace[book.opf_path] = add_manifest_item(book.opf)

    data = checkpoint_document(draft, plan)
    payload = checkpoint_bytes(data)
    problems = cp.check_bytes(payload)
    if problems:
        raise BuildError([Issue(p.code, p.path, f"checkpoints.json: {p.message}") for p in problems])
    target = checkpoints_zip_path(book)
    out = book.archive.rebuild(replace, [(target, payload, book.opf_path)])
    result = BuildResult(out, data, sorted(replace), target)
    verify(book, result, plan)
    return result


def anchors_inside_words(plan: list[PlannedSection], replace: dict[str, bytes]) -> list[Issue]:
    """``anchor-placement`` issues for anchors that the word pass finds strictly inside a word.

    Anchors go at element boundaries of blocks; that is a word boundary
    whenever the block element is one at which words split, which holds for
    every block ``prep extract`` makes from ordinary markup. This catches the
    rest (a block element that is not a word boundary, glued to a neighbour).
    """
    streams: dict[str, WordStream] = {}
    issues = []
    for section in plan:
        zip_path = section.doc.zip_path
        if zip_path not in streams:
            try:
                streams[zip_path] = word_stream(replace[zip_path], zip_path)
            except XmlError as exc:  # pragma: no cover - splicing empty spans keeps a document well formed
                raise AssertionError(f"{zip_path}: spliced document does not parse: {exc}") from exc
        stream = streams[zip_path]
        for side, anchor_id in (("start", section.start_anchor), ("end", section.end_anchor)):
            anchor = stream.anchor(anchor_id)
            if anchor is not None and anchor.inside_word:
                text = stream.words[anchor.index - 1].text if anchor.index > 0 else ""
                issues.append(Issue(ANCHOR_PLACEMENT, f"/sections/{section.index}",
                                    f"the {side} anchor of section {section.id} would fall inside the word "
                                    f"{text!r} in {section.doc.href}; the firmware splits words differently there"))
    return issues


def verify(original: Book, result: BuildResult, plan: list[PlannedSection]) -> None:
    """Re-read the prepared bytes and confirm the build changed nothing but the anchors.

    A failure here is a bug in this tool, not a problem with the input.
    """
    prepared = load_book_bytes(result.data, "prepared")
    for before, after in zip(original.spine, prepared.spine, strict=True):
        if not before.is_xhtml:
            continue
        assert before.doc is not None and after.doc is not None
        assert before.stream is not None and after.stream is not None
        if before.stream.words != after.stream.words:
            raise AssertionError(f"{before.href}: words or word flags changed")
        if (before.stream.markers, before.stream.noterefs) != (after.stream.markers, after.stream.noterefs):
            raise AssertionError(f"{before.href}: markers or footnote marks changed")
        if tokenize(all_text(before.doc.root)) != tokenize(all_text(after.doc.root)):
            raise AssertionError(f"{before.href}: visible text changed")
        if [(b.ref, b.kind, b.start, b.end) for b in before.blocks + before.notes] != [
                (b.ref, b.kind, b.start, b.end) for b in after.blocks + after.notes]:
            raise AssertionError(f"{before.href}: blocks changed")
    for section in plan:
        doc = prepared.doc_by_href(section.doc.href)
        assert doc is not None and doc.doc is not None and doc.stream is not None
        for anchor_id, index in ((section.start_anchor, section.start), (section.end_anchor, section.end)):
            if anchor_id not in doc.doc.ids or anchor_id in doc.doc.duplicate_ids:
                raise AssertionError(f"{section.doc.href}: anchor {anchor_id} missing or repeated")
            anchor = doc.stream.anchor(anchor_id)
            if anchor is None or anchor.index != index or anchor.inside_word:
                raise AssertionError(f"{section.doc.href}: anchor {anchor_id} is not at word {index}")


def build_file(epub: str | Path, draft: str | Path, out: str | Path) -> BuildResult:
    """Build ``out`` from ``epub`` and ``draft``. Never writes to ``epub``."""
    epub, draft, out = Path(epub), Path(draft), Path(out)
    if out.resolve() == epub.resolve() or (out.exists() and os.path.samefile(out, epub)):
        raise EpubError(f"the output {out} is the input EPUB; the original is never modified")
    if out.resolve() == draft.resolve():
        raise EpubError(f"the output {out} is the draft file")
    book = load_book(epub)
    try:
        draft_raw = draft.read_bytes()
    except OSError as exc:
        raise EpubError(f"cannot read {draft}: {exc.strerror}") from exc
    result = build_bytes(book, draft_raw)
    out.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=".prep-build-", suffix=".epub", dir=out.parent)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(result.data)
        umask = os.umask(0)
        os.umask(umask)
        os.chmod(tmp, 0o666 & ~umask)
        os.replace(tmp, out)
    except BaseException:
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise
    return result
