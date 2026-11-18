"""Tests for the guard probe (tools/prep/probe.py, ``prep guard-probe``) and the guard's probe rules.

The "guard active" case drives the real ``checker_guard.py`` script with the hook input Claude Code documents for a
PreToolUse Write call, then performs the write as Claude Code would; nothing here runs the ``prep-checker`` agent.
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

import pytest

from tools.prep import checker_guard as G
from tools.prep import loop as L
from tools.prep import probe as P
from tools.prep.extract import extract
from tools.prep.report import evaluate, load_history
from tools.prep.tests.prepkit import ORIGINAL, PREPARED, REPO, TS, ok_epubcheck, run_prep
from tools.prep.verify_check import verify_check_bytes

SCRIPT = REPO / "tools" / "prep" / "checker_guard.py"
COMMAND = REPO / ".claude" / "commands" / "prep-book.md"


def hook_input(tool: str, cwd: Path, **args) -> dict:
    """A PreToolUse hook input as Claude Code sends it for a call of the prep-checker agent."""
    return {"session_id": "probe-test", "transcript_path": "/dev/null", "cwd": str(cwd), "permission_mode": "default",
            "hook_event_name": "PreToolUse", "agent_type": "prep-checker", "tool_name": tool, "tool_input": args}


def run_guard(event: dict) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(SCRIPT)], input=json.dumps(event), capture_output=True, text=True)


def book_of(out: Path) -> dict:
    return json.loads((out / "guard-probe" / "input" / "book.json").read_text(encoding="utf-8"))


def probe_result(out: Path) -> str:
    """A complete, honest checker result for the probe, as the agent's instructions describe it."""
    book = book_of(out)
    return json.dumps({
        "format": "rsvp-check", "version": 1, "prepared_sha256": book["prepared_sha256"],
        "original_sha256": book["original_sha256"], "instructions_version": book["instructions_version"],
        "checker": {"model": "test", "date": book["date"]}, "round": book["round"],
        "prompts": [{"id": "c0001-p1", "verdict": "pass", "supported_by_section": True, "citations": ["d01-p003"],
                     "accepts_paraphrase": True, "answerable_from_section": True, "does_not_give_away_answer": True,
                     "evidence": "d01-p003: 'rang the bell five times, which meant that the ferry would not run'."}],
        "sections": [{"id": "s0001", "natural_boundary": True, "evidence": "The whole made-up text."}],
        "issues": [], "verdict": "pass"}, indent=2) + "\n"


def guarded_write(out: Path, content: str | None = None) -> Path:
    """The checker's one Write with the guard active: the hook runs and allows it, then Claude Code writes."""
    book = book_of(out)
    content = probe_result(out) if content is None else content
    done = run_guard(hook_input("Write", out, file_path=book["result_file"], content=content))
    assert done.returncode == 0, done.stderr
    assert json.loads(done.stdout)["hookSpecificOutput"]["permissionDecision"] == "allow"
    target = Path(book["result_file"])
    target.write_text(content, encoding="utf-8")
    return target


@pytest.fixture
def out(tmp_path: Path) -> Path:
    o = tmp_path / "Book-prepared"
    P.prepare(o, "2026-11-19")
    return o


# --------------------------------------------------------------------------
# The probe folder


def test_prepare_writes_a_small_synthetic_checker_folder(out):
    folder = out / "guard-probe"
    names = sorted(str(p.relative_to(folder)) for p in folder.rglob("*") if p.is_file())
    assert names == ["input/INSTRUCTIONS.md", "input/book.json", "input/sections/s0001.txt", "input/text/d01-probe.txt"]
    assert (folder / "result").is_dir() and not any((folder / "result").iterdir())
    book = book_of(out)
    assert book["format"] == "rsvp-guard-probe-input" != L.CHECKER_INPUT_FORMAT
    assert book["result_file"] == str((folder / "result" / "checker-reply.txt").resolve())
    assert book["prompt_ids"] == ["c0001-p1"] and book["section_ids"] == ["s0001"] and book["date"] == "2026-11-19"
    assert "not a book" in book["note"] and "made-up" in (folder / "input" / "INSTRUCTIONS.md").read_text()
    section = (folder / "input" / "sections" / "s0001.txt").read_text(encoding="utf-8")
    assert "### Prompt c0001-p1" in section and "[d01-p003] paragraph" in section
    assert sum(p.stat().st_size for p in folder.rglob("*") if p.is_file()) < 8000
    assert P.agent_prompt(out) == "Checker folder: " + str((folder / "input").resolve())


def test_prepare_replaces_an_earlier_probe_but_nothing_else(out, tmp_path):
    guarded_write(out)
    P.prepare(out)
    assert not (out / "guard-probe" / "result" / "checker-reply.txt").exists()
    assert not (out / "guard-probe" / "result" / G.RECORD).exists()
    other = tmp_path / "other"
    (other / "guard-probe").mkdir(parents=True)
    (other / "guard-probe" / "notes.txt").write_text("mine", encoding="utf-8")
    with pytest.raises(P.ProbeError):
        P.prepare(other)
    assert (other / "guard-probe" / "notes.txt").read_text(encoding="utf-8") == "mine"


# --------------------------------------------------------------------------
# --check: one outcome per case


def test_guard_active_passes(out):
    guarded_write(out)
    outcome = P.check(out)
    assert outcome.passed and outcome.code == "passed" and "guard is active" in outcome.message
    record = json.loads((out / "guard-probe" / "result" / G.RECORD).read_text(encoding="utf-8"))
    assert record["format"] == G.PROBE_RECORD_FORMAT and record["round"] == 1
    assert record["prepared_sha256"] == book_of(out)["prepared_sha256"]


def test_guard_active_ignores_line_endings_like_save_check(out):
    target = guarded_write(out)
    target.write_bytes(target.read_bytes().replace(b"\n", b"\r\n"))
    assert P.check(out).passed


def test_hook_not_run_fails_with_what_to_do(out):
    reply = Path(book_of(out)["result_file"])
    reply.write_text(probe_result(out), encoding="utf-8")  # the agent wrote it, but no hook ran
    outcome = P.check(out)
    assert not outcome.passed and outcome.code == "guard-not-active"
    for words in ("FAILED (guard-not-active)", "does not exist", "Nothing of your book was touched",
                  "on your own computer", "trust the folder", "restart Claude Code", "run /prep-book again",
                  "cloud session cannot do this"):
        assert words in outcome.message, words


def test_no_result_fails(out):
    outcome = P.check(out)
    assert outcome.code == "no-result" and "wrote no result file" in outcome.message
    assert "Nothing of your book was touched" in outcome.message


def test_result_changed_after_the_guard_recorded_it_fails(out):
    target = guarded_write(out)
    target.write_text(probe_result(out).replace('"pass"', '"fail"'), encoding="utf-8")
    outcome = P.check(out)
    assert outcome.code == "changed" and "changed afterwards" in outcome.message


def test_record_for_another_file_fails(out):
    guarded_write(out)
    record_path = out / "guard-probe" / "result" / G.RECORD
    record = json.loads(record_path.read_text(encoding="utf-8"))
    record["file"] = "other.txt"
    record_path.write_text(json.dumps(record), encoding="utf-8")
    outcome = P.check(out)
    assert outcome.code == "other-file" and "another file" in outcome.message


@pytest.mark.parametrize("change", [
    {"format": G.RECORD_FORMAT},  # a round's record, not a probe's
    {"round": 2},
    {"prepared_sha256": "ab" * 32},  # made for another checker folder
    None,  # not a record at all
])
def test_record_for_another_folder_fails(out, change):
    guarded_write(out)
    record_path = out / "guard-probe" / "result" / G.RECORD
    record = json.loads(record_path.read_text(encoding="utf-8"))
    record_path.write_text("not json" if change is None else json.dumps({**record, **change}), encoding="utf-8")
    outcome = P.check(out)
    assert outcome.code == "other-folder" and "another checker folder" in outcome.message


def test_check_without_a_probe_is_refused(tmp_path):
    with pytest.raises(P.ProbeError):
        P.check(tmp_path)


# --------------------------------------------------------------------------
# The guard's probe rules; its rules for rounds are tested unchanged in test_loop.py


def test_guard_lets_the_checker_read_the_probe_but_not_its_result(out):
    folder = (out / "guard-probe").resolve()
    for e in (hook_input("Read", out, file_path=str(folder / "input" / "book.json")),
              hook_input("Glob", out, pattern="sections/*.txt", path=str(folder / "input")),
              hook_input("Grep", out, pattern="bell", path=str(folder / "input"))):
        assert G.decide(e) is None, e
    for e in (hook_input("Read", out, file_path=str(folder / "result" / "checker-reply.txt")),
              hook_input("Glob", out, pattern="*", path=str(folder)),
              hook_input("Read", out, file_path=str(out / "work" / "draft.json"))):
        assert G.decide(e) is not None, e


def test_guard_allows_one_probe_write_and_no_other(out):
    folder = (out / "guard-probe").resolve()
    for path in (folder / "input" / "book.json", folder / "result" / "other.txt", folder / "result" / G.RECORD,
                 folder / "checker-reply.txt", folder / "input" / "checker-reply.txt"):
        reason = G.decide_write(hook_input("Write", out, file_path=str(path), content="{}"))
        assert reason is not None and "only its result file" in reason, path
    guarded_write(out)
    again = run_guard(hook_input("Write", out, file_path=book_of(out)["result_file"], content="{}"))
    assert again.returncode == 2 and "already exists" in again.stderr


def test_a_probe_folder_cannot_authorise_a_rounds_result(tmp_path):
    work = tmp_path / "work"
    (work / "rounds" / "01").mkdir(parents=True)
    reply = work / "rounds" / "01" / L.REPLY
    # A probe book.json in the place of a round's checker folder, naming the round's result file.
    (work / "checker").mkdir()
    (work / "checker" / "book.json").write_text(json.dumps({"format": G.PROBE_FORMAT, "round": 1,
                                                            "result_file": str(reply)}), encoding="utf-8")
    assert G.decide_write(hook_input("Write", tmp_path, file_path=str(reply), content="{}")) is not None
    # A real probe whose book.json is edited to name the round's result file.
    P.prepare(tmp_path)
    book_path = tmp_path / "guard-probe" / "input" / "book.json"
    book = json.loads(book_path.read_text(encoding="utf-8"))
    book_path.write_text(json.dumps({**book, "result_file": str(reply)}), encoding="utf-8")
    assert G.decide_write(hook_input("Write", tmp_path, file_path=str(reply), content="{}")) is not None
    assert not (work / "rounds" / "01" / G.RECORD).exists()


def test_a_rounds_checker_folder_cannot_authorise_a_probe_result(out):
    reply = Path(book_of(out)["result_file"])
    # The probe's input folder with a round's book.json.
    book_path = out / "guard-probe" / "input" / "book.json"
    book_path.write_text(json.dumps({**book_of(out), "format": L.CHECKER_INPUT_FORMAT}), encoding="utf-8")
    assert G.decide_write(hook_input("Write", out, file_path=str(reply), content="{}")) is not None
    # A round's checker folder naming the probe's result file.
    work = out / "work"
    (work / "checker").mkdir(parents=True)
    (work / "checker" / "book.json").write_text(json.dumps({"format": L.CHECKER_INPUT_FORMAT, "round": 1,
                                                            "result_file": str(reply)}), encoding="utf-8")
    assert G.decide_write(hook_input("Write", out, file_path=str(reply), content="{}")) is not None
    assert not (out / "guard-probe" / "result" / G.RECORD).exists()


# --------------------------------------------------------------------------
# The round commands ignore or refuse the probe


def test_round_commands_ignore_or_refuse_the_probe(out, tmp_path):
    guarded_write(out)
    probe = out / "guard-probe"
    for folder in (out, probe, probe / "result"):
        assert L.rounds(folder) == [] and load_history(folder) == []
        with pytest.raises(L.LoopError):
            L.save_check(folder)
        with pytest.raises(L.LoopError):
            L.start_round(folder, ORIGINAL, PREPARED)
    reply = (probe / "result" / L.REPLY).read_bytes()
    v = verify_check_bytes(ORIGINAL.read_bytes(), PREPARED.read_bytes(), reply)
    assert not v.passed and {"stale-check", "original-mismatch"} <= {f.code for f in v.failures}
    ev = evaluate(ORIGINAL, PREPARED, None, probe / "result" / L.REPLY, probe, None, TS, epubcheck=ok_epubcheck)
    assert not ev.ready


def test_a_probe_record_does_not_vouch_for_a_rounds_result(out, tmp_path):
    """Copied into a round, the probe's result and record are refused by save-check."""
    guarded_write(out)
    work = tmp_path / "work"
    extract(ORIGINAL, work)
    folder = work / "rounds" / "01"
    folder.mkdir(parents=True)
    for name in (L.REPLY, G.RECORD):
        (folder / name).write_bytes((out / "guard-probe" / "result" / name).read_bytes())
    assert L.reply_fault(folder, 1, (folder / L.REPLY).read_bytes()) == "other-record"
    with pytest.raises(L.LoopError) as exc:
        L.save_check(work)
    assert "the checker's guard did not approve" in exc.value.problems[0]


def test_extract_and_the_work_folder_are_untouched_by_the_probe(tmp_path):
    out = tmp_path / "Book-prepared"
    P.prepare(out)
    work = out / "work"
    assert not work.exists()
    extract(ORIGINAL, work)  # the next step of /prep-book: WORK is new and empty
    assert sorted(p.name for p in work.iterdir()) == ["source.json", "structure.json", "text"]


# --------------------------------------------------------------------------
# Command line and command text


def test_cli_outcomes_and_exit_status(tmp_path):
    out = tmp_path / "out"
    r = run_prep("guard-probe", out, "--check")
    assert r.returncode == 1 and "holds no guard probe" in r.stderr
    r = run_prep("guard-probe", out)
    assert r.returncode == 0 and r.stdout.splitlines()[-1] == P.agent_prompt(out), r.stderr
    r = run_prep("guard-probe", out, "--check")
    assert r.returncode == 1 and r.stderr.startswith("FAILED (no-result)") and r.stdout == ""
    reply = Path(book_of(out)["result_file"])
    reply.write_text(probe_result(out), encoding="utf-8")
    r = run_prep("guard-probe", out, "--check")
    assert r.returncode == 1 and r.stderr.startswith("FAILED (guard-not-active)") and r.stdout == ""
    assert run_prep("guard-probe", out).returncode == 0  # a clean probe
    guarded_write(out)
    r = run_prep("guard-probe", out, "--check")
    assert r.returncode == 0 and r.stdout.startswith("PASSED: the checker's guard is active"), r.stderr
    assert run_prep("guard-probe").returncode == 2


def test_command_runs_the_probe_before_extracting():
    text = COMMAND.read_text(encoding="utf-8")
    probe, check, extract_at = (text.index('python3 -m tools.prep guard-probe "OUT"\n'),
                                text.index('python3 -m tools.prep guard-probe "OUT" --check'),
                                text.index("python3 -m tools.prep extract "))
    assert probe < check < extract_at
    step = text[text.index("## Step 1"):text.index("## Step 2")]
    assert "`subagent_type` **`prep-checker`**" in step and "Checker folder: OUT/guard-probe/input" in step
    assert "guard-probe/" in text[text.index("## Rules for the whole run"):text.index("## Step 0")]
