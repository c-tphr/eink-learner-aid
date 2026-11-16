"""Tests for ``prep report`` (tools/prep/report.py): the readiness rule, the round history and the report files."""

from __future__ import annotations

import copy
import itertools
import json
import shutil
from pathlib import Path

import pytest

from tools.prep import report as R
from tools.prep import validate as V
from tools.prep.tests.prepkit import (CHECK, DRAFT, NEGATIVE, ORIGINAL, ORIGINAL_BYTES, PREPARED, PREPARED_BYTES, TS,
                                      ok_epubcheck, run_prep, sha, unavailable_epubcheck)

PASSING_CHECK = json.loads(CHECK.read_text(encoding="utf-8"))
FAILED_CHECK = json.loads((NEGATIVE / "checker-failed.check.json").read_text(encoding="utf-8"))
BROKEN = NEGATIVE / "word-count-off-by-one.prepared.epub"


def write_json(path: Path, data) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    return path


def stored_validation(prepared: Path, draft: bytes | None = None) -> dict:
    return V.validate_bytes(ORIGINAL_BYTES, ORIGINAL.name, prepared.read_bytes(), prepared.name, draft,
                            "draft.json" if draft else None, ok_epubcheck, TS).result


def check_for(prepared: Path, passing: bool = True, **changes) -> dict:
    data = copy.deepcopy(PASSING_CHECK if passing else FAILED_CHECK)
    data["prepared_sha256"] = sha(prepared.read_bytes())
    data.update(changes)
    return data


# --------------------------------------------------------------------------
# The readiness rule as a truth table


CASES = list(itertools.product(["pass", "fail"], ["pass", "fail"], ["match", "stale"], ["available", "unavailable"]))


@pytest.mark.parametrize("validator, checker, hashes, epubcheck", CASES, ids=["-".join(c) for c in CASES])
def test_readiness_truth_table(tmp_path, validator, checker, hashes, epubcheck):
    prepared = tmp_path / "book.prepared.epub"
    shutil.copyfile(PREPARED if validator == "pass" else BROKEN, prepared)
    validation = stored_validation(prepared)
    check = check_for(prepared, checker == "pass")
    if hashes == "stale":  # both results were made against an earlier prepared file
        validation["prepared"]["sha256"] = sha(b"an earlier round's prepared file")
        check["prepared_sha256"] = sha(b"an earlier round's prepared file")
    ev = R.evaluate(ORIGINAL, prepared, write_json(tmp_path / "validation.json", validation),
                    write_json(tmp_path / "check.json", check), created=TS,
                    epubcheck=ok_epubcheck if epubcheck == "available" else unavailable_epubcheck)
    expected = (validator, checker, hashes, epubcheck) == ("pass", "pass", "match", "available")
    assert ev.ready is expected
    assert ev.markdown.splitlines()[0].startswith("READY:" if expected else "NOT READY:")
    assert ev.report["status"] == ("READY" if expected else "NOT READY")
    assert (ev.reasons == []) is expected
    if not expected:
        assert any(i["open"] for i in ev.report["issues"])
    assert R.validate_report_schema(ev.report) == []


def test_original_must_match(tmp_path):
    other = tmp_path / "other.epub"
    other.write_bytes(ORIGINAL_BYTES + b"\0")  # not the file the book was prepared from
    ev = R.evaluate(other, PREPARED, write_json(tmp_path / "v.json", stored_validation(PREPARED)),
                    write_json(tmp_path / "c.json", check_for(PREPARED)), created=TS, epubcheck=ok_epubcheck)
    assert not ev.ready
    codes = {i["code"] for i in ev.report["issues"]}
    assert "verify-check: original-mismatch" in codes


def test_no_checker_result_is_not_ready(tmp_path):
    ev = R.evaluate(ORIGINAL, PREPARED, write_json(tmp_path / "v.json", stored_validation(PREPARED)), None,
                    created=TS, epubcheck=ok_epubcheck)
    assert not ev.ready
    assert "No checker result was given, so no checker has passed this file." in ev.reasons
    assert [r["status"] for r in ev.report["checks"] if r["source"] == "checker"] == ["not-run"] * 3


def test_no_validator_result_is_not_ready(tmp_path):
    ev = R.evaluate(ORIGINAL, PREPARED, None, write_json(tmp_path / "c.json", check_for(PREPARED)), created=TS,
                    epubcheck=ok_epubcheck)
    assert not ev.ready
    assert any("No validator result" in r for r in ev.reasons)


# --------------------------------------------------------------------------
# Stored results are evidence, not authority


def test_editing_stored_results_cannot_make_a_broken_book_ready(tmp_path):
    prepared = tmp_path / "book.prepared.epub"
    shutil.copyfile(BROKEN, prepared)
    forged = stored_validation(PREPARED)  # a passing result, re-labelled as made against the broken file
    forged["prepared"] = {"file": prepared.name, "sha256": sha(prepared.read_bytes())}
    ev = R.evaluate(ORIGINAL, prepared, write_json(tmp_path / "v.json", forged),
                    write_json(tmp_path / "c.json", check_for(prepared)), created=TS, epubcheck=ok_epubcheck)
    assert not ev.ready
    assert any("running the validator again on these files fails" in r for r in ev.reasons)
    assert any(i["code"] == "word-counts: word-count" for i in ev.report["issues"])


def test_editing_the_checker_verdict_cannot_make_a_book_ready(tmp_path):
    forged = check_for(PREPARED, passing=False)
    forged["verdict"] = "pass"  # prompt c0002-p2 still fails and issue I1 is still open
    ev = R.evaluate(ORIGINAL, PREPARED, write_json(tmp_path / "v.json", stored_validation(PREPARED)),
                    write_json(tmp_path / "c.json", forged), created=TS, epubcheck=ok_epubcheck)
    assert not ev.ready
    assert any(i["code"] == "verify-check: verdict-unjustified" for i in ev.report["issues"])


def test_a_stored_result_for_another_file_is_not_evidence(tmp_path):
    stale = stored_validation(BROKEN)  # failed, and for another file
    ev = R.evaluate(ORIGINAL, PREPARED, write_json(tmp_path / "v.json", stale),
                    write_json(tmp_path / "c.json", check_for(PREPARED)), created=TS, epubcheck=ok_epubcheck)
    assert not ev.ready
    assert any("made against another prepared file" in r for r in ev.reasons)


# --------------------------------------------------------------------------
# Round history


def _round(work: Path, n: int, prepared: Path, check: dict | None, draft: dict) -> None:
    d = work / "rounds" / f"{n:02d}"
    write_json(d / "draft.json", draft)
    write_json(d / "validation.json", stored_validation(prepared, json.dumps(draft).encode()))
    if check is not None:
        write_json(d / "check.json", check)


def _draft(model: str = "drafter-model-a") -> dict:
    draft = json.loads(DRAFT.read_text(encoding="utf-8"))
    draft["x_drafter"] = {"model": model, "date": "2026-01-01"}
    return draft


def test_history_two_rounds_ready(tmp_path):
    work = tmp_path / "work"
    broken = NEGATIVE / "broken-link.prepared.epub"
    _round(work, 1, broken, check_for(broken, passing=False), _draft())
    fixed = check_for(PREPARED)
    fixed["issues"] = [{"id": "I1", "severity": "blocking", "target": "c0002-p2", "description": "Unsupported part.",
                        "suggested_fix": "Trim it.", "status": "resolved", "resolution": "The answer was trimmed."}]
    fixed["checker"] = {"model": "checker-model-b", "date": "2026-01-02"}
    _round(work, 2, PREPARED, fixed, _draft())
    ev = R.evaluate(ORIGINAL, PREPARED, history=work, created=TS, epubcheck=ok_epubcheck)
    assert ev.ready, ev.reasons
    r = ev.report
    assert r["rounds"] == 2 and r["history"] is True
    assert r["validation"]["coverage_mode"] == "draft"  # the latest round's draft supplied the exclusions
    rows = {(i["round"], i["raised_by"], i["code"]): i for i in r["issues"]}
    assert rows[("1", "validator", "anchors: anchor-missing")]["resolution"].startswith("Fixed in round 2")
    assert rows[("1", "checker", "I1")]["resolution"] == "Resolved in round 2: The answer was trimmed."
    assert not any(i["open"] for i in r["issues"])
    assert {(m["role"], m["model"]) for m in r["models"]} == {
        ("drafter", "drafter-model-a"), ("checker", "fixture-sample"), ("checker", "checker-model-b")}
    assert "| 1 | checker | I1 |" in ev.markdown
    assert "Rounds of drafting and checking: 2." in ev.markdown
    assert "- Drafter model: drafter-model-a (2026-01-01) in rounds 1 and 2." in ev.markdown
    assert ("- Checker model: checker-model-b (2026-01-02), in round 2, the round that decided the result; earlier "
            "rounds: fixture-sample (2026-01-01) in round 1.") in ev.markdown
    assert [m["deciding"] for m in r["models"] if m["role"] == "checker"] == [False, True]
    assert R.validate_report_schema(r) == []


def test_checker_model_of_the_deciding_round_when_the_same_model_checked_earlier(tmp_path):
    work = tmp_path / "work"
    broken = NEGATIVE / "broken-link.prepared.epub"
    _round(work, 1, broken, check_for(broken, passing=False), _draft())
    _round(work, 2, PREPARED, check_for(PREPARED), _draft())
    ev = R.evaluate(ORIGINAL, PREPARED, history=work, created=TS, epubcheck=ok_epubcheck)
    assert ("- Checker model: fixture-sample (2026-01-01), in round 2, the round that decided the result; earlier "
            "rounds: the same model in round 1.") in ev.markdown


def test_ready_with_minor_issues_lists_them_apart(tmp_path):
    work = tmp_path / "work"
    check = check_for(PREPARED)
    check["issues"] = [{"id": "R1-I1", "severity": "minor", "target": "c0002-p1", "description": "Two questions.",
                        "suggested_fix": "Ask one.", "status": "open"}]
    _round(work, 1, PREPARED, check, _draft())
    ev = R.evaluate(ORIGINAL, PREPARED, history=work, created=TS, epubcheck=ok_epubcheck)
    assert ev.ready, ev.reasons
    md = ev.markdown
    assert "No issues were raised apart from the minor ones below." in md
    assert "## Minor issues: these do not block delivery" in md and "do not block" in md
    assert "| 1 | R1-I1 (minor) | c0002-p1 | Two questions. Suggested fix: Ask one. |" in md
    assert "| Open |" not in md
    assert [i["open"] for i in ev.report["issues"]] == [True]  # the JSON still records it as open


def test_committed_real_book_still_reproduces_ready(tmp_path):
    """Prepared under instructions 1.0 and tools 1.0.0: the current tools still deliver it unchanged."""
    books = ORIGINAL.parent
    original, prepared = books / "paine-essays.epub", books / "paine-essays.prepared.epub"
    history = books.parents[1] / "docs" / "evidence" / "p5"
    ev = R.evaluate(original, prepared, history=history, created=TS, epubcheck=ok_epubcheck)
    assert ev.ready, ev.reasons
    written = R.write_report(ev, tmp_path, original, prepared)
    assert (tmp_path / "paine-essays.prepared.epub").read_bytes() == prepared.read_bytes()
    assert len(written) == 3 and ev.report["book"]["instructions_version"] == "1.0"
    md = ev.markdown
    assert "text/titlepage.xhtml: a range with no words excluded by the draft" in md and "0--1" not in md
    assert "the round that decided the result" in md
    assert md.count("| 2 | R2-I") == 4 and "## Minor issues: these do not block delivery" in md
    exclusions = next(c for c in ev.report["validation"]["checks"] if c["name"] == "exclusions")
    assert exclusions["status"] == "pass"


def test_history_three_failed_rounds_not_ready_with_open_issues(tmp_path):
    work = tmp_path / "work"
    broken = NEGATIVE / "broken-link.prepared.epub"
    for n in (1, 2, 3):
        _round(work, n, broken, check_for(broken, passing=False), _draft())
    ev = R.evaluate(ORIGINAL, broken, history=work, created=TS, epubcheck=ok_epubcheck)
    assert not ev.ready
    assert ev.report["rounds"] == 3
    open_issues = [i for i in ev.report["issues"] if i["open"]]
    assert {i["code"] for i in open_issues} >= {"anchors: anchor-missing", "I1"}
    assert ev.markdown.startswith("NOT READY:")
    assert "| 3 | validator | anchors: anchor-missing |" in ev.markdown


# --------------------------------------------------------------------------
# Files, determinism, command line


def test_report_is_deterministic(tmp_path):
    v = write_json(tmp_path / "v.json", stored_validation(PREPARED))
    outs = []
    for name in ("a", "b"):
        ev = R.evaluate(ORIGINAL, PREPARED, v, CHECK, created=TS, epubcheck=ok_epubcheck)
        written = R.write_report(ev, tmp_path / name, ORIGINAL, PREPARED)
        outs.append({p.name: p.read_bytes() for p in written})
    assert outs[0] == outs[1]
    assert set(outs[0]) == {"fixture.prepared.epub", "fixture.validation-report.md", "fixture.validation-report.json"}
    assert outs[0]["fixture.prepared.epub"] == PREPARED_BYTES


def test_not_ready_report_delivers_no_epub(tmp_path):
    ev = R.evaluate(ORIGINAL, PREPARED, None, None, created=TS, epubcheck=ok_epubcheck)
    written = R.write_report(ev, tmp_path, ORIGINAL, PREPARED)
    assert sorted(p.name for p in written) == ["fixture.validation-report.json", "fixture.validation-report.md"]
    assert not (tmp_path / "fixture.prepared.epub").exists()


def test_markdown_has_one_table_of_checks_and_one_of_issues():
    ev = R.evaluate(ORIGINAL, PREPARED, None, None, created=TS, epubcheck=ok_epubcheck)
    md = ev.markdown
    separators = [line for line in md.splitlines() if line.startswith("| --- |")]
    assert separators == ["| --- | --- | --- |", "| --- | --- | --- | --- | --- | --- |"]
    for name, _ in V.CHECKS:
        assert f"| {name} |" in md
    assert "Report made: 2026-01-01T00:00:00Z." in md


def test_cli_ready_and_not_ready(tmp_path):
    v = tmp_path / "validation.json"
    assert run_prep("validate", ORIGINAL, PREPARED, "--json", v, "--timestamp", TS).returncode == 0
    result = run_prep("report", "--original", ORIGINAL, "--prepared", PREPARED, "--validation", v,
                      "-o", tmp_path / "none", "--timestamp", TS)
    assert result.returncode == 1
    assert result.stdout.startswith("NOT READY:")
    md = (tmp_path / "none" / "fixture.validation-report.md").read_text(encoding="utf-8")
    assert md.splitlines()[0].startswith("NOT READY:")
    stale = tmp_path / "stale.json"
    stale.write_bytes((NEGATIVE / "stale-hash.check.json").read_bytes())
    result = run_prep("report", "--original", ORIGINAL, "--prepared", PREPARED, "--validation", v, "--check", stale,
                      "-o", tmp_path / "stale", "--timestamp", TS)
    assert result.returncode == 1 and "stale" in result.stderr
    result = run_prep("report", "--original", ORIGINAL, "--prepared", PREPARED, "--validation", v, "--check", CHECK,
                      "-o", tmp_path / "ready", "--timestamp", TS)
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.startswith("READY:")
    report = json.loads((tmp_path / "ready" / "fixture.validation-report.json").read_text(encoding="utf-8"))
    assert report["ready"] and R.validate_report_schema(report) == []
    assert (tmp_path / "ready" / "fixture.prepared.epub").read_bytes() == PREPARED_BYTES


def test_cli_usage_errors(tmp_path):
    assert run_prep("report", "--original", ORIGINAL, "-o", tmp_path).returncode == 2
    assert run_prep("report", "--original", ORIGINAL, "--prepared", PREPARED, "--history", tmp_path / "missing",
                    "-o", tmp_path).returncode == 2
