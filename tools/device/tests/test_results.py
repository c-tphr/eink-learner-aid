"""results: the device test results files (/rsvp-bench/) against the schema and the consistency rules.

The valid files are real firmware output: the simulator scenarios devicetest_sweep,
devicetest_sweep_keys and devicetest_controls wrote them (docs/evidence/d3a/, format version 1), and
devicetest_acceptance, devicetest_acceptance_keys and devicetest_choose_profile (docs/evidence/d3b/,
version 2). Each test below breaks one rule and checks the validator says so.
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
    assert broken(TOUCH, lambda d: d.update(version=3))


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


# --- Version 2: the guided acceptance and the profile choice (task D3b) -----------------------------

EVIDENCE_D3B = os.path.join(DEVICE, "..", "..", "docs", "evidence", "d3b")
ACCEPTANCE = os.path.join(EVIDENCE_D3B, "devicetest_acceptance-0001-acceptance.json")
ACCEPTANCE_KEYS = os.path.join(EVIDENCE_D3B, "devicetest_acceptance_keys-0001-acceptance.json")
CHOICE_TRIAL = os.path.join(EVIDENCE_D3B, "devicetest_choose_profile-0002-choice.json")
CHOICE_STOCK = os.path.join(EVIDENCE_D3B, "devicetest_choose_profile-0003-choice.json")


@pytest.mark.parametrize("path", [ACCEPTANCE, ACCEPTANCE_KEYS, CHOICE_TRIAL, CHOICE_STOCK])
def test_d3b_simulator_files_are_valid(path):
    assert load(path)["version"] == 2
    assert problems(load(path)) == []


def test_version_1_files_still_validate_and_new_kinds_need_version_2():
    assert load(TOUCH)["version"] == 1
    assert problems(load(TOUCH)) == []
    assert broken(ACCEPTANCE, lambda d: d.update(version=1))
    assert broken(CHOICE_TRIAL, lambda d: d.update(version=1))


def test_cli_describes_acceptance_and_choice(capsys):
    assert results.main(["validate", ACCEPTANCE, CHOICE_TRIAL, CHOICE_STOCK]) == 0
    out = capsys.readouterr().out
    assert "guided acceptance run 1, complete, profile stock, part A done, 12 of 12 steps answered" in out
    assert "criterion 5 measured pass, owner pass" in out
    assert "profile choice run 2, trial-b chosen, kept trial-b, trial-c, byte 15 -> 38" in out
    assert "profile choice run 3, stock chosen, kept none, byte 54 -> 0" in out


def test_acceptance_records_both_runs_every_word_and_the_answers():
    doc = load(ACCEPTANCE)
    for run, target in zip(doc["part_a"]["runs"], (240, 600)):
        assert run["target_wpm"] == target
        assert run["playback"]["order"]["words_drawn"] == 150
        assert run["playback"]["order"]["in_order"] is True
        assert run["playback"]["delivered_wpm"] <= target
        assert run["measured_ok"] is True
        assert run["readable"] == "yes"
    assert doc["part_a"]["runs"][1]["attempts"] == 2  # stopped once, repeated
    assert [c["result"] for c in doc["criteria"]] == ["fail", "pass", "pass", "pass", "pass"]
    assert doc["part_b"]["steps"][1]["reason"] == "wrong-mode"


def test_acceptance_verdicts_must_agree_with_the_runs_and_answers():
    def dropped_word(d):
        d["part_a"]["runs"][1]["playback"]["order"]["words_drawn"] = 149
        d["part_a"]["runs"][1]["playback"]["order"]["gaps"] = 1

    out = broken(ACCEPTANCE, dropped_word)
    assert any("measured_ok" in p for p in out), out
    assert broken(ACCEPTANCE, lambda d: d["criterion_5"].update(owner_pass=False))
    assert broken(ACCEPTANCE, lambda d: d["part_a"]["runs"][0].update(readable="no"))
    assert broken(ACCEPTANCE, lambda d: d["criteria"][0].update(result="pass"))


def test_acceptance_steps_are_answered_in_order():
    assert broken(ACCEPTANCE, lambda d: d["part_b"]["steps"][4].update(result="not-done", answers=0))
    assert broken(ACCEPTANCE, lambda d: d["part_b"]["steps"][1].update(reason=None))
    assert broken(ACCEPTANCE, lambda d: d["part_b"]["steps"][0].update(reason="other"))
    assert broken(ACCEPTANCE, lambda d: d["part_b"].update(current="toc-jump"))
    assert broken(ACCEPTANCE, lambda d: d.update(status="running"))
    assert broken(ACCEPTANCE, lambda d: d["part_b"]["steps"][3].update(n=9))


def test_choice_byte_agrees_with_what_was_chosen_and_kept():
    doc = load(CHOICE_TRIAL)
    assert doc["trial_setting"]["after"] == 0x26
    assert doc["kept_after"] == ["trial-b", "trial-c"]
    assert broken(CHOICE_TRIAL, lambda d: d.update(chosen="trial-c"))
    assert broken(CHOICE_TRIAL, lambda d: d["trial_setting"].update(after=0x36))
    assert broken(CHOICE_TRIAL, lambda d: d.update(locked=["trial-a"]))
    assert broken(CHOICE_TRIAL, lambda d: d["candidates"][1].update(kept=True))
    assert broken(CHOICE_STOCK, lambda d: d["trial_setting"].update(after=6, unlocked_after=["trial-b", "trial-c"]))
