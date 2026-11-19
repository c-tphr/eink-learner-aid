"""Process control: a private Xvfb, the simulator process and its log.

Every child is started in its own session (process group), so stopping it
also stops anything it spawned. All waits have deadlines; nothing here sleeps
for a fixed time to let the simulator "settle".
"""

from __future__ import annotations

import os
import select
import shutil
import signal
import socket
import subprocess
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable


class ProcessError(Exception):
    pass


def free_port_pair() -> int:
    """A port p such that p and p+1 are free now (the simulator uses HTTP p, WebSocket p+1)."""
    for _ in range(200):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(("127.0.0.1", 0))
            port = s.getsockname()[1]
        if port >= 65535:
            continue
        ok = True
        for p in (port, port + 1):
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                try:
                    s.bind(("127.0.0.1", p))
                except OSError:
                    ok = False
        if ok:
            return port
    raise ProcessError("no free pair of TCP ports found")


def kill_group(proc: subprocess.Popen, sig: int = signal.SIGKILL) -> None:
    try:
        os.killpg(proc.pid, sig)
    except (ProcessLookupError, PermissionError):
        pass


class Xvfb:
    """A private X server. -displayfd lets Xvfb pick a free display itself, so
    parallel runs never race for the same display number (xvfb-run -a can)."""

    def __init__(self, geometry: str = "1024x1024x24"):
        self.geometry = geometry
        self.proc: subprocess.Popen | None = None
        self.display = ""

    def start(self, timeout_s: float = 15.0) -> str:
        exe = shutil.which("Xvfb")
        if not exe:
            raise ProcessError("Xvfb not found; install the xvfb package (apt-get install -y xvfb)")
        rfd, wfd = os.pipe()
        try:
            self.proc = subprocess.Popen(
                [exe, "-displayfd", str(wfd), "-screen", "0", self.geometry, "-nolisten", "tcp", "-noreset"],
                pass_fds=(wfd,), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL, start_new_session=True, env={"PATH": os.environ.get("PATH", "")})
        finally:
            os.close(wfd)
        data = b""
        deadline = time.monotonic() + timeout_s
        try:
            while not data.endswith(b"\n"):
                left = deadline - time.monotonic()
                if left <= 0 or self.proc.poll() is not None:
                    raise ProcessError("Xvfb did not report a display number")
                ready, _, _ = select.select([rfd], [], [], left)
                if ready:
                    chunk = os.read(rfd, 64)
                    if not chunk:
                        raise ProcessError("Xvfb closed its display fd without a display number")
                    data += chunk
        except ProcessError:
            self.stop()
            raise
        finally:
            os.close(rfd)
        self.display = ":" + data.decode().strip()
        return self.display

    def stop(self) -> None:
        if self.proc and self.proc.poll() is None:
            kill_group(self.proc, signal.SIGTERM)
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                kill_group(self.proc, signal.SIGKILL)
                self.proc.wait(5)
        self.proc = None


@dataclass
class LogLine:
    t: float  # seconds since the scenario started
    text: str
    source: str  # "sim" or "simtest"


class EventLog:
    """The simulator's output (stdout and stderr, merged in order) plus the
    harness's own lines, with timestamps. Thread-safe."""

    def __init__(self, t0: float | None = None):
        self.t0 = t0 if t0 is not None else time.monotonic()
        self.lines: list[LogLine] = []
        self.cond = threading.Condition()

    def now(self) -> float:
        return time.monotonic() - self.t0

    def add(self, text: str, source: str = "sim") -> int:
        with self.cond:
            self.lines.append(LogLine(self.now(), text, source))
            self.cond.notify_all()
            return len(self.lines) - 1

    def note(self, text: str) -> int:
        return self.add(f"[simtest] {text}", "simtest")

    def tail(self, n: int = 30) -> list[str]:
        with self.cond:
            return [f"{ln.t:8.3f} {ln.text}" for ln in self.lines[-n:]]

    def text(self) -> str:
        with self.cond:
            return "\n".join(f"{ln.t:8.3f} {ln.text}" for ln in self.lines) + "\n"


class SimProcess:
    """One simulator process. Output is read by a thread into an EventLog."""

    def __init__(self, argv: list[str], env: dict[str, str], cwd: Path, log: EventLog):
        self.log = log
        self.start_index = len(log.lines)
        self.proc = subprocess.Popen(argv, env=env, cwd=str(cwd), stdin=subprocess.DEVNULL,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     start_new_session=True)
        self.pid = self.proc.pid
        self._eof = threading.Event()
        self._reader = threading.Thread(target=self._read, daemon=True)
        self._reader.start()

    def _read(self) -> None:
        assert self.proc.stdout is not None
        for raw in self.proc.stdout:
            self.log.add(raw.decode("utf-8", "replace").rstrip("\r\n"))
        self._eof.set()
        with self.log.cond:
            self.log.cond.notify_all()

    def alive(self) -> bool:
        return self.proc.poll() is None

    def wait_for(self, match: Callable[[int, str], bool], deadline: float,
                 fail: Callable[[str], str | None] | None = None,
                 start: int | None = None) -> tuple[str, int]:
        """Scan log lines from `start` until match(index, text) is true.

        Returns ("ok", index), ("fail:<msg>", index), ("exit", -1) when the
        process ended (and its output was fully read) first, or ("timeout", -1).
        deadline is time.monotonic() based.
        """
        i = self.start_index if start is None else start
        with self.log.cond:
            while True:
                while i < len(self.log.lines):
                    line = self.log.lines[i]
                    if line.source == "sim":
                        if match(i, line.text):
                            return "ok", i
                        if fail:
                            msg = fail(line.text)
                            if msg:
                                return "fail:" + msg, i
                    i += 1
                if self._eof.is_set() and self.proc.poll() is not None:
                    return "exit", -1
                left = deadline - time.monotonic()
                if left <= 0:
                    return "timeout", -1
                self.log.cond.wait(min(left, 0.2))

    def signal_group(self, sig: int) -> None:
        kill_group(self.proc, sig)

    def signal_main(self, sig: int) -> None:
        try:
            self.proc.send_signal(sig)
        except ProcessLookupError:
            pass

    def wait_exit(self, timeout: float) -> int | None:
        try:
            code = self.proc.wait(timeout)
        except subprocess.TimeoutExpired:
            return None
        self._eof.wait(5)
        return code

    def stop_hard(self) -> int:
        """SIGKILL the whole process group and reap it."""
        self.signal_group(signal.SIGKILL)
        code = self.proc.wait()
        self._eof.wait(5)
        return code


def describe_status(code: int | None) -> str:
    if code is None:
        return "still running"
    if code < 0:
        try:
            return f"killed by {signal.Signals(-code).name}"
        except ValueError:
            return f"killed by signal {-code}"
    return f"exit status {code}"
