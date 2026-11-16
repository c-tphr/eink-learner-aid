"""Paragraph references: the identities ``prep extract`` gives to blocks.

A reference names one block of one spine document and is what ``draft.json``
cites. It depends only on the spine position of the document and the order of
the block inside it, never on ids in the source markup:

    d02-p005    spine document 2, reading-stream block 5
    d02-n001    spine document 2, footnote body block 1

- ``dNN``: 1-based spine position, zero-padded to the width of the spine
  length (at least 2 digits) so all references of one book line up.
- ``pNNN``: 1-based number among the document's reading-stream blocks,
  zero-padded to the width of that document's block count (at least 3).
- ``nNNN``: the same for footnote/endnote body blocks, numbered separately.

Standard library only: the fixture generator imports it too.
"""

from __future__ import annotations

import re

REF_RE = re.compile(r"^d(?P<doc>[0-9]+)-(?P<series>[pn])(?P<number>[0-9]+)$")
MAIN = "p"
NOTE = "n"


def _width(count: int, minimum: int) -> int:
    return max(minimum, len(str(count)))


def document_prefix(spine_index: int, spine_count: int) -> str:
    """``d02`` for the second of up to 99 spine documents."""
    return f"d{spine_index:0{_width(spine_count, 2)}d}"


def paragraph_ref(
    spine_index: int, spine_count: int, number: int, block_count: int, series: str = MAIN
) -> str:
    """The reference of block ``number`` (1-based) of spine document ``spine_index`` (1-based)."""
    if series not in (MAIN, NOTE):
        raise ValueError(f"unknown series {series!r}")
    return f"{document_prefix(spine_index, spine_count)}-{series}{number:0{_width(block_count, 3)}d}"


def parse_ref(ref: str) -> tuple[int, str, int] | None:
    """(spine index, series, number) of a well-formed reference, else None."""
    m = REF_RE.match(ref)
    if not m:
        return None
    return int(m.group("doc")), m.group("series"), int(m.group("number"))
