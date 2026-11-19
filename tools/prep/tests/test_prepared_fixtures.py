"""Tests for tools/prep/fixture/prepared.py: the prepared fixture, negative fixtures and sample check.json files."""

from __future__ import annotations

import json
import subprocess
import sys

import pytest

from tools.prep import version
from tools.prep.loop import instructions_version
from tools.prep.verify_check import CRITERIA
from tools.prep.build import build_file
from tools.prep.fixture import prepared as gen
from tools.prep.tests.prepkit import BOOKS, DRAFT, MANIFEST, NEGATIVE, ORIGINAL, PREPARED_BYTES, REPO

GENERATOR = REPO / "tools" / "prep" / "fixture" / "prepared.py"


def run_generator(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(GENERATOR), *args], capture_output=True, text=True, cwd=REPO)


def test_committed_files_are_current():
    result = run_generator("--check")
    assert result.returncode == 0, result.stdout + result.stderr
    assert "prepared and negative fixture files are current" in result.stdout


def test_generator_is_deterministic_and_matches_prep_build(tmp_path):
    first, second = gen.generate_files(), gen.generate_files()
    assert first == second
    out = tmp_path / "fixture.prepared.epub"
    build_file(ORIGINAL, DRAFT, out)
    assert out.read_bytes() == first["fixture.prepared.epub"] == PREPARED_BYTES


def test_check_names_stale_missing_and_unknown_files(tmp_path):
    assert run_generator("--out", str(tmp_path)).returncode == 0
    (tmp_path / "fixture.check.json").write_text("{}\n", encoding="utf-8")
    (tmp_path / "negative" / "broken-link.prepared.epub").unlink()
    (tmp_path / "negative" / "stray.epub").write_bytes(b"")
    result = run_generator("--check", "--out", str(tmp_path))
    assert result.returncode == 1
    assert f"differs: {tmp_path / 'fixture.check.json'}" in result.stdout
    assert f"missing: {tmp_path / 'negative' / 'broken-link.prepared.epub'}" in result.stdout
    assert "stray.epub" in result.stdout


def test_manifest_shape():
    assert MANIFEST["format"] == "rsvp-negative-fixtures" and MANIFEST["original"] == "../fixture.epub"
    assert any(e["file"] == "broken-link.prepared.epub" for e in MANIFEST["prepared"])
    for entry in MANIFEST["checks"]:
        assert set(entry) == {"file", "prepared", "codes", "reason"}
    for entry in MANIFEST["prepared"]:
        assert set(entry) == {"file", "failing_checks", "codes", "reason"}
    assert {e["file"] for e in MANIFEST["checker"]} == {"unsupported-answer.prepared.epub",
                                                        "answer-given-away.prepared.epub"}
    for entry in MANIFEST["checker"]:
        assert set(entry) == {"draft", "file", "prompt", "criterion", "reason"}
        assert entry["criterion"] in CRITERIA and entry["reason"].strip()


@pytest.mark.parametrize("entry", MANIFEST["checker"], ids=lambda e: e["file"])
def test_checker_fixture_plants_exactly_one_fault(entry, tmp_path):
    base = json.loads(DRAFT.read_text(encoding="utf-8"))
    draft = json.loads((NEGATIVE / entry["draft"]).read_text(encoding="utf-8"))
    assert draft["book"]["instructions_version"] == instructions_version()
    base["book"]["instructions_version"] = draft["book"]["instructions_version"]
    prompts = {p["id"]: p for c in draft["checkpoints"] for p in c["prompts"]}
    base_prompts = {p["id"]: p for c in base["checkpoints"] for p in c["prompts"]}
    changed = [pid for pid in base_prompts if prompts[pid] != base_prompts[pid]]
    assert changed == [entry["prompt"]]
    for pid in base_prompts:
        prompts[pid].update(base_prompts[pid])
    assert draft == base  # nothing else differs
    which = {"supported_by_section": "answer", "does_not_give_away_answer": "prompt"}[entry["criterion"]]
    old, new = base_prompts[entry["prompt"]], json.loads((NEGATIVE / entry["draft"]).read_text(encoding="utf-8"))
    new_prompt = next(p for c in new["checkpoints"] for p in c["prompts"] if p["id"] == entry["prompt"])
    assert new_prompt[which] != old[which]
    out = tmp_path / "x.prepared.epub"
    build_file(ORIGINAL, NEGATIVE / entry["draft"], out)
    assert out.read_bytes() == (NEGATIVE / entry["file"]).read_bytes()


def test_readme_lists_the_new_files():
    readme = (BOOKS / "README.md").read_text(encoding="utf-8")
    for name in ("`fixture.prepared.epub`", "`fixture.check.json`", "`negative/`", "tools/prep/fixture/prepared.py"):
        assert name in readme


def test_word_rules_version_matches_the_fixture():
    expected = json.loads((BOOKS / "fixture.expected.json").read_text(encoding="utf-8"))
    assert expected["rules_version"] == version.WORD_RULES_VERSION
