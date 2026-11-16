"""monitor: record the Picco's USB serial output until Ctrl+C. Sends nothing.

The port is opened with DTR and RTS de-asserted so opening it does not reset
the chip. The only thing ever sent is the optional reset pulse at the start
(``--reset``), which toggles the RTS control line; no data bytes are written.
When the port disappears (the USB-Serial/JTAG device re-enumerates on every
chip reset) the monitor waits for it to come back and carries on, marking
the gap in the log.
"""

from __future__ import annotations

import codecs
import datetime
import os
import time
from typing import Callable, Optional

from . import workdir
from .common import NOT_REACHED, OK, Context, PortProblem
from .device import MonitorPort
from .ui import UI


class SerialMonitorPort(MonitorPort):
    def __init__(self, port: str):
        self.port = port
        self._ser = None

    def open(self) -> None:
        import serial

        ser = serial.Serial()
        ser.port = self.port
        ser.baudrate = 115200  # ignored by USB-Serial/JTAG
        ser.timeout = 0.2
        ser.dtr = False  # de-assert both lines before opening: no reset on open
        ser.rts = False
        ser.open()
        self._ser = ser

    def read(self) -> bytes:
        ser = self._ser
        if ser is None:
            raise OSError("port not open")
        n = ser.in_waiting
        return ser.read(n or 1)

    def close(self) -> None:
        if self._ser is not None:
            try:
                self._ser.close()
            finally:
                self._ser = None

    def pulse_reset(self) -> None:
        ser = self._ser
        ser.dtr = False
        ser.rts = True  # USB-Serial/JTAG: RTS asserted with DTR low resets the core
        time.sleep(0.2)
        ser.rts = False


def run(ctx: Context, reset: bool, port_factory: Optional[Callable[[str], MonitorPort]] = None,
        sleep: Optional[Callable[[float], None]] = None) -> int:
    ui = ctx.ui
    ui.heading("Monitor: recording USB serial output (press Ctrl+C to stop)")
    work = workdir.ensure(ctx.work)
    stamp = workdir.timestamp()
    log_path = os.path.join(work, "serial-%s.log" % stamp)
    if ctx.dry_run:
        ui.say("DRY RUN: would open the serial port read-only (DTR and RTS off), %s," % (
            "pulse RTS once to reset the chip" if reset else "send nothing at all"))
        ui.say("and copy everything received to %s and to this window until Ctrl+C." % log_path)
        return OK
    port_factory = port_factory or SerialMonitorPort
    sleep = sleep or time.sleep
    try:
        port_name = ctx.port_resolver(ctx.port, ui)
    except PortProblem as exc:
        ui.say(str(exc))
        return NOT_REACHED
    ui.say("Saving to %s" % os.path.abspath(log_path))
    decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
    pending = ""
    first = True
    with open(log_path, "ab") as log:
        def mark(text: str) -> None:
            line = "\n[piccoflash %s] %s\n" % (datetime.datetime.now().isoformat(timespec="seconds"), text)
            log.write(line.encode("utf-8"))
            log.flush()
            ui.say(line.strip())

        port = None
        try:
            while True:
                if port is None:
                    try:
                        port = port_factory(port_name)
                        port.open()
                    except OSError as exc:
                        port = None
                        if first:
                            mark("cannot open %s yet (%s); waiting for it" % (port_name, exc))
                            first = False
                        sleep(0.5)
                        try:
                            port_name = ctx.port_resolver(ctx.port, _quiet())
                        except PortProblem:
                            pass
                        continue
                    mark("connected to %s" % port_name)
                    first = False
                    if reset:
                        port.pulse_reset()
                        mark("reset pulse sent (RTS)")
                        reset = False
                try:
                    chunk = port.read()
                except OSError as exc:
                    mark("port went away (%s); waiting for it to come back" % exc)
                    try:
                        port.close()
                    except OSError:
                        pass
                    port = None
                    first = True
                    continue
                if chunk:
                    log.write(chunk)
                    log.flush()
                    pending += decoder.decode(chunk)
                    while "\n" in pending:
                        line, pending = pending.split("\n", 1)
                        ui.say(line.rstrip("\r"))
        except KeyboardInterrupt:
            if pending:
                ui.say(pending)
            mark("stopped by Ctrl+C")
        finally:
            if port is not None:
                port.close()
    ui.say("Serial log saved: %s" % os.path.abspath(log_path))
    return OK


def _quiet() -> UI:
    class _N:
        def write(self, s):
            pass

        def flush(self):
            pass

    return UI(out=_N())
