"""The runner end to end against a stub simulator (tests/stub_sim.py): no SDL, no Xvfb."""

import shutil
import subprocess
import time
from pathlib import Path

import pytest
from PIL import Image, ImageDraw

from tools.simtest import __main__ as cli
from tools.simtest import runner
from tools.simtest.runner import Options, ScenarioRunner

from .helpers import load, stub_binary, write_scenario

FLOW = """
    description = "stub flow"
    sd = [ { text = "x", dst = "books/a.txt" } ]
    steps = [
      { wait_log = "Entering activity: Home" },
      { wait_screen = "page 0" },
      { shot = "start" },
      { press = "DOWN" },
      { wait_log = "DOWN -> page 1" },
      { shot = "after" },
      { screen_has = "page 1" },
      { screen_lacks = "page 0" },
      { expect_json = ".crosspoint/state.json", pointer = "/page", equals = 1 },
      { kill = true },
      { expect_file = ".crosspoint/*.json", contains = "DOWN" },
      { start = true },
      { wait_screen = "page 1", note = "the stub restores its page from the SD card" },
      { quit = true },
      { compare = "start" },
      { compare = "after" },
    ]
"""


def run(sc, tmp_path, **kw):
    opts = Options(binary=stub_binary(tmp_path), report_dir=tmp_path / "report", use_xvfb=False, **kw)
    return ScenarioRunner(sc, opts).run()


def no_process_mentions(text):
    out = subprocess.run(["pgrep", "-f", text], capture_output=True, text=True)
    return out.stdout.strip() == ""


def test_flow_kill_restart_and_reference_update(tmp_path):
    sc = load(tmp_path / "scen", "flow", FLOW)
    res = run(sc, tmp_path, update_refs=True)
    assert res.status == "pass", res.error
    assert sorted(p.name for p in sc.refs_dir.iterdir()) == ["after.png", "start.png"]
    assert len(res.runs) == 2 and res.runs[0]["exit"] == -9 and res.runs[1]["exit"] == 0
    assert res.runs[0]["sd"] == res.runs[1]["sd"]
    log = (tmp_path / "report" / "flow" / "log.txt").read_text()
    assert "sending SIGKILL to process group" in log and "run 2: starting" in log
    statuses = {s.action: s.status for s in res.steps}
    assert set(statuses.values()) == {"pass"}, res.steps

    # Second run against the stored references passes.
    res = run(sc, tmp_path)
    assert res.status == "pass", res.error

    # A changed block in one reference fails with its bounding box and diff files.
    ref = sc.refs_dir / "after.png"
    img = Image.open(ref).convert("RGB")
    ImageDraw.Draw(img).rectangle([10, 20, 19, 29], fill=(255, 255, 255))
    img.save(ref)
    res = run(sc, tmp_path)
    assert res.status == "fail"
    [bad] = [s for s in res.steps if s.status == "fail"]
    assert bad.action == "compare"
    assert "shot 'after' differs" in bad.message
    assert "100 pixel(s) differ in bounding box x=10..19, y=20..29 (10x10)" in bad.message
    for kind in ("actual", "reference", "diff"):
        assert (tmp_path / "report" / bad.images[kind]).is_file()


def test_wait_timeout_kills_stubborn_process_tree(tmp_path):
    sc = load(tmp_path / "scen", "stubborn", """
        description = "a wait that never matches"
        env = { vars = { CROSSPOINT_SIM_STUB_MODE = "stubborn" } }
        steps = [
          { wait_log = "Entering activity: Home" },
          { wait_log = "this line never comes", timeout_ms = 1500 },
        ]
    """)
    started = time.monotonic()
    res = run(sc, tmp_path)
    took = time.monotonic() - started
    assert res.status == "fail"
    assert took < 10, took
    assert "step 2 (wait_log): timed out after 1500 ms waiting for log line matching /this line never comes/" \
        in res.error
    assert "last 40 lines of the simulator log" in res.error
    assert "Entering activity: Home" in res.error
    assert no_process_mentions(res.sd_path), "the stub's child survived"


def test_quit_that_is_ignored_is_a_failure(tmp_path, monkeypatch):
    monkeypatch.setattr(runner, "QUIT_TIMEOUT_S", 1.0)
    sc = load(tmp_path / "scen", "noquit", """
        description = "SIGTERM ignored"
        env = { vars = { CROSSPOINT_SIM_STUB_MODE = "stubborn" } }
        steps = [ { wait_log = "Home" }, { quit = true } ]
    """)
    res = run(sc, tmp_path)
    assert res.status == "fail"
    assert "did not quit within 1 s of SIGTERM; killed" in res.error
    assert no_process_mentions(res.sd_path)


def test_overall_timeout(tmp_path):
    sc = load(tmp_path / "scen", "slow", """
        description = "overall timeout"
        timeout_s = 2
        steps = [ { wait_log = "never", timeout_ms = 60000 } ]
    """)
    started = time.monotonic()
    res = run(sc, tmp_path)
    assert time.monotonic() - started < 8
    assert "scenario exceeded its overall timeout of 2 s" in res.error


def test_crash_is_reported_with_exit_status(tmp_path):
    sc = load(tmp_path / "scen", "crash", """
        description = "the program dies"
        env = { vars = { CROSSPOINT_SIM_STUB_MODE = "crash" } }
        steps = [ { wait_log = "never" } ]
    """)
    res = run(sc, tmp_path)
    assert "simulator ended (exit status 3) while waiting for log line matching /never/" in res.error
    assert "Guru Meditation" in res.error


def test_wait_that_matches_an_old_line_is_refused(tmp_path):
    sc = load(tmp_path / "scen", "stale", """
        description = "second wait matches a line logged before the first wait's line"
        steps = [ { wait_log = "SCREEN: 100%" }, { wait_log = "Entering activity: Home" } ]
    """)
    res = run(sc, tmp_path)
    assert res.status == "fail"
    assert "step 2 (wait_log): the simulator's WAIT matched a line logged before" in res.error


def test_failed_file_check_lists_folder(tmp_path):
    sc = load(tmp_path / "scen", "nofile", """
        description = "missing file"
        sd = [ { text = "x", dst = "books/a.txt" } ]
        steps = [ { wait_log = "Home" }, { expect_file = "books/missing.epub" } ]
    """)
    res = run(sc, tmp_path)
    assert res.status == "fail"
    assert "no file matches /books/missing.epub on the SD card (/books holds: a.txt)" in res.error


def test_keep_leaves_sd_folder(tmp_path):
    sc = load(tmp_path / "scen", "keep", """
        description = "keep"
        sd = [ { text = "x", dst = "books/a.txt" } ]
        steps = [ { wait_log = "Home" } ]
    """)
    res = run(sc, tmp_path, keep=True)
    try:
        assert res.status == "pass", res.error
        assert res.sd_kept and (Path(res.sd_path) / "books/a.txt").is_file()
        assert {"path": "books/a.txt", "size": 1, "text": "x"} in res.sd_listing
    finally:
        shutil.rmtree(res.sd_path, ignore_errors=True)


def test_cli(tmp_path, capsys):
    scen = tmp_path / "scen"
    write_scenario(scen, "ok", 'description = "ok"\nsteps = [ { wait_log = "Home" } ]\n')
    write_scenario(scen, "broken", 'description = "broken"\nsteps = [ { wait_log = "never", timeout_ms = 500 } ]\n')
    binary = stub_binary(tmp_path)
    base = ["--scenarios-dir", str(scen)]
    common = ["--binary", str(binary), "--no-xvfb", "--report-dir", str(tmp_path / "rep")]

    assert cli.main(base + ["list"]) == 0
    assert "ok" in capsys.readouterr().out
    assert cli.main(base + ["run", "ok"] + common) == 0
    assert (tmp_path / "rep" / "results.json").is_file()
    assert (tmp_path / "rep" / "junit.xml").is_file()
    assert cli.main(base + ["run", "--all", "--jobs", "2"] + common) == 1
    out = capsys.readouterr().out
    assert "PASS ok" in out and "FAIL broken" in out and "1 passed, 1 failed" in out
    assert cli.main(base + ["run", "nope"] + common) == 2
    assert cli.main(base + ["run", "ok", "--binary", str(tmp_path / "missing"), "--no-xvfb"]) == 2
    assert "not found or not executable" in capsys.readouterr().err
    assert cli.main(base + ["run", "ok", "--repeat", "3"] + common) == 0
    out = capsys.readouterr().out
    assert "PASS ok-r01" in out and "PASS ok-r03" in out and "determinism" in out


def test_report_dir_that_is_not_a_report_is_refused(tmp_path):
    scen = tmp_path / "scen"
    write_scenario(scen, "ok", 'description = "ok"\nsteps = [ { wait_log = "Home" } ]\n')
    precious = tmp_path / "precious"
    precious.mkdir()
    (precious / "notes.txt").write_text("keep me")
    with pytest.raises(SystemExit, match="is not a simtest report"):
        cli.main(["--scenarios-dir", str(scen), "run", "ok", "--binary", str(stub_binary(tmp_path)), "--no-xvfb",
                  "--report-dir", str(precious)])
    assert (precious / "notes.txt").read_text() == "keep me"
