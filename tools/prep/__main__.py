"""Command-line entry point: ``python3 -m tools.prep <command> ...``.

    python3 -m tools.prep extract BOOK.epub WORKDIR
    python3 -m tools.prep build BOOK.epub DRAFT.json -o OUT.prepared.epub
    python3 -m tools.prep validate BOOK.epub OUT.prepared.epub [--draft DRAFT.json | --work-dir DIR] [--json OUT]
    python3 -m tools.prep verify-check BOOK.epub OUT.prepared.epub CHECK.json [--json OUT]
    python3 -m tools.prep report --original BOOK.epub --prepared OUT.prepared.epub [--validation V.json]
                                 [--check CHECK.json] [--history WORKDIR] [--draft DRAFT.json] -o OUTDIR

Steps of the /prep-book loop (tools/prep/loop.py):

    python3 -m tools.prep start-round WORKDIR ORIGINAL.epub OUT.prepared.epub
    python3 -m tools.prep checker-input WORKDIR OUT.prepared.epub [--date YYYY-MM-DD]
    python3 -m tools.prep save-check WORKDIR
    python3 -m tools.prep issues WORKDIR

The guard probe, /prep-book's first step (tools/prep/probe.py):

    python3 -m tools.prep guard-probe OUTDIR [--check]

Exit status: 0 success (for validate and verify-check: everything passed;
for report: the book is READY), 1 a problem with the input or a failed
check (each problem is printed on stderr), 2 a usage error.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .build import BuildError, build_file
from .epub import EpubError
from .extract import extract

EXIT_OK = 0
EXIT_INPUT = 1
EXIT_USAGE = 2


def _cmd_extract(args: argparse.Namespace) -> int:
    source = extract(args.epub, args.workdir)
    words = sum(d["words"] for d in source["spine"])
    blocks = sum(d["blocks"] for d in source["spine"])
    print(f"extracted {source['source_file']} (sha256 {source['source_sha256']}): {len(source['spine'])} spine "
          f"documents, {blocks} blocks, {words} words -> {args.workdir}")
    return EXIT_OK


def _cmd_build(args: argparse.Namespace) -> int:
    result = build_file(args.epub, args.draft, args.output)
    sections = result.checkpoints["sections"]
    prompts = sum(len(c["prompts"]) for c in result.checkpoints["checkpoints"])
    print(f"built {args.output}: {len(sections)} sections, {len(result.checkpoints['checkpoints'])} checkpoints, "
          f"{prompts} prompts; changed {', '.join(result.touched)}; added {result.added}")
    return EXIT_OK


def _created(args: argparse.Namespace) -> str:
    from .validate import timestamp
    try:
        return timestamp(args.timestamp)
    except ValueError as exc:
        raise UsageError(str(exc)) from exc


class UsageError(Exception):
    """A usage problem found after parsing (exit status 2)."""


def _write_json(path: Path | None, data: dict) -> None:
    if path is None:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def _cmd_validate(args: argparse.Namespace) -> int:
    from . import validate as V
    if args.draft is not None and args.work_dir is not None:
        raise UsageError("give --draft or --work-dir, not both")
    if args.work_dir is not None and not args.work_dir.is_dir():
        raise UsageError(f"--work-dir {args.work_dir} is not a folder")
    draft = V.find_draft(args.draft, args.work_dir)
    if draft is not None and not draft.is_file():
        raise UsageError(f"cannot read the draft {draft}")
    created = _created(args)
    outcome = V.validate_files(args.original, args.prepared, draft, created=created)
    _write_json(args.json, outcome.result)
    r = outcome.result
    print(f"prep validate: {r['prepared']['file']} against {r['original']['file']}"
          + (f" (exclusions from {r['draft']['file']})" if r["draft"] else ""))
    print(V.format_table(r))
    failures = [(c["name"], f) for c in r["checks"] for f in c["failures"]]
    for name, f in failures:
        print(f"  {name}: {f['code']} at {f['location']}: {f['message']}", file=sys.stderr)
    if outcome.passed:
        print(f"PASSED: all {len(r['checks'])} checks passed.")
        return EXIT_OK
    not_passed = [c["name"] for c in r["checks"] if c["status"] != "pass"]
    print(f"FAILED: {len(not_passed)} of {len(r['checks'])} checks did not pass ({', '.join(not_passed)}); "
          f"{len(failures)} failure{'s' if len(failures) != 1 else ''} listed on stderr.")
    return EXIT_INPUT


def _cmd_verify_check(args: argparse.Namespace) -> int:
    from .validate import sha256
    from .verify_check import verification_json, verify_check_bytes
    created = _created(args)
    try:
        original = args.original.read_bytes()
        prepared = args.prepared.read_bytes()
        raw = args.check.read_bytes()
    except OSError as exc:
        print(f"prep verify-check: cannot read {exc.filename}: {exc.strerror}", file=sys.stderr)
        return EXIT_INPUT
    v = verify_check_bytes(original, prepared, raw, args.check.name)
    _write_json(args.json, verification_json(v, args.original.name, sha256(original), args.prepared.name,
                                             sha256(prepared), args.check.name, created))
    for f in v.failures:
        print(f"  {f.code} at {f.location}: {f.message}", file=sys.stderr)
    if v.passed:
        print(f"PASSED: {args.check.name} is complete and consistent for {args.prepared.name}, and the checker "
              "passed the draft.")
        return EXIT_OK
    print(f"FAILED: {len(v.failures)} problem{'s' if len(v.failures) != 1 else ''} with {args.check.name} "
          "(listed on stderr).")
    return EXIT_INPUT


def _cmd_report(args: argparse.Namespace) -> int:
    from .report import ReportUsageError, evaluate, write_report
    if args.history is not None and not args.history.is_dir():
        raise UsageError(f"--history {args.history} is not a folder")
    created = _created(args)
    ev = evaluate(args.original, args.prepared, args.validation, args.check, args.history, args.draft, created)
    try:
        written = write_report(ev, args.output, args.original, args.prepared)
    except ReportUsageError as exc:
        raise UsageError(str(exc)) from exc
    print(ev.markdown.split("\n", 1)[0])
    for path in written:
        print(f"wrote {path}")
    if not ev.ready:
        for reason in ev.reasons:
            print(f"  {reason}", file=sys.stderr)
    return EXIT_OK if ev.ready else EXIT_INPUT


def _loop_errors(name: str, exc: Exception) -> int:
    problems = getattr(exc, "problems", [str(exc)])
    print(f"prep {name}: refused ({len(problems)} problem{'s' if len(problems) != 1 else ''}):", file=sys.stderr)
    for problem in problems:
        print(f"  {problem}", file=sys.stderr)
    return EXIT_INPUT


def _cmd_start_round(args: argparse.Namespace) -> int:
    from .loop import LoopError, start_round
    try:
        r = start_round(args.workdir, args.original, args.prepared)
    except LoopError as exc:
        return _loop_errors("start-round", exc)
    for note in r.notes:
        print(f"  {note}")
    print(f"round {r.number}{' (restarted: it never reached the validator)' if r.restarted else ''}: "
          f"archived the draft as {r.folder / 'draft.json'}")
    return EXIT_OK


def _cmd_checker_input(args: argparse.Namespace) -> int:
    from .loop import CHECKER_DIR, LoopError, checker_input
    try:
        book = checker_input(args.workdir, args.prepared, args.date)
    except LoopError as exc:
        return _loop_errors("checker-input", exc)
    print(f"wrote {args.workdir / CHECKER_DIR} for round {book['round']}: {len(book['sections'])} sections, "
          f"{len(book['prompt_ids'])} prompts (prepared SHA-256 {book['prepared_sha256']})")
    return EXIT_OK


def _cmd_save_check(args: argparse.Namespace) -> int:
    from .loop import LoopError, save_check
    try:
        path, extracted = save_check(args.workdir)
    except LoopError as exc:
        return _loop_errors("save-check", exc)
    if extracted:
        print(f"wrote {path} from the checker's reply")
        return EXIT_OK
    print(f"wrote {path}: the checker's reply holds no JSON object, so it was saved as it is; verify-check will "
          "reject it and the round fails", file=sys.stderr)
    return EXIT_INPUT


def _cmd_issues(args: argparse.Namespace) -> int:
    from .loop import LoopError, write_issues
    try:
        path, text, _ = write_issues(args.workdir)
    except LoopError as exc:
        return _loop_errors("issues", exc)
    print(text)
    print(f"wrote {path}")
    return EXIT_OK


def _cmd_guard_probe(args: argparse.Namespace) -> int:
    from .probe import ProbeError, agent_prompt, check, prepare
    try:
        if not args.check:
            folder = prepare(args.outdir, args.date)
            print(f"wrote the guard probe {folder}; run the prep-checker agent with this prompt and nothing else:")
            print(agent_prompt(args.outdir))
            return EXIT_OK
        outcome = check(args.outdir)
    except (ProbeError, OSError) as exc:
        print(f"prep guard-probe: {exc}", file=sys.stderr)
        return EXIT_INPUT
    if outcome.passed:
        print(outcome.message)
        return EXIT_OK
    print(outcome.message, file=sys.stderr)
    return EXIT_INPUT


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="python3 -m tools.prep", description="Book preparation tools.")
    sub = parser.add_subparsers(dest="command", metavar="COMMAND")
    sub.required = True

    p = sub.add_parser("extract", help="write the work folder (numbered text, structure, source facts)")
    p.add_argument("epub", type=Path, help="the original EPUB")
    p.add_argument("workdir", type=Path, help="folder to write (created if missing)")
    p.set_defaults(func=_cmd_extract)

    p = sub.add_parser("build", help="write the prepared EPUB from the original and a draft")
    p.add_argument("epub", type=Path, help="the original EPUB (never modified)")
    p.add_argument("draft", type=Path, help="draft.json")
    p.add_argument("-o", "--output", type=Path, required=True, help="the prepared EPUB to write")
    p.set_defaults(func=_cmd_build)

    p = sub.add_parser("validate", help="check a prepared EPUB against its original (every script-verifiable rule)")
    p.add_argument("original", type=Path, help="the original EPUB (only read)")
    p.add_argument("prepared", type=Path, help="the prepared EPUB")
    p.add_argument("--draft", type=Path, help="the draft the book was built from (its exclusions are used)")
    p.add_argument("--work-dir", type=Path, help="the work folder; its draft.json is used when present")
    p.add_argument("--json", type=Path, help="write the result (schema/validation.schema.json) here")
    p.add_argument("--timestamp", help="time to record (ISO 8601, UTC); default $SOURCE_DATE_EPOCH or now")
    p.set_defaults(func=_cmd_validate)

    p = sub.add_parser("verify-check", help="verify the checker's check.json against the files it judged")
    p.add_argument("original", type=Path, help="the original EPUB")
    p.add_argument("prepared", type=Path, help="the prepared EPUB the checker judged")
    p.add_argument("check", type=Path, help="check.json")
    p.add_argument("--json", type=Path, help="write the verification result here")
    p.add_argument("--timestamp", help="time to record (ISO 8601, UTC); default $SOURCE_DATE_EPOCH or now")
    p.set_defaults(func=_cmd_verify_check)

    p = sub.add_parser("report", help="decide READY or NOT READY and write the validation report")
    p.add_argument("--original", type=Path, required=True, help="the original EPUB")
    p.add_argument("--prepared", type=Path, required=True, help="the prepared EPUB to deliver")
    p.add_argument("--validation", type=Path, help="the latest validator result (default: the latest round's)")
    p.add_argument("--check", type=Path, help="the latest checker result (default: the latest round's)")
    p.add_argument("--history", type=Path, help="the work folder holding rounds/NN/{draft,validation,check}.json")
    p.add_argument("--draft", type=Path, help="the draft (for its exclusions; default: the latest round's)")
    p.add_argument("-o", "--output", type=Path, required=True, help="folder for the report (and the EPUB if ready)")
    p.add_argument("--timestamp", help="time to record (ISO 8601, UTC); default $SOURCE_DATE_EPOCH or now")
    p.set_defaults(func=_cmd_report)

    p = sub.add_parser("start-round", help="/prep-book: open the next round and archive the draft")
    p.add_argument("workdir", type=Path, help="the work folder")
    p.add_argument("original", type=Path, help="the original EPUB")
    p.add_argument("prepared", type=Path, help="the prepared EPUB just built from WORKDIR/draft.json")
    p.set_defaults(func=_cmd_start_round)

    p = sub.add_parser("checker-input", help="/prep-book: write WORKDIR/checker/, the checker's only input")
    p.add_argument("workdir", type=Path, help="the work folder")
    p.add_argument("prepared", type=Path, help="the prepared EPUB the round's validator passed")
    p.add_argument("--date", help="the date to give the checker (YYYY-MM-DD); default today (UTC)")
    p.set_defaults(func=_cmd_checker_input)

    p = sub.add_parser("save-check", help="/prep-book: rounds/NN/checker-reply.txt -> rounds/NN/check.json")
    p.add_argument("workdir", type=Path, help="the work folder")
    p.set_defaults(func=_cmd_save_check)

    p = sub.add_parser("issues", help="/prep-book: write rounds/NN/issues.md, the drafter's issue list")
    p.add_argument("workdir", type=Path, help="the work folder")
    p.set_defaults(func=_cmd_issues)

    p = sub.add_parser("guard-probe", help="/prep-book step 1: prove the checker's guard runs in this session")
    p.add_argument("outdir", type=Path, help="the output folder; the probe is OUTDIR/guard-probe/")
    p.add_argument("--check", action="store_true", help="judge the probe after the prep-checker agent ran on it")
    p.add_argument("--date", help="the date to give the checker (YYYY-MM-DD); default today (UTC)")
    p.set_defaults(func=_cmd_guard_probe)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = make_parser()
    args = parser.parse_args(argv)  # exits with status 2 on a usage error
    try:
        return args.func(args)
    except UsageError as exc:
        parser.error(f"{args.command}: {exc}")  # exits with status 2
    except BuildError as exc:
        count = len(exc.issues)
        print(f"prep {args.command}: the draft cannot be built ({count} problem{'s' if count != 1 else ''}):",
              file=sys.stderr)
        for issue in exc.issues:
            print(f"  {issue}", file=sys.stderr)
        return EXIT_INPUT
    except EpubError as exc:
        print(f"prep {args.command}: {exc}", file=sys.stderr)
        return EXIT_INPUT


if __name__ == "__main__":
    sys.exit(main())
