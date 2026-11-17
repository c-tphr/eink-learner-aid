"""``prep report``: decide whether a prepared book is ready, and write the validation report.

    python3 -m tools.prep report --original ORIGINAL.epub --prepared PREPARED.epub
        [--validation validation.json] [--check check.json] [--history WORKDIR] [--draft draft.json]
        -o OUTDIR [--timestamp ISO]

Writes ``<name>.validation-report.md`` and ``<name>.validation-report.json``
to ``OUTDIR`` (``<name>`` is the prepared file's name without
``.prepared.epub``) and, only when the book is ready, a copy of the
prepared EPUB beside them, written from the very bytes that were checked.

The book is READY only when all of these hold (docs/PLAN.md section 8,
"Controls: no pass, no delivery"):

1. ``validate``, re-run here on the files given, passes every check
   (EPUBCheck included: if it cannot run, the book is not ready);
2. the latest stored validator result passed and was made against the
   SHA-256 of exactly this prepared file and this original;
3. ``verify-check``, re-run here, passes: the latest checker result was made
   against exactly this prepared file and original, is complete and
   consistent, cites paragraphs inside each prompt's own section, and its
   verdict is pass.

Stored results are evidence of history, not authority: editing a stored
``validation.json`` or ``check.json`` cannot make a book ready, because the
validator and the script-side checks run again. Anything else gives a
report whose first line is ``NOT READY`` and exit status 1.

Round history (written by ``/prep-book``; optional): ``WORKDIR/rounds/NN/``
for round NN (01, 02, 03), each holding the round's ``draft.json``,
``validation.json`` and ``check.json``. When ``--validation``, ``--check``
or ``--draft`` is not given, the latest round's file is used.
"""

from __future__ import annotations

import json
import os
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from . import validate as V
from .verify_check import Verification, verification_json, verify_check_bytes
from .version import TOOL_NAME, TOOL_VERSION, WORD_RULES_VERSION

FORMAT = "rsvp-validation-report"
VERSION = 1
SCHEMA_PATH = Path(__file__).resolve().parent / "schema" / "report.schema.json"
READY, NOT_READY = "READY", "NOT READY"

STATUS_WORDS = {"pass": "Passed", "fail": "Failed", "incomplete": "Incomplete", "not-run": "Not run"}

# verify-check codes, grouped into the report's three checker rows.
CHECKER_FILE_CODES = {"stale-check", "original-mismatch", "instructions-version-mismatch", "prepared-unreadable"}
CHECKER_VERDICT_CODES = {"checker-verdict-fail"}


class ReportUsageError(ValueError):
    """The command line names files that cannot be used as asked (exit status 2)."""


# --------------------------------------------------------------------------
# Round history


@dataclass
class Round:
    number: int
    path: Path
    validation: dict[str, Any] | None = None
    check: dict[str, Any] | None = None
    draft: dict[str, Any] | None = None


def _load_json(path: Path) -> dict[str, Any] | None:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    return value if isinstance(value, dict) else None


def load_history(work_dir: Path | None) -> list[Round]:
    """Rounds in ``work_dir/rounds/NN`` in numeric order (empty when there is no history)."""
    if work_dir is None or not (work_dir / "rounds").is_dir():
        return []
    rounds = []
    for d in (work_dir / "rounds").iterdir():
        if d.is_dir() and d.name.isdigit():
            rounds.append(Round(int(d.name), d, _load_json(d / "validation.json"), _load_json(d / "check.json"),
                                _load_json(d / "draft.json")))
    return sorted(rounds, key=lambda r: r.number)


def _latest(rounds: list[Round], name: str) -> Path | None:
    for r in reversed(rounds):
        if (r.path / name).is_file():
            return r.path / name
    return None


# --------------------------------------------------------------------------
# Evaluation


@dataclass
class Issue:
    round: str
    raised_by: str  # validator, checker, report
    code: str
    where: str
    description: str
    severity: str = "blocking"
    resolution: str = "Open"

    @property
    def open(self) -> bool:
        return self.resolution == "Open"

    def as_json(self) -> dict[str, Any]:
        return {"round": self.round, "raised_by": self.raised_by, "code": self.code, "where": self.where,
                "description": self.description, "severity": self.severity, "resolution": self.resolution,
                "open": self.open}


@dataclass
class Evaluation:
    ready: bool
    report: dict[str, Any]
    markdown: str
    prepared_bytes: bytes | None
    name: str
    reasons: list[str] = field(default_factory=list)


def _read(path: Path | None) -> bytes | None:
    if path is None:
        return None
    try:
        return path.read_bytes()
    except OSError:
        return None


def report_name(prepared: Path) -> str:
    name = prepared.name
    for suffix in (".prepared.epub", ".epub"):
        if name.endswith(suffix) and len(name) > len(suffix):
            return name[: -len(suffix)]
    return name


def _stored_validation(raw: bytes | None, original_sha: str | None, prepared_sha: str | None,
                       rerun_passed: bool) -> tuple[bool, list[str]]:
    """(usable and passed for these exact files, problems)."""
    if raw is None:
        return False, ["No validator result was given, so there is no record that the validator passed this file."]
    try:
        stored = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        return False, ["The validator result cannot be read (it is not JSON)."]
    problems = V.validate_result_schema(stored)
    if problems:
        return False, [f"The validator result does not match its schema ({problems[0]})."]
    out = []
    if stored["prepared"]["sha256"] != prepared_sha:
        out.append(f"The latest validator result was made against another prepared file (SHA-256 "
                   f"{stored['prepared']['sha256']}), not the one being delivered.")
    if stored["original"]["sha256"] != original_sha:
        out.append(f"The latest validator result was made against another original (SHA-256 "
                   f"{stored['original']['sha256']}).")
    if not stored["passed"]:
        failed = [c["name"] for c in stored["checks"] if c["status"] != "pass"]
        out.append(f"The latest validator result did not pass (checks not passed: {', '.join(failed)}).")
    elif not rerun_passed:
        out.append("The stored validator result says it passed, but running the validator again on these files "
                   "fails; the stored result is not trusted.")
    return not out, out


def _checker_rows(v: Verification | None) -> list[tuple[str, str, str]]:
    """(name, what it confirms, status) for the three checker rows of the checks table."""
    rows = [("checker-files", "The checker's result is for exactly this prepared file, this original and these "
                              "instructions"),
            ("checker-complete", "The checker gave one verdict per prompt and section, and every cited paragraph "
                                 "lies inside the prompt's own section"),
            ("checker-verdict", "The checker passed every prompt and every section, with no open blocking issue")]
    if v is None:
        return [(n, t, "not-run") for n, t in rows]
    if v.check is None:  # unreadable or schema failure
        return [(rows[0][0], rows[0][1], "not-run"), (rows[1][0], rows[1][1], "fail"),
                (rows[2][0], rows[2][1], "not-run")]
    codes = {f.code for f in v.failures}
    files = "fail" if codes & CHECKER_FILE_CODES else "pass"
    complete = "fail" if codes - CHECKER_FILE_CODES - CHECKER_VERDICT_CODES else "pass"
    verdict = "fail" if codes & CHECKER_VERDICT_CODES else "pass"
    return [(rows[0][0], rows[0][1], files), (rows[1][0], rows[1][1], complete), (rows[2][0], rows[2][1], verdict)]


def _history_issues(rounds: list[Round]) -> list[Issue]:
    """Every issue raised in any round, with how it was resolved (by a later round's results)."""
    out: list[Issue] = []
    for i, r in enumerate(rounds):
        later = rounds[i + 1:]
        label = f"{r.number}"
        if r.validation and isinstance(r.validation.get("checks"), list):
            for c in r.validation["checks"]:
                for f in c.get("failures", []):
                    key = (c.get("name"), f.get("code"), f.get("location"))
                    resolution = "Open"
                    for s in later:
                        if s.validation and isinstance(s.validation.get("checks"), list):
                            keys = {(c2.get("name"), f2.get("code"), f2.get("location"))
                                    for c2 in s.validation["checks"] for f2 in c2.get("failures", [])}
                            if key not in keys:
                                resolution = f"Fixed in round {s.number}: the validator no longer reports it"
                                break
                    out.append(Issue(label, "validator", f"{c.get('name')}: {f.get('code')}", f.get("location", ""),
                                     f.get("message", ""), "blocking", resolution))
        if r.check and isinstance(r.check.get("issues"), list):
            for issue in r.check["issues"]:
                if issue.get("status") == "resolved":
                    continue  # reported as the resolution of the round that raised it
                resolution = "Open"
                for s in later:
                    if not (s.check and isinstance(s.check.get("issues"), list)):
                        continue
                    same = [x for x in s.check["issues"] if x.get("id") == issue.get("id")]
                    if not same:
                        resolution = f"Not raised again by the checker in round {s.number}"
                        break
                    if same[0].get("status") == "resolved":
                        how = same[0].get("resolution")
                        resolution = f"Resolved in round {s.number}" + (f": {how}" if how else "")
                        break
                out.append(Issue(label, "checker", str(issue.get("id", "")), str(issue.get("target", "")),
                                 f"{issue.get('description', '')} Suggested fix: {issue.get('suggested_fix', '')}",
                                 str(issue.get("severity", "")), resolution))
    return out


def _models(rounds: list[Round], check: dict[str, Any] | None, draft: dict[str, Any] | None) -> list[dict[str, Any]]:
    """One entry per (role, model, date): ``round`` where it first appears, ``rounds`` every round it was used in,
    and ``deciding`` for the checker of the result the report was decided on (``check``)."""
    seen: dict[tuple[str, str, str | None], dict[str, Any]] = {}
    for r in rounds:
        for role, doc in (("drafter", r.draft), ("checker", r.check)):
            m = _model_of(role, doc)
            if m:
                entry = seen.setdefault((role, m["model"], m["date"]), {**m, "round": r.number, "rounds": []})
                entry["rounds"].append(r.number)
    for role, doc in (("drafter", draft), ("checker", check)):
        m = _model_of(role, doc)
        if m:
            seen.setdefault((role, m["model"], m["date"]), {**m, "round": None, "rounds": []})
    deciding = _model_of("checker", check)
    for (role, model, date), entry in seen.items():
        entry["deciding"] = bool(deciding) and role == "checker" and (model, date) == (deciding["model"],
                                                                                    deciding["date"])
    return list(seen.values())


def _model_of(role: str, doc: dict[str, Any] | None) -> dict[str, Any] | None:
    if not doc:
        return None
    meta = doc.get("checker") if role == "checker" else doc.get("x_drafter")
    if isinstance(meta, dict) and isinstance(meta.get("model"), str):
        return {"role": role, "model": meta["model"], "date": meta.get("date") if isinstance(meta.get("date"), str)
                else None}
    return None


def evaluate(original: Path, prepared: Path, validation: Path | None = None, check: Path | None = None,
             history: Path | None = None, draft: Path | None = None, created: str | None = None,
             epubcheck: V.EpubcheckRunner | None = None) -> Evaluation:
    created = created or V.timestamp()
    rounds = load_history(history)
    validation = validation or _latest(rounds, "validation.json")
    check = check or _latest(rounds, "check.json")
    draft = draft or _latest(rounds, "draft.json") or (history / "draft.json" if history is not None and
                                                        (history / "draft.json").is_file() else None)

    original_bytes = _read(original)
    prepared_bytes = _read(prepared)
    draft_raw = _read(draft)
    check_raw = _read(check)
    original_sha = V.sha256(original_bytes) if original_bytes is not None else None
    prepared_sha = V.sha256(prepared_bytes) if prepared_bytes is not None else None

    rerun = V.validate_bytes(original_bytes, original.name, prepared_bytes, prepared.name, draft_raw,
                             draft.name if draft is not None else None, epubcheck, created,
                             original_after=lambda: _read(original))
    stored_ok, stored_problems = _stored_validation(_read(validation), original_sha, prepared_sha, rerun.passed)

    verification: Verification | None = None
    if check_raw is not None and original_bytes is not None and prepared_bytes is not None:
        verification = verify_check_bytes(original_bytes, prepared_bytes, check_raw, check.name if check else
                                          "check.json", rerun.prepared)
    check_data = verification.check if verification else None
    draft_data = None
    if draft_raw is not None:
        try:
            draft_data = json.loads(draft_raw.decode("utf-8"))
        except (UnicodeDecodeError, ValueError):
            draft_data = None

    reasons: list[str] = []
    if not rerun.passed:
        failed = [c["name"] for c in rerun.result["checks"] if c["status"] != "pass"]
        reasons.append(f"The validator, run again on these files, did not pass: {', '.join(failed)}.")
    reasons += stored_problems
    if check_raw is None:
        reasons.append("No checker result was given, so no checker has passed this file.")
    elif verification is None:
        reasons.append("The checker result could not be verified because a file could not be read.")
    elif not verification.passed:
        codes = {f.code for f in verification.failures}
        if codes & CHECKER_FILE_CODES:
            reasons.append("The checker result was not made against exactly these files (it is stale or for "
                           "another book), so it is no evidence for this file.")
        if codes - CHECKER_FILE_CODES - CHECKER_VERDICT_CODES:
            reasons.append("The checker result is incomplete or inconsistent (see the issues below).")
        if codes & CHECKER_VERDICT_CODES:
            reasons.append("The checker did not pass the draft.")
    ready = (rerun.passed and stored_ok and verification is not None and verification.passed
             and original_sha is not None and prepared_sha is not None)

    # Issues: history, then what is open now.
    issues = _history_issues(rounds)
    final_round = str(rounds[-1].number) if rounds else "1"
    open_keys = {(i.raised_by, i.code, i.where) for i in issues if i.open}
    current: list[Issue] = []
    for c in rerun.result["checks"]:
        for f in c["failures"]:
            current.append(Issue(final_round, "validator", f"{c['name']}: {f['code']}", f["location"], f["message"]))
        if c["status"] in ("not-run", "incomplete") and not c["failures"]:
            current.append(Issue(final_round, "validator", f"{c['name']}: {c['status']}", "",
                                 "; ".join(c["details"]) or f"the check is {c['status']}"))
    for problem in stored_problems:
        current.append(Issue(final_round, "report", "validator-record", validation.name if validation else "",
                             problem))
    if check_raw is None:
        current.append(Issue(final_round, "report", "checker-missing", "", "No checker result was given."))
    if verification is not None:
        for f in verification.failures:
            current.append(Issue(final_round, "checker" if f.code == "checker-verdict-fail" else "report",
                                 f"verify-check: {f.code}", f.location, f.message))
        if check_data:
            for issue in check_data["issues"]:
                if issue["status"] == "open":
                    current.append(Issue(final_round, "checker", issue["id"], issue["target"],
                                         f"{issue['description']} Suggested fix: {issue['suggested_fix']}",
                                         issue["severity"]))
    # A current problem already in the history as open is the same issue: keep the history row only. A
    # history issue the final results no longer show was fixed after the last recorded round.
    current_keys = {(i.raised_by, i.code, i.where) for i in current}
    open_checker_ids = {i["id"] for i in (check_data or {}).get("issues", []) if i["status"] == "open"}
    for item in issues:
        if not item.open:
            continue
        if item.raised_by == "validator" and (item.raised_by, item.code, item.where) not in current_keys:
            item.resolution = "Fixed: the validator run for this report no longer reports it"
        elif item.raised_by == "checker" and check_data is not None and item.code not in open_checker_ids:
            item.resolution = "Not raised again by the latest checker result"
    for item in current:
        if (item.raised_by, item.code, item.where) not in open_keys:
            issues.append(item)

    name = report_name(prepared)
    checkpoints = rerun.checkpoints or {}
    book = checkpoints.get("book", {})
    instructions = book.get("instructions_version") or (check_data or {}).get("instructions_version")
    checks = [{"name": c["name"], "title": c["title"], "status": c["status"], "source": "validator"}
              for c in rerun.result["checks"]]
    checks.append({"name": "validator-record", "title": "The latest stored validator result passed, for exactly "
                                                       "this prepared file and original",
                   "status": "pass" if stored_ok else "fail", "source": "report"})
    for n, t, s in _checker_rows(verification):
        checks.append({"name": n, "title": t, "status": s, "source": "checker"})
    report = {
        "format": FORMAT,
        "version": VERSION,
        "status": READY if ready else NOT_READY,
        "ready": ready,
        "created": created,
        "book": {"title": book.get("title"), "language": book.get("language"), "instructions_version": instructions},
        "original": {"file": original.name, "sha256": original_sha},
        "prepared": {"file": prepared.name, "sha256": prepared_sha},
        "delivered": f"{name}.prepared.epub" if ready else None,
        "rounds": len(rounds) if rounds else 1,
        "history": bool(rounds),
        "models": _models(rounds, check_data, draft_data),
        "tools": {"name": TOOL_NAME, "version": TOOL_VERSION, "word_rules_version": WORD_RULES_VERSION,
                  "epubcheck_version": rerun.result["tool"]["epubcheck_version"]},
        "reasons": reasons,
        "checks": checks,
        "issues": [i.as_json() for i in issues],
        "validation": rerun.result,
        "check_verification": (verification_json(verification, original.name, original_sha, prepared.name,
                                                 prepared_sha, check.name if check else "check.json", created)
                               if verification is not None else None),
    }
    return Evaluation(ready, report, render_markdown(report), prepared_bytes, name, reasons)


# --------------------------------------------------------------------------
# Markdown


def _rounds_text(numbers: list[int]) -> str:
    """'round 2', 'rounds 1 and 2', 'rounds 1, 2 and 3'."""
    words = [str(n) for n in numbers]
    if len(words) == 1:
        return f"round {words[0]}"
    return f"rounds {', '.join(words[:-1])} and {words[-1]}"


def _cell(text: Any) -> str:
    return " ".join(str(text if text is not None else "").split()).replace("|", "\\|") or "-"


def render_markdown(report: dict[str, Any]) -> str:
    ready = report["ready"]
    prepared = report["prepared"]["file"]
    title = report["book"]["title"] or prepared
    lines = []
    if ready:
        lines.append(f"READY: {report['delivered']} has passed every check and may be loaded onto the device.")
    else:
        n = sum(1 for i in report["issues"] if i["open"])
        lines.append(f"NOT READY: {prepared} must not be loaded onto the device yet; "
                     f"{n} issue{' is' if n == 1 else 's are'} open.")
    lines += ["", f"# Validation report: {title}", ""]
    if ready:
        lines.append(f"The validator and the checker both passed this exact prepared file, and it was made from the "
                     f"original named below. The prepared book is in this folder as `{report['delivered']}`; the "
                     "original EPUB stays a separate, unchanged file.")
    else:
        lines.append("This book is not ready, for these reasons:")
        lines.append("")
        lines += [f"- {r}" for r in report["reasons"]]
        lines.append("")
        lines.append("Fix the open issues in the table below, prepare the book again and make a new report.")
    models = report["models"]
    checkers = [m for m in models if m["role"] == "checker"]
    drafters = [m for m in models if m["role"] == "drafter"]

    def name(m: dict[str, Any]) -> str:
        return m["model"] + (f" ({m['date']})" if m["date"] else "")

    def used(m: dict[str, Any]) -> list[int]:
        return m.get("rounds") or ([m["round"]] if m.get("round") else [])

    def model_text(ms: list[dict[str, Any]]) -> str:
        if not ms:
            return "not recorded"
        return "; ".join(name(m) + (f" in {_rounds_text(used(m))}" if used(m) else "") for m in ms)

    def checker_text(ms: list[dict[str, Any]]) -> str:
        deciding = next((m for m in ms if m.get("deciding")), None)
        if deciding is None:
            return model_text(ms)
        last = max(used(deciding)) if used(deciding) else None
        text = name(deciding) + (f", in round {last}, the round that decided the result" if last else "")
        rest = [n for n in used(deciding) if n != last]
        earlier = [f"the same model in {_rounds_text(rest)}"] if rest else []
        earlier += [name(m) + (f" in {_rounds_text(used(m))}" if used(m) else "") for m in ms if m is not deciding]
        return text + (f"; earlier rounds: {'; '.join(earlier)}" if earlier else "")

    tools = report["tools"]
    lines += [
        "",
        "## About this book",
        "",
        f"- Original file: `{report['original']['file']}`, SHA-256 `{report['original']['sha256']}`.",
        f"- Prepared file: `{prepared}`, SHA-256 `{report['prepared']['sha256']}`.",
        f"- Preparation instructions version: {report['book']['instructions_version'] or 'unknown'}.",
        f"- Text in no section: {_excluded_text(report)}",
        f"- Rounds of drafting and checking: {report['rounds']}"
        + ("." if report["history"] else " (no round history was given; the files named here are the only round)."),
        f"- Drafter model: {model_text(drafters)}.",
        f"- Checker model: {checker_text(checkers)}.",
        f"- Tools: {tools['name']} {tools['version']}, word rules version {tools['word_rules_version']}, "
        f"EPUBCheck {tools['epubcheck_version'] or 'not run'}.",
        f"- Report made: {report['created']}.",
        "",
        "## Checks",
        "",
        "Each check ran again for this report; a stored result is only a record of what happened before.",
        "",
        "| Check | What it confirms | Status |",
        "| --- | --- | --- |",
    ]
    for c in report["checks"]:
        lines.append(f"| {_cell(c['name'])} | {_cell(c['title'])} | {STATUS_WORDS.get(c['status'], c['status'])} |")
    # On a READY book every open issue is a minor one on a prompt that passed (a blocking one fails the checker).
    minor = [i for i in report["issues"] if ready and i["open"] and i["raised_by"] == "checker"]
    issues = [i for i in report["issues"] if i not in minor]
    lines += ["", "## Issues", ""]
    if not report["issues"]:
        lines.append("No issues were raised.")
    elif not issues:
        lines.append("No issues were raised apart from the minor ones below.")
    else:
        lines += ["| Round | Raised by | Issue | Where | Description | How it was resolved |",
                  "| --- | --- | --- | --- | --- | --- |"]
        for i in issues:
            severity = "" if i["severity"] in ("blocking", "") else f" ({i['severity']})"
            lines.append(f"| {_cell(i['round'])} | {_cell(i['raised_by'])} | {_cell(i['code'] + severity)} | "
                         f"{_cell(i['where'])} | {_cell(i['description'])} | {_cell(i['resolution'])} |")
    if minor:
        lines += ["", "## Minor issues: these do not block delivery", "",
                  "The checker passed every prompt and section of this book, but noted these points of style or "
                  "wording. The book is READY with them. They can be addressed in a later re-preparation of the book "
                  "(see \"READY with minor issues\" in docs/PREPARING_BOOKS.md).", "",
                  "| Round | Issue | Where | Description |", "| --- | --- | --- | --- |"]
        for i in minor:
            severity = f" ({i['severity']})" if i["severity"] else ""
            lines.append(f"| {_cell(i['round'])} | {_cell(i['code'] + severity)} | "
                         f"{_cell(i['where'])} | {_cell(i['description'])} |")
    lines += ["", f"Every detail, including each check's findings, is in `{report_name_from(report)}"
                  ".validation-report.json`.", ""]
    return "\n".join(lines)


def _excluded_text(report: dict[str, Any]) -> str:
    validation = report["validation"]
    coverage = next((c for c in validation["checks"] if c["name"] == "coverage"), None)
    if coverage is None or validation.get("coverage_mode") is None:
        return "not known (the coverage check did not run)."
    excluded = [d for d in coverage["details"] if "excluded" in d and ":" in d and not d.startswith(("exclusions",
                                                                                                    "no draft"))]
    source = ("as listed in the draft" if validation["coverage_mode"] == "draft" else
              "no draft was given, so every document without a section counts as excluded")
    return (f"{'; '.join(excluded)} ({source})." if excluded else f"none ({source}).") + \
        " Footnote and endnote bodies need no section."


def report_name_from(report: dict[str, Any]) -> str:
    return report_name(Path(report["prepared"]["file"]))


# --------------------------------------------------------------------------
# Writing


def _write_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=".prep-report-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        umask = os.umask(0)
        os.umask(umask)
        os.chmod(tmp, 0o666 & ~umask)
        os.replace(tmp, path)
    except BaseException:
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise


def write_report(ev: Evaluation, out_dir: Path, original: Path, prepared: Path) -> list[Path]:
    """Write the two report files and, when ready, the prepared EPUB (from the bytes that were checked)."""
    md = out_dir / f"{ev.name}.validation-report.md"
    js = out_dir / f"{ev.name}.validation-report.json"
    epub = out_dir / f"{ev.name}.prepared.epub"

    def same(a: Path, b: Path) -> bool:
        return a.exists() and b.exists() and os.path.samefile(a, b)

    for target in (md, js, epub):
        if same(target, original) or (target != epub and same(target, prepared)):
            raise ReportUsageError(f"{target} would overwrite an input file")
    written = []
    if ev.ready:
        assert ev.prepared_bytes is not None
        if not same(epub, prepared):
            _write_atomic(epub, ev.prepared_bytes)
        if V.sha256(epub.read_bytes()) != ev.report["prepared"]["sha256"]:
            raise RuntimeError(f"{epub} does not hold the bytes that were checked")
        written.append(epub)
    _write_atomic(md, ev.markdown.encode("utf-8"))
    _write_atomic(js, (json.dumps(ev.report, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))
    return written + [md, js]


_validator: Any = None


def validate_report_schema(report: Any) -> list[str]:
    global _validator
    from . import checkpoints as cp
    if _validator is None:
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        cp.Draft202012Validator.check_schema(schema)
        _validator = cp.StrictValidator(schema)
    return [f"{cp.json_pointer(e.absolute_path) or '/'}: {e.message}" for e in _validator.iter_errors(report)]

