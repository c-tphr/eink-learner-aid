"""monitor (with a fake serial port), port selection, and the command line surface."""

import argparse
import os

import pytest

from helpers import run_cli
from piccoflash import cli, common
from piccoflash.common import PortProblem
from piccoflash.device import MonitorPort
from piccoflash.ports import PortInfo, choose_port

# --- monitor ------------------------------------------------------------------


class ScriptedPort(MonitorPort):
    """Plays a script shared across re-opens: bytes, 'GONE' (port vanishes), 'MISSING' (cannot open), 'STOP'."""

    def __init__(self, script, log):
        self.script, self.log = script, log

    def open(self):
        if self.script and self.script[0] == "MISSING":
            self.script.pop(0)
            raise OSError("could not open port: FileNotFoundError")
        self.log.append("open")

    def read(self):
        item = self.script.pop(0) if self.script else "STOP"
        if item == "GONE":
            raise OSError("ClearCommError failed (PermissionError)")
        if item == "STOP":
            raise KeyboardInterrupt
        return item

    def close(self):
        self.log.append("close")

    def pulse_reset(self):
        self.log.append("reset")

    def write(self, data):  # pragma: no cover - must never be called
        raise AssertionError("monitor must never send data")


def run_monitor(tmp_path, script, *extra):
    log = []
    code, out = run_cli(["monitor", "--work-dir", str(tmp_path), *extra], None,
                        monitor_port_factory=lambda name: ScriptedPort(script, log))
    return code, out, log


def test_monitor_logs_and_survives_reenumeration(tmp_path, monkeypatch):
    monkeypatch.setattr("piccoflash.monitor.time.sleep", lambda s: None)
    script = [b"ESP-ROM:esp32s3\r\nboot", b":0x8 (SPI_FAST_FLASH_BOOT)\r\n", "GONE", "MISSING", "MISSING",
              b"[EPD] Picco panel DEPG0397\r\n", b"", "STOP"]
    code, out, log = run_monitor(tmp_path, script)
    assert code == common.OK
    assert "boot:0x8 (SPI_FAST_FLASH_BOOT)" in out and "[EPD] Picco panel DEPG0397" in out
    assert "port went away" in out and "stopped by Ctrl+C" in out
    assert log.count("open") == 2 and "reset" not in log
    files = [f for f in os.listdir(tmp_path) if f.startswith("serial-")]
    raw = open(os.path.join(tmp_path, files[0]), "rb").read()
    assert b"ESP-ROM:esp32s3\r\nboot:0x8" in raw and b"[EPD] Picco panel DEPG0397\r\n" in raw
    assert b"port went away" in raw


def test_monitor_reset_is_only_a_line_pulse(tmp_path, monkeypatch):
    monkeypatch.setattr("piccoflash.monitor.time.sleep", lambda s: None)
    code, out, log = run_monitor(tmp_path, [b"hello\n", "STOP"], "--reset")
    assert code == common.OK and log[:2] == ["open", "reset"] and log.count("reset") == 1


def test_monitor_dry_run(tmp_path):
    code, out, log = run_monitor(tmp_path, [], "--dry-run")
    assert code == common.OK and log == [] and "send nothing" in out


# --- ports -------------------------------------------------------------------------

P_ESP = PortInfo("COM5", 0x303A, 0x1001, "USB JTAG/serial debug unit")
P_ESP2 = PortInfo("COM9", 0x303A, 0x1001, "USB JTAG/serial debug unit")
P_OTHER = PortInfo("COM3", 0x0403, 0x6001, "USB Serial Port (FTDI)")
P_BT = PortInfo("COM4", None, None, "Standard Serial over Bluetooth link")


def test_choose_port_rules():
    assert choose_port("COM12", [P_ESP, P_OTHER]) == "COM12"  # an explicit choice wins
    assert choose_port(None, [P_OTHER, P_ESP, P_BT]) == "COM5"  # exactly one Espressif port
    with pytest.raises(PortProblem) as e:
        choose_port(None, [P_ESP, P_ESP2])
    assert "More than one" in str(e.value) and "COM5" in str(e.value) and "COM9" in str(e.value)
    with pytest.raises(PortProblem) as e:
        choose_port(None, [P_OTHER, P_BT])
    assert "does not guess" in str(e.value) and "COM3" in str(e.value)
    with pytest.raises(PortProblem) as e:
        choose_port(None, [])
    assert "no serial ports at all" in str(e.value)


# --- command line ---------------------------------------------------------------------

DEVICE_COMMANDS = ["identify", "backup", "plan", "flash", "monitor", "restore"]


def _subparsers():
    parser = cli.build_parser()
    action = next(a for a in parser._actions if isinstance(a, argparse._SubParsersAction))
    return action.choices


def test_every_device_command_has_dry_run_port_and_work_dir():
    subs = _subparsers()
    for name in DEVICE_COMMANDS:
        opts = {o for a in subs[name]._actions for o in a.option_strings}
        assert {"--dry-run", "--port", "--work-dir"} <= opts, name
    for name in ("plan", "flash"):
        assert "--plan" in {o for a in subs[name]._actions for o in a.option_strings}


def test_no_option_can_bypass_the_guard():
    forbidden = ("force", "offset", "no-verify", "skip", "unsafe", "override", "address", "erase")
    for name, sub in _subparsers().items():
        for a in sub._actions:
            for o in a.option_strings:
                if o == "--expect-offset":
                    continue  # make-plan cross-check: it can only cause refusals
                assert not any(f in o for f in forbidden), (name, o)


@pytest.mark.parametrize("argv", [["flash", "--force"], ["restore", "--force", "x.bin"], ["flash", "--offset", "0"]])
def test_bypass_flags_are_rejected(argv, capsys):
    with pytest.raises(SystemExit) as e:
        cli.build_parser().parse_args(argv)
    assert e.value.code == common.USAGE


def test_ports_command_runs(monkeypatch):
    monkeypatch.setattr("piccoflash.ports.list_ports", lambda: [P_ESP, P_OTHER])
    code, out = run_cli(["ports"])
    assert code == common.OK and "COM5" in out and "Espressif USB-Serial/JTAG" in out
