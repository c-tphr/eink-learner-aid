"""The real device: drives the pinned esptool through its public Python API.

Why the Python API and not the esptool command line: the guard approves a
bytes object, and this driver hands that same bytes object to
``esptool.cmds.write_flash``. Nothing re-reads the file between the guard's
check and the write, so the bytes written are exactly the bytes checked.
Identify decisions also use esptool's structured results (the
GET_SECURITY_INFO dictionary, eFuse reads, the flash JEDEC id) instead of
parsing printed text. Every call is described in esptool_calls.py, printed
by --dry-run, logged with its command-line equivalent, and executed from
that same description.

esptool is GPL-2.0-or-later. piccoflash imports and calls it; no esptool
source is copied into this repository.
"""

from __future__ import annotations

import os
from typing import Any, Optional

from . import PINNED_ESPTOOL_VERSION, esptool_calls, guard
from .common import session_command
from .device import ChipInfo, Device, DeviceError, DeviceNotReached, ProgressFn, SecurityInfo
from .ui import UI

_esptool = None
_current_ui = [None]  # where esptool's own output goes (the UI of the running command)


def load_esptool(ui: UI):
    """Import esptool with a clean environment; check its version and that no config file is active."""
    global _esptool
    _current_ui[0] = ui
    # Environment variables that change esptool's behaviour or make it open a
    # network connection (ESP_IDE_WS) are removed for this process, before import.
    for name in sorted(os.environ):
        if name.startswith("ESPTOOL") or name in ("ESP_IDE_WS",):
            ui.say("Ignoring environment variable %s for this run." % name)
            del os.environ[name]
    import esptool  # noqa: F401
    import esptool.cmds
    import esptool.config
    from esptool.logger import TemplateLogger, log

    if esptool.__version__ != PINNED_ESPTOOL_VERSION:
        raise DeviceError("esptool %s is installed, but this tool was checked against esptool %s only. "
                          "Delete the 'runtime' folder in the kit folder, then run:  %s" % (
                              esptool.__version__, PINNED_ESPTOOL_VERSION, session_command("setup")))
    _parser, cfg_path = esptool.config.load_config_file()
    if cfg_path:
        raise DeviceError("an esptool configuration file is active (%s). It can change reset and timing "
                          "behaviour; move it away and run again." % cfg_path)
    if _esptool is not None:
        return _esptool

    class _Log(TemplateLogger):
        def __init__(self):
            self._last_pct = -1

        def print(self, *args, **kwargs):
            text = kwargs.get("sep", " ").join(str(a) for a in args)
            _current_ui[0].detail(text)

        def note(self, message):
            _current_ui[0].detail("Note: %s" % message)

        def warning(self, message):
            _current_ui[0].detail("Warning: %s" % message)

        def error(self, message):
            _current_ui[0].detail("Error: %s" % message)

        def stage(self, finish=False):
            pass

        def progress_bar(self, cur_iter, total_iters, prefix="", suffix="", bar_length=30):
            pct = int(100 * cur_iter / total_iters) if total_iters else 100
            if pct // 10 != self._last_pct // 10 or pct == 100 and self._last_pct != 100:
                self._last_pct = pct
                _current_ui[0].detail("%s %3d%% %s" % (prefix.strip(), pct, suffix.strip()))
            if pct == 100:
                self._last_pct = -1

        def set_verbosity(self, verbosity):
            pass

    log.set_logger(_Log())
    _esptool = esptool
    return esptool


class EsptoolDevice(Device):
    def __init__(self, port: str, ui: UI):
        self.port = port
        self.ui = ui
        self._esp: Any = None
        self._cmds: Any = None

    # --- execution of described calls ---------------------------------------
    def _run(self, call: esptool_calls.EsptoolCall) -> Any:
        """Execute exactly the described call."""
        args = call.args
        self.ui.log_only("esptool: %s   [CLI: %s]" % (call.api(), call.cli))
        if call.func.startswith("esp."):
            return getattr(self._esp, call.func[4:])(*args, **call.kw())
        fn = getattr(self._cmds, call.func)
        if call.func == "detect_chip":
            return fn(*args, **call.kw())
        return fn(self._esp, *args, **call.kw())

    def connect(self) -> None:
        try:
            esptool = load_esptool(self.ui)
        except ImportError as exc:
            raise DeviceNotReached("esptool is not installed in this Python (%s). Run:  %s" % (
                exc, session_command("setup")))
        except DeviceError as exc:
            raise DeviceNotReached(str(exc))
        self._cmds = esptool.cmds
        detect, stub, attach = esptool_calls.connect(self.port)
        try:
            self._esp = self._run(detect)
        except Exception as exc:  # esptool raises FatalError, serial errors, OSError
            raise DeviceNotReached("%s: %s" % (type(exc).__name__, exc))
        try:
            if self._esp.CHIP_NAME != "ESP32-S3" or self._esp.secure_download_mode:
                return  # identify reports this; nothing else is attempted
            self._esp = self._run(stub)
            self._run(attach)
        except Exception as exc:  # nothing has been written; report it like a failed connection
            self.close()
            raise DeviceNotReached("connected, but starting the flasher stub failed: %s: %s"
                                   % (type(exc).__name__, exc))

    def close(self) -> None:
        esp, self._esp = self._esp, None
        if esp is not None:
            try:
                esp._port.close()
            except Exception:
                pass

    def _need(self) -> Any:
        if self._esp is None:
            raise DeviceError("not connected")
        return self._esp

    # --- read-only -------------------------------------------------------------
    def chip_info(self) -> ChipInfo:
        esp = self._need()

        def opt(fn, default=None):
            try:
                return fn()
            except Exception as exc:
                self.ui.log_only("chip_info: %s failed: %s" % (getattr(fn, "__name__", fn), exc))
                return default

        if esp.secure_download_mode:  # register reads are refused; identify stops on this
            return ChipInfo(chip_name=esp.CHIP_NAME, description="%s (secure download mode)" % esp.CHIP_NAME,
                            mac=opt(lambda: ":".join("%02x" % b for b in esp.read_mac()), "unknown"))
        try:
            mac = ":".join("%02x" % b for b in esp.read_mac())
            description = esp.get_chip_description()
        except Exception as exc:
            raise DeviceError("%s: %s" % (type(exc).__name__, exc))
        features = opt(esp.get_chip_features, []) or []
        info = ChipInfo(chip_name=esp.CHIP_NAME, description=description, mac=mac, features=list(features),
                        crystal_mhz=opt(esp.get_crystal_freq), stub=bool(esp.IS_STUB))
        if esp.CHIP_NAME == "ESP32-S3" and not esp.secure_download_mode:
            fid = opt(lambda: esp.flash_id())
            if fid is not None:
                info.flash_manufacturer = fid & 0xFF
                info.flash_device = ((fid >> 8) & 0xFF) << 8 | ((fid >> 16) & 0xFF)
            info.flash_size = opt(lambda: self._cmds.detect_flash_size(esp))
            ft = opt(esp.flash_type)
            info.flash_type_efuse = {0: "quad", 1: "octal"}.get(ft) if ft is not None else None
        info.psram = next((f for f in info.features if "PSRAM" in f), None)
        if opt(esp.uses_usb_jtag_serial, False):
            info.usb_mode = "USB-Serial/JTAG"
        elif opt(esp.uses_usb_otg, False):
            info.usb_mode = "USB-OTG"
        else:
            info.usb_mode = "serial bridge or unknown"
        return info

    def security_info(self) -> SecurityInfo:
        esp = self._need()
        try:
            si = esp.get_security_info()
        except Exception as exc:
            raise DeviceError("get_security_info failed: %s: %s" % (type(exc).__name__, exc))
        pf = si["parsed_flags"]
        info = SecurityInfo(
            flags=si["flags"], flash_crypt_cnt=si["flash_crypt_cnt"], key_purposes=list(si["key_purposes"]),
            secure_boot_enabled=bool(pf["SECURE_BOOT_EN"]), secure_download_mode=bool(pf["SECURE_DOWNLOAD_ENABLE"]),
            dis_download_icache=bool(pf["DIS_DOWNLOAD_ICACHE"]), dis_download_dcache=bool(pf["DIS_DOWNLOAD_DCACHE"]),
            dis_usb=bool(pf["DIS_USB"]), chip_id=si.get("chip_id"),
            raw={k: v for k, v in si.items() if k != "parsed_flags"})
        info.raw["parsed_flags"] = dict(pf)
        if not info.secure_download_mode:
            for attr, fn in (("efuse_secure_boot_enabled", "get_secure_boot_enabled"),
                             ("efuse_flash_encryption_enabled", "get_flash_encryption_enabled")):
                try:
                    setattr(info, attr, bool(getattr(esp, fn)()))
                except Exception as exc:
                    self.ui.log_only("security_info: %s failed: %s" % (fn, exc))
        return info

    def read(self, offset: int, length: int, progress: ProgressFn = None) -> bytes:
        self._need()
        call = esptool_calls.read(self.port, offset, length)
        try:
            data = self._run(call)
        except Exception as exc:
            raise DeviceError("read of 0x%X+0x%X failed: %s: %s" % (offset, length, type(exc).__name__, exc))
        if not isinstance(data, (bytes, bytearray)) or len(data) != length:
            raise DeviceError("read of 0x%X+0x%X returned %s bytes" % (offset, length, len(data or b"")))
        return bytes(data)

    # --- the only write ----------------------------------------------------------
    def write(self, approved) -> None:
        self._need()
        guard.recheck(approved)  # immediately before esptool: the exact bytes, policy re-checked
        call = esptool_calls.write(self.port, approved.offset, approved.data, approved.role)
        try:
            self._run(call)  # call.args holds approved.data itself
        except Exception as exc:
            raise DeviceError("write at 0x%X failed: %s: %s" % (approved.offset, type(exc).__name__, exc))

    def hard_reset(self) -> None:
        self._need()
        try:
            self._run(esptool_calls.hard_reset(self.port))
        except Exception as exc:
            raise DeviceError("reset failed: %s: %s" % (type(exc).__name__, exc))


def factory(port: str, ui: UI) -> Device:
    return EsptoolDevice(port, ui)


def esptool_version() -> Optional[str]:
    try:
        import esptool

        return esptool.__version__
    except Exception:
        return None
