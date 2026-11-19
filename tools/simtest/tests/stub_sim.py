"""A tiny stand-in for the simulator, for the harness's own tests.

It reads CROSSPOINT_SIM_INPUT_SCRIPT like the real simulator (WAIT, SHOT,
QUIT, keys, TAP), prints log lines in the real formats, writes a state file
to the SD folder on every key press, and quits on SIGTERM. Behaviour is
chosen by CROSSPOINT_SIM_STUB_MODE:

  normal    as above
  stubborn  ignores SIGTERM and starts a child process that also ignores it
  crash     exits with status 3 right after booting
"""

import json
import os
import re
import signal
import subprocess
import sys
import time

from PIL import Image

MODE = os.environ.get("CROSSPOINT_SIM_STUB_MODE", "normal")
SD = os.environ["CROSSPOINT_SIM_SD"]
WAIT_TIMEOUT = int(os.environ.get("CROSSPOINT_SIM_WAIT_TIMEOUT_MS", "30000")) / 1000.0
lines: list[str] = []
since_input = 0
page = 0


def log(text: str) -> None:
    lines.append(text)
    sys.stderr.write(text + "\n")
    sys.stderr.flush()


def screen() -> None:
    log(f"[SIM] SCREEN: 100% | Stub | page {page}")


def on_term(_sig, _frame):
    os._exit(0)


if MODE == "stubborn":
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    # The SD path in its command line lets a test look for it with pgrep.
    subprocess.Popen([sys.executable, "-c",
                      "import signal,time; signal.signal(signal.SIGTERM, signal.SIG_IGN); time.sleep(600)",
                      "stub-child", SD])
else:
    signal.signal(signal.SIGTERM, on_term)

state_path = os.path.join(SD, ".crosspoint", "state.json")
if os.path.exists(state_path):
    with open(state_path) as f:
        page = json.load(f)["page"]
log("[    12] [DBG] [ACT] Entering activity: Home")
screen()
if MODE == "crash":
    log("Guru Meditation (stub crash)")
    sys.exit(3)

script = os.environ.get("CROSSPOINT_SIM_INPUT_SCRIPT", "")
for item in [i for i in script.split(";") if i]:
    t, _, rest = item.partition(":")
    key, _, arg = rest.partition(":")
    time.sleep(min(int(t), 200) / 1000.0)
    if key == "WAIT":
        pattern = re.compile(arg.replace(r"\x3B", ";"))
        started = time.monotonic()
        while True:
            found = next((ln for ln in lines[since_input:] if pattern.search(ln)), None)
            if found is not None:
                log(f"[SIM] WAIT matched: {found}")
                break
            if time.monotonic() - started > WAIT_TIMEOUT:
                log(f"[SIM] WAIT timed out after {int(WAIT_TIMEOUT * 1000)} ms: {arg}")
                sys.exit(1)
            time.sleep(0.05)
    elif key == "SHOT":
        Image.new("RGB", (48, 80), (255, 255, 255)).save(arg, format="BMP") if page == 0 else \
            Image.new("RGB", (48, 80), (0, 0, 0)).save(arg, format="BMP")
        log(f"[SIM] Saved screenshot: {arg}")
    elif key == "QUIT":
        sys.exit(0)
    elif key in ("DOWN", "UP", "ENTER", "POWER", "TAP", "SWIPE"):
        since_input = len(lines)
        page += 1 if key != "UP" else -1
        os.makedirs(os.path.dirname(state_path), exist_ok=True)
        with open(state_path, "w") as f:
            json.dump({"page": page, "key": key}, f)
        log(f"[   100] [DBG] [STUB] {key} -> page {page}")
        screen()
while True:
    time.sleep(1)
