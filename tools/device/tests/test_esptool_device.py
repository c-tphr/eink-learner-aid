"""The real driver (EsptoolDevice) running the pinned esptool's own command functions.

No serial port is opened. Only the lowest layer, the esptool loader object
that would talk to the chip's flasher stub over USB, is replaced by
FakeStubLoader, an ESP32S3StubLoader subclass whose protocol methods
(flash_id, read_flash, flash_defl_begin/block/finish, flash_md5sum, ...) act
on an in-memory 16 MB flash. Everything above it is esptool 5.6.0's real code:
esptool.cmds.read_flash and write_flash (header handling, padding,
compression, MD5 verification), driven by piccoflash's call descriptions.
"""

import hashlib
import inspect
import os
import zlib

import esptool
import esptool.cmds
import pytest
from esptool.targets.esp32s3 import ESP32S3ROM, ESP32S3StubLoader

import synth
from helpers import make_kit
from piccoflash import common, esptool_calls, esptool_device, guard, layout, plan as planmod
from piccoflash.device import DeviceNotReached
from piccoflash.ui import UI


class _Port:
    baudrate = 115200

    def close(self):
        pass


class FakeStubLoader(ESP32S3StubLoader):
    """An ESP32-S3 'running the stub', simulated at the loader-method level."""

    def __new__(cls, *a, **k):
        return object.__new__(cls)

    def __init__(self, flash: bytearray, mac=(0x24, 0x58, 0x7C, 0x12, 0x34, 0x56)):
        self.flash = flash
        self.mac = mac
        self.secure_download_mode = False
        self.sync_stub_detected = False
        self.cache = {"flash_id": None, "chip_id": None, "uart_no": None, "usb_pid": 0x1001, "usb_vid": 0x303A,
                      "security_info": None}
        self._port = _Port()
        self.events = []
        self._w = None

    # identity and eFuses
    def read_mac(self, mac_type="BASE_MAC"):
        return self.mac

    def get_chip_description(self):
        return "ESP32-S3 (QFN56) (revision v0.2)"

    def get_chip_features(self):
        return ["Wi-Fi", "BT 5 (LE)", "Dual Core + LP Core", "240MHz", "Embedded PSRAM 8MB (AP_1v8)"]

    def get_crystal_freq(self):
        return 40

    def get_chip_revision(self):
        return 2

    def get_major_chip_version(self):
        return 0

    def get_minor_chip_version(self):
        return 2

    def flash_type(self):
        return 0

    def get_usb_vid_pid(self):
        return (0x303A, 0x1001)

    def get_security_info(self, cache=True):
        names = ["SECURE_BOOT_EN", "SECURE_BOOT_AGGRESSIVE_REVOKE", "SECURE_DOWNLOAD_ENABLE",
                 "SECURE_BOOT_KEY_REVOKE0", "SECURE_BOOT_KEY_REVOKE1", "SECURE_BOOT_KEY_REVOKE2", "SOFT_DIS_JTAG",
                 "HARD_DIS_JTAG", "DIS_USB", "DIS_DOWNLOAD_DCACHE", "DIS_DOWNLOAD_ICACHE"]
        flags = {n: False for n in names}
        flags["SECURE_DOWNLOAD_ENABLE"] = self.secure_download_mode
        return {"flags": 4 if self.secure_download_mode else 0, "flash_crypt_cnt": 0, "key_purposes": [0] * 7,
                "chip_id": 9, "api_version": 0, "parsed_flags": flags}

    def get_secure_boot_enabled(self):
        return 0

    def get_flash_encryption_enabled(self):
        return False

    def uses_key_manager_for_flash_encryption(self):
        return False

    def get_encrypted_download_disabled(self):
        return False

    # SPI flash protocol
    def flash_id(self, cache=True):
        return 0x184020  # XMC, 16 MB

    def flash_set_parameters(self, size):
        self.events.append(("set_parameters", size))

    def read_flash(self, offset, length, progress_fn=None):
        self.events.append(("read", offset, length))
        if progress_fn:
            progress_fn(length, length, offset)
        return bytes(self.flash[offset:offset + length])

    def _erase(self, offset, size):
        start, end = guard.touched_span(offset, size)
        self.flash[start:end] = b"\xff" * (end - start)

    def flash_defl_begin(self, size, compsize, offset, encrypted_write=False):
        assert not encrypted_write
        self.events.append(("write_begin", offset, size))
        self._erase(offset, size)
        self._w = {"pos": offset, "z": zlib.decompressobj()}
        return (compsize + self.FLASH_WRITE_SIZE - 1) // self.FLASH_WRITE_SIZE

    def flash_defl_block(self, data, seq, timeout=None):
        out = self._w["z"].decompress(data)
        p = self._w["pos"]
        self.flash[p:p + len(out)] = out
        self._w["pos"] += len(out)

    def flash_defl_finish(self, reboot=False, timeout=None):
        self.events.append(("write_end", self._w["pos"]))

    def flash_begin(self, size, offset, encrypted_write=False, logging=True):
        self.events.append(("write_begin_raw", offset, size))
        self._erase(offset, size)
        self._w = {"pos": offset}
        return (size + self.FLASH_WRITE_SIZE - 1) // self.FLASH_WRITE_SIZE

    def flash_block(self, data, seq, timeout=None, encrypted=False):
        p = self._w["pos"]
        self.flash[p:p + len(data)] = data
        self._w["pos"] += len(data)

    def flash_finish(self, reboot=False, timeout=None):
        pass

    def flash_md5sum(self, addr, size):
        return hashlib.md5(bytes(self.flash[addr:addr + size])).hexdigest()

    def hard_reset(self):
        self.events.append(("hard_reset",))


@pytest.fixture
def patched(monkeypatch):
    """Route esptool's connection functions to one FakeStubLoader; record what was called."""
    state = {"loader": FakeStubLoader(synth.stock_flash()), "calls": []}

    def detect_chip(**kw):
        state["calls"].append(("detect_chip", kw))
        return state["loader"]

    def run_stub(esp):
        state["calls"].append(("run_stub",))
        return esp

    def attach_flash(esp):
        state["calls"].append(("attach_flash",))

    monkeypatch.setattr(esptool.cmds, "detect_chip", detect_chip)
    monkeypatch.setattr(esptool.cmds, "run_stub", run_stub)
    monkeypatch.setattr(esptool.cmds, "attach_flash", attach_flash)
    return state


def test_call_descriptions_bind_to_the_real_esptool_signatures():
    port = "COM7"
    calls = esptool_calls.connect(port) + [esptool_calls.read(port, 0, 16), esptool_calls.write(port, 0, b"x", "f"),
                                           esptool_calls.hard_reset(port)]
    for call in calls:
        fn = getattr(esptool.cmds, call.func)
        args = call.args if call.func == "detect_chip" else (object(),) + call.args
        inspect.signature(fn).bind(*args, **call.kw())  # raises TypeError on a wrong name
    # write_flash takes extra options through **kwargs: check each against its documented list
    doc = esptool.cmds.write_flash.__doc__
    for key, _ in esptool_calls.write(port, 0, b"x", "f").kwargs:
        assert key in doc, key
    for call in esptool_calls.chip_info(port) + esptool_calls.security_info(port):
        name = call.func.split(".", 1)[1] if call.func.startswith("esp.") else call.func
        assert hasattr(ESP32S3ROM, name) or hasattr(esptool.cmds, name), name


def test_keep_means_esptool_does_not_patch_the_bootloader_header():
    esp = ESP32S3ROM.__new__(ESP32S3ROM)
    esp.secure_download_mode = False
    boot = synth.bootloader()
    # what piccoflash passes: unchanged bytes
    assert esptool.cmds._update_image_flash_params(esp, 0x0, "keep", "keep", "keep", boot) == boot
    # what an explicit setting would do: the header byte and the appended SHA-256 change
    patched = esptool.cmds._update_image_flash_params(esp, 0x0, "keep", "qio", "keep", boot)
    assert patched != boot and patched[2] == 0 and len(patched) == len(boot)


def test_driver_runs_whole_session_through_real_esptool_code(tmp_path, patched):
    loader = patched["loader"]
    original = bytes(loader.flash)
    kit = make_kit(str(tmp_path))
    work = str(tmp_path / "work")

    def run(argv, answers=()):
        import io

        from helpers import scripted
        from piccoflash import cli

        ui = UI(out=io.StringIO(), ask_fn=scripted(answers))
        code = cli.main(argv, device_factory=esptool_device.factory, port_resolver=lambda r, u: "COM7", ui=ui)
        return code, "\n".join(ui.lines)

    code, out = run(["identify", "--work-dir", work])
    assert code == common.OK, out
    ack = hashlib.sha256(original).hexdigest()[-8:]
    code, out = run(["backup", "--work-dir", work], [ack])
    assert code == common.OK, out
    code, out = run(["flash", "--work-dir", work, "--plan", kit], [common.FLASH_PHRASE])
    assert code == common.OK, out
    assert "Hash of data verified" in out  # esptool's own post-write MD5 check ran

    plan = planmod.load_and_verify(kit)
    for img in plan.images:
        data = planmod.read_image_bytes(plan, img)
        assert bytes(loader.flash[img.offset:img.offset + len(data)]) == data  # exact bytes: header not patched
    # esptool compresses unless that does not help (random synthetic data): either path counts
    writes = [e for e in loader.events if e[0] in ("write_begin", "write_begin_raw")]
    assert [(w[1], w[2]) for w in writes] == [(0x20000, plan.image("app").size), (0x15000, 0x2000),
                                               (0x8000, 0xC00), (0x0, plan.image("bootloader").size)]
    assert bytes(loader.flash[0x9000:0x15000]) == original[0x9000:0x15000]
    detect = [c for c in patched["calls"] if c[0] == "detect_chip"]
    assert detect[0][1] == {"port": "COM7", "baud": 115200, "connect_mode": "default-reset", "connect_attempts": 7}

    backup_bin = next(f for f in os.listdir(work) if f.startswith("picco-backup-") and f.endswith(".bin"))
    code, out = run(["restore", "--work-dir", work, os.path.join(work, backup_bin)], [common.RESTORE_PHRASE])
    assert code == common.OK, out
    assert bytes(loader.flash) == original


def test_driver_write_refuses_unapproved_and_tampered(patched):
    dev = esptool_device.EsptoolDevice("COM7", UI(out=open(os.devnull, "w")))
    dev.connect()
    before = bytes(patched["loader"].flash)
    with pytest.raises(guard.GuardError):
        dev.write((0x0, b"\x00" * 16))
    img = guard.PlannedImage("boot_app0", "boot_app0.bin", 0x15000, 0x2000,
                             hashlib.sha256(layout.boot_app0_bytes()).hexdigest())
    ok = guard.approve_flash_write(img, layout.boot_app0_bytes())
    object.__setattr__(ok, "offset", 0xE000)
    with pytest.raises(guard.GuardError):
        dev.write(ok)
    assert bytes(patched["loader"].flash) == before
    assert not [e for e in patched["loader"].events if e[0].startswith("write")]


def test_wrong_esptool_version_or_config_file_refuses_to_connect(monkeypatch, patched, tmp_path):
    ui = UI(out=open(os.devnull, "w"))
    monkeypatch.setattr(esptool, "__version__", "4.9.0")
    with pytest.raises(DeviceNotReached) as e:
        esptool_device.EsptoolDevice("COM7", ui).connect()
    assert "4.9.0" in str(e.value)
    monkeypatch.setattr(esptool, "__version__", "5.6.0")
    monkeypatch.setattr(esptool.config, "load_config_file", lambda verbose=False: (None, str(tmp_path / "esptool.cfg")))
    with pytest.raises(DeviceNotReached) as e:
        esptool_device.EsptoolDevice("COM7", ui).connect()
    assert "configuration file" in str(e.value)
    assert patched["calls"] == []


def test_esptool_environment_variables_are_removed(monkeypatch, patched):
    monkeypatch.setenv("ESPTOOL_CFGFILE", "/nonexistent.cfg")
    monkeypatch.setenv("ESP_IDE_WS", "ws://127.0.0.1:1")
    dev = esptool_device.EsptoolDevice("COM7", UI(out=open(os.devnull, "w")))
    dev.connect()
    assert "ESPTOOL_CFGFILE" not in os.environ and "ESP_IDE_WS" not in os.environ


def test_connect_failure_is_not_reached(monkeypatch):
    def boom(**kw):
        raise esptool.util.FatalError("Failed to connect to ESP32-S3: No serial data received.")

    monkeypatch.setattr(esptool.cmds, "detect_chip", boom)
    with pytest.raises(DeviceNotReached) as e:
        esptool_device.EsptoolDevice("COM7", UI(out=open(os.devnull, "w"))).connect()
    assert "No serial data received" in str(e.value)


def test_wrong_chip_skips_stub_and_reports(monkeypatch, patched):
    patched["loader"].CHIP_NAME = "ESP32-S2"
    dev = esptool_device.EsptoolDevice("COM7", UI(out=open(os.devnull, "w")))
    dev.connect()
    assert [c[0] for c in patched["calls"]] == ["detect_chip"]
    assert dev.chip_info().chip_name == "ESP32-S2"


def _identify_real(tmp_path):
    import io

    from helpers import scripted
    from piccoflash import cli

    ui = UI(out=io.StringIO(), ask_fn=scripted([]))
    code = cli.main(["identify", "--work-dir", str(tmp_path)], device_factory=esptool_device.factory,
                    port_resolver=lambda r, u: "COM7", ui=ui)
    return code, "\n".join(ui.lines)


def test_identify_through_driver_stops_on_secure_download_mode(tmp_path, patched):
    patched["loader"].secure_download_mode = True
    code, out = _identify_real(tmp_path)
    assert code == common.STOP and "secure download mode is ON" in out
    assert [c[0] for c in patched["calls"]] == ["detect_chip"]  # no stub, no flash access
    assert not [e for e in patched["loader"].events if e[0] in ("read", "write_begin", "write_begin_raw")]


def test_identify_through_driver_stops_on_other_chip(tmp_path, patched):
    patched["loader"].CHIP_NAME = "ESP32-S2"
    code, out = _identify_real(tmp_path)
    assert code == common.STOP and "chip is ESP32-S2" in out
    assert not [e for e in patched["loader"].events if e[0] == "read"]
