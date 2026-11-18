#!/usr/bin/env python3
"""PreToolUse hook of the ``prep-checker`` subagent: it reads only its folder and writes only its result.

``.claude/agents/prep-checker.md`` runs this before every Read, Glob, Grep
and Write call of the checker (and before Edit, MultiEdit, NotebookEdit and
Bash, which the agent does not have, to refuse them if it ever did). It
reads the hook input (JSON) on stdin and exits

- 0 when a Read, Glob or Grep call only touches files inside a checker
  folder: a folder that holds a ``book.json`` whose ``format`` is
  ``rsvp-checker-input`` (written by ``python3 -m tools.prep checker-input``);
- 0, printing a decision that allows the call, for the one Write the checker
  may make: its result, to the ``result_file`` that ``book.json`` names
  (``WORKDIR/rounds/NN/checker-reply.txt`` of the round being checked), once,
  before ``save-check`` ran. Before allowing it, the guard records the
  SHA-256 of the content in ``rounds/NN/checker-write.json``, so ``save-check``
  can tell that the file is the one the checker wrote through this guard;
- the same for a guard probe (``python3 -m tools.prep guard-probe``), which
  /prep-book runs first to prove that this hook runs at all: reads inside
  ``guard-probe/input/`` (whose ``book.json`` has ``format``
  ``rsvp-guard-probe-input``) and one Write to ``guard-probe/result/checker-reply.txt``,
  recorded with ``format`` ``rsvp-guard-probe-write``. A probe folder cannot
  authorise a round's result, and a round's checker folder cannot authorise a
  probe's result;
- 2, with the reason on stderr, otherwise. Claude Code then blocks the call
  and shows the reason to the checker.

So the checker cannot open the draft (with its citations and notes), earlier
``check.json`` files, the round history or anything else in the repository,
even by mistake, and it can change no file but its own result. Glob and Grep
must name the checker folder as ``path`` and may not use absolute or ``..``
patterns.

Standard library only, so the hook runs in any Python 3.
"""

from __future__ import annotations

import hashlib
import json
import os
import sys

FORMAT = "rsvp-checker-input"
READ_TOOLS = ("Read", "Glob", "Grep")
REFUSED_TOOLS = ("Edit", "MultiEdit", "NotebookEdit", "Bash")
CHECKER_DIR = "checker"
REPLY = "checker-reply.txt"
RECORD = "checker-write.json"
RECORD_FORMAT = "rsvp-checker-write"
PROBE_FORMAT = "rsvp-guard-probe-input"
PROBE_RECORD_FORMAT = "rsvp-guard-probe-write"
PROBE_DIR = "guard-probe"
PROBE_INPUT_DIR = "input"
PROBE_RESULT_DIR = "result"


def reply_digest(data: bytes) -> str:
    """SHA-256 of a result file, ignoring line endings and trailing white space (JSON does not depend on them)."""
    return hashlib.sha256(data.replace(b"\r\n", b"\n").rstrip()).hexdigest()


def _book(folder: str, formats: tuple[str, ...] = (FORMAT,)) -> dict | None:
    try:
        with open(os.path.join(folder, "book.json"), encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return None
    return data if isinstance(data, dict) and data.get("format") in formats else None


def checker_root(path: str) -> str | None:
    """The checker folder that contains ``path`` (after resolving symlinks and ``..``), else ``None``."""
    real = os.path.realpath(path)
    here = real if os.path.isdir(real) else os.path.dirname(real)
    while True:
        if os.path.isfile(os.path.join(here, "book.json")) and _book(here, (FORMAT, PROBE_FORMAT)) is not None:
            return here
        parent = os.path.dirname(here)
        if parent == here:
            return None
        here = parent


def _bad_pattern(pattern: object) -> bool:
    if not isinstance(pattern, str):
        return False
    p = pattern.replace("\\", "/")
    return p.startswith("/") or p.startswith("~") or ".." in p.split("/") or (len(p) > 1 and p[1] == ":")


def _target(event: dict, value: object) -> str | None:
    if not isinstance(value, str) or not value:
        return None
    cwd = event.get("cwd") or os.getcwd()
    return os.path.join(cwd, os.path.expanduser(value.replace("\\", "/")))


def decide(event: dict) -> str | None:
    """``None`` to let a read through, else the reason to block it. Write calls are judged by ``decide_write``."""
    tool = event.get("tool_name")
    if tool in REFUSED_TOOLS:
        return f"the checker may not use {tool}; it reads its folder and writes only its result file"
    if tool not in READ_TOOLS:
        return None
    args = event.get("tool_input") or {}
    if tool == "Read":
        target = args.get("file_path")
    else:
        target = args.get("path")
        if not target:
            return (f"{tool} needs 'path' set to the checker folder (the folder holding book.json); searching the "
                    "working directory is not allowed")
        if _bad_pattern(args.get("pattern") if tool == "Glob" else args.get("glob")):
            return f"{tool} patterns must stay inside the checker folder (no absolute paths, no '..')"
    path = _target(event, target)
    if path is None:
        return f"{tool} has no path"
    if checker_root(path) is None:
        return (f"{path} is outside the checker folder. The checker reads only the folder it was given (the one "
                "holding book.json, INSTRUCTIONS.md, text/ and sections/): never the draft, earlier check results "
                "or the round history")
    return None


def _result_book(folder: str) -> dict | None:
    """The book.json that may name a result file in ``folder``: ``WORKDIR/checker/`` (format ``rsvp-checker-input``)
    for ``WORKDIR/rounds/NN/``, ``guard-probe/input/`` (format ``rsvp-guard-probe-input``) for ``guard-probe/result/``."""
    parent = os.path.dirname(folder)
    if os.path.basename(parent) == "rounds":
        return _book(os.path.join(os.path.dirname(parent), CHECKER_DIR), (FORMAT,))
    if os.path.basename(folder) == PROBE_RESULT_DIR and os.path.basename(parent) == PROBE_DIR:
        return _book(os.path.join(parent, PROBE_INPUT_DIR), (PROBE_FORMAT,))
    return None


def write_target(event: dict) -> tuple[str | None, str, dict | None]:
    """(reason to refuse or ``None``, the round folder, the checker folder's book.json) for a Write call."""
    args = event.get("tool_input") or {}
    path = _target(event, args.get("file_path"))
    if path is None:
        return "Write has no file_path", "", None
    if not isinstance(args.get("content"), str):
        return "Write has no content", "", None
    folder = os.path.realpath(os.path.dirname(path))
    where = os.path.join(folder, os.path.basename(path))
    book = _result_book(folder)
    expected = book.get("result_file") if book else None
    if not (isinstance(expected, str) and os.path.basename(path) == REPLY and where == os.path.join(
            os.path.realpath(os.path.dirname(expected)), os.path.basename(expected))):
        return (f"the checker may write only its result file, the 'result_file' that book.json names, not {where}",
                folder, None)
    if os.path.lexists(where):
        return f"{where} already exists: the checker writes its result once, in one Write call", folder, None
    if os.path.lexists(os.path.join(folder, "check.json")):
        return f"this round's result was already saved ({os.path.join(folder, 'check.json')} exists)", folder, None
    return None, folder, book


def decide_write(event: dict) -> str | None:
    """``None`` to allow the checker's one Write (after recording what it writes), else the reason to block it."""
    reason, folder, book = write_target(event)
    if reason is not None:
        return reason
    assert book is not None
    content = event["tool_input"]["content"].encode("utf-8")
    kind = PROBE_RECORD_FORMAT if book.get("format") == PROBE_FORMAT else RECORD_FORMAT
    record = {"format": kind, "version": 1, "file": REPLY, "round": book.get("round"),
              "prepared_sha256": book.get("prepared_sha256"), "sha256": reply_digest(content),
              "bytes": len(content)}
    try:
        with open(os.path.join(folder, RECORD), "w", encoding="utf-8", newline="\n") as f:
            f.write(json.dumps(record, indent=2) + "\n")
    except OSError as exc:
        return f"cannot record the write in {os.path.join(folder, RECORD)}: {exc.strerror}"
    return None


ALLOW = {"hookSpecificOutput": {"hookEventName": "PreToolUse", "permissionDecision": "allow",
                                "permissionDecisionReason": "prep-checker guard: the checker's own result file"}}


def main() -> int:
    try:
        event = json.load(sys.stdin)
    except ValueError:
        print("checker guard: the hook input is not JSON", file=sys.stderr)
        return 2
    event = event if isinstance(event, dict) else {}
    if event.get("tool_name") == "Write":
        reason = decide_write(event)
        if reason is None:
            print(json.dumps(ALLOW))
            return 0
    else:
        reason = decide(event)
        if reason is None:
            return 0
    print(f"Blocked by the prep-checker guard: {reason}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
