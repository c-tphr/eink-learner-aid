"""Console output, prompts and the session log file.

Everything shown to the owner, everything they type, and everything esptool
prints goes into the log file of the command, so a session can be reviewed
afterwards. Tests replace ``out`` and ``ask_fn``.
"""

from __future__ import annotations

import datetime
import os
import sys
from typing import Callable, List, Optional, TextIO


class UI:
    def __init__(self, out: Optional[TextIO] = None, ask_fn: Optional[Callable[[str], str]] = None):
        self.out = out or sys.stdout
        self._ask_fn = ask_fn
        self._log: Optional[TextIO] = None
        self.log_path: Optional[str] = None
        self.lines: List[str] = []  # everything shown, for tests

    # --- log file -----------------------------------------------------------
    def open_log(self, path: str) -> None:
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        self._log = open(path, "a", encoding="utf-8")
        self.log_path = path
        self._write_log("=== %s ===" % datetime.datetime.now().isoformat(timespec="seconds"))

    def close_log(self) -> None:
        if self._log:
            self._log.close()
            self._log = None

    def _write_log(self, text: str) -> None:
        if self._log:
            self._log.write(text + "\n")
            self._log.flush()

    # --- output -------------------------------------------------------------
    def say(self, text: str = "") -> None:
        for line in str(text).split("\n"):
            self.lines.append(line)
            try:
                self.out.write(line + "\n")
            except UnicodeEncodeError:
                self.out.write(line.encode("ascii", "replace").decode("ascii") + "\n")
            self._write_log(line)
        self.out.flush()

    def detail(self, text: str) -> None:
        """Tool chatter (esptool output): shown indented, and logged."""
        for line in str(text).rstrip("\n").split("\n"):
            if line.strip():
                self.say("    | " + line)

    def log_only(self, text: str) -> None:
        self._write_log(text)

    def heading(self, text: str) -> None:
        self.say("")
        self.say(text)
        self.say("-" * min(len(text), 78))

    # --- input --------------------------------------------------------------
    def ask(self, prompt: str) -> Optional[str]:
        """Ask for a line of text. Returns None if input is closed (EOF)."""
        self._write_log(prompt)
        try:
            if self._ask_fn is not None:
                answer = self._ask_fn(prompt)
            else:
                self.out.write(prompt)
                self.out.flush()
                answer = input()
        except EOFError:
            self._write_log("(no input: end of file)")
            return None
        self._write_log("> %s" % answer)
        self.lines.append(prompt + str(answer))
        return answer

    def confirm_phrase(self, phrase: str, what: str) -> bool:
        """Require the exact phrase (case-sensitive). One attempt."""
        self.say("")
        self.say("To %s, type exactly:  %s" % (what, phrase))
        self.say("Anything else cancels. Nothing has been written yet.")
        answer = self.ask("> ")
        if answer is not None and answer.strip() == phrase:
            return True
        self.say("Cancelled: the phrase did not match. Nothing was written.")
        return False
