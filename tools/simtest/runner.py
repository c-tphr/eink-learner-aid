"""Run one scenario against the simulator binary and collect the results."""

from __future__ import annotations

import fnmatch
import json
import os
import shutil
import signal
import tempfile
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any

from . import images, jsonchecks
from .process import EventLog, ProcessError, SimProcess, Xvfb, describe_status, free_port_pair
from .scenario import REPO_ROOT, Run, Scenario, Step

LOG_TAIL_LINES = 40
QUIT_TIMEOUT_S = 15.0
SIM_WAIT_MARGIN_MS = 5000
SMALL_FILE_BYTES = 8192
SMALL_BINARY_BYTES = 256

SIM_FAILURE_PREFIXES = ("[SIM] WAIT timed out", "[SIM] Bad WAIT pattern", "[SIM] Cannot save screenshot",
                        "[SIM] Cannot read screenshot", "[SIM] Cannot determine screenshot",
                        "[SIM] Cannot create screenshot")


@dataclass
class Options:
    binary: Path
    report_dir: Path
    keep: bool = False
    update_refs: bool = False
    use_xvfb: bool = True


@dataclass
class StepResult:
    index: int
    action: str
    line: str
    run: int | None
    status: str = "skipped"  # pass | fail | skipped
    t: float | None = None  # seconds since scenario start when the step finished
    duration: float | None = None
    message: str = ""
    images: dict[str, str] = field(default_factory=dict)  # label -> path relative to the report root


@dataclass
class ScenarioResult:
    name: str
    label: str  # report folder name (name, or name-rN with --repeat)
    description: str
    status: str = "fail"
    duration: float = 0.0
    error: str = ""
    steps: list[StepResult] = field(default_factory=list)
    runs: list[dict[str, Any]] = field(default_factory=list)
    shots: dict[str, dict[str, str]] = field(default_factory=dict)  # name -> {png, sha256}
    refs_updated: list[str] = field(default_factory=list)
    sd_path: str = ""
    sd_kept: bool = False
    sd_listing: list[dict[str, Any]] = field(default_factory=list)
    log_file: str = ""
    scenario_file: str = ""

    def to_json(self) -> dict[str, Any]:
        return asdict(self)


class Failure(Exception):
    def __init__(self, step: Step | None, message: str):
        super().__init__(message)
        self.step = step
        self.message = message


def _rel(p: Path) -> str:
    try:
        return str(p.resolve().relative_to(REPO_ROOT))
    except ValueError:
        return str(p)


def sd_glob(sd: Path, pattern: str) -> list[Path]:
    """Files under the SD folder matching a glob ('*' stays inside one folder; '**' spans folders)."""
    pattern = pattern.lstrip("/")
    hits = []
    for p in sorted(sd.rglob("*")):
        rel = p.relative_to(sd).as_posix()
        if _glob_match(rel, pattern):
            hits.append(p)
    return hits


def _glob_match(rel: str, pattern: str) -> bool:
    parts, pats = rel.split("/"), pattern.split("/")

    def go(i: int, j: int) -> bool:
        if j == len(pats):
            return i == len(parts)
        if pats[j] == "**":
            return any(go(k, j + 1) for k in range(i, len(parts) + 1))
        return i < len(parts) and fnmatch.fnmatchcase(parts[i], pats[j]) and go(i + 1, j + 1)

    return go(0, 0)


def sd_listing(sd: Path) -> list[dict[str, Any]]:
    out = []
    if not sd.is_dir():
        return out
    for p in sorted(sd.rglob("*")):
        rel = p.relative_to(sd).as_posix()
        if p.is_dir():
            out.append({"path": rel + "/", "size": None})
            continue
        size = p.stat().st_size
        entry: dict[str, Any] = {"path": rel, "size": size}
        if size <= SMALL_FILE_BYTES:
            data = p.read_bytes()
            try:
                text = data.decode("utf-8")
                if "\x00" not in text:
                    entry["text"] = text
            except UnicodeDecodeError:
                pass
            if "text" not in entry and size <= SMALL_BINARY_BYTES:
                entry["hex"] = data.hex(" ")
        out.append(entry)
    return out


def clean_env(display: str, home: Path, sd: Path, port: int, run: Run, sc: Scenario,
              wait_timeout_ms: int) -> dict[str, str]:
    """Only what the simulator needs: nothing from the caller's environment
    except a fixed PATH, so a developer's fs_ folder, HOME or proxies never leak in."""
    env = {
        "PATH": "/usr/local/bin:/usr/bin:/bin",
        "HOME": str(home),
        "TMPDIR": str(home),
        "LANG": "C.UTF-8",
        "LC_ALL": "C.UTF-8",
        "TZ": "UTC",
        "SDL_AUDIODRIVER": "dummy",
        "CROSSPOINT_SIM_SD": str(sd),
        "CROSSPOINT_SIM_HTTP_PORT": str(port),
        "CROSSPOINT_SIM_SCREEN_TEXT": "1",
        "CROSSPOINT_SIM_WAIT_TIMEOUT_MS": str(wait_timeout_ms),
        "CROSSPOINT_SIM_SLIDE_SWITCH": run.slide_switch,
        "CROSSPOINT_SIM_INPUT_SCRIPT": ";".join(run.script),
    }
    if display:
        env["DISPLAY"] = display
    if run.script_after_wake is not None:
        env["CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE"] = ";".join(run.script_after_wake)
    env.update(sc.extra_env)
    return env


class ScenarioRunner:
    def __init__(self, sc: Scenario, opts: Options, label: str | None = None):
        self.sc = sc
        self.opts = opts
        self.label = label or sc.name
        self.out = opts.report_dir / self.label
        self.shots_dir = self.out / "shots"
        self.result = ScenarioResult(sc.name, self.label, sc.description, scenario_file=_rel(sc.path))
        self.step_results = {s.index: StepResult(s.index, s.action, s.line, None) for s in sc.steps}
        self.log = EventLog()
        self.proc: SimProcess | None = None
        self.deadline = 0.0
        self.sd = Path()
        self.home = Path()

    # ---- helpers -------------------------------------------------------

    def _finish(self, step: Step, status: str, message: str = "", run: int | None = None,
                duration: float | None = None) -> StepResult:
        r = self.step_results[step.index]
        r.status = status
        r.message = message
        r.t = self.log.now()
        if run is not None:
            r.run = run
        if duration is not None:
            r.duration = max(0.0, duration)
        return r

    def _fail_text(self, message: str) -> str:
        tail = "\n".join(self.log.tail(LOG_TAIL_LINES))
        return f"{message}\n--- last {LOG_TAIL_LINES} lines of the simulator log ---\n{tail}"

    def _deadline(self, seconds: float) -> float:
        return min(time.monotonic() + seconds, self.deadline)

    def _overall_left(self) -> float:
        return self.deadline - time.monotonic()

    # ---- main ----------------------------------------------------------

    def run(self) -> ScenarioResult:
        start = time.monotonic()
        self.deadline = start + self.sc.timeout_s
        self.out.mkdir(parents=True, exist_ok=True)
        self.shots_dir.mkdir(parents=True, exist_ok=True)
        if ";" in str(self.shots_dir.resolve()):
            raise ProcessError(f"report folder path must not contain ';': {self.shots_dir}")
        self.sd = Path(tempfile.mkdtemp(prefix=f"simtest-{self.sc.name}-sd-"))
        self.home = Path(tempfile.mkdtemp(prefix=f"simtest-{self.sc.name}-home-"))
        self.result.sd_path = str(self.sd)
        xvfb = Xvfb() if self.opts.use_xvfb else None
        runs = self.sc.compiled(str(self.shots_dir.resolve()))
        try:
            self._populate_sd()
            display = xvfb.start() if xvfb else os.environ.get("DISPLAY", "")
            if xvfb:
                self.log.note(f"Xvfb started on display {display} (pid {xvfb.proc.pid})")
            port = free_port_pair()
            for run in runs:
                self._run_process(run, display, port)
            self._compare_shots(failed=False)
            self.result.status = "pass"
        except Failure as f:
            self.result.error = f.message
            if f.step is not None:
                self._finish(f.step, "fail", f.message)
            self.log.note(f"FAILED: {f.message.splitlines()[0]}")
            self._compare_shots(failed=True)
        except ProcessError as e:
            self.result.error = str(e)
            self.log.note(f"FAILED: {e}")
        except BaseException as e:  # KeyboardInterrupt included: always clean up below
            self.result.error = f"{type(e).__name__}: {e}"
            self.log.note(f"FAILED: {self.result.error}")
            raise
        finally:
            if self.proc and self.proc.alive():
                code = self.proc.stop_hard()
                self.log.note(f"cleanup: process group {self.proc.pid} killed ({describe_status(code)})")
            if xvfb:
                xvfb.stop()
            self.result.sd_listing = sd_listing(self.sd)
            self.result.duration = time.monotonic() - start
            self.result.steps = [self.step_results[s.index] for s in self.sc.steps]
            log_path = self.out / "log.txt"
            log_path.write_text(self.log.text(), encoding="utf-8")
            self.result.log_file = f"{self.label}/log.txt"
            if self.opts.keep:
                self.result.sd_kept = True
            else:
                shutil.rmtree(self.sd, ignore_errors=True)
            shutil.rmtree(self.home, ignore_errors=True)
        if any(r.status == "fail" for r in self.result.steps):
            self.result.status = "fail"
        return self.result

    def _populate_sd(self) -> None:
        for f in self.sc.sd:
            dst = self.sd / f.dst
            dst.parent.mkdir(parents=True, exist_ok=True)
            if f.src is not None:
                if f.src.is_dir():
                    shutil.copytree(f.src, dst)
                else:
                    shutil.copyfile(f.src, dst)
                self.log.note(f"SD: {_rel(f.src)} -> /{f.dst}")
            else:
                dst.write_bytes(f.content or b"")
                self.log.note(f"SD: wrote /{f.dst} ({len(f.content or b'')} bytes)")

    # ---- one process ---------------------------------------------------

    def _run_process(self, run: Run, display: str, port: int) -> None:
        sim_wait = max([s.timeout_ms for s in run.syncs] + [self.sc.wait_timeout_ms]) + SIM_WAIT_MARGIN_MS
        env = clean_env(display, self.home, self.sd, port, run, self.sc, sim_wait)
        workdir = self.home  # the simulator's cwd; its default ./fs_ is never used
        self.log.note(f"run {run.number}: starting {self.opts.binary} on SD folder {self.sd} "
                      f"(slide switch {run.slide_switch}, display {display or 'none'}, http port {port})")
        self.log.note(f"run {run.number}: CROSSPOINT_SIM_INPUT_SCRIPT={env['CROSSPOINT_SIM_INPUT_SCRIPT']}")
        if run.script_after_wake is not None:
            self.log.note(f"run {run.number}: CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE="
                          f"{env['CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE']}")
        try:
            self.proc = SimProcess([str(self.opts.binary)], env, workdir, self.log)
        except OSError as e:
            raise Failure(run.start_step or (run.steps[0] if run.steps else None),
                          f"cannot start simulator {self.opts.binary}: {e}") from None
        proc = self.proc
        if run.start_step is not None:
            self._finish(run.start_step, "pass", f"pid {proc.pid} on the same SD folder", run.number)
        info = {"number": run.number, "pid": proc.pid, "sd": str(self.sd), "end": run.end, "exit": None}
        self.result.runs.append(info)
        self.log.note(f"run {run.number}: pid {proc.pid}")

        sync_iter = iter(run.syncs)
        pending: list[Step] = []  # inputs sent since the last sync
        last_sync_t = time.monotonic()
        last_sync_line = proc.start_index
        scan_from = proc.start_index
        anchor = proc.start_index - 1  # log index of the line the last WAIT matched
        segment_started = time.monotonic()

        def fail_line(text: str) -> str | None:
            for prefix in SIM_FAILURE_PREFIXES:
                if text.startswith(prefix):
                    return text
            return None

        for step in run.steps:
            if step.kind == "log":
                self._check_screen(step, run, last_sync_line, proc.start_index)
                continue
            if step.action not in ("wait_log", "wait_screen", "shot"):
                pending.append(step)
                continue
            sync = next(sync_iter)
            limit_s = (sync.offset_ms + sync.timeout_ms) / 1000.0
            deadline = min(last_sync_t + limit_s, self.deadline)
            if sync.kind == "wait":
                def match(_i: int, text: str) -> bool:
                    return text.startswith("[SIM] WAIT matched:")
                what = f"log line matching /{step.value}/" if step.action == "wait_log" else \
                    f"screen text containing {step.value!r}"
            else:
                expected = f"[SIM] Saved screenshot: {sync.shot_file}"

                def match(_i: int, text: str, expected: str = expected) -> bool:
                    return text == expected
                what = f"screenshot {step.value!r} to be saved"
            status, index = proc.wait_for(match, deadline, fail_line, start=scan_from)
            if status == "ok":
                now = time.monotonic()
                for p in pending:
                    self._finish(p, "pass", "sent", run.number)
                pending = []
                matched = self.log.lines[index].text.removeprefix("[SIM] WAIT matched: ")
                if sync.kind == "wait":
                    anchor = self._check_fresh(step, matched, anchor, index)
                msg = f"matched: {matched}" if sync.kind == "wait" else f"saved {Path(sync.shot_file).name}"
                self._finish(step, "pass", msg, run.number, duration=now - segment_started)
                last_sync_t = now
                segment_started = now
                last_sync_line = index
                scan_from = index + 1
                continue
            for p in pending:
                self._finish(p, "pass", "sent (not observable on its own)", run.number)
            self.step_results[step.index].run = run.number
            if status == "timeout":
                overall = time.monotonic() >= self.deadline
                why = (f"scenario exceeded its overall timeout of {self.sc.timeout_s} s" if overall else
                       f"timed out after {sync.timeout_ms} ms waiting for {what}")
                raise Failure(step, self._fail_text(f"{step.describe()}: {why}"))
            if status == "exit":
                code = proc.wait_exit(1)
                info["exit"] = code
                raise Failure(step, self._fail_text(
                    f"{step.describe()}: simulator ended ({describe_status(code)}) while waiting for {what}"))
            raise Failure(step, self._fail_text(f"{step.describe()}: simulator reported: {status[5:]}"))

        for step in run.tail:
            self._tail_step(step, run, info)
        if run.end == "end":
            self._quit(run, info, None)

    def _check_fresh(self, step: Step, matched: str, anchor: int, index: int) -> int:
        """Return the log index of the line this WAIT matched, which must come
        after `anchor` (the line the previous WAIT of this run matched).

        The simulator's WAIT searches every line since the last scripted
        *input*, so two waits in a row can both match old lines and not wait
        at all. The harness has the full log and refuses such a match."""
        lines = self.log.lines
        for j in range(anchor + 1, index):
            if lines[j].source == "sim" and lines[j].text == matched:
                return j
        raise Failure(step, self._fail_text(
            f"{step.describe()}: the simulator's WAIT matched a line logged before the line the previous wait "
            f"matched: {matched!r}. The simulator's WAIT looks back to the last scripted input, not to the "
            "previous WAIT, so this step did not wait. Wait for a line that only the new state logs, or "
            "drop this wait"))

    def _check_screen(self, step: Step, run: Run, upto: int, start: int) -> None:
        screen = None
        for i in range(min(upto, len(self.log.lines) - 1), start - 1, -1):
            text = self.log.lines[i].text
            if text.startswith("[SIM] SCREEN:"):
                screen = text
                break
        if screen is None:
            raise Failure(step, self._fail_text(f"{step.describe()}: no screen text was logged yet in this run"))
        has = step.value in screen
        if (step.action == "screen_has") != has:
            verb = "does not contain" if step.action == "screen_has" else "contains"
            raise Failure(step, self._fail_text(f"{step.describe()}: the screen {verb} {step.value!r}: {screen}"))
        self._finish(step, "pass", screen[:300], run.number)

    def _tail_step(self, step: Step, run: Run, info: dict[str, Any]) -> None:
        a = step.action
        started = self.log.now()
        if a == "kill":
            assert self.proc
            self.log.note(f"run {run.number}: sending SIGKILL to process group {self.proc.pid} "
                          "(imitates pulling the battery)")
            code = self.proc.stop_hard()
            info["exit"] = code
            self.log.note(f"run {run.number}: pid {self.proc.pid} {describe_status(code)}; "
                          f"SD folder {self.sd} kept for the next start")
            self._finish(step, "pass", f"pid {self.proc.pid} {describe_status(code)}", run.number,
                         duration=self.log.now() - started)
            return
        if a == "quit":
            self._quit(run, info, step)
            return
        if a == "wait_file":
            deadline = self._deadline(step.opts.get("timeout_ms", self.sc.wait_timeout_ms) / 1000.0)
            while True:
                hits = sd_glob(self.sd, step.value)
                if hits:
                    self._finish(step, "pass", f"found /{hits[0].relative_to(self.sd).as_posix()}", run.number)
                    return
                if time.monotonic() >= deadline:
                    raise Failure(step, self._fail_text(
                        f"{step.describe()}: no file matching /{step.value} appeared on the SD card within "
                        f"{step.opts.get('timeout_ms', self.sc.wait_timeout_ms)} ms"))
                time.sleep(0.05)
        hits = sd_glob(self.sd, step.value)
        if a == "expect_no_file":
            if hits:
                raise Failure(step, f"{step.describe()}: expected no file matching /{step.value}, found "
                                    + ", ".join("/" + h.relative_to(self.sd).as_posix() for h in hits))
            self._finish(step, "pass", "no match", run.number)
            return
        files = [h for h in hits if h.is_file()]
        if not files:
            raise Failure(step, f"{step.describe()}: no file matches /{step.value} on the SD card"
                                f"{self._near(step.value)}")
        if len(files) > 1 and a != "expect_file":
            raise Failure(step, f"{step.describe()}: /{step.value} matches {len(files)} files; make it unique: "
                                + ", ".join("/" + h.relative_to(self.sd).as_posix() for h in files))
        target = files[0]
        shown = "/" + target.relative_to(self.sd).as_posix()
        if a == "expect_file":
            for f in files:
                data = f.read_bytes()
                if "hex_prefix" in step.opts:
                    want = bytes.fromhex(step.opts["hex_prefix"])
                    if not data.startswith(want):
                        raise Failure(step, f"{step.describe()}: /{f.relative_to(self.sd).as_posix()} starts "
                                            f"with {data[:len(want) + 4].hex(' ')}, expected {want.hex(' ')}")
                if "contains" in step.opts and step.opts["contains"].encode() not in data:
                    raise Failure(step, f"{step.describe()}: /{f.relative_to(self.sd).as_posix()} does not "
                                        f"contain {step.opts['contains']!r}")
            self._finish(step, "pass", ", ".join("/" + f.relative_to(self.sd).as_posix() for f in files),
                         run.number)
            return
        try:
            doc = json.loads(target.read_text(encoding="utf-8"))
        except (UnicodeDecodeError, ValueError) as e:
            raise Failure(step, f"{step.describe()}: {shown} is not valid JSON: {e}") from None
        if a == "expect_json":
            try:
                got = jsonchecks.resolve_pointer(doc, step.opts["pointer"])
            except jsonchecks.JsonCheckError as e:
                raise Failure(step, f"{step.describe()}: {shown}: {e}") from None
            if not jsonchecks.json_equal(got, step.opts["equals"]):
                raise Failure(step, f"{step.describe()}: {shown} at '{step.opts['pointer']}' is "
                                    f"{json.dumps(got)}, expected {json.dumps(step.opts['equals'])}")
            self._finish(step, "pass", f"{shown} {step.opts['pointer'] or '(root)'} = {json.dumps(got)}",
                         run.number)
            return
        if a == "expect_schema":
            schema = json.loads((REPO_ROOT / step.opts["schema"]).read_text(encoding="utf-8"))
            try:
                errors = jsonchecks.validate(doc, schema)
            except jsonchecks.JsonCheckError as e:
                raise Failure(step, f"{step.describe()}: schema problem: {e}") from None
            if errors:
                raise Failure(step, f"{step.describe()}: {shown} does not match {step.opts['schema']}:\n  "
                                    + "\n  ".join(errors[:20]))
            self._finish(step, "pass", f"{shown} matches {step.opts['schema']}", run.number)
            return
        raise AssertionError(a)

    def _near(self, pattern: str) -> str:
        folder = self.sd / Path(pattern.lstrip("/")).parent
        while not folder.is_dir() and folder != self.sd:
            folder = folder.parent
        names = sorted(p.name + ("/" if p.is_dir() else "") for p in folder.iterdir())[:30]
        return f" (/{folder.relative_to(self.sd).as_posix()} holds: {', '.join(names) or 'nothing'})"

    def _quit(self, run: Run, info: dict[str, Any], step: Step | None) -> None:
        assert self.proc
        proc = self.proc
        started = time.monotonic()
        if not proc.alive():
            code = proc.wait_exit(1)
            info["exit"] = code
            raise Failure(step, self._fail_text(f"run {run.number}: simulator had already ended "
                                                f"({describe_status(code)}) before the clean quit"))
        # SDL turns SIGTERM into SDL_QUIT, the same event as closing the window,
        # so this is the simulator's normal shutdown path (exit status 0).
        self.log.note(f"run {run.number}: sending SIGTERM to pid {proc.pid} (clean quit)")
        proc.signal_main(signal.SIGTERM)
        code = proc.wait_exit(min(QUIT_TIMEOUT_S, max(1.0, self._overall_left())))
        if code is None:
            code = proc.stop_hard()
            info["exit"] = code
            raise Failure(step, self._fail_text(f"run {run.number}: simulator did not quit within "
                                                f"{QUIT_TIMEOUT_S:.0f} s of SIGTERM; killed"))
        proc.signal_group(signal.SIGKILL)  # anything it left behind
        info["exit"] = code
        self.log.note(f"run {run.number}: pid {proc.pid} {describe_status(code)}")
        if code != 0:
            raise Failure(step, self._fail_text(f"run {run.number}: simulator quit with {describe_status(code)}"))
        if step is not None:
            self._finish(step, "pass", describe_status(code), run.number, duration=time.monotonic() - started)

    # ---- screenshots ---------------------------------------------------

    def _compare_shots(self, failed: bool) -> None:
        for name in self.sc.shots():
            bmp = self.shots_dir / f"{name}.bmp"
            if not bmp.is_file():
                continue
            png = self.shots_dir / f"{name}.png"
            img = images.bmp_to_png(bmp, png)
            bmp.unlink()
            self.result.shots[name] = {"png": f"{self.label}/shots/{name}.png", "sha256": images.pixel_hash(img)}
        for step in self.sc.steps:
            if step.action != "compare":
                continue
            r = self.step_results[step.index]
            shot = self.result.shots.get(step.value)
            if shot is None:
                r.status = "skipped"
                r.message = "shot was not taken"
                continue
            ref_path = self.sc.refs_dir / f"{step.value}.png"
            actual = images.Image.open(self.opts.report_dir / shot["png"])
            actual.load()
            r.images["actual"] = shot["png"]
            masks = [tuple(m) for m in step.opts.get("mask", [])]
            other = step.opts.get("same_as")
            if other is not None:
                # Compared with another screenshot of this scenario, not with a stored reference.
                other_shot = self.result.shots.get(other)
                if other_shot is None:
                    r.status, r.message = "skipped", f"shot {other!r} was not taken"
                    continue
                ref_path = self.opts.report_dir / other_shot["png"]
            elif self.opts.update_refs:
                if failed:
                    r.status = "skipped"
                    r.message = "reference not updated: the scenario failed"
                    continue
                if ref_path.is_file():
                    with images.Image.open(ref_path) as old:
                        cmp = images.compare(actual, old)
                    if cmp.equal:
                        r.status, r.message = "pass", "reference unchanged"
                        continue
                    note = f"changed: {cmp.message}"
                else:
                    note = "new"
                images.save_png(actual, ref_path)
                self.result.refs_updated.append(f"{_rel(ref_path)} ({note})")
                r.status, r.message = "pass", f"reference updated ({note})"
                continue
            if not ref_path.is_file():
                r.status = "fail"
                r.message = (f"no reference image {_rel(ref_path)}; run with --update-refs, "
                             "then review the new image before committing it")
                continue
            with images.Image.open(ref_path) as ref:
                ref.load()
                cmp = images.compare(actual, ref, masks, step.opts.get("max_diff_pixels", 0))
                what = f"shot {other!r}" if other else "reference"
                if cmp.equal:
                    r.status = "pass"
                    r.message = f"identical to {what}" if not cmp.diff_pixels else cmp.message
                    continue
                diff_dir = self.out / "diff"
                diff_dir.mkdir(exist_ok=True)
                images.save_png(ref, diff_dir / f"{step.value}.reference.png")
                images.save_png(actual, diff_dir / f"{step.value}.actual.png")
                images.diff_image(actual, ref, masks, cmp.bbox).save(diff_dir / f"{step.value}.diff.png")
            r.images["reference"] = f"{self.label}/diff/{step.value}.reference.png"
            r.images["actual"] = f"{self.label}/diff/{step.value}.actual.png"
            r.images["diff"] = f"{self.label}/diff/{step.value}.diff.png"
            r.status = "fail"
            against = f"shot {other!r}" if other else _rel(ref_path)
            r.message = f"shot {step.value!r} differs from {against}: {cmp.message}"
