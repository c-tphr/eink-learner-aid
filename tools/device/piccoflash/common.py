"""Shared pieces of the commands: exit codes, the run context, small helpers."""

from __future__ import annotations

import os
import re
from dataclasses import dataclass
from typing import Callable, Optional

from .device import Device
from .ui import UI

# Exit codes. session.ps1 and the checklist rely on these numbers.
OK = 0
STOP = 1  # refused or a stop condition; nothing was written
USAGE = 2  # bad command line (argparse)
ALREADY_CONVERTED = 3  # identify: the table is already the two-slot table
NOT_REACHED = 4  # could not talk to the chip; nothing was written
FLASH_FAILED = 5  # a flash write started and did not complete and verify: run restore
RESTORE_FAILED = 6  # restore did not complete and verify: run restore again

EXIT_NAMES = {OK: "OK", STOP: "STOP", USAGE: "USAGE", ALREADY_CONVERTED: "ALREADY_CONVERTED",
              NOT_REACHED: "NOT_REACHED", FLASH_FAILED: "FLASH_FAILED", RESTORE_FAILED: "RESTORE_FAILED"}

FLASH_PHRASE = "REPLACE ONYX"
RESTORE_PHRASE = "RESTORE BACKUP"
OTHER_DEVICE_PHRASE = "RESTORE TO A DIFFERENT DEVICE"


# How the owner starts the tool, exactly as the checklists write it. The bypass works on a PC that
# blocks scripts and changes no setting. session.ps1 holds the same text in $Launcher (a test checks).
SESSION_LAUNCHER = "powershell -ExecutionPolicy Bypass -File .\\session.ps1"

_PLAIN_ARG = re.compile(r"[A-Za-z0-9_.:-]+")  # a word such as COM5; every path is quoted
_SWITCH = re.compile(r"-[A-Za-z]+")  # a session.ps1 switch such as -Reset
_PS_SINGLE_QUOTES = "'\u2018\u2019\u201a\u201b"  # PowerShell reads all of these as single quotes
_PS_NOT_IN_DOUBLE = "\"\u201c\u201d\u201e$`"  # end or expand a double-quoted PowerShell string


def ps_arg(arg: str) -> str:
    """One argument as it must be typed at a PowerShell prompt to arrive unchanged."""
    if _PLAIN_ARG.fullmatch(arg) and (not arg.startswith("-") or _SWITCH.fullmatch(arg)):
        return arg
    if not any(c in _PS_NOT_IN_DOUBLE for c in arg):
        return '"%s"' % arg
    return "'%s'" % "".join(c + c if c in _PS_SINGLE_QUOTES else c for c in arg)


def session_command(command: str, *args: str) -> str:
    """The one place that writes a command for the owner to type: the full session.ps1 line."""
    return " ".join([SESSION_LAUNCHER, command] + [ps_arg(a) for a in args])


@dataclass
class Context:
    ui: UI
    work: str
    plan_path: str
    port: Optional[str]
    dry_run: bool
    device_factory: Callable[[str, UI], Device]
    port_resolver: Callable[[Optional[str], UI], str]

    def command_hint(self, command: str, *args: str) -> str:
        return session_command(command, *args)


class PortProblem(Exception):
    """No usable serial port could be chosen; the message explains what to do."""


def not_reached_help(ui: UI, detail: str) -> None:
    ui.say("")
    ui.say("COULD NOT REACH THE PICCO'S CHIP. Nothing was read or written.")
    ui.say("Detail: %s" % detail)
    ui.say("Check, in this order:")
    ui.say("  1. The cable is a data cable (some charge-only cables have no data wires). Try another one.")
    ui.say("  2. The cable goes straight into the PC, not through a hub or a monitor's USB port.")
    ui.say("  3. The Picco is switched on, on its Home screen, and NOT in USB drive mode.")
    ui.say("  4. Windows Device Manager > Ports (COM & LPT) shows a new entry when you plug it in.")
    ui.say("     Note its COM number and add -Port COMn at the end of the command.")
    ui.say("  5. No other program (Arduino IDE, a serial terminal, another window running this tool,")
    ui.say("     for example the serial log) has the port open.")
    ui.say("If the chip still cannot be reached, stop here: the checklist (DEVICE_SESSION.md) says what")
    ui.say("is known about entering download mode by hand. Do not try button combinations at random.")


def kit_missing_help(ui: UI, reasons, work: str) -> None:
    """Say where the kit comes from when there is no flash plan (the tool was started outside a kit folder)."""
    if not any(str(r).startswith("no flash plan at ") for r in reasons):
        return
    ui.say("")
    ui.say("This folder has no 'kit' folder. The flash plan and the four images are not in the")
    ui.say("repository: they are in the device kit, picco-2slot-<version>.zip, on the project's")
    ui.say("release page. Nothing is wrong with the Picco, and nothing was written.")
    ui.say("  1. Download the kit and extract it (docs/DEVICE_SESSION.md, \"Get the kit\").")
    ui.say("  2. Copy your work folder into the extracted kit folder, so that the identify and backup")
    ui.say("     results come with it: %s" % os.path.abspath(work))
    ui.say("  3. Open PowerShell in the kit folder and type the command again there.")


def restore_instructions(ui: UI, ctx: "Context", backup_path: Optional[str], sha: Optional[str]) -> None:
    ui.say("")
    ui.say("=" * 78)
    ui.say("FLASHING DID NOT COMPLETE. The Picco may not start until it is restored.")
    ui.say("Do not unplug it and do not close this window yet.")
    if backup_path:
        ui.say("Your verified backup: %s" % backup_path)
        if sha:
            ui.say("Its SHA-256:          %s" % sha)
        ui.say("To put the Picco back exactly as it was, run:")
        ui.say("    %s" % ctx.command_hint("restore", os.path.abspath(backup_path)))
    else:
        ui.say("To put the Picco back, run this with your backup file's path:")
        ui.say("    %s" % ctx.command_hint("restore", "<your backup file>"))
    ui.say("Restore works from the chip's ROM download mode, which no flash content can break.")
    ui.say("=" * 78)
