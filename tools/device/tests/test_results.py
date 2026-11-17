"""results: the device test results files (/rsvp-bench/) against the schema and the consistency rules.

The valid files are real firmware output: the simulator scenarios devicetest_sweep,
devicetest_sweep_keys and devicetest_controls wrote them (docs/evidence/d3a/). Each test below breaks
one rule and checks the validator says so.
"""

import copy
import json
import os
import subprocess
import sys

import jsonschema
import pytest

from piccoflash import results

HERE = os.path.dirname(os.path.abspath(__file__))
DEVICE = os.path.dirname(HERE)
EVIDENCE = os.path.join(DEVICE, "..", "..", "docs", "evidence", "d3a")
TOUCH = os.path.join(EVIDENCE, "devicetest_sweep-0001-bench.json")
KEYS = os.path.join(EVIDENCE, "devicetest_sweep_keys-0001-bench.json")
CONTROLS = os.path.join(EVIDENCE, "devicetest_controls-0001-controls.json")


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def problems(doc):
    return results.schema_problems(doc) or results.consistency_problems(doc)


def test_schema_is_valid_draft_2020_12():
    jsonschema.Draft202012Validator.check_schema(results.load_schema())


@pytest.mark.parametrize("path", [TOUCH, KEYS, CONTROLS])
def test_simulator_files_are_valid(path):
    assert problems(load(path)) == []


def test_cli_validates_and_describes(capsys):
    assert results.main(["validate", TOUCH, CONTROLS]) == 0
    out = capsys.readouterr().out
    assert "OK " in out and "trial-a skipped-part-way from word-band" in out
    assert "controls check run 1, complete, 8 events" in out


def test_cli_as_a_module():
    proc = subprocess.run([sys.executable, "-m", "piccoflash.results", "validate", KEYS], cwd=DEVICE,
                          capture_output=True, text=True)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert "skipped-part-way from playback-240-1" in proc.stdout


def test_cli_usage_and_unreadable_file(tmp_path, capsys):
    assert results.main([]) == 2
    assert results.main(["check", TOUCH]) == 2
    bad = tmp_path / "0001-bench.json"
    bad.write_text("{not json")
    assert results.main(["validate", str(bad)]) == 1
    assert "cannot read" in capsys.readouterr().out


def test_the_files_hold_the_plans_counts():
    doc = load(TOUCH)
    stock = doc["profiles"][0]
    steps = {s["step"]: s for s in stock["steps"]}
    assert steps["word-band"]["updates"]["count"] == 100
    assert steps["full-screen"]["updates"]["count"] == 10
    assert steps["clearing"]["clears"]["count"] == 5
    for key in ("playback-120-1", "playback-180-1", "playback-240-1", "playback-300-1", "playback-240-2",
                "playback-240-3"):
        assert steps[key]["playback"]["words_shown"] == 150
        assert steps[key]["playback"]["order"]["in_order"] is True
        assert steps[key]["rating"] in ("clean", "tolerable", "unreadable")
    assert steps["endurance"]["playback"]["frames"] == 300
    assert steps["endurance"]["residue"] in ("yes", "no")
    assert doc["trial_setting"]["chosen_after"] == "stock"


def broken(path, change):
    doc = copy.deepcopy(load(path))
    change(doc)
    return problems(doc)


def test_unknown_or_missing_members_are_refused():
    assert broken(TOUCH, lambda d: d["profiles"][0]["steps"][0].update(extra=1))
    assert broken(TOUCH, lambda d: d["conditions"].pop("room_temperature_c"))
    assert broken(CONTROLS, lambda d: d["events"][0].pop("held_ms"))
    assert broken(TOUCH, lambda d: d.update(version=2))


def test_a_finished_series_must_hold_the_planned_count():
    def word_band_99(d):
        d["profiles"][0]["steps"][0]["updates"]["count"] = 99

    out = broken(TOUCH, word_band_99)
    assert any("word updates but" in p or "word-band updates" in p for p in out), out

    def short_passage(d):
        d["profiles"][0]["steps"][3]["playback"]["words_shown"] = 149

    assert broken(TOUCH, short_passage)


def test_a_finished_rated_step_needs_its_rating_and_endurance_its_residue():
    assert broken(TOUCH, lambda d: d["profiles"][0]["steps"][4].update(rating=None))
    assert broken(TOUCH, lambda d: d["profiles"][0]["steps"][9].update(residue=None))


def test_a_skipped_profile_is_skipped_from_one_step_on():
    def done_after_skip(d):
        d["profiles"][1]["steps"][5]["status"] = "done"

    assert broken(KEYS, done_after_skip)
    assert broken(KEYS, lambda d: d["profiles"][1].update(skipped_from=None))
    assert broken(TOUCH, lambda d: d["profiles"][3].update(reason=None))


def test_profiles_and_steps_are_in_the_plans_order():
    def swap(d):
        d["profiles"][1], d["profiles"][2] = d["profiles"][2], d["profiles"][1]

    assert broken(TOUCH, swap)
    assert broken(TOUCH, lambda d: d["profiles"][0].update(status="not-allowed", reason="refused"))


def test_complete_means_nothing_left_to_run():
    assert broken(TOUCH, lambda d: d.update(status="complete"))


def test_controls_events_are_consistent():
    def short_hold(d):
        d["events"][1]["held_ms"] = 300  # a "hold" below hold_ms

    assert broken(CONTROLS, short_hold)

    def renumbered(d):
        d["events"][2]["n"] = 7

    assert broken(CONTROLS, renumbered)
    assert broken(CONTROLS, lambda d: d.update(file="0002-controls.json"))
