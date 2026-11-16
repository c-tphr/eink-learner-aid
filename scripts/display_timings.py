#!/usr/bin/env python3
"""Validate the Picco display timings file and turn it into the simulator's generated header.

docs/device/picco-timings.json holds the refresh times measured in the device session (format:
tools/device/schema/picco-timings.schema.json). The simulator build runs this module through
scripts/gen_display_timings.py before compiling:

  - no file:      a header that says "no file", and the simulator keeps its built-in defaults;
  - a valid file: a header with each listed profile's word-update and clear medians;
  - anything else (bad JSON, a schema error, a duplicate member, a fraction, NaN, an unknown
    profile id, p95 below the median, an example file): the build stops with the reason.
    Nothing is half-applied.

Command line, for whoever writes the file after the device session:

  python3 scripts/display_timings.py check [FILE]       # default docs/device/picco-timings.json
  python3 scripts/display_timings.py check --example docs/device/picco-timings.example.json
  python3 scripts/display_timings.py header OUT.h [FILE]

Only the standard library and tools/simtest/jsonchecks.py (also standard library only) are used.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.simtest import jsonchecks  # noqa: E402

TIMINGS_PATH = "docs/device/picco-timings.json"
SCHEMA_PATH = "tools/device/schema/picco-timings.schema.json"
HEADER_NAME = "DisplayTimingsFile.generated.h"

# Profile key -> displayprofile::ProfileId enumerator, in enum order (DisplayProfiles.h).
PROFILE_IDS = {
    "stock": "STOCK",
    "trial-a": "TRIAL_A",
    "trial-b": "TRIAL_B",
    "trial-c": "TRIAL_C",
    "trial-d": "TRIAL_D",
}


class TimingsError(Exception):
    """The timings file is malformed; the message names the file and the problem."""


def _no_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    seen: dict[str, Any] = {}
    for key, value in pairs:
        if key in seen:
            raise TimingsError(f"duplicate member {key!r}")
        seen[key] = value
    return seen


def _no_constant(name: str) -> Any:
    raise TimingsError(f"{name} is not a valid time")


def _no_fraction(text: str) -> Any:
    raise TimingsError(f"{text} is not a whole number of milliseconds")


def parse(text: str, where: str) -> Any:
    """Strict JSON: no duplicate members, no fractions or exponents, no NaN or Infinity."""
    try:
        return json.loads(text, object_pairs_hook=_no_duplicates, parse_constant=_no_constant,
                          parse_float=_no_fraction)
    except TimingsError as e:
        raise TimingsError(f"{where}: {e}") from None
    except json.JSONDecodeError as e:
        raise TimingsError(f"{where}: not valid JSON: {e}") from None


def _schema() -> Any:
    return json.loads((REPO_ROOT / SCHEMA_PATH).read_text(encoding="utf-8"))


def problems(doc: Any, allow_example: bool = False) -> list[str]:
    """Every problem with a parsed document; empty when it is a usable timings file."""
    out = jsonchecks.validate(doc, _schema())
    if out:
        return out
    if doc.get("example") is True and not allow_example:
        out.append("/example: this is an example file; the simulator only takes real measurements "
                   "(see docs/device/picco-timings.example.json)")
    if doc["panel"] == "XteinkX4" and not allow_example:
        out.append("/panel: XteinkX4 is allowed only in the example; name the Picco panel measured")
    for key, profile in doc["profiles"].items():
        update = profile["wordUpdateMs"]
        median, p95, high = update["median"], update["p95"], update["max"]
        if p95 is not None and p95 < median:
            out.append(f"/profiles/{key}/wordUpdateMs: p95 {p95} is below the median {median}")
        if high is not None and high < median:
            out.append(f"/profiles/{key}/wordUpdateMs: max {high} is below the median {median}")
        if high is not None and p95 is not None and high < p95:
            out.append(f"/profiles/{key}/wordUpdateMs: max {high} is below p95 {p95}")
    return out


def load(path: Path, allow_example: bool = False) -> dict[str, Any]:
    """Read, parse and validate a timings file. Raises TimingsError with every problem found."""
    where = str(path)
    try:
        text = path.read_bytes().decode("utf-8")
    except UnicodeDecodeError as e:
        raise TimingsError(f"{where}: not UTF-8: {e}") from None
    doc = parse(text, where)
    found = problems(doc, allow_example)
    if found:
        raise TimingsError(f"{where}: " + "; ".join(found))
    return doc


def _c_string(text: str) -> str:
    out = []
    for byte in text.encode("utf-8"):
        ch = chr(byte)
        if ch in '"\\':
            out.append("\\" + ch)
        elif 0x20 <= byte < 0x7F:
            out.append(ch)
        else:
            out.append(f"\\{byte:03o}")
    return '"' + "".join(out) + '"'


def render_header(doc: dict[str, Any] | None, source: str = TIMINGS_PATH) -> str:
    """The generated C++ header: a displayprofile::TimingFile named generated::TIMINGS_FILE."""
    lines = [
        "// THIS FILE IS AUTOGENERATED by scripts/gen_display_timings.py, DO NOT EDIT.",
        f"// From {source}" + (" (absent: the simulator keeps its built-in defaults)." if doc is None else "."),
        "#pragma once",
        "",
        "#include <DisplayTiming.h>",
        "",
        "namespace displayprofile::generated {",
        "",
        f"inline constexpr char TIMINGS_PATH[] = {_c_string(source)};",
    ]
    if doc is None:
        lines += [
            "// No file: count 0. The entry only gives the array a size.",
            "inline constexpr FileProfileTiming TIMINGS_PROFILES[] = {{ProfileId::STOCK, 0, 0}};",
            'inline constexpr TimingFile TIMINGS_FILE{TIMINGS_PROFILES, 0, "", "", ""};',
        ]
    else:
        entries = [
            f"    {{ProfileId::{PROFILE_IDS[key]}, {doc['profiles'][key]['wordUpdateMs']['median']}, "
            f"{doc['profiles'][key]['clearMs']['median']}}},  // {key}"
            for key in PROFILE_IDS if key in doc["profiles"]
        ]
        lines += [
            "inline constexpr FileProfileTiming TIMINGS_PROFILES[] = {",
            *entries,
            "};",
            f"inline constexpr TimingFile TIMINGS_FILE{{TIMINGS_PROFILES, {len(entries)}, {_c_string(doc['panel'])},",
            f"                                        {_c_string(doc['source'])},",
            f"                                        {_c_string(doc['date'])}}};",
        ]
    lines += ["", "}  // namespace displayprofile::generated", ""]
    return "\n".join(lines)


def generate(project_dir: Path, out_dir: Path) -> tuple[Path, str]:
    """Write the header for project_dir's timings file into out_dir (only if its text changed).

    Returns the header path and a one-line summary. Raises TimingsError on a malformed file."""
    timings = project_dir / TIMINGS_PATH
    doc = load(timings) if timings.exists() else None
    header = out_dir / HEADER_NAME
    text = render_header(doc)
    out_dir.mkdir(parents=True, exist_ok=True)
    if not header.exists() or header.read_text(encoding="utf-8") != text:
        header.write_text(text, encoding="utf-8")
    if doc is None:
        summary = f"{TIMINGS_PATH} absent: built-in defaults"
    else:
        listed = ", ".join(
            f"{k} {v['wordUpdateMs']['median']}/{v['clearMs']['median']} ms" for k, v in doc["profiles"].items())
        summary = f"{TIMINGS_PATH} ({doc['panel']}, {doc['date']}): {listed}"
    return header, summary


def main(argv: list[str]) -> int:
    args = list(argv)
    if not args or args[0] not in ("check", "header"):
        print(__doc__, file=sys.stderr)
        return 2
    command = args.pop(0)
    try:
        if command == "check":
            allow_example = "--example" in args
            args = [a for a in args if a != "--example"]
            path = Path(args[0]) if args else REPO_ROOT / TIMINGS_PATH
            doc = load(path, allow_example)
            print(f"{path}: OK, panel {doc['panel']}, profiles {', '.join(doc['profiles'])}")
            return 0
        if not args:
            print("header: give the output path", file=sys.stderr)
            return 2
        out = Path(args[0])
        path = Path(args[1]) if len(args) > 1 else REPO_ROOT / TIMINGS_PATH
        doc = load(path) if path.exists() else None
        out.write_text(render_header(doc), encoding="utf-8")
        print(f"wrote {out}")
        return 0
    except TimingsError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
