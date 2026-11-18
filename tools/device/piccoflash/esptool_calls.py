"""Every esptool call piccoflash makes, as data.

Each operation is described once, here. ``--dry-run`` prints these
descriptions; the real driver (esptool_device.py) executes exactly the call
described (function name, arguments, keyword arguments), so what a dry run
prints is what a real run does. Each call also carries the equivalent
esptool 5.x command line, for anyone who wants to reproduce a step by hand
(esptool 4.x spelled the commands with underscores: read_flash, write_flash).

Options relied on, and why (esptool 5.6.0, docs.espressif.com/projects/esptool):

* ``connect_mode="default-reset"`` (CLI ``--before default-reset``): esptool
  detects the ESP32-S3 USB-Serial/JTAG peripheral (VID 0x303A, PID 0x1001)
  and uses its reset sequence to enter ROM download mode.
* ``baud=115200``: the ROM's initial rate. Over USB-Serial/JTAG the baud rate
  has no effect on speed, so it is never changed.
* the stub flasher (``run_stub``, i.e. no ``--no-stub``): it runs in RAM only
  and is needed for fast reads of 16 MB, compressed writes and the post-write
  MD5 check. The ROM-only read path is far too slow for a full backup.
* ``attach_flash``: required by esptool before any flash operation.
* ``flash_mode/flash_freq/flash_size="keep"``: esptool must not patch the
  bootloader header, so the bytes in flash equal the hashed file.
* ``erase_all=False, encrypt=False, force=False``: never erase outside the
  written sectors, never encrypt, never bypass esptool's own safety checks.
* reads use ``flash_size="keep"`` (the default): the stub reads by address.
* ``reset_chip(esp, "hard-reset")`` (CLI ``--after hard-reset``) only after a
  command has finished successfully; after a failed write the chip is left
  in the loader (``--after no-reset``) so ``restore`` can reconnect.
"""

from __future__ import annotations

import hashlib
from dataclasses import dataclass, field
from typing import Any, Dict, List, Tuple

ROM_BAUD = 115200
CONNECT_ATTEMPTS = 7
CONNECT_MODE = "default-reset"
KEEP = "keep"


@dataclass(frozen=True)
class Data:
    """Placeholder for bytes in a call description."""

    length: int
    sha256: str
    name: str = ""

    def __repr__(self) -> str:
        n = " %s" % self.name if self.name else ""
        return "<%d bytes%s, sha256 %s...>" % (self.length, n, self.sha256[:16])

    @classmethod
    def of(cls, data: bytes, name: str = "") -> "Data":
        return cls(len(data), hashlib.sha256(data).hexdigest(), name)


@dataclass(frozen=True)
class EsptoolCall:
    func: str  # name in esptool.cmds, or "esp.<method>" for a loader method
    args: Tuple[Any, ...] = ()
    kwargs: Tuple[Tuple[str, Any], ...] = ()
    cli: str = ""

    def kw(self) -> Dict[str, Any]:
        return dict(self.kwargs)

    def api(self) -> str:
        parts = [] if self.func.startswith("esp.") or self.func == "detect_chip" else ["esp"]
        parts += [_fmt(a, hex_ints=True) for a in self.args]
        parts += ["%s=%s" % (k, _fmt(v)) for k, v in self.kwargs]
        name = self.func if self.func.startswith("esp.") else "esptool.cmds." + self.func
        return "%s(%s)" % (name, ", ".join(parts))


def _fmt(v: Any, hex_ints: bool = False) -> str:
    """Render a call argument; addresses and sizes (positional ints) in hex, bytes as a digest."""
    if isinstance(v, (bytes, bytearray)):
        return repr(Data.of(bytes(v)))
    if hex_ints and isinstance(v, int) and not isinstance(v, bool):
        return "0x%X" % v
    if isinstance(v, list):
        return "[" + ", ".join(_fmt(x, hex_ints) for x in v) + "]"
    if isinstance(v, tuple):
        return "(" + ", ".join(_fmt(x, hex_ints) for x in v) + ")"
    return repr(v)


def _cli_prefix(port: str, after: str) -> str:
    return "esptool --chip esp32s3 --port %s --baud %d --before %s --after %s" % (
        port or "<port>", ROM_BAUD, CONNECT_MODE, after)


def connect(port: str) -> List[EsptoolCall]:
    return [
        EsptoolCall("detect_chip", (), (("port", port), ("baud", ROM_BAUD), ("connect_mode", CONNECT_MODE),
                                         ("connect_attempts", CONNECT_ATTEMPTS)),
                    _cli_prefix(port, "no-reset") + " chip-id"),
        EsptoolCall("run_stub", (), (), "(implied: esptool uploads its RAM stub unless --no-stub)"),
        EsptoolCall("attach_flash", (), (), "(implied by every flash command)"),
    ]


def chip_info(port: str) -> List[EsptoolCall]:
    p = _cli_prefix(port, "no-reset")
    return [
        EsptoolCall("esp.get_chip_description", cli=p + " chip-id"),
        EsptoolCall("esp.read_mac", cli=p + " read-mac"),
        EsptoolCall("esp.get_chip_features", cli=p + " chip-id"),
        EsptoolCall("esp.flash_id", cli=p + " flash-id"),
        EsptoolCall("detect_flash_size", cli=p + " flash-id"),
        EsptoolCall("esp.flash_type", cli=p + " flash-id"),
    ]


def security_info(port: str) -> List[EsptoolCall]:
    p = _cli_prefix(port, "no-reset")
    return [
        EsptoolCall("esp.get_security_info", cli=p + " get-security-info"),
        EsptoolCall("esp.get_secure_boot_enabled", cli=p + " get-security-info"),
        EsptoolCall("esp.get_flash_encryption_enabled", cli=p + " get-security-info"),
    ]


def read(port: str, offset: int, length: int, out_name: str = "out.bin") -> EsptoolCall:
    return EsptoolCall(
        "read_flash", (offset, length), (("output", None), ("flash_size", KEEP), ("no_progress", False)),
        "%s read-flash 0x%X 0x%X %s" % (_cli_prefix(port, "no-reset"), offset, length, out_name))


def write(port: str, offset: int, data: Any, file_name: str) -> EsptoolCall:
    return EsptoolCall(
        "write_flash", ([(offset, data)],),
        (("flash_freq", KEEP), ("flash_mode", KEEP), ("flash_size", KEEP), ("erase_all", False),
         ("encrypt", False), ("force", False)),
        "%s write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x%X %s" % (
            _cli_prefix(port, "no-reset"), offset, file_name))


def hard_reset(port: str) -> EsptoolCall:
    return EsptoolCall("reset_chip", ("hard-reset",), (), "(the --after hard-reset of the last command)")


@dataclass
class CallLog:
    """Collects the calls a command made or would make, for logs and tests."""

    calls: List[EsptoolCall] = field(default_factory=list)

    def add(self, call: EsptoolCall) -> EsptoolCall:
        self.calls.append(call)
        return call
