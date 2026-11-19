"""The firmware's limits on ``rsvp/checkpoints.json``, mirrored for ``prep validate``.

The numbers are copied from ``lib/CheckpointCore/CheckpointLimits.h``;
``tests/test_limits.py`` parses that header and fails if the two differ. The
firmware rejects a file that exceeds any of them (its issue code ``limit``),
so a prepared book that breaks one would open on the device as an ordinary
EPUB without checkpoints. ``check_limits`` reports every limit a file
exceeds, each as ``Limit(name, path, message)`` where ``name`` is the C++
constant.

The limits a JSON Schema can express (counts of one array, lengths of one
string) are also in ``schema/checkpoints.schema.json``; the rest (file size,
total prompts, distinct chapters, the string pool, JSON nesting, open
members, number length) are only here. Characters are Unicode code points
after JSON unescaping, as in the schema.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any

# Whole file.
MAX_FILE_BYTES = 1024 * 1024
# Entry counts.
MAX_SECTIONS = 512
MAX_CHECKPOINTS = 512
MAX_PROMPTS = 1024  # all checkpoints together
MAX_PROMPTS_PER_CHECKPOINT = 32
MAX_CHAPTERS = 256  # distinct chapter_href values
# Section ids, checkpoint ids, checkpoint section references and distinct chapter hrefs, each as UTF-8 plus a NUL.
STRING_POOL_BYTES = 16384
# Text lengths in code points.
MAX_ID_CHARS = 64
MAX_PROMPT_CHARS = 300
MAX_ANSWER_CHARS = 600
MAX_HREF_CHARS = 255
MAX_LABEL_CHARS = 200
MAX_TITLE_CHARS = 300
MAX_LANGUAGE_CHARS = 35
MAX_INSTRUCTIONS_VERSION_CHARS = 32
# JSON structure, anywhere in the file including unknown members.
MAX_DEPTH = 32
MAX_OPEN_MEMBERS = 128
MAX_NUMBER_CHARS = 64

# Every limit above by its C++ name (the test compares this with the header).
LIMITS = {
    name: value for name, value in globals().items() if name.startswith(("MAX_", "STRING_POOL")) and isinstance(value, int)
}


@dataclass(frozen=True)
class Limit:
    name: str  # the C++ constant
    path: str  # RFC 6901 pointer into the file ("" for the whole file)
    message: str


def _pointer(parts: list[Any]) -> str:
    return "".join("/" + str(p).replace("~", "~0").replace("/", "~1") for p in parts)


def _structure(value: Any, depth: int, open_members: int, parts: list[Any], out: dict[str, Limit]) -> None:
    """Depth and open-member counts as the firmware's streaming parser sees them (first excess only)."""
    if isinstance(value, (dict, list)):
        depth += 1
        if depth > MAX_DEPTH and "MAX_DEPTH" not in out:
            out["MAX_DEPTH"] = Limit("MAX_DEPTH", _pointer(parts),
                                     f"objects and arrays are nested {depth} deep here; the firmware limit is {MAX_DEPTH}")
            return
    if isinstance(value, dict):
        for n, (key, item) in enumerate(value.items(), start=1):
            total = open_members + n
            if total > MAX_OPEN_MEMBERS and "MAX_OPEN_MEMBERS" not in out:
                out["MAX_OPEN_MEMBERS"] = Limit(
                    "MAX_OPEN_MEMBERS", _pointer(parts + [key]),
                    f"{total} members of open objects at once; the firmware limit is {MAX_OPEN_MEMBERS}")
                return
            _structure(item, depth, total, parts + [key], out)
    elif isinstance(value, list):
        for i, item in enumerate(value):
            _structure(item, depth, open_members, parts + [i], out)


def _long_numbers(raw: bytes) -> list[str]:
    """Number tokens of the file longer than MAX_NUMBER_CHARS (needs the literal text, not the value)."""
    found: list[str] = []

    def keep(token: str) -> Any:
        if len(token) > MAX_NUMBER_CHARS:
            found.append(token)
        return 0

    try:
        json.loads(raw.decode("utf-8"), parse_int=keep, parse_float=keep, parse_constant=keep)
    except ValueError:
        pass  # a parse problem is the checkpoint validator's to report
    return found


def check_limits(raw: bytes, data: Any) -> list[Limit]:
    """Every firmware limit ``raw`` (with parsed value ``data``, or None) exceeds.

    Members of the wrong type are skipped: the schema reports them.
    """
    out: list[Limit] = []
    if len(raw) > MAX_FILE_BYTES:
        out.append(Limit("MAX_FILE_BYTES", "", f"the file has {len(raw)} bytes; the firmware limit is {MAX_FILE_BYTES}"))
    for token in _long_numbers(raw):
        out.append(Limit("MAX_NUMBER_CHARS", "", f"a number has {len(token)} characters; the firmware limit is "
                                                f"{MAX_NUMBER_CHARS}"))
        break
    if data is None:
        return out
    structure: dict[str, Limit] = {}
    _structure(data, 0, 0, [], structure)
    out += [structure[k] for k in ("MAX_DEPTH", "MAX_OPEN_MEMBERS") if k in structure]
    if not isinstance(data, dict):
        return out

    def text(value: Any, name: str, limit: int, path: str, what: str) -> None:
        if isinstance(value, str) and len(value) > limit:
            out.append(Limit(name, path, f"{what} has {len(value)} characters; the firmware limit is {limit}"))

    book = data.get("book")
    if isinstance(book, dict):
        text(book.get("title"), "MAX_TITLE_CHARS", MAX_TITLE_CHARS, "/book/title", "title")
        text(book.get("language"), "MAX_LANGUAGE_CHARS", MAX_LANGUAGE_CHARS, "/book/language", "language")
        text(book.get("instructions_version"), "MAX_INSTRUCTIONS_VERSION_CHARS", MAX_INSTRUCTIONS_VERSION_CHARS,
             "/book/instructions_version", "instructions_version")

    pool = 0
    chapters: list[str] = []
    sections = data.get("sections")
    if isinstance(sections, list):
        if len(sections) > MAX_SECTIONS:
            out.append(Limit("MAX_SECTIONS", "/sections",
                             f"{len(sections)} sections; the firmware limit is {MAX_SECTIONS}"))
        for i, s in enumerate(sections):
            if not isinstance(s, dict):
                continue
            base = f"/sections/{i}"
            text(s.get("id"), "MAX_ID_CHARS", MAX_ID_CHARS, f"{base}/id", "id")
            text(s.get("chapter_href"), "MAX_HREF_CHARS", MAX_HREF_CHARS, f"{base}/chapter_href", "chapter_href")
            text(s.get("label"), "MAX_LABEL_CHARS", MAX_LABEL_CHARS, f"{base}/label", "label")
            if isinstance(s.get("id"), str):
                pool += len(s["id"].encode("utf-8")) + 1
            href = s.get("chapter_href")
            if isinstance(href, str) and href not in chapters:
                chapters.append(href)
    if len(chapters) > MAX_CHAPTERS:
        out.append(Limit("MAX_CHAPTERS", "/sections",
                         f"{len(chapters)} distinct chapter_href values; the firmware limit is {MAX_CHAPTERS}"))
    pool += sum(len(h.encode("utf-8")) + 1 for h in chapters)

    prompts_total = 0
    checkpoints = data.get("checkpoints")
    if isinstance(checkpoints, list):
        if len(checkpoints) > MAX_CHECKPOINTS:
            out.append(Limit("MAX_CHECKPOINTS", "/checkpoints",
                             f"{len(checkpoints)} checkpoints; the firmware limit is {MAX_CHECKPOINTS}"))
        for i, c in enumerate(checkpoints):
            if not isinstance(c, dict):
                continue
            base = f"/checkpoints/{i}"
            text(c.get("id"), "MAX_ID_CHARS", MAX_ID_CHARS, f"{base}/id", "id")
            text(c.get("section"), "MAX_ID_CHARS", MAX_ID_CHARS, f"{base}/section", "section")
            for key in ("id", "section"):
                if isinstance(c.get(key), str):
                    pool += len(c[key].encode("utf-8")) + 1
            prompts = c.get("prompts")
            if not isinstance(prompts, list):
                continue
            prompts_total += len(prompts)
            if len(prompts) > MAX_PROMPTS_PER_CHECKPOINT:
                out.append(Limit("MAX_PROMPTS_PER_CHECKPOINT", f"{base}/prompts",
                                 f"{len(prompts)} prompts in one checkpoint; the firmware limit is "
                                 f"{MAX_PROMPTS_PER_CHECKPOINT}"))
            for j, p in enumerate(prompts):
                if not isinstance(p, dict):
                    continue
                pbase = f"{base}/prompts/{j}"
                text(p.get("id"), "MAX_ID_CHARS", MAX_ID_CHARS, f"{pbase}/id", "id")
                text(p.get("prompt"), "MAX_PROMPT_CHARS", MAX_PROMPT_CHARS, f"{pbase}/prompt", "prompt")
                text(p.get("answer"), "MAX_ANSWER_CHARS", MAX_ANSWER_CHARS, f"{pbase}/answer", "answer")
    if prompts_total > MAX_PROMPTS:
        out.append(Limit("MAX_PROMPTS", "/checkpoints",
                         f"{prompts_total} prompts in the file; the firmware limit is {MAX_PROMPTS}"))
    if pool > STRING_POOL_BYTES:
        out.append(Limit("STRING_POOL_BYTES", "",
                         f"section ids, checkpoint ids, section references and distinct chapter hrefs need {pool} "
                         f"bytes of string pool; the firmware limit is {STRING_POOL_BYTES}"))
    return out
