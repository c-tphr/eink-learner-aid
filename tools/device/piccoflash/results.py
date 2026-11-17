"""results: check the device test results files from /rsvp-bench/ on the Picco's SD card.

    python -m piccoflash.results validate <file> [<file> ...]

(from tools/device, or with tools/device on PYTHONPATH). Each file is checked against
schema/rsvp-bench.schema.json (needs the jsonschema package, as in requirements-test.txt) and then
against the rules the schema cannot express: a step that finished holds exactly the counts the plan
asks for, a skipped profile is skipped from one step on, every finished rated step has its rating;
in a guided acceptance the verdicts agree with the runs and the answers, and a profile choice's
stored byte agrees with what was chosen and kept. The format is described member by member in schema/rsvp-bench.md. Exit status: 0 every file is
valid, 1 a file is not, 2 a usage problem (or jsonschema is missing).

Nothing here talks to a device; it only reads the files given.
"""

from __future__ import annotations

import json
import os
import sys
from typing import Any, Dict, List, Optional

SCHEMA_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "schema",
                           "rsvp-bench.schema.json")
SUPPORTED_VERSIONS = (1, 2)  # 2 added the acceptance and choice files; benchmark and controls are unchanged

PROFILE_ORDER = ("stock", "trial-a", "trial-b", "trial-c", "trial-d")
STEP_ORDER = ("word-band", "full-screen", "clearing", "playback-120-1", "playback-180-1", "playback-240-1",
              "playback-300-1", "playback-240-2", "playback-240-3", "endurance")
RATED = {"playback", "endurance"}
FINISHED = ("done", "unrated")


def load_schema() -> Dict[str, Any]:
    with open(SCHEMA_PATH, encoding="utf-8") as f:
        return json.load(f)


def schema_problems(doc: Any, schema: Optional[Dict[str, Any]] = None) -> List[str]:
    """Messages from the JSON Schema (empty: valid). Needs jsonschema."""
    import jsonschema  # deferred: the consistency checks below need only the standard library

    validator = jsonschema.Draft202012Validator(schema or load_schema())
    out = []
    for err in sorted(validator.iter_errors(doc), key=lambda e: list(e.absolute_path)):
        where = "/" + "/".join(str(p) for p in err.absolute_path)
        out.append("%s: %s" % (where, err.message))
    return out


def _series_count(step: Dict[str, Any], name: str) -> Optional[int]:
    series = step.get(name)
    return series.get("count") if isinstance(series, dict) else None


def _step_problems(where: str, step: Dict[str, Any]) -> List[str]:
    out: List[str] = []
    status = step.get("status")
    kind = step.get("kind")
    requested = step.get("requested")
    has_figures = step.get("updates") is not None
    if status in FINISHED + ("stopped",):
        if not has_figures:
            out.append("%s: a %s step has no figures" % (where, status))
            return out
        if step.get("attempts", 0) < 1:
            out.append("%s: a %s step has no attempt" % (where, status))
    elif has_figures and status != "pending":
        out.append("%s: a %s step has figures" % (where, status))

    if status in ("pending", "skipped"):
        if step.get("rating") is not None or step.get("residue") is not None:
            out.append("%s: a %s step has a rating" % (where, status))
        return out
    if not has_figures:
        return out

    updates = _series_count(step, "updates")
    clears = _series_count(step, "clears")
    playback = step.get("playback")
    finished = status in FINISHED
    if kind == "full-screen":
        if finished and updates != requested:
            out.append("%s: %s full-screen updates, the plan asks for %s" % (where, updates, requested))
        if clears != 0:
            out.append("%s: %s clearing refreshes during the full-screen series" % (where, clears))
    elif kind == "clearing":
        if finished and clears != requested:
            out.append("%s: %s clearing refreshes, the plan asks for %s" % (where, clears, requested))
        if updates != 0:
            out.append("%s: %s updates during the clearing series" % (where, updates))
    else:
        if not isinstance(playback, dict):
            out.append("%s: no playback figures for a %s step" % (where, kind))
            return out
        order = playback.get("order", {})
        if updates != playback.get("frames"):
            out.append("%s: %s word updates but %s frames" % (where, updates, playback.get("frames")))
        if clears != playback.get("clearing_refreshes"):
            out.append("%s: %s clears but the scheduler ran %s" % (where, clears, playback.get("clearing_refreshes")))
        if finished:
            for name, value in (("words shown", playback.get("words_shown")),
                                ("words drawn", order.get("words_drawn"))):
                if value != requested:
                    out.append("%s: %s %s, the plan asks for %s" % (where, value, name, requested))
            if kind == "word-band" and updates != requested:
                out.append("%s: %s word-band updates, the plan asks for %s" % (where, updates, requested))
    if status == "stopped":
        done = updates if kind != "clearing" else clears
        if done is not None and requested is not None and done > requested:
            out.append("%s: a stopped series has more updates (%s) than planned (%s)" % (where, done, requested))
    if status == "done" and kind in RATED:
        if step.get("rating") is None:
            out.append("%s: a finished rated step has no rating" % where)
        if kind == "endurance" and step.get("residue") is None:
            out.append("%s: the finished endurance run has no residue answer" % where)
    return out


def _benchmark_problems(doc: Dict[str, Any]) -> List[str]:
    out: List[str] = []
    profiles = doc.get("profiles", [])
    keys = tuple(p.get("key") for p in profiles)
    if keys != PROFILE_ORDER:
        out.append("/profiles: expected %s in this order, got %s" % (", ".join(PROFILE_ORDER), ", ".join(map(str, keys))))
        return out
    if profiles[0].get("status") == "not-allowed":
        out.append("/profiles/0: Stock is always allowed")
    for i, p in enumerate(profiles):
        where = "/profiles/%d (%s)" % (i, p.get("key"))
        steps = p.get("steps", [])
        if tuple(s.get("step") for s in steps) != STEP_ORDER:
            out.append("%s: the steps are not the plan's ten in order" % where)
            continue
        statuses = [s.get("status") for s in steps]
        status = p.get("status")
        if status in ("not-allowed", "not-run"):
            if p.get("reason") is None:
                out.append("%s: %s without a reason" % (where, status))
            if any(s != "skipped" for s in statuses):
                out.append("%s: a %s profile has steps that are not skipped" % (where, status))
        elif p.get("reason") is not None:
            out.append("%s: a reason is given for a %s profile" % (where, status))
        if status == "done" and any(s != "done" for s in statuses):
            out.append("%s: a done profile has steps that are not done" % where)
        if status == "skipped-part-way":
            start = p.get("skipped_from")
            if start not in STEP_ORDER:
                out.append("%s: skipped part-way but no skipped_from step" % where)
            else:
                k = STEP_ORDER.index(start)
                if any(s not in FINISHED for s in statuses[:k]):
                    out.append("%s: a step before %s did not finish" % (where, start))
                if statuses[k] not in ("stopped", "skipped"):
                    out.append("%s: %s is %s, not stopped or skipped" % (where, start, statuses[k]))
                if any(s != "skipped" for s in statuses[k + 1:]):
                    out.append("%s: a step after %s is not skipped" % (where, start))
        elif p.get("skipped_from") is not None:
            out.append("%s: skipped_from given for a %s profile" % (where, status))
        for j, s in enumerate(steps):
            out += _step_problems("%s/steps/%d (%s)" % (where, j, s.get("step")), s)
    if doc.get("status") == "complete" and any(p.get("status") in ("pending", "running", "not-run") for p in profiles):
        out.append("/status: complete, but a profile is still pending, running or not run")
    return out


def _controls_problems(doc: Dict[str, Any]) -> List[str]:
    out: List[str] = []
    settings = doc.get("settings", {})
    hold_ms = settings.get("hold_ms", 0)
    exit_ms = settings.get("exit_hold_ms", 0)
    for i, e in enumerate(doc.get("events", [])):
        where = "/events/%d" % i
        if e.get("n") != i + 1:
            out.append("%s: numbered %s" % (where, e.get("n")))
        key = e.get("source") == "key"
        if key != (e.get("raw") is not None) or key != (e.get("held_ms") is not None):
            out.append("%s: a %s event with raw=%s held_ms=%s" % (where, e.get("source"), e.get("raw"), e.get("held_ms")))
            continue
        if not key:
            continue
        held = e.get("held_ms")
        kind = e.get("kind")
        if kind == "hold" and held < hold_ms:
            out.append("%s: a hold of %s ms is shorter than %s ms" % (where, held, hold_ms))
        if kind == "short" and held >= hold_ms and e.get("raw_name") != "BTN_POWER":
            out.append("%s: a short press of %s ms" % (where, held))
        if kind == "exit-hold" and held < exit_ms:
            out.append("%s: an exit hold of %s ms" % (where, held))
        if kind not in ("short", "hold", "exit-hold"):
            out.append("%s: a key event of kind %s" % (where, kind))
    return out


# --- Guided acceptance and profile choice (version 2, task D3b) ------------------------------------

TRIALS = ("trial-a", "trial-b", "trial-c", "trial-d")


def _combine(results: List[str]) -> str:
    """A criterion's result over its evidence: any fail, else any not done, else any skipped, else pass."""
    for r in ("fail", "not-done", "skipped"):
        if r in results:
            return r
    return "pass"


def _run_measured_ok(run: Dict[str, Any]) -> bool:
    p = run.get("playback") or {}
    order = p.get("order") or {}
    requested = run.get("requested")
    delivered = p.get("delivered_wpm") or 0
    return (order.get("in_order") is True and order.get("words_drawn") == requested and
            p.get("words_shown") == requested and order.get("violations") == 0 and order.get("gaps") == 0 and
            order.get("repeats") == 0 and 0 < delivered <= run.get("target_wpm", 0))


def _acceptance_problems(doc: Dict[str, Any]) -> List[str]:
    out: List[str] = []
    part_a = doc["part_a"]
    runs = part_a["runs"]
    for i, run in enumerate(runs):
        where = "/part_a/runs/%d (%s)" % (i, run["run"])
        status = run["status"]
        has_figures = run["playback"] is not None
        if status == "done":
            if not has_figures or run["attempts"] < 1:
                out.append("%s: a done run without figures or attempts" % where)
                continue
            p = run["playback"]
            if run["updates"]["count"] != p["frames"]:
                out.append("%s: %s word updates but %s frames" % (where, run["updates"]["count"], p["frames"]))
            if run["measured_ok"] != _run_measured_ok(run):
                out.append("%s: measured_ok is %s, the figures say %s" % (where, run["measured_ok"],
                                                                           _run_measured_ok(run)))
        else:
            if run["measured_ok"] is not None or run["readable"] is not None:
                out.append("%s: a %s run has a verdict or an answer" % (where, status))
            if status == "pending" and has_figures:
                out.append("%s: a pending run has figures" % where)
    if part_a["status"] == "done":
        if any(r["status"] != "done" or r["readable"] is None for r in runs) or part_a["slower"] is None:
            out.append("/part_a: done, but a run or an answer is missing")
    elif part_a["status"] in ("pending", "skipped") and part_a["slower"] is not None:
        out.append("/part_a: %s, but the slowdown was answered" % part_a["status"])

    part_b = doc["part_b"]
    steps = part_b["steps"]
    ids = [s["id"] for s in steps]
    if len(set(ids)) != len(ids):
        out.append("/part_b/steps: an id appears twice")
    for i, s in enumerate(steps):
        where = "/part_b/steps/%d (%s)" % (i, s["id"])
        if s["n"] != i + 1:
            out.append("%s: numbered %s" % (where, s["n"]))
        if (s["result"] == "fail") != (s["reason"] is not None):
            out.append("%s: a %s step with reason %s" % (where, s["result"], s["reason"]))
        if s["result"] != "not-done" and s["answers"] < 1:
            out.append("%s: a %s step without an answer" % (where, s["result"]))
    open_steps = [s for s in steps if s["result"] == "not-done"]
    if part_b["answered"] != len(steps) - len(open_steps):
        out.append("/part_b/answered: %s, but %d steps are answered" % (part_b["answered"],
                                                                         len(steps) - len(open_steps)))
    # Steps are answered in order: the step in progress is the first not answered, and none after it is.
    expected_current = open_steps[0]["id"] if open_steps else None
    if part_b["current"] != expected_current:
        out.append("/part_b/current: %s, expected %s" % (part_b["current"], expected_current))
    if expected_current is not None:
        k = ids.index(expected_current)
        if any(s["result"] != "not-done" for s in steps[k + 1:]):
            out.append("/part_b/steps: a step after %s is answered" % expected_current)
    elif part_b["away"]:
        out.append("/part_b/away: true, but every step is answered")

    complete = part_a["status"] in ("done", "skipped") and expected_current is None
    if (doc["status"] == "complete") != complete:
        out.append("/status: %s, but Part A is %s and %d steps are open" % (doc["status"], part_a["status"],
                                                                             len(open_steps)))

    # Criterion 5: the automatic verdict and the owner's, recomputed.
    if part_a["status"] == "skipped":
        measured = owner = "skipped"
    else:
        measured = owner = "not-done"
        if all(r["status"] == "done" for r in runs):
            measured = "pass" if all(_run_measured_ok(r) for r in runs) else "fail"
        if part_a["status"] == "done":
            owner = "pass" if all(r["readable"] == "yes" for r in runs) and part_a["slower"] == "yes" else "fail"
    c5 = doc["criterion_5"]
    for name, value in (("measured", measured), ("owner", owner)):
        if c5[name] != value:
            out.append("/criterion_5/%s: %s, the runs and answers say %s" % (name, c5[name], value))
        verdict = {"pass": True, "fail": False}.get(value)
        if c5[name + "_pass"] != verdict:
            out.append("/criterion_5/%s_pass: %s, expected %s" % (name, c5[name + "_pass"], verdict))
    by_id = {s["id"]: s for s in steps}
    pause = [s["result"] for s in steps if 5 in s["criteria"]]
    if c5["pause_screen"] != (pause[-1] if pause else "not-done"):
        out.append("/criterion_5/pause_screen: %s, the steps say %s" % (c5["pause_screen"], pause))

    criteria = doc["criteria"]
    for k, c in enumerate(criteria, start=1):
        where = "/criteria/%d" % (k - 1)
        if c["criterion"] != k:
            out.append("%s: criterion %s out of order" % (where, c["criterion"]))
            continue
        expected_evidence = (["part-a"] if k == 5 else []) + [s["id"] for s in steps if k in s["criteria"]]
        if c["evidence"] != expected_evidence:
            out.append("%s: evidence %s, the steps say %s" % (where, c["evidence"], expected_evidence))
        results = [by_id[e]["result"] for e in c["evidence"] if e in by_id]
        if k == 5:
            results += [measured, owner]
        if c["result"] != _combine(results):
            out.append("%s: criterion %d is %s, its evidence says %s" % (where, k, c["result"], _combine(results)))
    return out


def _decode_setting(value: int) -> Dict[str, Any]:
    """lib/DisplayProfileCore/TrialUnlock.h, decodeTrialSetting()."""
    chosen = (value >> 4) & 0x07
    if value & 0x80 or chosen >= 5:
        return {"unlocked": [], "chosen": "stock"}
    unlocked = [t for i, t in enumerate(TRIALS) if value & (1 << i)]
    key = ("stock",) + TRIALS
    return {"unlocked": unlocked, "chosen": key[chosen] if chosen == 0 or key[chosen] in unlocked else "stock"}


def _choice_problems(doc: Dict[str, Any]) -> List[str]:
    out: List[str] = []
    ts = doc["trial_setting"]
    for side in ("before", "after"):
        decoded = _decode_setting(ts[side])
        if decoded["unlocked"] != ts["unlocked_" + side] or decoded["chosen"] != ts["chosen_" + side]:
            out.append("/trial_setting: %s=%s decodes to %s" % (side, ts[side], decoded))
    if ts["chosen_after"] != doc["chosen"]:
        out.append("/chosen: %s, but the stored byte chooses %s" % (doc["chosen"], ts["chosen_after"]))
    if ts["unlocked_after"] != doc["kept_after"]:
        out.append("/kept_after: %s, but %s stay unlocked" % (doc["kept_after"], ts["unlocked_after"]))
    if doc["chosen"] != "stock" and doc["chosen"] not in doc["kept_after"]:
        out.append("/kept_after: the chosen %s is not kept" % doc["chosen"])
    locked = [t for t in ts["unlocked_before"] if t not in ts["unlocked_after"]]
    if doc["locked"] != locked:
        out.append("/locked: %s, expected %s" % (doc["locked"], locked))
    if doc["chosen"] == "stock" and not doc["kept_after"] and ts["after"] != 0:
        out.append("/trial_setting/after: Stock with nothing kept must store 0, not %s" % ts["after"])
    candidates = doc["candidates"]
    keys = [c["key"] for c in candidates]
    order = ("stock",) + TRIALS
    if not keys or keys[0] != "stock" or keys != sorted(keys, key=order.index) or len(set(keys)) != len(keys):
        out.append("/candidates: %s is not Stock then trials in table order" % keys)
    chosen = [c["key"] for c in candidates if c["chosen"]]
    if chosen != [doc["chosen"]]:
        out.append("/candidates: chosen %s, expected only %s" % (chosen, doc["chosen"]))
    for i, c in enumerate(candidates):
        kept = c["key"] == "stock" or c["key"] in doc["kept_after"]
        if c["kept"] != kept:
            out.append("/candidates/%d (%s): kept is %s, expected %s" % (i, c["key"], c["kept"], kept))
        if doc["benchmark_file"] is None and (c["ratings"] is not None or c["delivered_wpm_240"] is not None):
            out.append("/candidates/%d (%s): ratings without a benchmark file" % (i, c["key"]))
        if c["key"] != "stock" and c["key"] not in ts["unlocked_before"]:
            out.append("/candidates/%d (%s): offered but not unlocked before" % (i, c["key"]))
    for t in doc["kept_after"]:
        if t not in keys:
            out.append("/kept_after: %s was not offered" % t)
    return out


def consistency_problems(doc: Any) -> List[str]:
    """The rules beyond the schema (standard library only). Assumes the schema passed."""
    if not isinstance(doc, dict):
        return ["/: not a JSON object"]
    out: List[str] = []
    if doc.get("version") not in SUPPORTED_VERSIONS:
        out.append("/version: %r is not a version this tool reads" % doc.get("version"))
        return out
    name = doc.get("file", "")
    if not name.startswith("%04d-" % doc.get("run", -1)):
        out.append("/file: %s does not carry run number %s" % (name, doc.get("run")))
    if doc.get("kind") == "benchmark":
        out += _benchmark_problems(doc)
    elif doc.get("kind") == "controls":
        out += _controls_problems(doc)
    elif doc.get("kind") == "acceptance":
        out += _acceptance_problems(doc)
    elif doc.get("kind") == "choice":
        out += _choice_problems(doc)
    return out


def describe(doc: Dict[str, Any]) -> str:
    """One line about a valid file."""
    if doc.get("kind") == "acceptance":
        criteria = ", ".join("%d %s" % (c["criterion"], c["result"]) for c in doc["criteria"])
        c5 = doc["criterion_5"]
        return "guided acceptance run %d, %s, profile %s, part A %s, %d of %d steps answered; criteria: %s; " \
               "criterion 5 measured %s, owner %s" % (
                   doc["run"], doc["status"], doc["profile"]["key"], doc["part_a"]["status"],
                   doc["part_b"]["answered"], len(doc["part_b"]["steps"]), criteria, c5["measured"], c5["owner"])
    if doc.get("kind") == "choice":
        return "profile choice run %d, %s chosen, kept %s, byte %d -> %d%s" % (
            doc["run"], doc["chosen"], ", ".join(doc["kept_after"]) or "none", doc["trial_setting"]["before"],
            doc["trial_setting"]["after"], "" if doc["saved"] else " (NOT saved)")
    if doc.get("kind") == "controls":
        s = doc.get("summary", {})
        tried = [k for k in ("up_pressed", "up_held", "down_pressed", "down_held", "confirm_pressed", "power_clicked",
                             "touched") if s.get(k)]
        return "controls check run %d, %s, %d events (%s)" % (doc["run"], doc["status"], len(doc.get("events", [])),
                                                              ", ".join(tried) or "nothing tried")
    parts = []
    for p in doc.get("profiles", []):
        text = "%s %s" % (p["key"], p["status"])
        if p.get("skipped_from"):
            text += " from %s" % p["skipped_from"]
        elif p.get("reason"):
            text += " (%s)" % p["reason"]
        parts.append(text)
    temp = doc["conditions"]["room_temperature_c"]
    return "benchmark run %d, %s, panel %s, %s, profiles: %s" % (
        doc["run"], doc["status"], doc["device"]["panel"], "unknown temperature" if temp is None else "%d C" % temp,
        "; ".join(parts))


def validate_file(path: str, schema: Optional[Dict[str, Any]] = None) -> List[str]:
    try:
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
    except (OSError, ValueError) as exc:
        return ["cannot read: %s" % exc]
    problems = schema_problems(doc, schema)
    if problems:
        return problems
    return consistency_problems(doc)


def main(argv: Optional[List[str]] = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    if len(args) < 2 or args[0] != "validate":
        print("usage: python -m piccoflash.results validate <file> [<file> ...]", file=sys.stderr)
        return 2
    try:
        import jsonschema  # noqa: F401
    except ImportError:
        print("the jsonschema package is needed: python -m pip install -r tools/device/requirements-test.txt",
              file=sys.stderr)
        return 2
    schema = load_schema()
    bad = 0
    for path in args[1:]:
        problems = validate_file(path, schema)
        if problems:
            bad += 1
            print("INVALID %s" % path)
            for p in problems:
                print("  " + p)
        else:
            with open(path, encoding="utf-8") as f:
                print("OK %s: %s" % (path, describe(json.load(f))))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
