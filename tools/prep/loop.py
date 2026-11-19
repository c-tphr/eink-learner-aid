"""The script steps of the ``/prep-book`` loop (docs/PLAN.md section 8, steps 3 to 6).

    python3 -m tools.prep start-round WORKDIR ORIGINAL.epub PREPARED.epub
    python3 -m tools.prep checker-input WORKDIR PREPARED.epub [--date YYYY-MM-DD]
    python3 -m tools.prep save-check WORKDIR
    python3 -m tools.prep issues WORKDIR

The AI sessions only write ``draft.json`` (the drafter) and a reply (the
checker); every other file of a round is written here, so the model-facing
surface stays small and every rule a script can hold is held by a script:

- ``start-round`` opens round NN (``WORKDIR/rounds/NN/``): it refuses a fourth
  round, a draft for another instructions version, a prepared file that is not
  exactly ``prep build`` of the current draft, and prompt or checkpoint ids
  that moved to another section or were renumbered; then it archives the
  draft as ``rounds/NN/draft.json``.
- ``checker-input`` writes ``WORKDIR/checker/``, the only folder the checker
  reads: the instructions, the numbered text, ``book.json`` (the values the
  checker copies into ``check.json``, every section with its paragraph range
  and every prompt id) and one file per section with its text, prompts and
  reference answers. It runs only after the round's validator result passed,
  for exactly this prepared file. The draft's citations and notes, earlier
  check results and the round history are not in it.
- ``save-check`` turns the checker's final message, saved verbatim as
  ``rounds/NN/checker-reply.txt``, into ``rounds/NN/check.json``. It extracts
  the JSON object but never repairs it: an unusable reply is written as it is,
  so ``verify-check`` rejects it and the round fails.
- ``issues`` writes ``rounds/NN/issues.md``, everything the drafter may see
  about a failed round: validator failures, and the checker's open issues and
  failed prompts and sections. Passed prompts and their evidence are left out.
"""

from __future__ import annotations

import datetime as _dt
import json
import re
import shutil
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .build import BuildError, build_bytes
from .epub import Block, EpubError, SpineDoc, load_book_bytes
from .extract import _block_header, display_text, text_file_name
from .validate import sha256
from .verify_check import CRITERIA, CRITERION_TEXT, _prepared_sections

MAX_ROUNDS = 3
INSTRUCTIONS_PATH = Path(__file__).resolve().parent / "INSTRUCTIONS.md"
CHECKER_DIR = "checker"
CHECKER_INPUT_FORMAT = "rsvp-checker-input"
CHECKER_INPUT_VERSION = 1
REPLY = "checker-reply.txt"
_VERSION_LINE = re.compile(r"^instructions_version: (\S+)[ \t]*$", re.MULTILINE)


class LoopError(Exception):
    """A loop step refused; ``problems`` lists every reason (exit status 1)."""

    def __init__(self, problems: list[str]):
        self.problems = problems
        super().__init__("\n".join(problems))


def instructions_version(path: Path = INSTRUCTIONS_PATH) -> str:
    """The version declared at the top of ``INSTRUCTIONS.md`` (``instructions_version: 1.0``)."""
    match = _VERSION_LINE.search(path.read_text(encoding="utf-8"))
    if match is None:
        raise LoopError([f"{path} declares no 'instructions_version: ...' line"])
    return match.group(1)


def _today() -> str:
    return _dt.datetime.now(_dt.timezone.utc).date().isoformat()


def _json_text(data: Any) -> str:
    return json.dumps(data, indent=2, ensure_ascii=False) + "\n"


def _load(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, ValueError):
        return None


# --------------------------------------------------------------------------
# Rounds


def rounds(work: Path) -> list[tuple[int, Path]]:
    """``(number, folder)`` of ``WORKDIR/rounds/NN`` in numeric order."""
    base = work / "rounds"
    if not base.is_dir():
        return []
    found = [(int(d.name), d) for d in base.iterdir() if d.is_dir() and d.name.isdigit()]
    return sorted(found)


def latest_round(work: Path) -> tuple[int, Path]:
    found = rounds(work)
    if not found:
        raise LoopError([f"{work} has no round yet; run start-round first"])
    return found[-1]


def round_passed(folder: Path) -> bool:
    """The round's stored validator result and checker verification both passed."""
    validation = _load(folder / "validation.json")
    verification = _load(folder / "verify-check.json")
    return (isinstance(validation, dict) and validation.get("passed") is True and isinstance(verification, dict)
            and verification.get("passed") is True)


def _id_history(drafts: list[tuple[int, dict[str, Any]]]) -> tuple[dict[str, tuple[str, int]], dict[str, tuple[str, int]],
                                                                   dict[str, tuple[str, int]]]:
    """From earlier drafts: prompt id -> (section, round), checkpoint id -> (section, round), prompt text -> (id, round)."""
    prompt_section: dict[str, tuple[str, int]] = {}
    checkpoint_section: dict[str, tuple[str, int]] = {}
    text_id: dict[str, tuple[str, int]] = {}
    for number, draft in drafts:
        for c in draft.get("checkpoints", []) if isinstance(draft.get("checkpoints"), list) else []:
            if not isinstance(c, dict):
                continue
            checkpoint_section.setdefault(str(c.get("id")), (str(c.get("section")), number))
            for p in c.get("prompts", []) if isinstance(c.get("prompts"), list) else []:
                if isinstance(p, dict):
                    prompt_section.setdefault(str(p.get("id")), (str(c.get("section")), number))
                    if isinstance(p.get("prompt"), str):
                        text_id.setdefault(p["prompt"], (str(p.get("id")), number))
    return prompt_section, checkpoint_section, text_id


def check_id_stability(previous: list[tuple[int, dict[str, Any]]], draft: dict[str, Any]) -> tuple[list[str], list[str]]:
    """(problems, notes) for the current draft's ids against every earlier round's draft.

    Saved grades are keyed by prompt id (docs/PLAN.md section 5, rule 4), so a
    prompt or checkpoint id may not move to another section, and a prompt's
    exact text may not reappear under another id (renumbering).
    """
    problems: list[str] = []
    notes: list[str] = []
    if not previous:
        return problems, notes
    prompt_section, checkpoint_section, text_id = _id_history(previous)
    current_prompts: set[str] = set()
    for c in draft["checkpoints"]:
        old = checkpoint_section.get(c["id"])
        if old is not None and old[0] != c["section"]:
            problems.append(f"id-moved: checkpoint {c['id']} names section {c['section']}, but in round {old[1]} it "
                            f"named {old[0]}; keep a checkpoint on its section, or give a new checkpoint a new id")
        for p in c["prompts"]:
            current_prompts.add(p["id"])
            old = prompt_section.get(p["id"])
            if old is not None and old[0] != c["section"]:
                problems.append(f"id-moved: prompt {p['id']} is in section {c['section']}, but in round {old[1]} it "
                                f"was in section {old[0]}; a prompt id never moves to another section")
            was = text_id.get(p["prompt"])
            if was is not None and was[0] != p["id"]:
                problems.append(f"id-renumbered: prompt {p['id']} has exactly the text that prompt {was[0]} had in "
                                f"round {was[1]}; keep {was[0]} as its id (ids are never renumbered)")
    last_number, last = previous[-1]
    last_ids = [p.get("id") for c in last.get("checkpoints", []) for p in c.get("prompts", [])]
    removed = [i for i in last_ids if i not in current_prompts]
    added = sorted(current_prompts - set(last_ids))
    if removed:
        notes.append(f"prompts removed since round {last_number}: {', '.join(removed)}")
    if added:
        notes.append(f"prompts added since round {last_number}: {', '.join(added)}")
    return problems, notes


@dataclass
class RoundStart:
    number: int
    folder: Path
    restarted: bool
    notes: list[str] = field(default_factory=list)


def start_round(work: Path, original: Path, prepared: Path) -> RoundStart:
    """Open the next round (or reopen a round that never reached the validator) and archive the draft."""
    problems: list[str] = []
    source = _load(work / "source.json")
    if not isinstance(source, dict):
        raise LoopError([f"{work / 'source.json'} cannot be read; run prep extract first"])
    try:
        original_bytes = original.read_bytes()
    except OSError as exc:
        raise LoopError([f"cannot read the original {original}: {exc.strerror}"]) from exc
    if sha256(original_bytes) != source.get("source_sha256"):
        problems.append(f"the original {original} (SHA-256 {sha256(original_bytes)}) is not the file the work folder "
                        f"was extracted from (SHA-256 {source.get('source_sha256')})")
    draft_path = work / "draft.json"
    try:
        draft_raw = draft_path.read_bytes()
    except OSError:
        raise LoopError(problems + [f"{draft_path} does not exist; the drafter writes it first"])
    draft = _load(draft_path)
    if not isinstance(draft, dict):
        raise LoopError(problems + [f"{draft_path} is not a JSON object; run prep build to see why"])
    version = instructions_version()
    have = draft.get("book", {}).get("instructions_version") if isinstance(draft.get("book"), dict) else None
    if have != version:
        problems.append(f"instructions-version: the draft says instructions_version {have!r}, but "
                        f"tools/prep/INSTRUCTIONS.md is version {version!r}; draft under the current instructions")

    found = rounds(work)
    restarted = False
    if found and not (found[-1][1] / "validation.json").is_file():
        number, folder = found[-1]  # the round never reached the validator: reuse it
        restarted = True
    else:
        if found and round_passed(found[-1][1]):
            problems.append(f"round {found[-1][0]} passed both checks; run prep report instead of a new round")
        number = found[-1][0] + 1 if found else 1
        folder = work / "rounds" / f"{number:02d}"
        if number > MAX_ROUNDS:
            problems.append(f"all {MAX_ROUNDS} rounds are used; run prep report to write the NOT READY report")
    if [n for n, _ in found] != list(range(1, len(found) + 1)):
        problems.append(f"{work / 'rounds'} holds rounds {[n for n, _ in found]}, not 1 to {len(found)}")

    try:
        prepared_bytes = prepared.read_bytes()
    except OSError:
        prepared_bytes = None
        problems.append(f"{prepared} does not exist; run prep build first")
    if prepared_bytes is not None:
        try:
            expected = build_bytes(load_book_bytes(original_bytes, original.name), draft_raw).data
        except BuildError as exc:
            problems.append(f"the draft cannot be built ({len(exc.issues)} problems); run prep build to see them")
        except EpubError as exc:
            problems.append(f"the original cannot be read: {exc}")
        else:
            if expected != prepared_bytes:
                problems.append(f"{prepared} is not prep build output for the current draft.json; run prep build "
                                "again")

    previous = [(n, d) for n, f in found if n < number and isinstance(d := _load(f / "draft.json"), dict)]
    notes: list[str] = []
    if isinstance(draft.get("checkpoints"), list):
        try:
            stable, notes = check_id_stability(previous, draft)
        except (KeyError, TypeError):
            stable = ["the draft's checkpoints cannot be read; run prep build to see why"]
        problems += stable
    if problems:
        raise LoopError(problems)
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "draft.json").write_bytes(draft_raw)
    return RoundStart(number, folder, restarted, notes)


# --------------------------------------------------------------------------
# The checker's view


def _section_blocks(doc: SpineDoc, start: int, end: int) -> list[Block]:
    return [b for b in doc.blocks if start <= b.start and b.end <= end and b.start < end]


def _neighbour(doc: SpineDoc, block: Block | None, owner: dict[str, str], where: str) -> str:
    if block is None:
        return f"{where} of {doc.href}"
    return f"{block.ref} ({owner.get(block.ref, 'in no section')})"


def _write_view(folder: Path, files: dict[str, str]) -> None:
    if folder.exists():
        marker = _load(folder / "book.json")
        empty = folder.is_dir() and not any(folder.iterdir())
        if not empty and not (isinstance(marker, dict) and marker.get("format") == CHECKER_INPUT_FORMAT):
            raise LoopError([f"{folder} exists and is not a checker folder written by checker-input; remove it"])
        shutil.rmtree(folder)
    for name, text in files.items():
        target = folder / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8", newline="\n")


def checker_input(work: Path, prepared: Path, date: str | None = None,
                  instructions: Path = INSTRUCTIONS_PATH) -> dict[str, Any]:
    """Write ``WORKDIR/checker/`` for the latest round; return its ``book.json``."""
    number, folder = latest_round(work)
    try:
        prepared_bytes = prepared.read_bytes()
    except OSError as exc:
        raise LoopError([f"cannot read {prepared}: {exc.strerror}"]) from exc
    prepared_sha = sha256(prepared_bytes)
    validation = _load(folder / "validation.json")
    problems = []
    if not isinstance(validation, dict):
        problems.append(f"round {number} has no validator result ({folder / 'validation.json'}); run prep validate "
                        "first: the checker only reads a book that passed the validator")
    else:
        if validation.get("passed") is not True:
            problems.append(f"round {number}'s validator result did not pass; the checker is not run on a "
                            "structurally broken book (give the drafter the issue list instead)")
        if (validation.get("prepared") or {}).get("sha256") != prepared_sha:
            problems.append(f"round {number}'s validator result is for another prepared file, not {prepared}")
    draft = _load(folder / "draft.json")
    if not isinstance(draft, dict):
        problems.append(f"round {number} has no readable draft.json")
    if problems:
        raise LoopError(problems)

    book = load_book_bytes(prepared_bytes, prepared.name)
    data, resolved, problem = _prepared_sections(book)
    if data is None:
        raise LoopError([problem])
    missing = [s["id"] for s in data["sections"] if s["id"] not in resolved]
    if missing:
        raise LoopError([f"sections {', '.join(missing)} have no valid anchors in {prepared}"])
    version = instructions_version(instructions)
    if data["book"]["instructions_version"] != version:
        raise LoopError([f"the book was prepared under instructions version {data['book']['instructions_version']!r}, "
                         f"but {instructions.name} is version {version!r}"])
    text_dir = work / "text"
    texts = sorted(text_dir.glob("*.txt")) if text_dir.is_dir() else []
    if not texts:
        raise LoopError([f"{text_dir} holds no text files; run prep extract first"])

    owner: dict[str, str] = {}
    section_blocks: dict[str, list[Block]] = {}
    for s in data["sections"]:
        r = resolved[s["id"]]
        blocks = _section_blocks(r.doc, r.start, r.end)
        section_blocks[s["id"]] = blocks
        for b in blocks:
            owner[b.ref] = f"section {s['id']}"
    excluded = []
    for e in draft.get("excluded", []):
        doc = book.doc_by_href(e.get("chapter_href", ""))
        excluded.append({"chapter_href": e.get("chapter_href"),
                         "text_file": text_file_name(doc) if doc is not None else None,
                         "first_paragraph": e.get("first_paragraph"), "last_paragraph": e.get("last_paragraph"),
                         "reason": e.get("reason")})
        if doc is not None:
            refs = [b.ref for b in doc.blocks]
            if e.get("first_paragraph") in refs and e.get("last_paragraph") in refs:
                for ref in refs[refs.index(e["first_paragraph"]): refs.index(e["last_paragraph"]) + 1]:
                    owner[ref] = "excluded: " + str(e.get("reason"))

    checkpoint_of = {c["section"]: c for c in data["checkpoints"]}
    files: dict[str, str] = {"INSTRUCTIONS.md": instructions.read_text(encoding="utf-8")}
    for t in texts:
        files[f"text/{t.name}"] = t.read_text(encoding="utf-8")
    sections_out = []
    for s in data["sections"]:
        r = resolved[s["id"]]
        doc, blocks = r.doc, section_blocks[s["id"]]
        if not blocks:
            raise LoopError([f"section {s['id']} holds no paragraph of {doc.href}"])
        i, j = doc.blocks.index(blocks[0]), doc.blocks.index(blocks[-1])
        before = _neighbour(doc, doc.blocks[i - 1] if i > 0 else None, owner, "start")
        after = _neighbour(doc, doc.blocks[j + 1] if j + 1 < len(doc.blocks) else None, owner, "end")
        c = checkpoint_of.get(s["id"])
        entry = {
            "id": s["id"], "label": s["label"], "chapter_href": s["chapter_href"], "text_file": text_file_name(doc),
            "first_paragraph": blocks[0].ref, "last_paragraph": blocks[-1].ref, "paragraphs": len(blocks),
            "word_count": s["word_count"], "ends_document": j + 1 == len(doc.blocks),
            "before": before, "after": after, "checkpoint": c["id"] if c else None,
            "prompt_ids": [p["id"] for p in c["prompts"]] if c else [],
            "section_file": f"sections/{s['id']}.txt",
        }
        sections_out.append(entry)
        lines = [
            f"# Section {s['id']}: {s['label']}",
            f"# Document {doc.href} (whole text: {entry['text_file']}).",
            f"# Paragraphs {entry['first_paragraph']} to {entry['last_paragraph']} ({len(blocks)} blocks), "
            f"{s['word_count']} words.",
            f"# Just before it: {before}. Just after it: {after}.",
            "# Cite only the paragraph references below; they are this section's whole text.",
            "",
        ]
        for b in blocks:
            lines += [_block_header(b), display_text(doc, b), ""]
        if c is None:
            lines += ["## No checkpoint", "", "The drafter gave this section no checkpoint.", ""]
        else:
            lines += [f"## Checkpoint {c['id']} (shown when the reader reaches the end of this section; "
                      f"{len(c['prompts'])} prompt{'s' if len(c['prompts']) != 1 else ''}, in this order)", ""]
            for p in c["prompts"]:
                lines += [f"### Prompt {p['id']}", p["prompt"], "", f"### Reference answer {p['id']}", p["answer"], ""]
        files[entry["section_file"]] = "\n".join(lines)
    book_json = {
        "format": CHECKER_INPUT_FORMAT,
        "version": CHECKER_INPUT_VERSION,
        "note": "Written by 'python3 -m tools.prep checker-input'. Copy round, date, prepared_sha256, "
                "original_sha256 and instructions_version into check.json unchanged.",
        "round": number,
        "date": date or _today(),
        "prepared_file": prepared.name,
        "prepared_sha256": prepared_sha,
        "original_sha256": data["book"]["source_sha256"],
        "instructions_version": data["book"]["instructions_version"],
        "title": data["book"]["title"],
        "language": data["book"]["language"],
        "issue_id_prefix": f"R{number}-",
        "text_files": [f"text/{t.name}" for t in texts],
        "excluded": excluded,
        "sections": sections_out,
        "prompt_ids": [p["id"] for c in data["checkpoints"] for p in c["prompts"]],
        "section_ids": [s["id"] for s in data["sections"]],
    }
    files["book.json"] = _json_text(book_json)
    _write_view(work / CHECKER_DIR, files)
    return book_json


# --------------------------------------------------------------------------
# The checker's reply


_FENCE = re.compile(r"```[A-Za-z]*[ \t]*\n(.*?)\n[ \t]*```", re.DOTALL)


def extract_check(reply: str) -> str | None:
    """The JSON object in the checker's reply, exactly as written; ``None`` when there is none.

    Accepted, in this order: the whole reply; a fenced block (the last that
    parses as an object); the text from the first ``{`` to the last ``}``.
    The text is returned unchanged (not re-serialised), so ``verify-check``
    judges exactly what the checker produced.
    """
    def is_object(text: str) -> bool:
        try:
            return isinstance(json.loads(text), dict)
        except ValueError:
            return False

    stripped = reply.strip()
    if is_object(stripped):
        return stripped
    fenced = [m.group(1).strip() for m in _FENCE.finditer(reply)]
    for text in reversed(fenced):
        if is_object(text):
            return text
    first, last = reply.find("{"), reply.rfind("}")
    if 0 <= first < last and is_object(reply[first:last + 1]):
        return reply[first:last + 1]
    return None


def save_check(work: Path) -> tuple[Path, bool]:
    """Write the latest round's ``check.json`` from ``checker-reply.txt``; ``(path, extracted)``."""
    number, folder = latest_round(work)
    reply_path = folder / REPLY
    try:
        raw = reply_path.read_bytes()
    except OSError:
        raise LoopError([f"{reply_path} does not exist; save the checker's final message there first, verbatim"])
    target = folder / "check.json"
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        target.write_bytes(raw)
        return target, False
    found = extract_check(text)
    if found is None:
        target.write_bytes(raw)
        return target, False
    target.write_text(found + "\n", encoding="utf-8", newline="\n")
    return target, True


# --------------------------------------------------------------------------
# The issue list for the drafter


def issue_list(work: Path) -> tuple[str, dict[str, Any]]:
    """Markdown for ``rounds/NN/issues.md`` and the same content as data."""
    number, folder = latest_round(work)
    validation = _load(folder / "validation.json")
    verification = _load(folder / "verify-check.json")
    check = _load(folder / "check.json")
    out: dict[str, Any] = {"round": number, "validator": [], "checker_unusable": [], "checker_issues": [],
                           "failed_prompts": [], "failed_sections": []}
    lines = [f"# Issues from round {number}", ""]

    if not isinstance(validation, dict):
        out["validator_status"] = "not run"
        lines += ["The validator has not run for this round: there is nothing to fix yet.", ""]
    else:
        out["validator_status"] = "passed" if validation.get("passed") else "failed"
        for c in validation.get("checks", []):
            for f in c.get("failures", []):
                out["validator"].append({"check": c.get("name"), "code": f.get("code"),
                                         "location": f.get("location"), "message": f.get("message")})
            if c.get("status") in ("not-run", "incomplete") and not c.get("failures"):
                out["validator"].append({"check": c.get("name"), "code": c.get("status"), "location": "",
                                         "message": "; ".join(c.get("details", []))})

    if isinstance(verification, dict):
        failures = verification.get("failures", [])
        unusable = [f for f in failures if f.get("code") != "checker-verdict-fail"]
        out["checker_status"] = ("passed" if verification.get("passed") else
                                 "unusable" if unusable else "failed")
        out["checker_unusable"] = unusable
        readable = not any(f.get("code") in ("check-unreadable", "check-schema") for f in failures)
        if readable and isinstance(check, dict):
            for issue in check.get("issues", []):
                if issue.get("status") == "open":
                    out["checker_issues"].append({k: issue.get(k) for k in
                                                  ("id", "severity", "target", "description", "suggested_fix")})
            for p in check.get("prompts", []):
                if p.get("verdict") != "pass":
                    out["failed_prompts"].append({"id": p.get("id"),
                                                  "failed": [CRITERION_TEXT[k] for k in CRITERIA if not p.get(k)],
                                                  "citations": p.get("citations"), "evidence": p.get("evidence")})
            for s in check.get("sections", []):
                if not s.get("natural_boundary"):
                    out["failed_sections"].append({"id": s.get("id"), "evidence": s.get("evidence")})
    else:
        out["checker_status"] = "not run"

    if out["validator"]:
        lines += ["## Validator failures", "",
                  "The prepared book is structurally broken. Fix the draft so that each of these goes away.", ""]
        lines += [f"- `{v['check']}: {v['code']}` at `{v['location']}`: {v['message']}" for v in out["validator"]]
        lines.append("")
    if out["checker_status"] == "not run" and out["validator_status"] == "failed":
        lines += ["The checker was not run, because the validator failed.", ""]
    judged = any(out[k] for k in ("checker_issues", "failed_prompts", "failed_sections"))
    if out["checker_unusable"]:
        lines += ["## The checker's result could not be used", "",
                  ("The checker's result is also inconsistent, so it is not accepted; its issues are listed below. "
                   if judged else
                   "This is not a problem with your draft. Do not change the draft for it; the next round runs the "
                   "checker again on the same book. ") + "What `verify-check` reported:", ""]
        lines += [f"- `{f.get('code')}` at `{f.get('location')}`: {f.get('message')}" for f in out["checker_unusable"]]
        lines.append("")
    if out["checker_issues"]:
        lines += ["## Checker issues", ""]
        for i in out["checker_issues"]:
            lines += [f"- **{i['id']}** ({i['severity']}, {i['target']}): {i['description']} "
                      f"Suggested fix: {i['suggested_fix']}"]
        lines.append("")
    if out["failed_prompts"]:
        lines += ["## Prompts the checker failed", ""]
        for p in out["failed_prompts"]:
            lines += [f"- **{p['id']}**: failed: {'; '.join(p['failed']) or 'verdict fail'}. Checker's evidence "
                      f"(citing {', '.join(p['citations'] or []) or 'nothing'}): {p['evidence']}"]
        lines.append("")
    if out["failed_sections"]:
        lines += ["## Section breaks the checker failed", ""]
        lines += [f"- **{s['id']}**: {s['evidence']}" for s in out["failed_sections"]]
        lines.append("")
    passed = out["validator_status"] == "passed" and out["checker_status"] == "passed"
    out["passed"] = passed
    if passed:
        lines += ["Round passed: the validator and the checker both passed. Nothing to fix.", ""]
    elif any(out[k] for k in ("validator", "checker_issues", "failed_prompts", "failed_sections")):
        lines += ["Fix exactly these, keep everything else byte-identical (ids included), and record one "
                  "`x_changes` entry per issue (tools/prep/INSTRUCTIONS.md, section 7).", ""]
    return "\n".join(lines), out


def write_issues(work: Path) -> tuple[Path, str, dict[str, Any]]:
    number, folder = latest_round(work)
    text, data = issue_list(work)
    target = folder / "issues.md"
    target.write_text(text, encoding="utf-8", newline="\n")
    return target, text, data
