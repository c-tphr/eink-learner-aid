#!/usr/bin/env python3
"""PreToolUse hook of the ``prep-checker`` subagent: keep its reads inside the checker folder.

``.claude/agents/prep-checker.md`` runs this before every Read, Glob and
Grep call of the checker. It reads the hook input (JSON) on stdin and exits

- 0 when the call only touches files inside a checker folder: a folder that
  holds a ``book.json`` whose ``format`` is ``rsvp-checker-input`` (written by
  ``python3 -m tools.prep checker-input``);
- 2, with the reason on stderr, otherwise. Claude Code then blocks the call
  and shows the reason to the checker.

So the checker cannot open the draft (with its citations and notes), earlier
``check.json`` files, the round history or anything else in the repository,
even by mistake. Glob and Grep must name the checker folder as ``path`` and
may not use absolute or ``..`` patterns. Any other tool is not this hook's
business (the agent's ``tools`` list already allows only these three).

Standard library only, so the hook runs in any Python 3.
"""

from __future__ import annotations

import json
import os
import sys

FORMAT = "rsvp-checker-input"
GUARDED = ("Read", "Glob", "Grep")


def checker_root(path: str) -> str | None:
    """The checker folder that contains ``path`` (after resolving symlinks and ``..``), else ``None``."""
    real = os.path.realpath(path)
    here = real if os.path.isdir(real) else os.path.dirname(real)
    while True:
        marker = os.path.join(here, "book.json")
        if os.path.isfile(marker):
            try:
                with open(marker, encoding="utf-8") as f:
                    if json.load(f).get("format") == FORMAT:
                        return here
            except (OSError, ValueError, AttributeError):
                pass
        parent = os.path.dirname(here)
        if parent == here:
            return None
        here = parent


def _bad_pattern(pattern: object) -> bool:
    if not isinstance(pattern, str):
        return False
    p = pattern.replace("\\", "/")
    return p.startswith("/") or p.startswith("~") or ".." in p.split("/") or (len(p) > 1 and p[1] == ":")


def decide(event: dict) -> str | None:
    """``None`` to allow the call, else the reason to block it."""
    tool = event.get("tool_name")
    if tool not in GUARDED:
        return None
    args = event.get("tool_input") or {}
    cwd = event.get("cwd") or os.getcwd()
    if tool == "Read":
        target = args.get("file_path")
    else:
        target = args.get("path")
        if not target:
            return (f"{tool} needs 'path' set to the checker folder (the folder holding book.json); searching the "
                    "working directory is not allowed")
        if _bad_pattern(args.get("pattern") if tool == "Glob" else args.get("glob")):
            return f"{tool} patterns must stay inside the checker folder (no absolute paths, no '..')"
    if not isinstance(target, str) or not target:
        return f"{tool} has no path"
    target = os.path.join(cwd, os.path.expanduser(target.replace("\\", "/")))
    if checker_root(target) is None:
        return (f"{target} is outside the checker folder. The checker reads only the folder it was given (the one "
                "holding book.json, INSTRUCTIONS.md, text/ and sections/): never the draft, earlier check results "
                "or the round history")
    return None


def main() -> int:
    try:
        event = json.load(sys.stdin)
    except ValueError:
        print("checker guard: the hook input is not JSON", file=sys.stderr)
        return 2
    reason = decide(event if isinstance(event, dict) else {})
    if reason is None:
        return 0
    print(f"Blocked by the prep-checker guard: {reason}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
