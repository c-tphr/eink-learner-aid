"""Scenario parsing, validation and compilation (no simulator needed)."""

import pytest

from tools.simtest import scenario
from tools.simtest.scenario import ScenarioError

from .helpers import load


def test_committed_scenarios_load():
    found = scenario.discover()
    assert {"open_book", "restart_keeps_place"} <= set(found)
    for path in found.values():
        sc = scenario.load(path)
        assert sc.description
        assert (sc.refs_dir).is_dir(), f"{sc.name} has no refs/ folder"
        for step in sc.steps:
            if step.action == "compare" and "same_as" not in step.opts:
                assert (sc.refs_dir / f"{step.value}.png").is_file(), f"{sc.name}: missing ref {step.value}"


def test_compiles_script_with_cursor_times(tmp_path):
    sc = load(tmp_path, "demo", """
        description = "demo"
        steps = [
          { wait_log = "Entering activity: Home" },
          { shot = "home" },
          { press = "DOWN" },
          { press = "ENTER", hold_ms = 900 },
          { wait_screen = "a;b (c)" },
          { tap = [10, 20] },
          { swipe = [400, 400, 80, 400], duration_ms = 300 },
          { switch = "on" },
          { wait_log = "x", timeout_ms = 2000 },
        ]
    """)
    [run] = sc.compiled("/shots")
    assert run.script == [
        "0:WAIT:Entering activity: Home",
        "0:SHOT:/shots/home.bmp",
        "120:DOWN:80",          # after the shot, one gap
        "320:ENTER:900",        # after DOWN is released, one gap
        r"1340:WAIT:\[SIM\] SCREEN:.*a\x3Bb\ \(c\)",  # ';' cannot appear in the script
        "0:TAP:10,20,80",
        "200:SWIPE:400,400,80,400,300",
        "620:SWITCH_ON",
        "740:WAIT:x",
    ]
    assert [s.kind for s in run.syncs] == ["wait", "shot", "wait", "wait"]
    assert run.syncs[-1].timeout_ms == 2000 and run.syncs[-1].offset_ms == 740
    assert run.end == "end"


def test_kill_start_makes_two_runs(tmp_path):
    sc = load(tmp_path, "two", """
        description = "two runs"
        env = { slide_switch = "on" }
        steps = [
          { wait_log = "Home" },
          { expect_file = "books/x.epub" },
          { kill = true },
          { expect_no_file = "tmp/*.bin" },
          { start = true, slide_switch = "off" },
          { wait_log = "Home" },
          { quit = true },
        ]
    """)
    runs = sc.compiled("/s")
    assert len(runs) == 2
    assert runs[0].slide_switch == "on"
    assert [s.action for s in runs[0].tail] == ["expect_file", "kill", "expect_no_file"]
    assert runs[0].end == "kill" and runs[1].end == "quit"
    assert runs[1].slide_switch == "off" and runs[1].start_step is not None


def test_sleep_wake_splits_script(tmp_path):
    sc = load(tmp_path, "sleepy", """
        description = "sleep"
        steps = [
          { wait_log = "Reader" },
          { sleep = true },
          { wait_log = "Entering deep sleep" },
          { wake = "POWER" },
          { wait_log = "Loaded cache" },
        ]
    """)
    [run] = sc.compiled("/s")
    assert run.script == ["0:WAIT:Reader", "0:SLEEP", "120:WAIT:Entering deep sleep", "0:POWER:80"]
    assert run.script_after_wake == ["0:WAIT:Loaded cache"]


BAD = [
    ('steps = [ { press = "DOWN", tap = [1, 2] } ]', "step 1: each step needs exactly one action"),
    ('steps = [ { jump = 1 } ]', "step 1: each step needs exactly one action"),
    ('steps = [ { press = "DOWN", hold = 3 } ]', "step 1 (press): unknown option(s) hold; allowed: delay_ms, hold_ms"),
    ('steps = [ { press = "BACK" } ]', "does not exist on the Picco profile"),
    ('steps = [ { press = "SPACE" } ]', "unknown key 'SPACE'"),
    ('steps = [ { tap = [1] } ]', "tap takes [x, y]"),
    ('steps = [ { tap = [900, 10] } ]', "outside the 480x800"),
    ('steps = [ { tap = [1, 10] } ]', "coordinate value 1"),
    ('steps = [ { swipe = [400, 400, 80, 1] } ]', "read by the simulator as the far edge"),
    ('steps = [ { press = "LEFT" } ]', "does not exist on the Picco profile"),
    ('steps = [ { press = "HOME" } ]', "does not exist on the Picco profile"),
    ('steps = [ { press = "NEXT" } ]', "does not exist on the Picco profile"),
    ('steps = [ { wait_log = "(" } ]', "bad regular expression"),
    ('steps = [ { wait_log = "x", timeout_ms = 0 } ]', "timeout_ms must be an integer >= 1"),
    ('steps = [ { press = "DOWN" } ]', "the last in-process step must be a wait or shot"),
    ('steps = [ { press = "DOWN" }, { kill = true } ]',
     "kill must come after a wait_log, wait_screen or shot step"),
    ('steps = [ { wait_log = "a" }, { expect_file = "x" }, { press = "DOWN" }, { wait_log = "b" } ]',
     "step 2 (expect_file) is a harness-side check, which may only come after the last in-process step"),
    ('steps = [ { wait_log = "a" }, { kill = true }, { press = "DOWN" } ]', "add { start = true } first"),
    ('steps = [ { wait_log = "a" }, { start = true } ]', "already running"),
    ('steps = [ { shot = "a" }, { shot = "a" } ]', "used twice"),
    ('steps = [ { shot = "a" }, { compare = "b" } ]', "no earlier shot named 'b'"),
    ('steps = [ { shot = "A b" } ]', "shot name must match"),
    ('steps = [ { shot = "a" }, { compare = "a", same_as = "a" } ]', "same_as must name another shot"),
    ('steps = [ { press = "DOWN" }, { screen_has = "x" } ]', "put one directly before it"),
    ('steps = [ { wake = "POWER" } ]', "wake must follow sleep"),
    ('steps = [ { wait_log = "a" }, { sleep = true }, { wake = "POWER" } ]', "put a wait_log for the sleep"),
    ('steps = [ { wait_log = "a" }, { sleep = true }, { shot = "z" } ]', "does not capture screenshots"),
    ('steps = [ { expect_json = "a.json" } ]', "expect_json needs equals"),
    ('steps = [ { expect_file = "../etc/passwd" } ]', "must stay inside the SD card"),
    ('steps = [ { expect_schema = "a.json", schema = "nope.json" } ]', "schema file nope.json not found"),
    ('steps = []', "steps must be a non-empty list"),
    ('sd = [ { src = "fixtures/none.epub", dst = "books/a.epub" } ]\nsteps = [ { wait_log = "a" } ]',
     "src fixtures/none.epub not found"),
    ('sd = [ { dst = "books/a.epub" } ]\nsteps = [ { wait_log = "a" } ]', "give exactly one of src"),
    ('env = { vars = { CROSSPOINT_SIM_SD = "/x" } }\nsteps = [ { wait_log = "a" } ]', "is set by the harness"),
    ('env = { vars = { PATH = "/x" } }\nsteps = [ { wait_log = "a" } ]', "only CROSSPOINT_SIM_* and SDL_*"),
    ('env = { slide_switch = "up" }\nsteps = [ { wait_log = "a" } ]', 'must be "on" or "off"'),
    ('stepz = []', "unknown key(s) stepz"),
    ('steps = [ { wait_log = "a" ', "TOML"),
]


@pytest.mark.parametrize("body,message", BAD)
def test_bad_scenarios_have_clear_messages(tmp_path, body, message):
    text = 'description = "bad"\n' + body + "\n"
    with pytest.raises(ScenarioError) as info:
        load(tmp_path, "bad", text)
    assert message in str(info.value)
    assert "bad/scenario.toml" in str(info.value)


def test_missing_description(tmp_path):
    with pytest.raises(ScenarioError, match="description"):
        load(tmp_path, "nodesc", 'steps = [ { wait_log = "a" } ]\n')


def test_bad_folder_name(tmp_path):
    with pytest.raises(ScenarioError, match="must match"):
        load(tmp_path, "Bad-Name", 'description = "x"\nsteps = [ { wait_log = "a" } ]\n')


def test_sd_text_and_json_entries(tmp_path):
    sc = load(tmp_path, "seeded", """
        description = "seeded settings"
        sd = [
          { text = "hello", dst = "/notes/a.txt" },
          { json = { fontSize = 2, list = [1, 2] }, dst = ".crosspoint/settings.json" },
        ]
        steps = [ { wait_log = "a" } ]
    """)
    assert sc.sd[0].dst == "notes/a.txt" and sc.sd[0].content == b"hello"
    assert b'"fontSize": 2' in sc.sd[1].content
