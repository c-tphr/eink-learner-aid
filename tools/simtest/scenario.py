"""Scenario files: parsing, validation and compilation into simulator runs.

A scenario is tools/simtest/scenarios/<name>/scenario.toml. See README.md for
the format. This module only reads and checks files; it never starts a
process, so it is unit-tested without the simulator.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

try:
    import tomllib  # Python 3.11+
except ModuleNotFoundError:  # pragma: no cover - Python 3.10 and older
    import tomli as tomllib  # type: ignore[no-redef]

SIMTEST_DIR = Path(__file__).resolve().parent
REPO_ROOT = SIMTEST_DIR.parent.parent
SCENARIOS_DIR = SIMTEST_DIR / "scenarios"

NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")
SHOT_RE = re.compile(r"^[a-z0-9][a-z0-9_-]*$")

# Keys the simulator script accepts (HalGPIO.cpp namedButton, at the pinned
# simulator commit). On the Picco profile only these reach the firmware;
# BACK, LEFT, RIGHT and HOME are parsed but dropped (buttonAvailable()).
PICCO_KEYS = {"ENTER": "ENTER", "CONFIRM": "ENTER", "RETURN": "ENTER", "UP": "UP",
              "DOWN": "DOWN", "POWER": "POWER", "P": "POWER"}
DROPPED_ON_PICCO = {"BACK", "ESCAPE", "LEFT", "RIGHT", "HOME", "PREV", "NEXT"}

# Environment variables the harness owns; a scenario may not set them.
HARNESS_VARS = {
    "CROSSPOINT_SIM_SD", "CROSSPOINT_EMU_SD", "CROSSPOINT_SIM_INPUT_SCRIPT",
    "CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE", "CROSSPOINT_SIM_SCREENSHOTS",
    "CROSSPOINT_SIM_SCREENSHOTS_AFTER_WAKE", "CROSSPOINT_SIM_SCREEN_TEXT",
    "CROSSPOINT_SIM_WAIT_TIMEOUT_MS", "CROSSPOINT_SIM_HTTP_PORT", "CROSSPOINT_SIM_SLIDE_SWITCH",
    "CROSSPOINT_SIM_WAKE_REASON", "DISPLAY", "HOME",
}

DEFAULT_WAIT_TIMEOUT_MS = 15000
DEFAULT_SCENARIO_TIMEOUT_S = 180
DEFAULT_HOLD_MS = 80
DEFAULT_SWIPE_MS = 250
# Time between the end of one scripted input and the next scripted event when
# no wait separates them, so they land in different simulator frames.
INPUT_GAP_MS = 120

ACTIONS = {
    # in-process: compiled into CROSSPOINT_SIM_INPUT_SCRIPT
    "wait_log": "process", "wait_screen": "process", "press": "process", "tap": "process",
    "swipe": "process", "switch": "process", "shot": "process", "sleep": "process",
    "wake": "process",
    # read from the captured log after the run, at the position of the previous sync step
    "screen_has": "log", "screen_lacks": "log",
    # harness side: only where the simulator script has nothing left to do
    "wait_file": "tail", "expect_file": "tail", "expect_no_file": "tail", "expect_json": "tail",
    "expect_schema": "tail",
    # process control
    "kill": "control", "quit": "control", "start": "control",
    # image comparison, after the scenario
    "compare": "post",
}

MODIFIERS = {
    "wait_log": {"timeout_ms", "note"},
    "wait_screen": {"timeout_ms", "note"},
    "press": {"hold_ms", "delay_ms", "note"},
    "tap": {"hold_ms", "delay_ms", "note"},
    "swipe": {"duration_ms", "delay_ms", "note"},
    "switch": {"delay_ms", "note"},
    "shot": {"delay_ms", "note"},
    "sleep": {"delay_ms", "note"},
    "wake": {"hold_ms", "delay_ms", "note"},
    "screen_has": {"note"},
    "screen_lacks": {"note"},
    "wait_file": {"timeout_ms", "note"},
    "expect_file": {"hex_prefix", "contains", "note"},
    "expect_no_file": {"note"},
    "expect_json": {"pointer", "equals", "note"},
    "expect_schema": {"schema", "note"},
    "kill": {"note"},
    "quit": {"note"},
    "start": {"slide_switch", "note"},
    "compare": {"mask", "max_diff_pixels", "same_as", "note"},
}


class ScenarioError(Exception):
    """A scenario file problem. str() names the file and the step."""


@dataclass
class Step:
    index: int  # 1-based position in the scenario's steps list
    action: str
    value: Any
    opts: dict[str, Any]
    line: str = ""  # the step as written, for reports

    @property
    def kind(self) -> str:
        return ACTIONS[self.action]

    def describe(self) -> str:
        return f"step {self.index} ({self.action})"


@dataclass
class SdFile:
    dst: str
    src: Path | None = None
    content: bytes | None = None


@dataclass
class Sync:
    """An event the harness sees in the simulator log: a WAIT match or a saved SHOT."""
    step: Step
    kind: str  # "wait" or "shot"
    offset_ms: int  # scheduled time after the previous sync (segment start)
    timeout_ms: int
    shot_file: str = ""


@dataclass
class Run:
    """One simulator process lifetime (a sleep/wake relaunch stays in the same run)."""
    number: int
    slide_switch: str
    script: list[str] = field(default_factory=list)
    script_after_wake: list[str] | None = None
    syncs: list[Sync] = field(default_factory=list)
    steps: list[Step] = field(default_factory=list)  # in-process and log steps, in order
    # After the last sync: harness-side checks and the kill/quit step, in order.
    tail: list[Step] = field(default_factory=list)
    end: str = "quit"  # "kill", "quit" or "end" (clean quit after the scenario's last step)
    end_step: Step | None = None
    start_step: Step | None = None  # the { start = true } step that began this run (runs 2+)


@dataclass
class Scenario:
    name: str
    path: Path
    description: str
    timeout_s: int
    wait_timeout_ms: int
    sd: list[SdFile]
    slide_switch: str
    extra_env: dict[str, str]
    steps: list[Step]
    raw_text: str = ""

    @property
    def dir(self) -> Path:
        return self.path.parent

    @property
    def refs_dir(self) -> Path:
        return self.dir / "refs"

    def shots(self) -> list[str]:
        return [s.value for s in self.steps if s.action == "shot"]

    def compiled(self, shot_dir: str) -> list[Run]:
        return compile_runs(self, shot_dir)


def _err(path: Path, where: str, msg: str) -> ScenarioError:
    rel = path
    try:
        rel = path.relative_to(REPO_ROOT)
    except ValueError:
        pass
    return ScenarioError(f"{rel}: {where}: {msg}")


def _int(path: Path, where: str, value: Any, name: str, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise _err(path, where, f"{name} must be an integer >= {minimum}, got {value!r}")
    return value


def _regex_ok(path: Path, where: str, pattern: str) -> None:
    if not isinstance(pattern, str) or not pattern:
        raise _err(path, where, "pattern must be a non-empty string")
    if "\n" in pattern:
        raise _err(path, where, "pattern must be on one line")
    try:
        re.compile(pattern)
    except re.error as e:
        raise _err(path, where, f"bad regular expression {pattern!r}: {e}") from None


def sim_regex(pattern: str) -> str:
    """Make a pattern safe inside the simulator script (';' separates items)."""
    return pattern.replace(";", r"\x3B")


def _parse_step(path: Path, index: int, raw: Any) -> Step:
    where = f"step {index}"
    if not isinstance(raw, dict):
        raise _err(path, where, f"each step must be a table like {{ press = \"DOWN\" }}, got {raw!r}")
    actions = [k for k in raw if k in ACTIONS]
    if len(actions) != 1:
        found = ", ".join(actions) if actions else "none"
        raise _err(path, where, f"each step needs exactly one action ({', '.join(sorted(ACTIONS))}); "
                                f"found {found} in {raw!r}")
    action = actions[0]
    where = f"step {index} ({action})"
    unknown = sorted(set(raw) - {action} - MODIFIERS[action])
    if unknown:
        allowed = ", ".join(sorted(MODIFIERS[action])) or "none"
        raise _err(path, where, f"unknown option(s) {', '.join(unknown)}; allowed: {allowed}")
    value = raw[action]
    opts = {k: v for k, v in raw.items() if k != action}
    for key in ("timeout_ms", "hold_ms", "delay_ms", "duration_ms", "max_diff_pixels"):
        if key in opts:
            _int(path, where, opts[key], key, 1 if key in ("timeout_ms", "hold_ms", "duration_ms") else 0)

    if action == "wait_log":
        _regex_ok(path, where, value)
    elif action in ("wait_screen", "screen_has", "screen_lacks"):
        if not isinstance(value, str) or not value or "\n" in value:
            raise _err(path, where, "text must be a non-empty one-line string")
    elif action in ("press", "wake"):
        if not isinstance(value, str):
            raise _err(path, where, f"key must be a string, got {value!r}")
        key = value.upper()
        if key in DROPPED_ON_PICCO:
            raise _err(path, where, f"key {value!r} does not exist on the Picco profile; the simulator "
                                    f"drops it. Use one of: {', '.join(sorted(set(PICCO_KEYS.values())))}")
        if key not in PICCO_KEYS:
            raise _err(path, where, f"unknown key {value!r}; use one of: "
                                    f"{', '.join(sorted(set(PICCO_KEYS.values())))}")
        value = PICCO_KEYS[key]
    elif action == "tap":
        if (not isinstance(value, list) or len(value) != 2
                or not all(isinstance(v, int) and not isinstance(v, bool) for v in value)):
            raise _err(path, where, f"tap takes [x, y] in screen pixels, got {value!r}")
        _check_xy(path, where, value)
    elif action == "swipe":
        if (not isinstance(value, list) or len(value) != 4
                or not all(isinstance(v, int) and not isinstance(v, bool) for v in value)):
            raise _err(path, where, f"swipe takes [x1, y1, x2, y2] in screen pixels, got {value!r}")
        _check_xy(path, where, value[:2])
        _check_xy(path, where, value[2:])
    elif action == "switch":
        if value not in ("on", "off"):
            raise _err(path, where, f"switch takes \"on\" or \"off\", got {value!r}")
    elif action in ("shot", "compare"):
        if not isinstance(value, str) or not SHOT_RE.match(value):
            raise _err(path, where, f"shot name must match {SHOT_RE.pattern}, got {value!r}")
    elif action in ("sleep", "kill", "quit", "start"):
        if value is not True:
            raise _err(path, where, f"write {{ {action} = true }}")
    elif action in ("wait_file", "expect_file", "expect_no_file", "expect_json", "expect_schema"):
        _check_sd_path(path, where, value, glob_ok=True)

    if action == "expect_file":
        if "hex_prefix" in opts:
            try:
                bytes.fromhex(opts["hex_prefix"])
            except (TypeError, ValueError):
                raise _err(path, where, f"hex_prefix must be hex digits, got {opts['hex_prefix']!r}") from None
        if "contains" in opts and not isinstance(opts["contains"], str):
            raise _err(path, where, "contains must be a string")
    if action == "expect_json":
        if "equals" not in opts:
            raise _err(path, where, "expect_json needs equals = <value> (and usually pointer = \"/a/b\")")
        pointer = opts.setdefault("pointer", "")
        if not isinstance(pointer, str) or (pointer and not pointer.startswith("/")):
            raise _err(path, where, f"pointer must be \"\" or start with '/', got {pointer!r}")
    if action == "expect_schema":
        schema = opts.get("schema")
        if not isinstance(schema, str):
            raise _err(path, where, "expect_schema needs schema = \"<repo-relative path to a .json schema>\"")
        schema_path = REPO_ROOT / schema
        if not schema_path.is_file():
            raise _err(path, where, f"schema file {schema} not found (paths are relative to the repo root)")
        try:
            json.loads(schema_path.read_text(encoding="utf-8"))
        except ValueError as e:
            raise _err(path, where, f"schema {schema} is not valid JSON: {e}") from None
    if action == "start" and "slide_switch" in opts and opts["slide_switch"] not in ("on", "off"):
        raise _err(path, where, "slide_switch must be \"on\" or \"off\"")
    if action == "compare" and "mask" in opts:
        mask = opts["mask"]
        if not isinstance(mask, list) or not all(
                isinstance(r, list) and len(r) == 4 and all(isinstance(v, int) and v >= 0 for v in r)
                for r in mask):
            raise _err(path, where, "mask must be a list of [x, y, width, height] rectangles")
    return Step(index, action, value, opts, line=_render(raw))


def _render(raw: dict[str, Any]) -> str:
    parts = []
    for k, v in raw.items():
        parts.append(f"{k} = {json.dumps(v)}")
    return "{ " + ", ".join(parts) + " }"


def _check_xy(path: Path, where: str, xy: list[int]) -> None:
    # Portrait UI is 480x800; landscape 800x480. Accept the union, reject nonsense.
    if not (0 <= xy[0] < 800 and 0 <= xy[1] < 800):
        raise _err(path, where, f"coordinates {xy} are outside the 480x800 / 800x480 screen")
    # The simulator reads values from 0.0 to 1.0 as fractions of the screen
    # (HalGPIO.cpp parseTouchSpec), so 1 means the far edge, not pixel 1.
    if 1 in xy:
        raise _err(path, where, f"coordinate value 1 in {xy} is read by the simulator as the far edge "
                                "(values 0..1 are fractions); use 0 or 2")


def _check_sd_path(path: Path, where: str, value: Any, glob_ok: bool = False) -> None:
    if not isinstance(value, str) or not value:
        raise _err(path, where, "SD path must be a non-empty string")
    p = value.lstrip("/")
    if p != value.removeprefix("/") or ".." in Path(p).parts or "\\" in p:
        raise _err(path, where, f"SD path {value!r} must stay inside the SD card")
    if not glob_ok and any(c in p for c in "*?["):
        raise _err(path, where, f"SD path {value!r} must not contain wildcards")


def load(path: Path) -> Scenario:
    path = Path(path).resolve()
    if not path.is_file():
        raise ScenarioError(f"{path}: scenario file not found")
    name = path.parent.name
    if not NAME_RE.match(name):
        raise _err(path, "name", f"scenario folder name {name!r} must match {NAME_RE.pattern}")
    text = path.read_text(encoding="utf-8")
    try:
        data = tomllib.loads(text)
    except tomllib.TOMLDecodeError as e:
        raise _err(path, "TOML", str(e)) from None

    known = {"description", "timeout_s", "wait_timeout_ms", "sd", "env", "steps"}
    unknown = sorted(set(data) - known)
    if unknown:
        raise _err(path, "top level", f"unknown key(s) {', '.join(unknown)}; allowed: {', '.join(sorted(known))}")
    description = data.get("description", "")
    if not isinstance(description, str) or not description.strip():
        raise _err(path, "description", "write a one-line description of the flow")
    timeout_s = _int(path, "timeout_s", data.get("timeout_s", DEFAULT_SCENARIO_TIMEOUT_S), "timeout_s", 1)
    wait_timeout = _int(path, "wait_timeout_ms", data.get("wait_timeout_ms", DEFAULT_WAIT_TIMEOUT_MS),
                        "wait_timeout_ms", 1)

    sd: list[SdFile] = []
    raw_sd = data.get("sd", [])
    if not isinstance(raw_sd, list):
        raise _err(path, "sd", "sd must be a list of { src = ..., dst = ... } tables")
    for i, entry in enumerate(raw_sd, 1):
        where = f"sd entry {i}"
        if not isinstance(entry, dict) or "dst" not in entry:
            raise _err(path, where, "each sd entry needs dst = \"<path on the SD card>\"")
        extra = sorted(set(entry) - {"src", "dst", "text", "json"})
        if extra:
            raise _err(path, where, f"unknown key(s) {', '.join(extra)}; allowed: src, dst, text, json")
        sources = [k for k in ("src", "text", "json") if k in entry]
        if len(sources) != 1:
            raise _err(path, where, "give exactly one of src (a repo file), text or json")
        _check_sd_path(path, where, entry["dst"])
        dst = entry["dst"].lstrip("/")
        if "src" in entry:
            src = REPO_ROOT / entry["src"]
            if not src.exists():
                raise _err(path, where, f"src {entry['src']} not found (paths are relative to the repo root)")
            sd.append(SdFile(dst=dst, src=src))
        elif "text" in entry:
            sd.append(SdFile(dst=dst, content=str(entry["text"]).encode("utf-8")))
        else:
            sd.append(SdFile(dst=dst, content=(json.dumps(entry["json"], indent=2) + "\n").encode("utf-8")))

    env = data.get("env", {})
    if not isinstance(env, dict):
        raise _err(path, "env", "env must be a table")
    extra_env_keys = sorted(set(env) - {"slide_switch", "vars"})
    if extra_env_keys:
        raise _err(path, "env", f"unknown key(s) {', '.join(extra_env_keys)}; allowed: slide_switch, vars")
    slide = env.get("slide_switch", "off")
    if slide not in ("on", "off"):
        raise _err(path, "env.slide_switch", f"must be \"on\" or \"off\", got {slide!r}")
    extra_env: dict[str, str] = {}
    for key, value in env.get("vars", {}).items():
        if key in HARNESS_VARS:
            raise _err(path, "env.vars", f"{key} is set by the harness; use the scenario keys instead")
        if not re.fullmatch(r"(CROSSPOINT_SIM|SDL)_[A-Z0-9_]+", key):
            raise _err(path, "env.vars", f"{key}: only CROSSPOINT_SIM_* and SDL_* variables may be set")
        extra_env[key] = str(value)

    raw_steps = data.get("steps")
    if not isinstance(raw_steps, list) or not raw_steps:
        raise _err(path, "steps", "steps must be a non-empty list")
    steps = [_parse_step(path, i, raw) for i, raw in enumerate(raw_steps, 1)]
    scenario = Scenario(name, path, description.strip(), timeout_s, wait_timeout, sd, slide, extra_env,
                        steps, raw_text=text)
    compile_runs(scenario, "shots")  # validates step order
    return scenario


def compile_runs(sc: Scenario, shot_dir: str) -> list[Run]:
    """Split the steps into simulator runs and build each run's input script.

    Rules (each violation is a ScenarioError naming the step):
    - In-process steps are compiled into the run's CROSSPOINT_SIM_INPUT_SCRIPT.
    - Harness-side steps (wait_file, expect_*) are only allowed after the last
      in-process step of a run: the script is fixed at launch and the harness
      cannot pause it, so a file check in the middle would race the script.
    - The last in-process step before kill, quit, a harness-side step or the
      end must be a wait or a shot, which the harness can see in the log.
    - screen_has / screen_lacks follow a wait or shot step.
    """
    path = sc.path
    runs: list[Run] = []
    run = Run(1, sc.slide_switch)
    runs.append(run)
    state = "running"  # running | stopped
    cursor = 0  # ms since the start of the current script segment
    after_wake = False
    asleep = False
    waited_asleep = False
    last_process: Step | None = None  # last in-process step of this run
    shots_seen: set[str] = set()
    compared: set[str] = set()

    def script() -> list[str]:
        return run.script_after_wake if after_wake else run.script

    def need_running(step: Step) -> None:
        if state != "running":
            raise _err(path, step.describe(), "the simulator is not running here; add { start = true } first")

    def need_synced(step: Step, what: str) -> None:
        if last_process is not None and last_process.action not in ("wait_log", "wait_screen", "shot"):
            raise _err(path, step.describe(),
                       f"{what} must come after a wait_log, wait_screen or shot step: the input in "
                       f"{last_process.describe()} is not visible in the log, so the harness cannot tell "
                       "when it has been handled")

    for step in sc.steps:
        a = step.action
        if step.kind == "process":
            need_running(step)
            if run.tail:
                raise _err(path, step.describe(),
                           f"{run.tail[0].describe()} is a harness-side check, which may only come after "
                           "the last in-process step of a run (the simulator's input script is fixed at "
                           "launch, so the harness cannot pause it while it checks files). Move the check "
                           "after a kill/quit, or end the run there")
            if asleep and a not in ("wait_log", "wake"):
                raise _err(path, step.describe(), "the simulator is asleep; only wait_log or wake may follow "
                                                  "sleep (it does not capture screenshots in deep sleep)")
            delay = step.opts.get("delay_ms", 0)
            at = cursor + delay
            if a in ("wait_log", "wait_screen"):
                waited_asleep = waited_asleep or asleep
                timeout = step.opts.get("timeout_ms", sc.wait_timeout_ms)
                pattern = step.value if a == "wait_log" else r"\[SIM\] SCREEN:.*" + re.escape(step.value)
                script().append(f"{at}:WAIT:{sim_regex(pattern)}")
                run.syncs.append(Sync(step, "wait", at, timeout))
                cursor = 0
            elif a == "shot":
                if step.value in shots_seen:
                    raise _err(path, step.describe(), f"shot name {step.value!r} is used twice")
                shots_seen.add(step.value)
                shot_file = f"{shot_dir}/{step.value}.bmp"
                script().append(f"{at}:SHOT:{shot_file}")
                run.syncs.append(Sync(step, "shot", at, sc.wait_timeout_ms, shot_file=shot_file))
                cursor = at + INPUT_GAP_MS
            elif a == "press":
                hold = step.opts.get("hold_ms", DEFAULT_HOLD_MS)
                script().append(f"{at}:{step.value}:{hold}")
                cursor = at + hold + INPUT_GAP_MS
            elif a == "tap":
                hold = step.opts.get("hold_ms", DEFAULT_HOLD_MS)
                x, y = step.value
                script().append(f"{at}:TAP:{x},{y},{hold}")
                cursor = at + hold + INPUT_GAP_MS
            elif a == "swipe":
                dur = step.opts.get("duration_ms", DEFAULT_SWIPE_MS)
                x1, y1, x2, y2 = step.value
                script().append(f"{at}:SWIPE:{x1},{y1},{x2},{y2},{dur}")
                cursor = at + dur + INPUT_GAP_MS
            elif a == "switch":
                script().append(f"{at}:SWITCH_{step.value.upper()}")
                cursor = at + INPUT_GAP_MS
            elif a == "sleep":
                if after_wake or asleep:
                    raise _err(path, step.describe(), "only one sleep/wake cycle per run is supported "
                                                      "(the simulator promotes *_AFTER_WAKE only once)")
                script().append(f"{at}:SLEEP")
                asleep = True
                waited_asleep = False
                cursor = at + INPUT_GAP_MS
            elif a == "wake":
                if not asleep:
                    raise _err(path, step.describe(), "wake must follow sleep")
                if not waited_asleep:
                    raise _err(path, step.describe(), "put a wait_log for the sleep log line (for example "
                                                      "\"Entering deep sleep\") between sleep and wake, so the "
                                                      "key is pressed once the device sleeps")
                hold = step.opts.get("hold_ms", DEFAULT_HOLD_MS)
                script().append(f"{at}:{step.value}:{hold}")
                asleep = False
                after_wake = True
                run.script_after_wake = []
                cursor = 0
            run.steps.append(step)
            last_process = step
        elif step.kind == "log":
            need_running(step)
            if last_process is None or last_process.action not in ("wait_log", "wait_screen", "shot"):
                raise _err(path, step.describe(), "screen_has / screen_lacks read the screen text current at the "
                                                  "previous wait or shot; put one directly before it")
            run.steps.append(step)
        elif step.kind == "tail":
            if state == "running":
                need_synced(step, "a harness-side check")
                if asleep:
                    raise _err(path, step.describe(), "wake the simulator before a harness-side check")
            run.tail.append(step)
        elif a in ("kill", "quit"):
            need_running(step)
            need_synced(step, a)
            if asleep:
                raise _err(path, step.describe(), f"{a} while asleep is not supported; wake first")
            run.end = a
            run.end_step = step
            run.tail.append(step)
            state = "stopped"
        elif a == "start":
            if state == "running":
                raise _err(path, step.describe(), "the simulator is already running; kill or quit it first")
            run = Run(len(runs) + 1, step.opts.get("slide_switch", sc.slide_switch), start_step=step)
            runs.append(run)
            state = "running"
            cursor = 0
            after_wake = False
            asleep = False
            last_process = None
        elif a == "compare":
            if step.value not in shots_seen:
                raise _err(path, step.describe(), f"no earlier shot named {step.value!r}")
            if step.value in compared:
                raise _err(path, step.describe(), f"shot {step.value!r} is compared twice")
            other = step.opts.get("same_as")
            if other is not None and (other not in shots_seen or other == step.value):
                raise _err(path, step.describe(), f"same_as must name another shot taken earlier, got {other!r}")
            compared.add(step.value)  # evaluated after the whole scenario

    if state == "running":
        if asleep:
            raise _err(path, "end", "the scenario ends while the simulator sleeps; add a wake step")
        if last_process is not None and last_process.action not in ("wait_log", "wait_screen", "shot"):
            raise _err(path, last_process.describe(),
                       "the last in-process step must be a wait or shot so the harness knows the input "
                       "was handled before it stops the simulator")
        run.end = "end"
    return runs


def discover(root: Path = SCENARIOS_DIR) -> dict[str, Path]:
    found = {}
    if root.is_dir():
        for child in sorted(root.iterdir()):
            f = child / "scenario.toml"
            if f.is_file():
                found[child.name] = f
    return found
