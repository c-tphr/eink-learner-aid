"""python3 -m tools.simtest: list and run simulator scenarios."""

from __future__ import annotations

import argparse
import os
import shutil
import sys
import time
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from . import report, scenario
from .runner import Options, ScenarioResult, ScenarioRunner

DEFAULT_BINARY = scenario.REPO_ROOT / ".pio" / "build" / "simulator_picco" / "program"
DEFAULT_REPORT = scenario.REPO_ROOT / "build" / "simtest-report"


def _parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="python3 -m tools.simtest", description=__doc__)
    p.add_argument("--scenarios-dir", type=Path, default=scenario.SCENARIOS_DIR, help=argparse.SUPPRESS)
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list", help="list scenarios and check that each file is valid")
    r = sub.add_parser("run", help="run scenarios")
    r.add_argument("scenarios", nargs="*", help="scenario names (folders under tools/simtest/scenarios)")
    r.add_argument("--all", action="store_true", help="run every scenario")
    r.add_argument("--binary", type=Path, default=None,
                   help=f"simulator program (default {DEFAULT_BINARY.relative_to(scenario.REPO_ROOT)})")
    r.add_argument("--report-dir", type=Path, default=DEFAULT_REPORT,
                   help=f"report folder, replaced on each run (default {DEFAULT_REPORT.relative_to(scenario.REPO_ROOT)})")
    r.add_argument("--update-refs", action="store_true",
                   help="write each compared screenshot as its new reference and list the ones that changed")
    r.add_argument("--keep", action="store_true", help="keep each scenario's temporary SD folder")
    r.add_argument("--jobs", type=int, default=1, help="scenarios to run at once (each gets its own Xvfb, "
                                                       "SD folder and HTTP port; default 1)")
    r.add_argument("--repeat", type=int, default=1,
                   help="run each scenario N times and report distinct screenshot hashes per shot")
    r.add_argument("--no-xvfb", action="store_true", help=argparse.SUPPRESS)  # self-tests with a stub binary
    return p


def _load_all(root: Path) -> tuple[dict[str, scenario.Scenario], list[str]]:
    loaded, errors = {}, []
    for name, path in scenario.discover(root).items():
        try:
            loaded[name] = scenario.load(path)
        except scenario.ScenarioError as e:
            errors.append(str(e))
    return loaded, errors


def cmd_list(args: argparse.Namespace) -> int:
    loaded, errors = _load_all(args.scenarios_dir)
    for name, sc in loaded.items():
        print(f"{name:32} {sc.description}")
    for e in errors:
        print(f"INVALID {e}", file=sys.stderr)
    if not loaded and not errors:
        print(f"no scenarios under {args.scenarios_dir}")
    return 1 if errors else 0


def _prepare_report_dir(path: Path) -> None:
    if path.exists():
        markers = [path / "results.json", path / "index.html"]
        if path.resolve() == DEFAULT_REPORT.resolve() or any(m.exists() for m in markers) \
                or not any(path.iterdir()):
            shutil.rmtree(path)
        else:
            raise SystemExit(f"error: --report-dir {path} exists and is not a simtest report; "
                             "choose an empty or new folder")
    path.mkdir(parents=True)


def cmd_run(args: argparse.Namespace) -> int:
    root = args.scenarios_dir
    available = scenario.discover(root)
    if args.all and args.scenarios:
        print("error: give scenario names or --all, not both", file=sys.stderr)
        return 2
    names = list(available) if args.all else args.scenarios
    if not names:
        print("error: name at least one scenario, or use --all (see: python3 -m tools.simtest list)",
              file=sys.stderr)
        return 2
    unknown = [n for n in names if n not in available]
    if unknown:
        print(f"error: unknown scenario(s) {', '.join(unknown)}; known: {', '.join(available) or 'none'}",
              file=sys.stderr)
        return 2
    scenarios = []
    for n in names:
        try:
            scenarios.append(scenario.load(available[n]))
        except scenario.ScenarioError as e:
            print(f"error: {e}", file=sys.stderr)
            return 2
    binary = args.binary or DEFAULT_BINARY
    binary = binary if binary.is_absolute() else Path.cwd() / binary
    if not binary.is_file() or not os.access(binary, os.X_OK):
        print(f"error: simulator binary {binary} not found or not executable. Build it with "
              "./scripts/cloud-setup.sh && pio run -e simulator_picco, or pass --binary PATH", file=sys.stderr)
        return 2
    if args.jobs < 1 or args.repeat < 1:
        print("error: --jobs and --repeat must be at least 1", file=sys.stderr)
        return 2
    if args.update_refs and args.repeat > 1:
        print("error: --update-refs and --repeat do not go together", file=sys.stderr)
        return 2
    report_dir = args.report_dir if args.report_dir.is_absolute() else Path.cwd() / args.report_dir
    _prepare_report_dir(report_dir)
    opts = Options(binary=binary, report_dir=report_dir, keep=args.keep, update_refs=args.update_refs,
                   use_xvfb=not args.no_xvfb)

    jobs = [(sc, sc.name if args.repeat == 1 else f"{sc.name}-r{i:02d}")
            for sc in scenarios for i in range(1, args.repeat + 1)]
    started = time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime())
    t0 = time.monotonic()

    def one(job: tuple[scenario.Scenario, str]) -> ScenarioResult:
        sc, label = job
        res = ScenarioRunner(sc, opts, label).run()
        try:
            _print_result(res)
        except OSError:  # a closed stdout (e.g. piped into head) must not lose the report
            pass
        return res

    try:
        print(f"simtest: {len(jobs)} run(s), binary {binary}, report {report_dir}", flush=True)
    except OSError:
        pass
    if args.jobs == 1:
        results = [one(j) for j in jobs]
    else:
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            results = list(pool.map(one, jobs))
    meta = {"binary": str(binary), "started": started, "duration": time.monotonic() - t0,
            "update_refs": args.update_refs, "repeat": args.repeat, "jobs": args.jobs}
    determinism_ok = True
    if args.repeat > 1:
        determinism_ok = _determinism(results, meta)
    report.write_all(results, report_dir, meta)
    passed = sum(r.status == "pass" for r in results)
    print(f"\nsimtest: {passed} passed, {len(results) - passed} failed ({meta['duration']:.1f} s); "
          f"report: {report_dir / 'index.html'}")
    if args.update_refs:
        changed = [u for r in results for u in r.refs_updated]
        print("references updated:" if changed else "references: none changed")
        for u in changed:
            print(f"  {u}")
    return 0 if passed == len(results) and determinism_ok else 1


def _print_result(r: ScenarioResult) -> None:
    head = f"{'PASS' if r.status == 'pass' else 'FAIL'} {r.label} ({r.duration:.1f} s)"
    lines = [head]
    if r.status != "pass":
        failed = [s for s in r.steps if s.status == "fail"]
        msg = r.error or "\n".join(s.message for s in failed)
        for s in failed:
            if s.message not in msg:
                msg += "\n" + s.message
        lines += ["    " + ln for ln in msg.splitlines()]
        for s in failed:
            for kind, path in s.images.items():
                lines.append(f"    {kind}: {path}")
    if r.sd_kept:
        lines.append(f"    SD folder kept: {r.sd_path}")
    print("\n".join(lines), flush=True)


def _determinism(results: list[ScenarioResult], meta: dict) -> bool:
    by_shot: dict[tuple[str, str], set[str]] = defaultdict(set)
    counts: dict[tuple[str, str], int] = defaultdict(int)
    for r in results:
        for shot, info in r.shots.items():
            by_shot[(r.name, shot)].add(info["sha256"])
            counts[(r.name, shot)] += 1
    print("\ndeterminism (distinct screenshot hashes per shot):")
    ok = True
    summary = {}
    for (name, shot), hashes in sorted(by_shot.items()):
        flag = "ok" if len(hashes) == 1 else "VARIES"
        ok = ok and len(hashes) == 1
        print(f"  {name}/{shot}: {counts[(name, shot)]} runs, {len(hashes)} distinct  {flag}")
        summary[f"{name}/{shot}"] = {"runs": counts[(name, shot)], "distinct": len(hashes),
                                     "hashes": sorted(hashes)}
    meta["determinism"] = summary
    return ok


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    if args.cmd == "list":
        return cmd_list(args)
    return cmd_run(args)


if __name__ == "__main__":
    sys.exit(main())
