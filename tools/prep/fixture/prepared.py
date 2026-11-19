#!/usr/bin/env python3
"""Generate the prepared fixture, the negative fixtures and the sample checker results.

    python3 tools/prep/fixture/prepared.py            # write the files
    python3 tools/prep/fixture/prepared.py --check    # exit 1 if the committed files are stale
    python3 tools/prep/fixture/prepared.py --out DIR  # write (or check) somewhere else

Sibling of ``generate.py``, run after it: it reads the committed
``fixture.epub`` and ``fixture.draft.json`` and never changes them. Outputs
(under ``fixtures/books/``), all byte-for-byte reproducible:

- ``fixture.prepared.epub``: ``prep build`` output for the fixture.
- ``fixture.check.json``: a passing checker result for it (schema
  ``tools/prep/schema/check.schema.json``), written by hand here as a sample
  for tests; its checker model is ``fixture-sample``, not a real model.
- ``negative/*.prepared.epub``: copies of the prepared fixture, each broken
  in one way a careless tool could produce. ``prep validate`` must reject
  each, failing exactly the checks ``negative/manifest.json`` names.
- ``negative/*.check.json``: checker results ``prep verify-check`` must
  reject, with the codes the manifest names.
- ``negative/manifest.json``: the list.

Unlike ``generate.py`` this uses the preparation tools themselves (``prep
build`` and the zip rewriter), so it needs ``jsonschema``.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import sys
from pathlib import Path
from typing import Any, Callable

REPO = Path(__file__).resolve().parents[3]
if __package__ in (None, ""):
    sys.path.insert(0, str(REPO))

from tools.prep.build import build_bytes, checkpoint_bytes  # noqa: E402
from tools.prep.epub import load_book_bytes  # noqa: E402
from tools.prep.epubzip import Archive  # noqa: E402

BOOKS = REPO / "fixtures" / "books"
ORIGINAL = "fixture.epub"
DRAFT = "fixture.draft.json"
PREPARED = "fixture.prepared.epub"
CHECK = "fixture.check.json"
NEGATIVE = "negative"
MANIFEST = f"{NEGATIVE}/manifest.json"
CP_PATH = "OEBPS/rsvp/checkpoints.json"
CH1, CH2, CH3 = "OEBPS/text/chapter-1.xhtml", "OEBPS/text/chapter-2.xhtml", "OEBPS/text/chapter-3.xhtml"
CHECKER = {"model": "fixture-sample", "date": "2026-01-01"}


def _span(anchor: str) -> bytes:
    return f'<span id="{anchor}"></span>'.encode("ascii")


def _json_bytes(data: Any) -> bytes:
    return (json.dumps(data, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def _replace_once(data: bytes, old: bytes, new: bytes) -> bytes:
    if data.count(old) != 1:
        raise ValueError(f"expected exactly one {old!r}")
    return data.replace(old, new)


# --------------------------------------------------------------------------
# Negative prepared files. Each mutation takes the prepared archive and returns new bytes.


def _edit(archive: Archive, edits: dict[str, Callable[[bytes], bytes]], add: list[tuple[str, bytes]] = ()) -> bytes:
    replace = {name: fn(archive.read(name)) for name, fn in edits.items()}
    opf = next(n for n in archive.names() if n.endswith(".opf"))
    return archive.rebuild(replace, [(name, data, opf) for name, data in add])


def _edit_checkpoints(archive: Archive, fn: Callable[[dict[str, Any]], None]) -> bytes:
    def change(raw: bytes) -> bytes:
        data = json.loads(raw)
        fn(data)
        return checkpoint_bytes(data)
    return _edit(archive, {CP_PATH: change})


def _move_spans(archive: Archive, anchors: list[str], from_doc: str, to_doc: str, before: bytes) -> bytes:
    """Remove the anchor spans from one document and insert them, in order, right before ``before`` in another."""
    def remove(data: bytes) -> bytes:
        for a in anchors:
            data = _replace_once(data, _span(a), b"")
        return data

    def insert(data: bytes) -> bytes:
        return _replace_once(data, before, b"".join(_span(a) for a in anchors) + before)
    return _edit(archive, {from_doc: remove, to_doc: insert})


def _swap(data: bytes, a: bytes, b: bytes) -> bytes:
    marker = b"\x00SWAP\x00"
    return _replace_once(_replace_once(_replace_once(data, a, marker), b, a), marker, b)


# (file stem, failing checks, codes, reason, mutation)
VARIANTS: list[tuple[str, list[str], list[str], str, Callable[[Archive, bytes], bytes]]] = [
    ("broken-link", ["anchors"], ["anchor-missing"],
     "The multi-prompt checkpoint c0002 links to section s0004, whose end anchor <span id=\"rsvp-s0004-end\"> is "
     "missing from text/chapter-2.xhtml (a tool wrote the checkpoint file but dropped the span), so the checkpoint "
     "could never fire.",
     lambda a, o: _edit(a, {CH2: lambda d: _replace_once(d, _span("rsvp-s0004-end"), b"")})),
    ("unknown-section", ["checkpoint-file"], ["unknown-section"],
     "Checkpoint c0002 names section s0006, which does not exist (the sections were renumbered but the checkpoint "
     "kept an old id).",
     lambda a, o: _edit_checkpoints(a, lambda d: d["checkpoints"][1].__setitem__("section", "s0006"))),
    ("end-anchor-misspelt", ["anchors"], ["anchor-missing", "unaccounted-anchor"],
     "The end anchor of s0004 is written rsvp-s004-end in the XHTML: the checkpoint file's anchor is missing and "
     "the misspelt id is an rsvp- id no section accounts for.",
     lambda a, o: _edit(a, {CH2: lambda d: _replace_once(d, _span("rsvp-s0004-end"), _span("rsvp-s004-end"))})),
    ("anchors-wrong-document", ["anchors"], ["anchor-wrong-document"],
     "Both anchors of s0003 (chapter_href text/chapter-2.xhtml) sit in text/chapter-3.xhtml instead.",
     lambda a, o: _move_spans(a, ["rsvp-s0003-start", "rsvp-s0003-end"], CH2, CH3, _span("rsvp-s0005-start"))),
    ("section-spans-documents", ["anchors"], ["anchor-wrong-document", "section-spans-documents"],
     "The end anchor of s0004 was written at the start of the next chapter, so the section would run from "
     "text/chapter-2.xhtml into text/chapter-3.xhtml.",
     lambda a, o: _move_spans(a, ["rsvp-s0004-end"], CH2, CH3, _span("rsvp-s0005-start"))),
    ("start-after-end", ["anchors"], ["anchor-order"],
     "The ids of s0001's two anchors are swapped: its start anchor comes after its end anchor.",
     lambda a, o: _edit(a, {CH1: lambda d: _swap(d, b'"rsvp-s0001-start"', b'"rsvp-s0001-end"')})),
    ("changed-word", ["text-unchanged", "archive-unchanged"], ["words-changed", "document-changed"],
     "A tool 'smartened' the apostrophe in chapter 3's heading (Keeper's to Keeper’s): the word sequence "
     "differs (text-unchanged) and removing the anchors no longer restores the original bytes "
     "(archive-unchanged); a changed word necessarily fails both.",
     lambda a, o: _edit(a, {CH3: lambda d: _replace_once(d, b"></span>The Keeper's Log</h1>",
                                                         "></span>The Keeper’s Log</h1>".encode("utf-8"))})),
    ("extra-member", ["archive-unchanged"], ["member-added"],
     "The draft was packed into the EPUB as OEBPS/rsvp/draft.json; the only new member may be the checkpoint file.",
     lambda a, o: _edit(a, {}, [("OEBPS/rsvp/draft.json", (BOOKS / DRAFT).read_bytes())])),
    ("wrong-source-hash", ["source-hash"], ["source-mismatch"],
     "book.source_sha256 names another edition (the SHA-256 of the bytes 'another edition'), not fixture.epub.",
     lambda a, o: _edit_checkpoints(a, lambda d: d["book"].__setitem__(
         "source_sha256", hashlib.sha256(b"another edition").hexdigest()))),
    ("word-count-off-by-one", ["word-counts"], ["word-count"],
     "Section s0002's word_count is 195; its anchors hold 194 words.",
     lambda a, o: _edit_checkpoints(a, lambda d: d["sections"][1].__setitem__("word_count",
                                                                              d["sections"][1]["word_count"] + 1))),
]


# --------------------------------------------------------------------------
# Checker results


EVIDENCE = {
    "c0001-p1": "d02-p006 says each light had its own pattern of flashes that sailors counted, and that Gannet Rock "
                "gave one white flash every ten seconds, which no other light on the coast did.",
    "c0002-p1": "d03-p009 says the fisherman steered by the sound of the horn alone, two blasts and silence, until "
                "the boat grounded on the shingle below the tower.",
    "c0002-p2": "d03-p010 explains the green light as phosphorescence made by tiny creatures when the water is "
                "disturbed; d03-p011 has Dr. Vane say it is not dangerous but means plankton, so fish.",
    "c0002-p3": "d03-p013 gives Ione's advice: never put to sea in November without oars, however good the engine.",
}
SECTION_EVIDENCE = {
    "s0001": "Ends after the paragraph on the light's flash pattern, before the clockwork scene begins.",
    "s0002": "The clockwork scene runs to the end of chapter 1.",
    "s0003": "Ends after the fog-signal paragraphs, where the visitor's arrival starts a new scene under its own "
             "heading.",
    "s0004": "Runs from the visitor's heading to the end of chapter 2.",
    "s0005": "The whole of chapter 3, one short chapter.",
}


def passing_check(draft: dict[str, Any], prepared_sha: str, original_sha: str) -> dict[str, Any]:
    prompts = []
    for c in draft["checkpoints"]:
        for p in c["prompts"]:
            prompts.append({
                "id": p["id"], "verdict": "pass", "supported_by_section": True,
                "citations": list(p["supporting_paragraphs"]), "accepts_paraphrase": True,
                "answerable_from_section": True, "does_not_give_away_answer": True, "evidence": EVIDENCE[p["id"]],
            })
    return {
        "format": "rsvp-check",
        "version": 1,
        "prepared_sha256": prepared_sha,
        "original_sha256": original_sha,
        "instructions_version": draft["book"]["instructions_version"],
        "checker": dict(CHECKER),
        "round": 1,
        "prompts": prompts,
        "sections": [{"id": s["id"], "natural_boundary": True, "evidence": SECTION_EVIDENCE[s["id"]]}
                     for s in draft["sections"]],
        "issues": [],
        "verdict": "pass",
    }


def _prompt(check: dict[str, Any], pid: str) -> dict[str, Any]:
    return next(p for p in check["prompts"] if p["id"] == pid)


def _citation_outside(c: dict[str, Any], stale: str) -> None:
    _prompt(c, "c0002-p1")["citations"] = ["d03-p003"]


def _missing_prompt(c: dict[str, Any], stale: str) -> None:
    c["prompts"] = [p for p in c["prompts"] if p["id"] != "c0002-p3"]


def _stale(c: dict[str, Any], stale: str) -> None:
    c["prepared_sha256"] = stale


def _no_citation(c: dict[str, Any], stale: str) -> None:
    _prompt(c, "c0001-p1")["citations"] = []


def _unjustified(c: dict[str, Any], stale: str) -> None:
    p = _prompt(c, "c0002-p2")
    p["verdict"] = "fail"
    p["accepts_paraphrase"] = False
    p["evidence"] = "The answer runs two ideas together; a reader could not tell which part is essential."


def _failed(c: dict[str, Any], stale: str) -> None:
    p = _prompt(c, "c0002-p2")
    p.update({"verdict": "fail", "supported_by_section": False, "citations": ["d03-p010"],
              "evidence": "d03-p010 explains the light, but no paragraph of s0004 says what it told Dr. Vane about "
                          "fish (sample: the answer is treated as unsupported)."})
    c["issues"] = [{"id": "I1", "severity": "blocking", "target": "c0002-p2",
                    "description": "Part of the reference answer (the plankton means fish) is not supported by "
                                   "section s0004.",
                    "suggested_fix": "Cite the paragraph that says it, or drop that part of the answer.",
                    "status": "open"}]
    c["verdict"] = "fail"


# (file stem, codes, reason, mutation)
CHECK_VARIANTS: list[tuple[str, list[str], str, Callable[[dict[str, Any], str], None]]] = [
    ("citation-outside-section", ["citation-outside-section"],
     "Prompt c0002-p1 (section s0004) cites d03-p003, a paragraph of section s0003.", _citation_outside),
    ("missing-prompt", ["missing-prompt"], "Prompt c0002-p3 has no verdict.", _missing_prompt),
    ("stale-hash", ["stale-check"],
     "prepared_sha256 is that of an earlier prepared file (negative/broken-link.prepared.epub): the check is "
     "no evidence for fixture.prepared.epub.", _stale),
    ("pass-without-citation", ["pass-without-citation"], "Prompt c0001-p1 passes without a citation.",
     _no_citation),
    ("unjustified-verdict", ["verdict-unjustified"],
     "Prompt c0002-p2 fails, but the overall verdict is pass.", _unjustified),
    ("checker-failed", ["checker-verdict-fail"],
     "A consistent result in which the checker flags an unsupported answer (c0002-p2) with an open blocking issue "
     "and fails the draft.", _failed),
]


# --------------------------------------------------------------------------
# Entry points


def generate_files(books: Path = BOOKS) -> dict[str, bytes]:
    """Every output path (relative to ``fixtures/books``) mapped to its bytes."""
    original = (books / ORIGINAL).read_bytes()
    draft_raw = (books / DRAFT).read_bytes()
    draft = json.loads(draft_raw)
    prepared = build_bytes(load_book_bytes(original, ORIGINAL), draft_raw).data
    archive = Archive(prepared)
    out: dict[str, bytes] = {PREPARED: prepared}
    prepared_sha = hashlib.sha256(prepared).hexdigest()
    original_sha = hashlib.sha256(original).hexdigest()
    check = passing_check(draft, prepared_sha, original_sha)
    out[CHECK] = _json_bytes(check)

    manifest: dict[str, Any] = {
        "format": "rsvp-negative-fixtures",
        "version": 1,
        "description": "Generated by tools/prep/fixture/prepared.py. 'prepared': copies of fixture.prepared.epub "
                       "that 'prep validate ../fixture.epub FILE' must reject; 'failing_checks' is the exact set of "
                       "checks with status fail, 'codes' the exact set of failure codes. 'checks': checker results "
                       "that 'prep verify-check ../fixture.epub ../fixture.prepared.epub FILE' must reject with "
                       "exactly 'codes'.",
        "original": f"../{ORIGINAL}",
        "prepared": [],
        "checks": [],
    }
    for stem, checks, codes, reason, mutate in VARIANTS:
        name = f"{stem}.prepared.epub"
        out[f"{NEGATIVE}/{name}"] = mutate(archive, original)
        manifest["prepared"].append({"file": name, "failing_checks": checks, "codes": codes, "reason": reason})
    stale = hashlib.sha256(out[f"{NEGATIVE}/broken-link.prepared.epub"]).hexdigest()
    for stem, codes, reason, mutate in CHECK_VARIANTS:
        variant = copy.deepcopy(check)
        mutate(variant, stale)
        name = f"{stem}.check.json"
        out[f"{NEGATIVE}/{name}"] = _json_bytes(variant)
        manifest["checks"].append({"file": name, "prepared": f"../{PREPARED}", "codes": codes, "reason": reason})
    out[MANIFEST] = _json_bytes(manifest)
    return out


def write_files(out_dir: Path, files: dict[str, bytes]) -> None:
    for name, data in files.items():
        target = out_dir / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)


def _show(path: Path) -> Path:
    try:
        return path.resolve().relative_to(Path.cwd().resolve())
    except ValueError:
        return path


def check(out_dir: Path) -> list[str]:
    problems = []
    files = generate_files()
    for name, data in files.items():
        target = out_dir / name
        if not target.exists():
            problems.append(f"missing: {_show(target)}")
        elif target.read_bytes() != data:
            problems.append(f"differs: {_show(target)}")
    negative = out_dir / NEGATIVE
    if negative.is_dir():
        for path in sorted(negative.iterdir()):
            if f"{NEGATIVE}/{path.name}" not in files:
                problems.append(f"not generated (remove it or add it to prepared.py): {_show(path)}")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true", help="exit 1 if the files in --out are not current")
    parser.add_argument("--out", type=Path, default=BOOKS, help=f"output directory (default {BOOKS})")
    args = parser.parse_args(argv)
    if args.check:
        problems = check(args.out)
        for problem in problems:
            print(problem)
        if problems:
            print(f"{len(problems)} problems; run tools/prep/fixture/prepared.py")
            return 1
        print(f"{_show(args.out)}: all {len(generate_files())} prepared and negative fixture files are current")
        return 0
    files = generate_files()
    write_files(args.out, files)
    for name, data in files.items():
        print(f"wrote {_show(args.out / name)} ({len(data)} bytes, sha256 {hashlib.sha256(data).hexdigest()})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
