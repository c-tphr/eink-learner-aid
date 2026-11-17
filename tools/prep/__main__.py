"""Command-line entry point: ``python3 -m tools.prep <command> ...``.

    python3 -m tools.prep extract BOOK.epub WORKDIR
    python3 -m tools.prep build BOOK.epub DRAFT.json -o OUT.prepared.epub

Exit status: 0 success, 1 a problem with the input (each problem is printed
on stderr), 2 a usage error. ``validate`` and ``report`` are added by a later
task as further sub-commands.
"""

from __future__ import annotations

import argparse
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
    return parser


def main(argv: list[str] | None = None) -> int:
    args = make_parser().parse_args(argv)  # exits with status 2 on a usage error
    try:
        return args.func(args)
    except BuildError as exc:
        print(f"prep {args.command}: the draft cannot be built ({len(exc.issues)} problems):", file=sys.stderr)
        for issue in exc.issues:
            print(f"  {issue}", file=sys.stderr)
        return EXIT_INPUT
    except EpubError as exc:
        print(f"prep {args.command}: {exc}", file=sys.stderr)
        return EXIT_INPUT


if __name__ == "__main__":
    sys.exit(main())
