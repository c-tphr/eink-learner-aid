"""Shared pieces of the commands: exit codes, the run context, small helpers."""

from __future__ import annotations

import os
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


@dataclass
class Context:
    ui: UI
    work: str
    plan_path: str
    port: Optional[str]
    dry_run: bool
    device_factory: Callable[[str, UI], Device]
    port_resolver: Callable[[Optional[str], UI], str]
    launcher: str = ".\\session.ps1"  # how the owner runs commands, for printed instructions

    def command_hint(self, cmd: str) -> str:
        return "%s %s" % (self.launcher, cmd)


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
    ui.say("     Note its COM number and pass it with -Port COMn.")
    ui.say("  5. No other program (Arduino IDE, a serial terminal, another session.ps1) has the port open.")
    ui.say("If the chip still cannot be reached, stop here: the checklist (DEVICE_SESSION.md) says what")
    ui.say("is known about entering download mode by hand. Do not try button combinations at random.")


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
        ui.say("    %s \"%s\"" % (ctx.command_hint("restore"), os.path.abspath(backup_path)))
    else:
        ui.say("Run '%s <your backup file>' to put the Picco back." % ctx.command_hint("restore"))
    ui.say("Restore works from the chip's ROM download mode, which no flash content can break.")
    ui.say("=" * 78)
