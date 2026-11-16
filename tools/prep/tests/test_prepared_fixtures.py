"""Tests for tools/prep/fixture/prepared.py: the prepared fixture, negative fixtures and sample check.json files."""

from __future__ import annotations

import json
import subprocess
import sys

from tools.prep import version
from tools.prep.build import build_file
from tools.prep.fixture import prepared as gen
from tools.prep.tests.prepkit import BOOKS, DRAFT, MANIFEST, ORIGINAL, PREPARED_BYTES, REPO

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


def test_readme_lists_the_new_files():
    readme = (BOOKS / "README.md").read_text(encoding="utf-8")
    for name in ("`fixture.prepared.epub`", "`fixture.check.json`", "`negative/`", "tools/prep/fixture/prepared.py"):
        assert name in readme


def test_word_rules_version_matches_the_fixture():
    expected = json.loads((BOOKS / "fixture.expected.json").read_text(encoding="utf-8"))
    assert expected["rules_version"] == version.WORD_RULES_VERSION
