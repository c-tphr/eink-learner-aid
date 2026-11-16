"""Choosing the serial port. Never guesses between several candidates."""

from __future__ import annotations

from dataclasses import dataclass
from typing import List, Optional

from .common import PortProblem
from .ui import UI

ESPRESSIF_VID = 0x303A
USB_JTAG_SERIAL_PID = 0x1001  # ESP32-S3 USB-Serial/JTAG peripheral


@dataclass
class PortInfo:
    device: str
    vid: Optional[int]
    pid: Optional[int]
    description: str = ""
    serial_number: Optional[str] = None

    def label(self) -> str:
        ids = "%04X:%04X" % (self.vid, self.pid) if self.vid is not None and self.pid is not None else "no USB id"
        kind = ""
        if self.vid == ESPRESSIF_VID:
            kind = "  <- Espressif%s" % (" USB-Serial/JTAG" if self.pid == USB_JTAG_SERIAL_PID else "")
        return "%-8s %s  %s%s" % (self.device, ids, self.description, kind)


def list_ports() -> List[PortInfo]:
    """The serial ports the OS knows about (read-only; opens nothing)."""
    from serial.tools import list_ports as lp

    return [PortInfo(p.device, p.vid, p.pid, p.description or "", p.serial_number) for p in lp.comports()]


def choose_port(requested: Optional[str], ports: List[PortInfo]) -> str:
    if requested:
        return requested
    esp = [p for p in ports if p.vid == ESPRESSIF_VID]
    if len(esp) == 1:
        return esp[0].device
    listing = "\n".join("    " + p.label() for p in ports) or "    (no serial ports at all)"
    if len(esp) > 1:
        raise PortProblem(
            "More than one Espressif device is connected:\n%s\n"
            "Unplug the ones that are not the Picco, or name the port: -Port COMn (session.ps1) or --port COMn."
            % "\n".join("    " + p.label() for p in esp))
    raise PortProblem(
        "No Espressif USB serial port was found. Serial ports seen:\n%s\n"
        "The Picco's chip shows up as a USB serial device with id 303A:1001 (Device Manager > Ports (COM & LPT),\n"
        "often named 'USB Serial Device' or 'USB JTAG/serial debug unit'). Check:\n"
        "  - a data-capable cable, plugged straight into the PC;\n"
        "  - the Picco is on, on its Home screen, and not in USB drive mode;\n"
        "  - unplug and replug the cable and watch which COM port appears.\n"
        "If you know the port, name it: -Port COMn (session.ps1) or --port COMn. This tool does not guess."
        % listing)


def resolver(requested: Optional[str], ui: UI) -> str:
    port = choose_port(requested, list_ports())
    ui.say("Using serial port %s%s." % (port, "" if requested else " (the only Espressif device found)"))
    return port


def print_ports(ui: UI) -> int:
    ports = list_ports()
    ui.say("Serial ports:")
    for p in ports:
        ui.say("  " + p.label())
    if not ports:
        ui.say("  (none)")
    return 0
