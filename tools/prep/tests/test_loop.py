"""Tests for the /prep-book loop steps (tools/prep/loop.py) and the checker's read guard (checker_guard.py)."""

from __future__ import annotations

import copy
import json
import subprocess
import sys
from pathlib import Path

import pytest

from tools.prep import checker_guard as G
from tools.prep import loop as L
from tools.prep import validate as V
from tools.prep.build import build_file
from tools.prep.extract import extract
from tools.prep.tests.prepkit import CHECK, DRAFT, MANIFEST, NEGATIVE, ORIGINAL, REPO, TS, ok_epubcheck, run_prep
from tools.prep.verify_check import verification_json, verify_check_bytes

VERSION = L.instructions_version()
BASE_DRAFT = json.loads(DRAFT.read_text(encoding="utf-8"))
BASE_DRAFT["book"]["instructions_version"] = VERSION


def write_json(path: Path, data: object) -> None:
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


@pytest.fixture
def work(tmp_path: Path) -> Path:
    w = tmp_path / "work"
    extract(ORIGINAL, w)
    write_json(w / "draft.json", BASE_DRAFT)
    return w


def build(work: Path) -> Path:
    prep = work / "fixture.prepared.epub"
    build_file(ORIGINAL, work / "draft.json", prep)
    return prep


def validate_round(work: Path, prep: Path, number: int = 1) -> dict:
    out = V.validate_files(ORIGINAL, prep, work / "draft.json", epubcheck=ok_epubcheck, created=TS)
    (work / "rounds" / f"{number:02d}" / "validation.json").write_bytes(V.result_bytes(out.result))
    return out.result


def passing_check(prep: Path, book: dict) -> dict:
    check = json.loads(CHECK.read_text(encoding="utf-8"))
    check.update(prepared_sha256=book["prepared_sha256"], instructions_version=book["instructions_version"],
                 round=book["round"])
    return check


def verify_round(work: Path, prep: Path, number: int = 1) -> dict:
    folder = work / "rounds" / f"{number:02d}"
    raw = (folder / "check.json").read_bytes()
    v = verify_check_bytes(ORIGINAL.read_bytes(), prep.read_bytes(), raw)
    data = verification_json(v, ORIGINAL.name, None, prep.name, None, "check.json", TS)
    write_json(folder / "verify-check.json", data)
    return data


# --------------------------------------------------------------------------
# Instructions version


def test_instructions_version_is_declared_and_parsed(tmp_path):
    assert VERSION == "1.0"
    bad = tmp_path / "I.md"
    bad.write_text("# Instructions\n\nno version here\n", encoding="utf-8")
    with pytest.raises(L.LoopError):
        L.instructions_version(bad)


# --------------------------------------------------------------------------
# start-round


def test_start_round_archives_the_draft(work):
    prep = build(work)
    r = L.start_round(work, ORIGINAL, prep)
    assert (r.number, r.restarted) == (1, False)
    assert (work / "rounds" / "01" / "draft.json").read_bytes() == (work / "draft.json").read_bytes()


def test_start_round_reuses_a_round_that_never_reached_the_validator(work):
    prep = build(work)
    L.start_round(work, ORIGINAL, prep)
    again = L.start_round(work, ORIGINAL, prep)
    assert (again.number, again.restarted) == (1, True)
    validate_round(work, prep)
    assert L.start_round(work, ORIGINAL, prep).number == 2


def test_start_round_refuses_another_instructions_version(work):
    draft = copy.deepcopy(BASE_DRAFT)
    draft["book"]["instructions_version"] = "fixture"
    write_json(work / "draft.json", draft)
    prep = build(work)
    with pytest.raises(L.LoopError) as exc:
        L.start_round(work, ORIGINAL, prep)
    assert any(p.startswith("instructions-version") for p in exc.value.problems)
    assert not (work / "rounds").exists()


def test_start_round_refuses_a_prepared_file_from_another_draft(work):
    prep = build(work)
    draft = copy.deepcopy(BASE_DRAFT)
    draft["checkpoints"][0]["prompts"][0]["answer"] += " Changed."
    write_json(work / "draft.json", draft)
    with pytest.raises(L.LoopError) as exc:
        L.start_round(work, ORIGINAL, prep)
    assert any("not prep build output" in p for p in exc.value.problems)


def test_start_round_refuses_a_missing_prepared_file_and_another_original(work, tmp_path):
    other = tmp_path / "other.epub"
    other.write_bytes(ORIGINAL.read_bytes() + b"x")
    with pytest.raises(L.LoopError) as exc:
        L.start_round(work, other, work / "missing.epub")
    text = "\n".join(exc.value.problems)
    assert "not the file the work folder was extracted from" in text and "does not exist" in text


def test_start_round_stops_after_three_rounds_and_after_a_pass(work):
    prep = build(work)
    for n in (1, 2, 3):
        assert L.start_round(work, ORIGINAL, prep).number == n
        validate_round(work, prep, n)
    with pytest.raises(L.LoopError) as exc:
        L.start_round(work, ORIGINAL, prep)
    assert any("all 3 rounds are used" in p for p in exc.value.problems)

    third = work / "rounds" / "03"
    write_json(third / "verify-check.json", {"passed": True})
    with pytest.raises(L.LoopError) as exc:
        L.start_round(work, ORIGINAL, prep)
    assert any("passed both checks" in p for p in exc.value.problems)


def test_ids_may_not_move_or_be_renumbered(work):
    prep = build(work)
    L.start_round(work, ORIGINAL, prep)
    validate_round(work, prep)

    moved = copy.deepcopy(BASE_DRAFT)
    moved["checkpoints"][0]["section"] = "s0002"
    moved["checkpoints"][0]["prompts"][0]["supporting_paragraphs"] = ["d02-p008"]
    write_json(work / "draft.json", moved)
    with pytest.raises(L.LoopError) as exc:
        L.start_round(work, ORIGINAL, build(work))
    codes = {p.split(":")[0] for p in exc.value.problems}
    assert codes == {"id-moved"}

    renumbered = copy.deepcopy(BASE_DRAFT)
    prompts = renumbered["checkpoints"][1]["prompts"]
    del prompts[1]  # drop c0002-p2 ...
    prompts[1]["id"] = "c0002-p2"  # ... and close the gap: the old -p3 text is now under -p2
    write_json(work / "draft.json", renumbered)
    with pytest.raises(L.LoopError) as exc:
        L.start_round(work, ORIGINAL, build(work))
    assert [p.split(":")[0] for p in exc.value.problems] == ["id-renumbered"]
    assert "c0002-p3" in exc.value.problems[0]

    kept = copy.deepcopy(BASE_DRAFT)
    kept["checkpoints"][1]["prompts"][1].update(id="c0002-p4", prompt="Why was the green light no danger?")
    del kept["checkpoints"][1]["prompts"][1]
    kept["checkpoints"][1]["prompts"].append({"id": "c0002-p4", "prompt": "What did the green light show?",
                                              "answer": "That the water was full of plankton, so there would be fish.",
                                              "supporting_paragraphs": ["d03-p011"]})
    write_json(work / "draft.json", kept)
    r = L.start_round(work, ORIGINAL, build(work))
    assert r.number == 2
    assert r.notes == ["prompts removed since round 1: c0002-p2", "prompts added since round 1: c0002-p4"]


# --------------------------------------------------------------------------
# checker-input


def test_checker_input_needs_a_passed_validation_of_this_file(work):
    prep = build(work)
    with pytest.raises(L.LoopError):
        L.checker_input(work, prep)  # no round
    L.start_round(work, ORIGINAL, prep)
    with pytest.raises(L.LoopError) as exc:
        L.checker_input(work, prep)
    assert "no validator result" in exc.value.problems[0]
    result = validate_round(work, prep)
    failed = dict(result, passed=False)
    write_json(work / "rounds" / "01" / "validation.json", failed)
    with pytest.raises(L.LoopError) as exc:
        L.checker_input(work, prep)
    assert "did not pass" in exc.value.problems[0]
    stale = copy.deepcopy(result)
    stale["prepared"]["sha256"] = "0" * 64
    write_json(work / "rounds" / "01" / "validation.json", stale)
    with pytest.raises(L.LoopError) as exc:
        L.checker_input(work, prep)
    assert "another prepared file" in exc.value.problems[0]


def test_checker_input_writes_only_the_checker_view(work):
    draft = copy.deepcopy(BASE_DRAFT)
    draft["x_changes"] = [{"round": 1, "issue": "R1-I1", "change": "DRAFTER NOTE"}]
    write_json(work / "draft.json", draft)
    prep = build(work)
    L.start_round(work, ORIGINAL, prep)
    validate_round(work, prep)
    book = L.checker_input(work, prep, date="2026-11-19")
    view = work / "checker"
    names = sorted(str(p.relative_to(view)) for p in view.rglob("*") if p.is_file())
    assert names == ["INSTRUCTIONS.md", "book.json", "sections/s0001.txt", "sections/s0002.txt",
                     "sections/s0003.txt", "sections/s0004.txt", "sections/s0005.txt", "text/d01-title.txt",
                     "text/d02-chapter-1.txt", "text/d03-chapter-2.txt", "text/d04-chapter-3.txt",
                     "text/d05-endnotes.txt"]
    assert json.loads((view / "book.json").read_text(encoding="utf-8")) == book
    assert book["round"] == 1 and book["date"] == "2026-11-19" and book["issue_id_prefix"] == "R1-"
    assert book["prepared_sha256"] == V.sha256(prep.read_bytes())
    assert book["original_sha256"] == V.sha256(ORIGINAL.read_bytes()) and book["instructions_version"] == VERSION
    assert book["prompt_ids"] == ["c0001-p1", "c0002-p1", "c0002-p2", "c0002-p3"]
    s1 = book["sections"][0]
    assert (s1["first_paragraph"], s1["last_paragraph"], s1["word_count"], s1["ends_document"]) == \
        ("d02-p001", "d02-p006", 233, False)
    assert s1["before"] == "start of text/chapter-1.xhtml" and s1["after"] == "d02-p007 (section s0002)"
    assert book["sections"][3]["after"] == "end of text/chapter-2.xhtml" and book["sections"][3]["ends_document"]
    assert book["excluded"][0]["reason"] == "Front matter: title page."
    s4 = (view / "sections" / "s0004.txt").read_text(encoding="utf-8")
    assert "[d03-p008]" in s4 and "[d03-p013]" in s4 and "[d03-p007]" not in s4
    assert "### Prompt c0002-p2" in s4 and "### Reference answer c0002-p3" in s4
    assert "No checkpoint" in (view / "sections" / "s0003.txt").read_text(encoding="utf-8")
    everything = "".join(p.read_text(encoding="utf-8") for p in view.rglob("*")
                         if p.is_file() and p.name != "INSTRUCTIONS.md")
    assert "supporting_paragraphs" not in everything and "DRAFTER NOTE" not in everything
    assert '"d03-p010",' not in everything  # c0002-p2's citations are not handed to the checker
    assert (view / "INSTRUCTIONS.md").read_bytes() == L.INSTRUCTIONS_PATH.read_bytes()
    assert (view / "text" / "d03-chapter-2.txt").read_bytes() == (work / "text" / "d03-chapter-2.txt").read_bytes()
    assert G.checker_root(str(view / "sections" / "s0004.txt")) == str(view.resolve())

    (view / "stale.txt").write_text("old", encoding="utf-8")
    L.checker_input(work, prep)
    assert not (view / "stale.txt").exists()
    (view / "book.json").write_text("{}", encoding="utf-8")
    with pytest.raises(L.LoopError):
        L.checker_input(work, prep)  # not our folder any more: never deleted


# --------------------------------------------------------------------------
# save-check


@pytest.mark.parametrize("reply, expected", [
    ('{"a": 1}', '{"a": 1}'),
    ('  \n{"a": 1}\n\n', '{"a": 1}'),
    ('Here it is:\n```json\n{"a": 1}\n```\n', '{"a": 1}'),
    ('```\n{"a": [1, 2]}\n```', '{"a": [1, 2]}'),
    ('The result {"a": {"b": 2}} as asked.', '{"a": {"b": 2}}'),
    ('no json here', None),
    ('```json\n{"a": 1,}\n```', None),
    ('[1, 2]', None),
])
def test_extract_check(reply, expected):
    assert L.extract_check(reply) == expected


def round_with_reply(work: Path, reply: str) -> tuple[Path, dict]:
    prep = build(work)
    L.start_round(work, ORIGINAL, prep)
    validate_round(work, prep)
    book = L.checker_input(work, prep)
    (work / "rounds" / "01" / L.REPLY).write_text(reply, encoding="utf-8")
    return prep, book


def test_save_check_extracts_without_repairing(work):
    prep = build(work)
    L.start_round(work, ORIGINAL, prep)
    validate_round(work, prep)
    book = L.checker_input(work, prep)
    check = passing_check(prep, book)
    body = json.dumps(check, indent=2)
    (work / "rounds" / "01" / L.REPLY).write_text(f"```json\n{body}\n```\n", encoding="utf-8")
    path, ok = L.save_check(work)
    assert ok and path.read_text(encoding="utf-8") == body + "\n"
    assert verify_round(work, prep)["passed"] is True


def test_save_check_keeps_an_unusable_reply_so_verify_check_fails(work):
    prep, _ = round_with_reply(work, "I could not finish the check.")
    path, ok = L.save_check(work)
    assert not ok and path.read_text(encoding="utf-8") == "I could not finish the check."
    v = verify_round(work, prep)
    assert v["passed"] is False and {f["code"] for f in v["failures"]} == {"check-unreadable"}


def test_save_check_needs_a_reply(work):
    prep = build(work)
    L.start_round(work, ORIGINAL, prep)
    with pytest.raises(L.LoopError):
        L.save_check(work)


# --------------------------------------------------------------------------
# issues


def test_issues_after_a_validator_failure(work):
    draft = copy.deepcopy(BASE_DRAFT)
    write_json(work / "draft.json", draft)
    prep = build(work)
    L.start_round(work, ORIGINAL, prep)
    result = validate_round(work, prep)
    result = copy.deepcopy(result)
    result["passed"] = False
    result["checks"][0]["status"] = "fail"
    result["checks"][0]["failures"] = [{"code": "anchor-missing", "location": "OEBPS/x.xhtml#rsvp-s0004-end",
                                        "message": "the end anchor is missing"}]
    write_json(work / "rounds" / "01" / "validation.json", result)
    text, data = L.issue_list(work)
    assert data["validator_status"] == "failed" and data["checker_status"] == "not run" and not data["passed"]
    assert "anchor-missing" in text and "The checker was not run" in text


def test_issues_list_only_failed_prompts_and_open_issues(work):
    prep = build(work)
    L.start_round(work, ORIGINAL, prep)
    validate_round(work, prep)
    book = L.checker_input(work, prep)
    check = passing_check(prep, book)
    bad = next(p for p in check["prompts"] if p["id"] == "c0002-p2")
    bad.update(verdict="fail", supported_by_section=False, evidence="FAILED EVIDENCE: plankton is not in s0004.")
    check["issues"] = [{"id": "R1-I1", "severity": "blocking", "target": "c0002-p2", "description": "Unsupported.",
                        "suggested_fix": "Drop it.", "status": "open"}]
    check["verdict"] = "fail"
    write_json(work / "rounds" / "01" / "check.json", check)
    assert verify_round(work, prep)["passed"] is False
    path, text, data = L.write_issues(work)
    assert path == work / "rounds" / "01" / "issues.md" and path.read_text(encoding="utf-8") == text
    assert data["checker_status"] == "failed" and [p["id"] for p in data["failed_prompts"]] == ["c0002-p2"]
    assert data["failed_prompts"][0]["failed"] == ["the answer is supported by its own section"]
    assert "R1-I1" in text and "FAILED EVIDENCE" in text
    for passed in ("c0001-p1", "c0002-p1", "c0002-p3"):
        assert passed not in text  # nothing about prompts that passed
    assert "d02-p006 says" not in text


def test_issues_when_the_checker_result_is_unusable(work):
    prep, _ = round_with_reply(work, "no json")
    L.save_check(work)
    verify_round(work, prep)
    text, data = L.issue_list(work)
    assert data["checker_status"] == "unusable" and not data["checker_issues"]
    assert "Do not change the draft" in text


def test_issues_after_a_pass(work):
    prep = build(work)
    L.start_round(work, ORIGINAL, prep)
    validate_round(work, prep)
    book = L.checker_input(work, prep)
    write_json(work / "rounds" / "01" / "check.json", passing_check(prep, book))
    verify_round(work, prep)
    text, data = L.issue_list(work)
    assert data["passed"] and "Nothing to fix" in text


# --------------------------------------------------------------------------
# Command line


def test_cli_runs_a_whole_round(work):
    prep = work / "fixture.prepared.epub"
    assert run_prep("build", ORIGINAL, work / "draft.json", "-o", prep).returncode == 0
    r = run_prep("start-round", work, ORIGINAL, prep)
    assert r.returncode == 0 and "round 1:" in r.stdout, r.stderr
    validate_round(work, prep)
    r = run_prep("checker-input", work, prep, "--date", "2026-11-19")
    assert r.returncode == 0 and "5 sections, 4 prompts" in r.stdout, r.stderr
    book = json.loads((work / "checker" / "book.json").read_text(encoding="utf-8"))
    (work / "rounds" / "01" / L.REPLY).write_text(json.dumps(passing_check(prep, book)), encoding="utf-8")
    r = run_prep("save-check", work)
    assert r.returncode == 0, r.stderr
    r = run_prep("verify-check", ORIGINAL, prep, work / "rounds" / "01" / "check.json")
    assert r.returncode == 0, r.stderr
    (work / "rounds" / "01" / L.REPLY).write_text("nothing", encoding="utf-8")
    r = run_prep("save-check", work)
    assert r.returncode == 1 and "saved as it is" in r.stderr
    r = run_prep("issues", work)
    assert r.returncode == 0 and "issues.md" in r.stdout


def test_cli_refusals_exit_1(work, tmp_path):
    prep = work / "fixture.prepared.epub"
    r = run_prep("start-round", work, ORIGINAL, prep)
    assert r.returncode == 1 and "does not exist; run prep build first" in r.stderr
    for args in (("checker-input", work, prep), ("save-check", work), ("issues", tmp_path)):
        assert run_prep(*args).returncode == 1, args
    assert run_prep("start-round", work).returncode == 2


# --------------------------------------------------------------------------
# The checker's read guard


@pytest.fixture
def view(tmp_path: Path) -> Path:
    v = tmp_path / "work" / "checker"
    (v / "sections").mkdir(parents=True)
    write_json(v / "book.json", {"format": L.CHECKER_INPUT_FORMAT})
    (v / "sections" / "s0001.txt").write_text("text", encoding="utf-8")
    (tmp_path / "work" / "draft.json").write_text("{}", encoding="utf-8")
    return v


def event(tool: str, **args) -> dict:
    return {"hook_event_name": "PreToolUse", "tool_name": tool, "tool_input": args, "cwd": "/"}


def test_guard_allows_reads_inside_the_checker_folder(view):
    assert G.decide(event("Read", file_path=str(view / "sections" / "s0001.txt"))) is None
    assert G.decide(event("Read", file_path=str(view / "book.json"))) is None
    assert G.decide(event("Glob", pattern="sections/*.txt", path=str(view))) is None
    assert G.decide(event("Grep", pattern="flash", path=str(view / "sections"), glob="*.txt")) is None
    assert G.decide(event("SomethingElse", file_path="/etc/passwd")) is None  # not a guarded tool


def test_guard_blocks_everything_else(view, tmp_path):
    work = view.parent
    blocked = [
        event("Read", file_path=str(work / "draft.json")),
        event("Read", file_path=str(view / ".." / "draft.json")),
        event("Read", file_path=str(REPO / "fixtures" / "books" / "fixture.check.json")),
        event("Read"),
        event("Glob", pattern="**/*.json"),
        event("Glob", pattern="**/*.json", path=str(work)),
        event("Glob", pattern="../*.json", path=str(view)),
        event("Glob", pattern="/etc/*", path=str(view)),
        event("Grep", pattern="answer"),
        event("Grep", pattern="answer", path=str(work / "rounds")),
        event("Grep", pattern="answer", path=str(view), glob="../**"),
    ]
    for e in blocked:
        assert G.decide(e) is not None, e
    link = view / "escape"
    link.symlink_to(work)
    assert G.decide(event("Read", file_path=str(link / "draft.json"))) is not None
    fake = tmp_path / "fake"
    fake.mkdir()
    write_json(fake / "book.json", {"format": "something-else"})
    assert G.decide(event("Read", file_path=str(fake / "book.json"))) is not None


def test_guard_script_exit_codes(view):
    script = REPO / "tools" / "prep" / "checker_guard.py"

    def run(data: str) -> subprocess.CompletedProcess:
        return subprocess.run([sys.executable, str(script)], input=data, capture_output=True, text=True)

    ok = run(json.dumps(event("Read", file_path=str(view / "book.json"))))
    assert ok.returncode == 0 and ok.stderr == ""
    no = run(json.dumps(event("Read", file_path=str(view.parent / "draft.json"))))
    assert no.returncode == 2 and "outside the checker folder" in no.stderr
    assert run("not json").returncode == 2


def test_agent_and_command_files_are_wired_to_these_tools():
    agent = (REPO / ".claude" / "agents" / "prep-checker.md").read_text(encoding="utf-8")
    command = (REPO / ".claude" / "commands" / "prep-book.md").read_text(encoding="utf-8")
    front = agent.split("---")[1]
    assert agent.startswith("---\n") and "name: prep-checker" in front
    assert "tools: Read, Glob, Grep" in front and "Write" not in front and "Bash" not in front
    assert "tools/prep/checker_guard.py" in front and "onFailure: block" in front
    assert command.startswith("---\n") and "disable-model-invocation: true" in command
    for step in ("extract", "build", "start-round", "validate", "checker-input", "save-check", "verify-check",
                 "issues", "report"):
        assert f"python3 -m tools.prep {step} " in command, step
    assert "`subagent_type` **`prep-checker`**" in command


@pytest.mark.parametrize("entry", MANIFEST["checker"], ids=lambda e: e["file"])
def test_checker_fixtures_go_through_the_loop_to_the_checker(entry, work):
    """The planted fixtures reach the checker: built from their draft, validated, and given a checker folder."""
    (work / "draft.json").write_bytes((NEGATIVE / entry["draft"]).read_bytes())
    prep = build(work)
    assert prep.read_bytes() == (NEGATIVE / entry["file"]).read_bytes()
    L.start_round(work, ORIGINAL, prep)
    assert validate_round(work, prep)["passed"]
    book = L.checker_input(work, prep)
    assert entry["prompt"] in book["prompt_ids"]
    section = next(s for s in book["sections"] if entry["prompt"] in s["prompt_ids"])
    text = (work / "checker" / section["section_file"]).read_text(encoding="utf-8")
    assert f"### Prompt {entry['prompt']}" in text
