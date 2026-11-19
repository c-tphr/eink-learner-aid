"""identify: every stop condition, the distinct already-converted result, and read-only behaviour."""

import json
import os

import pytest

import synth
from fakedevice import FakeChip, Faults
from helpers import run_cli
from piccoflash import common, layout


def chip(**kw):
    return FakeChip(synth.stock_flash(), **kw)


def identify(c, tmp_path):
    work = str(tmp_path / "work")
    code, out = run_cli(["identify", "--work-dir", work], c)
    assert c.writes == []  # identify never writes
    return code, out, work


def test_stock_device_passes(tmp_path):
    c = chip()
    before = bytes(c.flash)
    code, out, work = identify(c, tmp_path)
    assert code == common.OK, out
    assert "RESULT: PASS" in out
    assert bytes(c.flash) == before
    assert ("reset",) in c.ops
    recs = [f for f in os.listdir(work) if f.startswith("identify-")]
    assert len(recs) == 4  # .json plus bootloader, table and otadata blobs
    rec = json.load(open(os.path.join(work, [f for f in recs if f.endswith(".json")][0])))
    assert rec["verdict"] == "PASS" and rec["chip"]["mac"] == c.mac
    assert rec["partition_table"]["kind"] == "stock"
    reads = [op for op in c.ops if op[0] == "read"]
    assert reads == [("read", 0x0, 0x8000), ("read", 0x8000, 0xC00), ("read", 0x15000, 0x2000)]


def _set(c, **kw):
    for k, v in kw.items():
        setattr(c, k, v)
    return c


@pytest.mark.parametrize("change,needle", [
    (dict(chip_name="ESP32-S2"), "chip is ESP32-S2"),
    (dict(chip_name="ESP32"), "chip is ESP32 "),
    (dict(flash_size="8MB"), "flash chip reports 8MB"),
    (dict(flash_size=None), "unknown size"),
    (dict(flash_type="octal"), "octal"),
    (dict(secure_boot=True), "Secure Boot is ENABLED"),
    (dict(efuse_secure_boot=True), "Secure Boot is ENABLED"),
    (dict(flash_crypt_cnt=1), "flash encryption is ENABLED"),
    (dict(flash_crypt_cnt=3), "flash encryption is ENABLED"),
    (dict(secure_download_mode=True), "secure download mode is ON"),
    (dict(dis_download_icache=True), "RESTRICTED"),
    (dict(key_purposes=[0, 4, 0, 0, 0, 0, 0]), "XTS_AES_128_KEY"),
    (dict(key_purposes=[9, 0, 0, 0, 0, 0, 0]), "SECURE_BOOT_DIGEST0"),
])
def test_stop_conditions(tmp_path, change, needle):
    c = _set(chip(), **change)
    code, out, _ = identify(c, tmp_path)
    assert code == common.STOP, out
    assert "RESULT: STOP" in out and needle in out


def test_table_differing_from_stock_stops(tmp_path):
    c = chip()
    rows = list(layout.STOCK_TABLE)
    rows[0] = layout.Row("nvs", "data", "nvs", 0x9000, 0x5000)
    c.flash[0x8000:0x8C00] = layout.encode_table(rows)
    code, out, _ = identify(c, tmp_path)
    assert code == common.STOP and "differs from partitions_picco.csv" in out and "row 1" in out


def test_corrupt_table_stops(tmp_path):
    c = chip()
    c.flash[0x8010] ^= 0x01
    code, out, _ = identify(c, tmp_path)
    assert code == common.STOP and "MD5" in out


def test_stock_table_without_md5_entry_passes(tmp_path):
    c = chip()
    c.flash[0x8000:0x8C00] = layout.encode_table(layout.STOCK_TABLE, md5=False)
    code, out, _ = identify(c, tmp_path)
    assert code == common.OK and "no MD5 entry" in out


def test_already_converted_is_a_distinct_result(tmp_path):
    c = chip()
    c.flash[0x8000:0x8C00] = layout.encode_table(layout.TWO_SLOT_TABLE)
    code, out, _ = identify(c, tmp_path)
    assert code == common.ALREADY_CONVERTED
    assert "ALREADY CONVERTED" in out and "'flash' refuses this device" in out


def test_signed_bootloader_stops(tmp_path):
    c = chip()
    boot = synth.bootloader(seed=101, idf=b"v4.4-onyx", length=22000)
    at = (len(boot) + 0xFFF) & ~0xFFF
    c.flash[at:at + 2] = b"\xe7\x02"
    code, out, _ = identify(c, tmp_path)
    assert code == common.STOP and "signature block after the bootloader: PRESENT" in out


def test_missing_bootloader_stops(tmp_path):
    c = chip()
    c.flash[0:0x8000] = b"\xff" * 0x8000
    code, out, _ = identify(c, tmp_path)
    assert code == common.STOP and "not a valid ESP32-S3 image" in out


def test_unreachable_device_changes_nothing(tmp_path):
    c = chip(faults=Faults(unreachable=True))
    code, out, work = identify(c, tmp_path)
    assert code == common.NOT_REACHED
    assert "COULD NOT REACH" in out and "Device Manager" in out
    assert [op[0] for op in c.ops] == ["connect"]
    assert not [f for f in os.listdir(work) if f.startswith("identify-")]


def test_disconnect_during_identify(tmp_path):
    c = chip(faults=Faults(disconnect_on_read=2))
    code, out, _ = identify(c, tmp_path)
    assert code == common.NOT_REACHED and "Nothing was written" in out


def test_no_port_found_is_explained(tmp_path):
    from piccoflash.common import PortProblem

    def resolver(req, ui):
        raise PortProblem("No Espressif USB serial port was found.")

    code, out = run_cli(["identify", "--work-dir", str(tmp_path)], chip(), resolver=resolver)
    assert code == common.NOT_REACHED and "No Espressif" in out


def test_identify_dry_run_touches_nothing(tmp_path):
    c = chip()
    code, out = run_cli(["identify", "--dry-run", "--work-dir", str(tmp_path)], c)
    assert code == common.OK and c.ops == []
    assert "esptool.cmds.read_flash(esp, 0x8000, 0xC00" in out
