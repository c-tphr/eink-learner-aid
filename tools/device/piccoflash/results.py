"""results: check the device test results files from /rsvp-bench/ on the Picco's SD card.

    python -m piccoflash.results validate <file> [<file> ...]

(from tools/device, or with tools/device on PYTHONPATH). Each file is checked against
schema/rsvp-bench.schema.json (needs the jsonschema package, as in requirements-test.txt) and then
against the rules the schema cannot express: a step that finished holds exactly the counts the plan
asks for, a skipped profile is skipped from one step on, every finished rated step has its rating.
The format is described member by member in schema/rsvp-bench.md. Exit status: 0 every file is
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
SUPPORTED_VERSIONS = (1,)

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
    return out


def describe(doc: Dict[str, Any]) -> str:
    """One line about a valid file."""
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
