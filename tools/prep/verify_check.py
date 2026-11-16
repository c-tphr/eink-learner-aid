"""``prep verify-check``: what a script can confirm about the checker's ``check.json``.

    python3 -m tools.prep verify-check ORIGINAL.epub PREPARED.epub CHECK.json [--json OUT] [--timestamp ISO]

The checker session judges reading comprehension; this command holds its
output to everything that can be verified mechanically (docs/PLAN.md
section 8, "What the checker session checks": "the script confirms those
paragraphs lie inside the section"):

- the file parses and matches ``schema/check.schema.json``;
- it was made against exactly these files (``prepared_sha256`` and
  ``original_sha256``) and the checkpoint file's instructions version;
- every prompt and every section of the book has exactly one verdict, and
  no unknown prompt or section appears;
- every cited paragraph exists and lies inside the prompt's own section, as
  the prepared file's anchors place it;
- a ``pass`` verdict has at least one citation and every criterion true;
- the overall verdict is ``pass`` only if every prompt and section passes
  and no open blocking issue remains; issue targets exist.

The result passes only when nothing above fails AND the checker's verdict
is ``pass`` (code ``checker-verdict-fail`` otherwise). Codes are listed in
tools/prep/README.md ("check.json").
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from . import checkpoints as cp
from .epub import Book, EpubError, load_book_bytes
from .validate import Check, Failure, Resolved, _check_item, _resolve_anchors, checkpoint_zip_path, sha256, timestamp
from .version import TOOL_NAME, TOOL_VERSION

FORMAT = "rsvp-check"
VERSION = 1
SCHEMA_PATH = Path(__file__).resolve().parent / "schema" / "check.schema.json"
CRITERIA = ("supported_by_section", "accepts_paraphrase", "answerable_from_section", "does_not_give_away_answer")
CRITERION_TEXT = {
    "supported_by_section": "the answer is supported by its own section",
    "accepts_paraphrase": "the answer states the essential ideas clearly enough to accept a paraphrase",
    "answerable_from_section": "the prompt can be answered from its section alone",
    "does_not_give_away_answer": "the prompt does not give its answer away",
}

_validator: Any = None


def _schema_validator() -> Any:
    global _validator
    if _validator is None:
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        cp.Draft202012Validator.check_schema(schema)
        _validator = cp.StrictValidator(schema)
    return _validator


@dataclass
class Verification:
    failures: list[Failure] = field(default_factory=list)
    check: dict[str, Any] | None = None  # the parsed check.json, when it passed its schema
    check_sha256: str | None = None

    @property
    def passed(self) -> bool:
        return not self.failures

    @property
    def consistent(self) -> bool:
        """No failure other than the checker's own fail verdict."""
        return all(f.code == "checker-verdict-fail" for f in self.failures)

    def fail(self, code: str, location: str, message: str) -> None:
        self.failures.append(Failure(code, location, message))


def _prepared_sections(book: Book) -> tuple[dict[str, Any] | None, dict[str, Resolved], str]:
    """The prepared book's checkpoint data and resolved sections (empty when unusable)."""
    item = Check("checkpoint-item")
    raw = _check_item(book, item)
    if raw is None:
        return None, {}, "the prepared file has no readable rsvp/checkpoints.json"
    data, issues = cp.parse_checkpoints(raw)
    if issues or cp.validate_schema(data):
        return None, {}, "the prepared file's checkpoint file fails its schema"
    resolved, _ = _resolve_anchors(book, data, Check("anchors"), checkpoint_zip_path(book))
    return data, resolved, ""


def verify_check_bytes(original_bytes: bytes, prepared_bytes: bytes, check_raw: bytes, check_name: str = "check.json",
                       prepared_book: Book | None = None) -> Verification:
    v = Verification(check_sha256=sha256(check_raw))
    where = check_name
    parsed, issues = cp.parse_checkpoints(check_raw)
    if issues:
        for issue in issues:
            v.fail("check-unreadable", f"{where}#{issue.path}", issue.message)
        return v
    errors = sorted(_schema_validator().iter_errors(parsed), key=lambda e: cp.json_pointer(e.absolute_path))
    if errors:
        for e in errors:
            v.fail("check-schema", f"{where}#{cp.json_pointer(e.absolute_path)}", cp._shorten(e.message))
        return v
    v.check = parsed

    original_sha = sha256(original_bytes)
    prepared_sha = sha256(prepared_bytes)
    if parsed["prepared_sha256"] != prepared_sha:
        v.fail("stale-check", f"{where}#/prepared_sha256",
               f"the checker judged a prepared file with SHA-256 {parsed['prepared_sha256']}, but the prepared file "
               f"given has {prepared_sha}; check this file again")
    if parsed["original_sha256"] != original_sha:
        v.fail("original-mismatch", f"{where}#/original_sha256",
               f"the checker worked from an original with SHA-256 {parsed['original_sha256']}, but the original given "
               f"has {original_sha}")

    book = prepared_book
    if book is None:
        try:
            book = load_book_bytes(prepared_bytes, "prepared")
        except EpubError as exc:
            v.fail("prepared-unreadable", "prepared", str(exc))
            return v
    data, resolved, problem = _prepared_sections(book)
    if data is None:
        v.fail("prepared-unreadable", "prepared", problem)
        return v
    if parsed["instructions_version"] != data["book"]["instructions_version"]:
        v.fail("instructions-version-mismatch", f"{where}#/instructions_version",
               f"the checker applied instructions version {parsed['instructions_version']!r}, but the book was prepared "
               f"under {data['book']['instructions_version']!r}")

    section_of_prompt: dict[str, str] = {}
    checkpoint_ids = set()
    for c in data["checkpoints"]:
        checkpoint_ids.add(c["id"])
        for p in c["prompts"]:
            section_of_prompt.setdefault(p["id"], c["section"])
    section_ids = [s["id"] for s in data["sections"]]

    # One verdict per prompt and per section.
    for kind, entries, known, unknown_code, missing_code in (
            ("prompt", parsed["prompts"], list(section_of_prompt), "unknown-prompt", "missing-prompt"),
            ("section", parsed["sections"], section_ids, "unknown-section", "missing-section")):
        seen: dict[str, int] = {}
        for i, entry in enumerate(entries):
            eid = entry["id"]
            if eid in seen:
                v.fail(f"duplicate-{kind}", f"{where}#/{kind}s/{i}/id",
                       f"{kind} {eid} has a second verdict (first at /{kind}s/{seen[eid]})")
                continue
            seen[eid] = i
            if eid not in known:
                v.fail(unknown_code, f"{where}#/{kind}s/{i}/id", f"the book has no {kind} {eid!r}")
        for eid in known:
            if eid not in seen:
                v.fail(missing_code, f"{where}#/{kind}s", f"{kind} {eid} has no verdict")

    refs = book.all_refs()
    failed_prompts = []
    for i, entry in enumerate(parsed["prompts"]):
        pid = entry["id"]
        base = f"{where}#/prompts/{i}"
        sid = section_of_prompt.get(pid)
        failed_criteria = [c for c in CRITERIA if not entry[c]]
        if entry["verdict"] == "pass":
            if not entry["citations"]:
                v.fail("pass-without-citation", f"{base}/citations",
                       f"prompt {pid} passes without citing a paragraph that supports its answer")
            if failed_criteria:
                v.fail("pass-with-failed-criterion", f"{base}/verdict",
                       f"prompt {pid} passes although {', '.join(failed_criteria)} is false")
        else:
            failed_prompts.append(pid)
        if sid is None:
            continue
        r = resolved.get(sid)
        for k, ref in enumerate(entry["citations"]):
            hit = refs.get(ref)
            if hit is None or hit[1].series != "p":
                v.fail("unknown-paragraph", f"{base}/citations/{k}",
                       f"prompt {pid} cites {ref}, which is not a paragraph of this book")
                continue
            if r is None:
                v.fail("section-unresolved", f"{base}/citations/{k}",
                       f"prompt {pid}'s section {sid} has no valid anchors in the prepared file, so its citations "
                       "cannot be confirmed (see prep validate, check anchors)")
                break
            sd, block = hit
            inside = sd is r.doc and r.start <= block.start and block.end <= r.end and block.start < r.end
            if not inside:
                v.fail("citation-outside-section", f"{base}/citations/{k}",
                       f"prompt {pid} cites {ref} ({sd.href}, words {block.start}-{block.end - 1}), which is not "
                       f"inside its section {sid} ({r.doc.href}, words {r.start}-{r.end - 1}); an answer must be "
                       "supported by its own section")

    failed_sections = [s["id"] for s in parsed["sections"] if not s["natural_boundary"]]
    targets = set(section_of_prompt) | checkpoint_ids | set(section_ids) | {"book"}
    open_blocking = []
    for i, issue in enumerate(parsed["issues"]):
        if issue["target"] not in targets:
            v.fail("unknown-issue-target", f"{where}#/issues/{i}/target",
                   f"issue {issue['id']} targets {issue['target']!r}, which is no prompt, checkpoint or section of "
                   "this book (nor 'book')")
        if issue["status"] == "open" and issue["severity"] == "blocking":
            open_blocking.append(issue["id"])
    seen_issue_ids: set[str] = set()
    for i, issue in enumerate(parsed["issues"]):
        if issue["id"] in seen_issue_ids:
            v.fail("duplicate-issue", f"{where}#/issues/{i}/id", f"issue id {issue['id']} is used twice")
        seen_issue_ids.add(issue["id"])

    reasons = []
    if failed_prompts:
        reasons.append(f"prompts {', '.join(failed_prompts)} fail")
    if failed_sections:
        reasons.append(f"sections {', '.join(failed_sections)} do not break at a natural boundary")
    if open_blocking:
        reasons.append(f"blocking issues {', '.join(open_blocking)} are open")
    if parsed["verdict"] == "pass" and reasons:
        v.fail("verdict-unjustified", f"{where}#/verdict", "the overall verdict is pass, but " + "; ".join(reasons))
    if parsed["verdict"] == "fail":
        v.fail("checker-verdict-fail", f"{where}#/verdict",
               "the checker did not pass the draft" + (": " + "; ".join(reasons) if reasons else ""))
    return v


def verification_json(v: Verification, original_name: str, original_sha: str | None, prepared_name: str,
                      prepared_sha: str | None, check_name: str, created: str | None = None) -> dict[str, Any]:
    c = v.check
    return {
        "format": "rsvp-check-verification",
        "version": 1,
        "tool": {"name": TOOL_NAME, "version": TOOL_VERSION},
        "created": created or timestamp(),
        "original": {"file": original_name, "sha256": original_sha},
        "prepared": {"file": prepared_name, "sha256": prepared_sha},
        "check": {"file": check_name, "sha256": v.check_sha256},
        "checker": c["checker"] if c else None,
        "checker_verdict": c["verdict"] if c else None,
        "consistent": v.consistent,
        "passed": v.passed,
        "failures": [f.as_json() for f in v.failures],
    }
