"""restore: guard on size and recorded hash, MAC check with override, read-back verification."""

import json
import os
import shutil

import pytest

import synth
from fakedevice import FakeChip
from helpers import identify_and_backup, run_cli
from piccoflash import common


@pytest.fixture
def backed_up(tmp_path):
    chip = FakeChip(synth.stock_flash())
    original = bytes(chip.flash)
    work = str(tmp_path / "work")
    side, rec = identify_and_backup(chip, work)
    chip.ops.clear()
    # simulate a half-flashed device
    chip.flash[0:0x30000] = b"\xab" * 0x30000
    return chip, original, work, os.path.join(work, rec["file"]), side


def restore(chip, path, answers=(common.RESTORE_PHRASE,), *extra):
    return run_cli(["restore", "--work-dir", os.path.dirname(path), path, *extra], chip, answers=list(answers))


def test_restore_writes_and_verifies(backed_up):
    chip, original, work, path, _ = backed_up
    code, out = restore(chip, path)
    assert code == common.OK, out
    assert bytes(chip.flash) == original
    assert chip.writes == [("write", 0, 16 * 1024 * 1024)]
    reads = [op for op in chip.ops if op[0] == "read"]
    assert reads == [("read", 0, 16 * 1024 * 1024)]
    assert ("reset",) in chip.ops


def test_restore_does_not_need_identify_to_pass(backed_up):
    chip, original, work, path, _ = backed_up
    chip.flash[0x8000:0x9000] = b"\x00" * 0x1000  # table destroyed: identify would STOP
    code, out = restore(chip, path)
    assert code == common.OK and bytes(chip.flash) == original


def test_wrong_size_is_refused(backed_up, tmp_path):
    chip, original, work, path, side = backed_up
    with open(path, "ab") as fp:
        fp.write(b"\xff")
    code, out = restore(chip, path)
    assert code == common.STOP and "16 MiB" in out and chip.ops == []


def test_wrong_hash_is_refused(backed_up):
    chip, original, work, path, side = backed_up
    with open(path, "r+b") as fp:
        fp.seek(0x800000)
        fp.write(b"\x00\x00")
    code, out = restore(chip, path)
    assert code == common.STOP and "does not match the hash recorded" in out and chip.ops == []


def test_missing_or_unverified_record_is_refused(backed_up):
    chip, original, work, path, side = backed_up
    rec = json.load(open(side))
    rec["verified"] = False
    json.dump(rec, open(side, "w"))
    code, out = restore(chip, path)
    assert code == common.STOP and "verified" in out
    os.remove(side)
    code, out = restore(chip, path)
    assert code == common.STOP and "no backup record" in out
    assert chip.ops == []


def test_renamed_backup_without_matching_record_is_refused(backed_up, tmp_path):
    chip, original, work, path, side = backed_up
    other = str(tmp_path / "copy.bin")
    shutil.copy(path, other)
    shutil.copy(side, str(tmp_path / "copy.json"))
    code, out = restore(chip, other)
    assert code == common.STOP and "the record is for" in out


def test_different_device_needs_override_phrase(backed_up):
    chip, original, work, path, _ = backed_up
    chip.mac = "24:58:7c:00:00:02"
    code, out = restore(chip, path, (common.RESTORE_PHRASE, "yes"))
    assert code == common.STOP and "DIFFERENT device" in out and chip.writes == []
    code, out = restore(chip, path, (common.RESTORE_PHRASE, common.OTHER_DEVICE_PHRASE))
    assert code == common.OK and bytes(chip.flash) == original


def test_wrong_phrase_writes_nothing(backed_up):
    chip, original, work, path, _ = backed_up
    code, out = restore(chip, path, ("restore",))
    assert code == common.STOP and chip.ops == []


@pytest.mark.parametrize("change,needle", [
    (dict(chip_name="ESP32-C3"), "not an ESP32-S3"),
    (dict(flash_size="8MB"), "not 16MB"),
    (dict(flash_crypt_cnt=1), "flash encryption"),
    (dict(secure_download_mode=True), "secure download mode"),
])
def test_unsafe_targets_are_refused(backed_up, change, needle):
    chip, original, work, path, _ = backed_up
    for k, v in change.items():
        setattr(chip, k, v)
    code, out = restore(chip, path)
    assert code == common.STOP and needle in out and chip.writes == []


def test_readback_mismatch_reports_failure(backed_up):
    chip, original, work, path, _ = backed_up
    chip.faults.corrupt_after_write_at = 0
    code, out = restore(chip, path)
    assert code == common.RESTORE_FAILED and "RESTORE DID NOT COMPLETE" in out
    assert ("reset",) not in chip.ops
    chip.faults.corrupt_after_write_at = None
    code, out = restore(chip, path)  # running it again fixes it
    assert code == common.OK and bytes(chip.flash) == original


def test_write_failure_reports_and_retry_works(backed_up):
    chip, original, work, path, _ = backed_up
    chip.faults.fail_write_at = (0, 1_000_000)
    code, out = restore(chip, path)
    assert code == common.RESTORE_FAILED and "run the restore again" in out.lower()
    chip.faults.fail_write_at = None
    code, out = restore(chip, path)
    assert code == common.OK and bytes(chip.flash) == original


def test_restore_dry_run_touches_nothing(backed_up):
    chip, original, work, path, _ = backed_up
    before = bytes(chip.flash)
    code, out = run_cli(["restore", "--dry-run", "--work-dir", work, path], chip)
    assert code == common.OK and chip.ops == [] and bytes(chip.flash) == before
    assert "write_flash(esp, [(0x0, <16777216 bytes" in out and "flash_mode='keep'" in out
