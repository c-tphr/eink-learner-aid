"""Tests for ``prep verify-check`` (tools/prep/verify_check.py) and schema/check.schema.json."""

from __future__ import annotations

import copy
import json

import pytest

from tools.prep import verify_check as VC
from tools.prep.tests.prepkit import (BOOKS, CHECK, MANIFEST, NEGATIVE, ORIGINAL, ORIGINAL_BYTES, PREPARED,
                                      PREPARED_BYTES, TS, run_prep, sha)

PASSING = json.loads(CHECK.read_text(encoding="utf-8"))


def verify(check: dict | bytes, prepared: bytes = PREPARED_BYTES, original: bytes = ORIGINAL_BYTES) -> set[str]:
    raw = check if isinstance(check, bytes) else json.dumps(check).encode("utf-8")
    return {f.code for f in VC.verify_check_bytes(original, prepared, raw).failures}


def changed(mutate) -> dict:
    data = copy.deepcopy(PASSING)
    mutate(data)
    return data


def prompt(data: dict, pid: str) -> dict:
    return next(p for p in data["prompts"] if p["id"] == pid)


def test_passing_sample_passes():
    v = VC.verify_check_bytes(ORIGINAL_BYTES, PREPARED_BYTES, CHECK.read_bytes())
    assert v.passed and v.consistent and v.failures == []
    assert v.check["checker"] == {"model": "fixture-sample", "date": "2026-01-01"}


@pytest.mark.parametrize("entry", MANIFEST["checks"], ids=lambda e: e["file"])
def test_negative_checks_fail_with_exactly_their_codes(entry):
    prepared = (NEGATIVE / entry["prepared"]).read_bytes()
    assert verify((NEGATIVE / entry["file"]).read_bytes(), prepared) == set(entry["codes"])


def test_failed_checker_is_consistent_but_not_passed():
    v = VC.verify_check_bytes(ORIGINAL_BYTES, PREPARED_BYTES, (NEGATIVE / "checker-failed.check.json").read_bytes())
    assert v.consistent and not v.passed


@pytest.mark.parametrize("mutate, codes", [
    (lambda d: d.__setitem__("original_sha256", sha(b"other")), {"original-mismatch"}),
    (lambda d: d.__setitem__("instructions_version", "2.0"), {"instructions-version-mismatch"}),
    (lambda d: d["prompts"].append(dict(d["prompts"][0], id="c0009-p1")), {"unknown-prompt"}),
    (lambda d: d["prompts"].append(dict(d["prompts"][0])), {"duplicate-prompt"}),
    (lambda d: d["sections"].pop(), {"missing-section"}),
    (lambda d: d["sections"].append(dict(d["sections"][0], id="s0099")), {"unknown-section"}),
    (lambda d: d["sections"].append(dict(d["sections"][0])), {"duplicate-section"}),
    (lambda d: prompt(d, "c0002-p3").__setitem__("does_not_give_away_answer", False), {"pass-with-failed-criterion"}),
    (lambda d: prompt(d, "c0002-p3").__setitem__("citations", ["d03-p099"]), {"unknown-paragraph"}),
    (lambda d: prompt(d, "c0001-p1").__setitem__("citations", ["d03-p009"]), {"citation-outside-section"}),
    (lambda d: d["sections"][1].__setitem__("natural_boundary", False), {"verdict-unjustified"}),
    (lambda d: d["issues"].append({"id": "I1", "severity": "blocking", "target": "c0002-p2", "description": "x",
                                   "suggested_fix": "y", "status": "open"}), {"verdict-unjustified"}),
    (lambda d: d["issues"].append({"id": "I1", "severity": "minor", "target": "nowhere", "description": "x",
                                   "suggested_fix": "y", "status": "open"}), {"unknown-issue-target"}),
    (lambda d: d["issues"].extend([{"id": "I1", "severity": "minor", "target": "book", "description": "x",
                                    "suggested_fix": "y", "status": "open"}] * 2), {"duplicate-issue"}),
    (lambda d: d.__setitem__("verdict", "fail"), {"checker-verdict-fail"}),
])
def test_script_rules(mutate, codes):
    assert verify(changed(mutate)) == codes


def test_open_minor_issue_does_not_block():
    data = changed(lambda d: d["issues"].append({"id": "I1", "severity": "minor", "target": "c0002-p2",
                                                 "description": "Could be shorter.", "suggested_fix": "Shorten.",
                                                 "status": "open"}))
    assert verify(data) == set()


@pytest.mark.parametrize("mutate", [
    lambda d: d.pop("checker"),
    lambda d: d.__setitem__("verdict", "maybe"),
    lambda d: prompt(d, "c0001-p1").__setitem__("citations", ["c1-p006"]),  # a source XHTML id, not a reference
    lambda d: prompt(d, "c0001-p1").__setitem__("citations", ["d02-n001"]),  # a footnote body
    lambda d: prompt(d, "c0001-p1").__setitem__("extra", True),
    lambda d: d["checker"].__setitem__("date", "18 Nov 2026"),
    lambda d: d.__setitem__("prepared_sha256", "ABC"),
])
def test_schema_rejects(mutate):
    assert verify(changed(mutate)) == {"check-schema"}


def test_x_members_are_allowed():
    assert verify(changed(lambda d: (d.__setitem__("x_note", 1), prompt(d, "c0001-p1").__setitem__("x_a", 2)))) == set()


def test_unreadable_check():
    assert verify(b"{") == {"check-unreadable"}
    assert verify(b'{"a": 1, "a": 2}') == {"check-unreadable"}


def test_citations_of_a_section_without_anchors_cannot_be_confirmed():
    broken = (NEGATIVE / "broken-link.prepared.epub").read_bytes()
    data = changed(lambda d: d.__setitem__("prepared_sha256", sha(broken)))
    assert verify(data, broken) == {"section-unresolved"}


def test_cli(tmp_path):
    result = run_prep("verify-check", ORIGINAL, PREPARED, CHECK, "--json", tmp_path / "v.json", "--timestamp", TS)
    assert result.returncode == 0, result.stderr
    out = json.loads((tmp_path / "v.json").read_text(encoding="utf-8"))
    assert out["passed"] and out["created"] == TS and out["checker_verdict"] == "pass"
    result = run_prep("verify-check", ORIGINAL, PREPARED, NEGATIVE / "citation-outside-section.check.json")
    assert result.returncode == 1
    assert "citation-outside-section" in result.stderr and "d03-p003" in result.stderr
    assert run_prep("verify-check", ORIGINAL, PREPARED).returncode == 2
    assert run_prep("verify-check", ORIGINAL, PREPARED, BOOKS / "missing.json").returncode == 1


def test_schema_is_a_valid_json_schema():
    schema = json.loads(VC.SCHEMA_PATH.read_text(encoding="utf-8"))
    VC.cp.Draft202012Validator.check_schema(schema)
    assert set(schema["$defs"]["prompt"]["required"]) >= set(VC.CRITERIA) | {"citations", "verdict", "evidence"}
