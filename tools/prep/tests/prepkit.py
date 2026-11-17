"""Shared helpers for the validate, verify-check and report tests."""

from __future__ import annotations

import hashlib
import io
import json
import subprocess
import sys
import zipfile
from pathlib import Path
from typing import Any, Callable

import pytest

from tools.prep import epubcheck
from tools.prep.build import checkpoint_bytes
from tools.prep.epubzip import Archive

REPO = Path(__file__).resolve().parents[3]
BOOKS = REPO / "fixtures" / "books"
NEGATIVE = BOOKS / "negative"
ORIGINAL = BOOKS / "fixture.epub"
PREPARED = BOOKS / "fixture.prepared.epub"
DRAFT = BOOKS / "fixture.draft.json"
CHECK = BOOKS / "fixture.check.json"
ORIGINAL_BYTES = ORIGINAL.read_bytes()
PREPARED_BYTES = PREPARED.read_bytes()
MANIFEST = json.loads((NEGATIVE / "manifest.json").read_text(encoding="utf-8"))
DRAFT_MANIFEST = json.loads((NEGATIVE / "draft-manifest.json").read_text(encoding="utf-8"))
CP = "OEBPS/rsvp/checkpoints.json"
OPF = "OEBPS/content.opf"
CH1, CH2, CH3 = "OEBPS/text/chapter-1.xhtml", "OEBPS/text/chapter-2.xhtml", "OEBPS/text/chapter-3.xhtml"
TS = "2026-01-01T00:00:00Z"


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def ok_epubcheck(path: Path) -> epubcheck.Report:
    """A stand-in for EPUBCheck that reports nothing (tests that are not about EPUBCheck)."""
    return epubcheck.Report("5.3.0", 0, [])


def unavailable_epubcheck(path: Path) -> epubcheck.Report:
    raise epubcheck.EpubcheckUnavailable("Java not found: set JAVA_HOME or put 'java' on PATH")


def error_epubcheck(path: Path) -> epubcheck.Report:
    return epubcheck.Report("5.3.0", 1, [epubcheck.Message("ERROR", "RSC-005", "Error while parsing file",
                                                           ("OEBPS/text/chapter-1.xhtml:9:3",))])


def real_epubcheck(path: Path) -> epubcheck.Report:
    try:
        return epubcheck.run_epubcheck(path)
    except epubcheck.EpubcheckUnavailable as exc:
        pytest.skip(f"EPUBCheck unavailable, NOT checked: {exc}")


def edit(data: bytes, changes: dict[str, Callable[[bytes], bytes]] | None = None,
         add: list[tuple[str, bytes]] = ()) -> bytes:
    """``data`` (an EPUB) with members changed by ``changes`` and new members appended (byte-preserving)."""
    archive = Archive(data)
    replace = {name: fn(archive.read(name)) for name, fn in (changes or {}).items()}
    return archive.rebuild(replace, [(name, content, OPF) for name, content in add])


def replace_once(old: bytes | str, new: bytes | str) -> Callable[[bytes], bytes]:
    old_b = old.encode("utf-8") if isinstance(old, str) else old
    new_b = new.encode("utf-8") if isinstance(new, str) else new

    def fn(data: bytes) -> bytes:
        assert data.count(old_b) == 1, (old_b, data.count(old_b))
        return data.replace(old_b, new_b)
    return fn


def edit_checkpoints(fn: Callable[[dict[str, Any]], None], data: bytes = PREPARED_BYTES) -> bytes:
    def change(raw: bytes) -> bytes:
        doc = json.loads(raw)
        fn(doc)
        return checkpoint_bytes(doc)
    return edit(data, {CP: change})


def without_member(data: bytes, name: str) -> bytes:
    """``data`` rewritten by zipfile without ``name`` (members recompressed as they were)."""
    src = zipfile.ZipFile(io.BytesIO(data))
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w") as dst:
        for info in src.infolist():
            if info.filename != name:
                dst.writestr(info, src.read(info.filename))
    return out.getvalue()


def span(anchor: str) -> str:
    return f'<span id="{anchor}"></span>'


def run_prep(*args: Any, env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, "-m", "tools.prep", *map(str, args)], capture_output=True, text=True,
                          cwd=REPO, env=env)


def failing(result: dict[str, Any]) -> dict[str, set[str]]:
    """{check name: set of failure codes} for every check whose status is fail."""
    return {c["name"]: {f["code"] for f in c["failures"]} for c in result["checks"] if c["status"] == "fail"}


def status(result: dict[str, Any]) -> dict[str, str]:
    return {c["name"]: c["status"] for c in result["checks"]}
