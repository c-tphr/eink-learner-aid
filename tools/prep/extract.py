"""``prep extract``: EPUB -> work folder for the drafter and checker sessions.

    <workdir>/source.json       book facts, spine (with each text file), table of contents
    <workdir>/structure.json    every block of every spine document, machine-readable
    <workdir>/text/dNN-<name>.txt   the visible text of one spine document as numbered blocks

The paragraph references in these files (``d02-p006``) are what ``draft.json``
cites; ``prep build`` recomputes them from the same EPUB with the same code
(``tools/prep/epub.py``), so they always agree.
"""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

from .epub import Block, Book, EpubError, SpineDoc, load_book

SOURCE_FORMAT = "rsvp-prep-source"
STRUCTURE_FORMAT = "rsvp-prep-structure"
SOURCE_VERSION = 1
# 2: words follow the firmware's definition; every block has first_word and word_count (P6, issue #49).
STRUCTURE_VERSION = 2
OUTPUT_FILES = ("source.json", "structure.json")


def _json(data: Any) -> str:
    return json.dumps(data, indent=2, ensure_ascii=False) + "\n"


def text_file_name(sd: SpineDoc) -> str:
    stem = sd.href.rsplit("/", 1)[-1].rsplit(".", 1)[0]
    stem = re.sub(r"[^A-Za-z0-9._-]+", "-", stem).strip("-.") or "document"
    return f"text/{sd.prefix}-{stem}.txt"


def display_text(sd: SpineDoc, block: Block) -> str:
    """The block's words joined by single spaces, with footnote marks shown as ``[^mark]``.

    A mark is not a word: it is glued to the word that carries its NOTEREF
    flag, so the line has exactly ``block.word_count`` space-separated words.
    A non-prose block shows its placeholder instead.
    """
    if block.placeholder is not None:
        return block.placeholder
    assert sd.stream is not None
    shown = list(block.words)
    for ref in sd.stream.noterefs:
        if block.start <= ref.word < block.end:
            shown[ref.word - block.start] += f"[^{' '.join(ref.mark.split()) or '*'}]"
    return " ".join(shown)


def _block_header(block: Block) -> str:
    bits = [block.kind]
    if block.kind == "heading":
        bits[0] += f" {block.element.local}"
    bits.append(f"{block.word_count} word{'s' if block.word_count != 1 else ''}")
    if block.note_id:
        bits.append(f"note {block.note_id}")
    if block.source_id:
        bits.append(f"id {block.source_id}")
    return f"[{block.ref}] " + ", ".join(bits)


def render_text(sd: SpineDoc, spine_count: int) -> str:
    lines = [
        f"# {sd.prefix} {sd.href}",
        f"# Spine position {sd.index} of {spine_count} ({'linear' if sd.linear else 'non-linear'})."
        + (f" Title: {sd.title}." if sd.title else ""),
    ]
    notes = f"{len(sd.notes)} footnote block{'s' if len(sd.notes) != 1 else ''}"
    if sd.blocks:
        lines.append(f"# {len(sd.blocks)} blocks ({sd.blocks[0].ref} to {sd.blocks[-1].ref}), {len(sd.words)} words"
                     + (f"; {notes}." if sd.notes else "."))
    else:
        lines.append(f"# No reading-stream blocks{f'; {notes}' if sd.notes else ''}.")
    lines += [
        "# Each block: [reference] kind, word count, source id; then its text. A draft cites these references.",
        "# [^N] is a footnote mark (not a word). [Image: ...], [Table: ...], [Code: ...] stand for non-prose blocks;",
        "# their caption, cell and code words count in the word stream (RSVP pauses at them).",
        "",
    ]
    for block in sd.blocks:
        lines += [_block_header(block), display_text(sd, block), ""]
    if sd.notes:
        lines += ["# Footnote bodies: their words are in the word stream (RSVP passes over them), but they cannot be",
                  "# cited in ranges or as support.", ""]
        for block in sd.notes:
            lines += [_block_header(block), display_text(sd, block), ""]
    return "\n".join(lines)


def source_document(book: Book) -> dict[str, Any]:
    return {
        "format": SOURCE_FORMAT,
        "version": SOURCE_VERSION,
        "source_file": book.source_name,
        "source_sha256": book.sha256,
        "title": book.title,
        "language": book.language,
        "opf_path": book.opf_path,
        "opf_dir": book.opf_dir,
        "spine": [
            {
                "index": sd.index,
                "prefix": sd.prefix,
                "idref": sd.idref,
                "href": sd.href,
                "media_type": sd.media_type,
                "linear": sd.linear,
                "title": sd.title,
                "text_file": text_file_name(sd) if sd.is_xhtml else None,
                "blocks": len(sd.blocks),
                "first_paragraph": sd.blocks[0].ref if sd.blocks else None,
                "last_paragraph": sd.blocks[-1].ref if sd.blocks else None,
                "words": len(sd.words),
                "footnote_blocks": len(sd.notes),
            }
            for sd in book.spine
        ],
        "toc": [
            {"label": t.label, "href": t.href, "depth": t.depth, "source": t.source, "paragraph": t.ref}
            for t in book.toc
        ],
    }


def structure_document(book: Book) -> dict[str, Any]:
    return {
        "format": STRUCTURE_FORMAT,
        "version": STRUCTURE_VERSION,
        "source_sha256": book.sha256,
        "opf_path": book.opf_path,
        "documents": [
            {
                "index": sd.index,
                "prefix": sd.prefix,
                "href": sd.href,
                "zip_path": sd.zip_path,
                "media_type": sd.media_type,
                "linear": sd.linear,
                "text_file": text_file_name(sd) if sd.is_xhtml else None,
                "word_count": len(sd.words),
                "blocks": [b.summary() for b in sd.blocks],
                "footnotes": [b.summary() for b in sd.notes],
            }
            for sd in book.spine
        ],
    }


def extract(epub: str | Path, workdir: str | Path) -> dict[str, Any]:
    """Write the work folder; return source.json's content."""
    workdir = Path(workdir)
    if workdir.exists() and not workdir.is_dir():
        raise EpubError(f"{workdir} exists and is not a directory")
    if workdir.is_dir() and any(workdir.iterdir()) and not (workdir / "source.json").is_file():
        raise EpubError(f"{workdir} is not empty and holds no source.json; choose an empty or new folder")
    book = load_book(epub)
    texts = {text_file_name(sd): render_text(sd, len(book.spine)) for sd in book.spine if sd.is_xhtml}
    text_dir = workdir / "text"
    text_dir.mkdir(parents=True, exist_ok=True)
    for old in text_dir.glob("*.txt"):
        old.unlink()
    for name, text in texts.items():
        (workdir / name).write_text(text, encoding="utf-8", newline="\n")
    source = source_document(book)
    (workdir / "source.json").write_text(_json(source), encoding="utf-8", newline="\n")
    (workdir / "structure.json").write_text(_json(structure_document(book)), encoding="utf-8", newline="\n")
    return source
