"""The guard probe: does the ``prep-checker`` agent's guard run in this session? (/prep-book step 1)

    python3 -m tools.prep guard-probe OUTDIR            write OUTDIR/guard-probe/
    python3 -m tools.prep guard-probe OUTDIR --check    judge what the checker and its guard left there

``save-check`` accepts a checker result only when the checker's guard (the
PreToolUse hook in ``.claude/agents/prep-checker.md``, ``checker_guard.py``)
recorded writing it. Claude Code runs that hook only in a trusted workspace,
so in a session where it does not run, every round fails, but only after
hours of drafting. The probe finds this out first, in about a minute: the
real ``prep-checker`` agent is run, exactly as in a round, on a tiny made-up
text, and ``--check`` then judges the probe's result with the same code
``save-check`` uses (``loop.reply_fault``).

The probe never touches the book or the work folder. Its files cannot be
mistaken for a round: they live in ``OUTDIR/guard-probe/`` (not ``checker/``
or ``rounds/``), its ``book.json`` has ``format`` ``rsvp-guard-probe-input``
and the guard records its write with ``format`` ``rsvp-guard-probe-write``,
which ``save-check`` does not accept; its hashes are of the made-up text, so
``verify-check`` and ``report`` reject its result for any book.

    OUTDIR/guard-probe/
      input/                       the checker folder of the probe (the agent's only input)
        book.json, INSTRUCTIONS.md, text/d01-probe.txt, sections/s0001.txt
      result/checker-reply.txt     written by the checker, through its guard
      result/checker-write.json    written by the guard: what it saw the checker write
"""

from __future__ import annotations

import datetime as _dt
import hashlib
import json
import shutil
from dataclasses import dataclass
from pathlib import Path

from . import checker_guard as guard
from .loop import REPLY, reply_fault

PROBE_DIR = guard.PROBE_DIR
INPUT_DIR = guard.PROBE_INPUT_DIR
RESULT_DIR = guard.PROBE_RESULT_DIR
PROBE_FORMAT = guard.PROBE_FORMAT
PROBE_VERSION = 1
ROUND = 1
PROMPT_PREFIX = "Checker folder: "

HEADING = "The Bell at Marrow Ferry"
PARAGRAPHS = (
    "Every evening at six, the ferry keeper at Marrow Ferry rang a brass bell three times. The bell told the farmers "
    "on the far bank that the last crossing of the day was leaving.",
    "In winter the river froze near the banks, so the keeper broke the ice with a long pole before each crossing. On "
    "the coldest days he rang the bell five times, which meant that the ferry would not run at all.",
)
PROMPT = "What did it mean when the ferry keeper rang the bell five times?"
ANSWER = "That the ferry would not run at all."

INSTRUCTIONS = """\
# Guard probe

This folder is a guard probe, written by `python3 -m tools.prep guard-probe`. It is not a book: the text is two
made-up paragraphs, and your judgement of them is not used for anything. The probe only shows whether the checker's
guard approves and records your one Write call, before a real book is prepared.

Do your work exactly as your agent instructions say. The rules that apply here, in short:

- A section is a run of paragraphs that starts and ends at a natural boundary. This probe has one section: the whole
  made-up text. Treat its length as reasonable.
- A reference answer must be supported by its own section, plain enough that a correct paraphrase can be accepted,
  and the prompt must be answerable from the section alone without giving its answer away.

Write your result, with one Write call, to the `result_file` that `book.json` names, then end with one short line.
"""


@dataclass
class ProbeOutcome:
    """``code`` is ``passed`` or the failure's code; ``message`` is what the command prints."""
    code: str
    message: str

    @property
    def passed(self) -> bool:
        return self.code == "passed"


class ProbeError(Exception):
    """The probe folder cannot be written or read (exit status 1)."""


def probe_dir(out: Path) -> Path:
    return out / PROBE_DIR


def agent_prompt(out: Path) -> str:
    """The one-line prompt for the ``prep-checker`` agent."""
    return PROMPT_PREFIX + str((probe_dir(out) / INPUT_DIR).resolve())


def _sha(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def _files(result_file: Path, date: str) -> dict[str, str]:
    words = [len(HEADING.split())] + [len(p.split()) for p in PARAGRAPHS]
    refs = [f"d01-p{i:03d}" for i in range(1, len(words) + 1)]
    blocks = [f"[{refs[0]}] heading h1, {words[0]} words", HEADING, ""]
    for ref, count, text in zip(refs[1:], words[1:], PARAGRAPHS):
        blocks += [f"[{ref}] paragraph, {count} words", text, ""]
    text = "\n".join([
        "# d01 probe.xhtml",
        "# Spine position 1 of 1 (linear). Title: Guard probe (made-up text, not a book).",
        f"# {len(refs)} blocks ({refs[0]} to {refs[-1]}), {sum(words)} words.",
        "# Each block: [reference] kind, word count; then its text.",
        "",
    ] + blocks)
    section = "\n".join([
        f"# Section s0001: {HEADING}",
        "# Document probe.xhtml (whole text: text/d01-probe.txt).",
        f"# Paragraphs {refs[0]} to {refs[-1]} ({len(refs)} blocks), {sum(words)} words.",
        "# Just before it: start of probe.xhtml. Just after it: end of probe.xhtml.",
        "# Cite only the paragraph references below; they are this section's whole text.",
        "",
    ] + blocks + [
        "## Checkpoint c0001 (shown when the reader reaches the end of this section; 1 prompt, in this order)", "",
        "### Prompt c0001-p1", PROMPT, "", "### Reference answer c0001-p1", ANSWER, "",
    ])
    book = {
        "format": PROBE_FORMAT,
        "version": PROBE_VERSION,
        "note": "Written by 'python3 -m tools.prep guard-probe'. This is a guard probe, not a book: the text is made "
                "up. Judge it as usual and write your result, with one Write call, to result_file. For a probe, "
                "result_file ends in guard-probe/result/checker-reply.txt instead of rounds/NN/checker-reply.txt.",
        "round": ROUND,
        "result_file": str(result_file),
        "date": date,
        "prepared_file": "guard-probe (no file)",
        "prepared_sha256": _sha(section),
        "original_sha256": _sha(text),
        "instructions_version": "guard-probe",
        "title": "Guard probe (made-up text, not a book)",
        "language": "en",
        "issue_id_prefix": "PROBE-",
        "text_files": ["text/d01-probe.txt"],
        "excluded": [],
        "sections": [{
            "id": "s0001", "label": HEADING, "chapter_href": "probe.xhtml", "text_file": "text/d01-probe.txt",
            "first_paragraph": refs[0], "last_paragraph": refs[-1], "paragraphs": len(refs),
            "word_count": sum(words), "ends_document": True, "before": "start of probe.xhtml",
            "after": "end of probe.xhtml", "checkpoint": "c0001", "prompt_ids": ["c0001-p1"],
            "section_file": "sections/s0001.txt",
        }],
        "prompt_ids": ["c0001-p1"],
        "section_ids": ["s0001"],
    }
    return {
        "book.json": json.dumps(book, indent=2, ensure_ascii=False) + "\n",
        "INSTRUCTIONS.md": INSTRUCTIONS,
        "text/d01-probe.txt": text,
        "sections/s0001.txt": section,
    }


def _probe_book(out: Path) -> dict | None:
    try:
        data = json.loads((probe_dir(out) / INPUT_DIR / "book.json").read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, ValueError):
        return None
    return data if isinstance(data, dict) and data.get("format") == PROBE_FORMAT else None


def prepare(out: Path, date: str | None = None) -> Path:
    """Write a fresh ``OUTDIR/guard-probe/`` (replacing an earlier probe, never anything else); return its input."""
    folder = probe_dir(out)
    if folder.is_symlink() or (folder.exists() and not folder.is_dir()):
        raise ProbeError(f"{folder} exists and is not a folder written by guard-probe; remove it")
    if folder.exists():
        if any(folder.iterdir()) and _probe_book(out) is None:
            raise ProbeError(f"{folder} exists and is not a folder written by guard-probe; remove it")
        shutil.rmtree(folder)
    (folder / RESULT_DIR).mkdir(parents=True)
    result = (folder / RESULT_DIR).resolve()
    date = date or _dt.datetime.now(_dt.timezone.utc).date().isoformat()
    for name, text in _files(result / REPLY, date).items():
        target = folder / INPUT_DIR / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8", newline="\n")
    return folder / INPUT_DIR


NOT_TOUCHED = "Nothing of your book was touched: the probe used only a made-up text in {folder}."
RERUN = "Run /prep-book again: it starts with a clean probe."


def check(out: Path) -> ProbeOutcome:
    """Judge the probe's result by the rules ``save-check`` applies to a round's result."""
    folder = probe_dir(out)
    book = _probe_book(out)
    if book is None:
        raise ProbeError(f"{folder} holds no guard probe; run 'python3 -m tools.prep guard-probe {out}' first")
    result = folder / RESULT_DIR
    reply = result / REPLY
    record = result / guard.RECORD
    try:
        raw: bytes | None = reply.read_bytes()
    except OSError:
        raw = None
    fault = reply_fault(result, ROUND, raw, guard.PROBE_RECORD_FORMAT, book.get("prepared_sha256"))
    touched = NOT_TOUCHED.format(folder=folder)
    if fault is None:
        return ProbeOutcome("passed", f"PASSED: the checker's guard is active. It approved and recorded the "
                                      f"checker's one Write ({record}), and {reply} is exactly what it recorded. "
                                      "/prep-book can prepare a book in this session.")
    if fault == "no-record":
        return ProbeOutcome("guard-not-active", "\n".join([
            "FAILED (guard-not-active): the checker's guard is not active in this session.",
            f"The checker wrote {reply}, but its guard recorded nothing ({record} does not exist). So Claude Code "
            "did not run the hook in .claude/agents/prep-checker.md, and save-check would refuse every checker "
            "result: this session cannot prepare a book.",
            touched,
            "What to do: start Claude Code on your own computer, in a terminal, in the folder of this repository. "
            "When it asks whether you trust the folder, answer yes. Then quit and restart Claude Code, and run "
            "/prep-book again.",
            "A Claude Code cloud session cannot do this: it does not run this hook.",
        ]))
    if fault == "no-result":
        return ProbeOutcome("no-result", "\n".join([
            f"FAILED (no-result): the checker wrote no result file ({reply} does not exist).",
            "Either the prep-checker agent did not run on the probe, or it did not finish, or its Write call was "
            "blocked. Without a result the probe cannot tell whether the guard is active.",
            touched, RERUN]))
    if fault == "other-file":
        return ProbeOutcome("other-file", "\n".join([
            f"FAILED (other-file): {record} records a write of another file, not {REPLY}.",
            "Only the guard writes that record, so something else changed it.", touched, RERUN]))
    if fault in ("other-record", "other-round", "other-book"):
        return ProbeOutcome("other-folder", "\n".join([
            f"FAILED (other-folder): {record} is not the guard's record of a write to this probe: it was made for "
            "another checker folder (a book's round, or another probe), or it is not a record at all.",
            "Only the guard writes that record, so something else put it there.", touched, RERUN]))
    return ProbeOutcome("changed", "\n".join([
        f"FAILED (changed): {reply} is not the file the checker wrote: its SHA-256 differs from the one the guard "
        f"recorded in {record}, so it was changed afterwards. Nobody but the checker may write it.",
        touched, RERUN]))
