"""backup: two full reads, keep only on equality, acknowledgement."""

import hashlib
import io
import json
import os

import synth
from fakedevice import FakeChip, Faults
from helpers import identify_and_backup, run_cli, scripted
from piccoflash import cli, common, workdir
from piccoflash.ui import UI


def test_backup_happy_path(tmp_path):
    chip = FakeChip(synth.stock_flash())
    work = str(tmp_path / "work")
    side_path, rec = identify_and_backup(chip, work)
    assert chip.writes == []
    assert chip.full_reads == 2
    assert rec["verified"] is True and rec["size"] == 16 * 1024 * 1024
    assert rec["sha256"] == hashlib.sha256(bytes(chip.flash)).hexdigest()
    assert rec["reads"][0]["sha256"] == rec["reads"][1]["sha256"] == rec["sha256"]
    assert rec["mac"] == chip.mac and rec["partition_table"] == "stock"
    bin_path = os.path.join(work, rec["file"])
    assert open(bin_path, "rb").read() == bytes(chip.flash)
    assert rec["file"].startswith("picco-backup-24587c123456-") and rec["file"].endswith(".bin")
    assert not [f for f in os.listdir(work) if ".read" in f]  # the second read was removed after comparison
    assert chip.ops.count(("reset",)) == 2  # after identify and after backup


def test_backup_requires_identify(tmp_path):
    chip = FakeChip(synth.stock_flash())
    code, out = run_cli(["backup", "--work-dir", str(tmp_path)], chip)
    assert code == common.STOP and "no passing 'identify'" in out
    assert chip.ops == []


def test_backup_requires_identify_of_this_device(tmp_path):
    work = str(tmp_path)
    first = FakeChip(synth.stock_flash())
    assert run_cli(["identify", "--work-dir", work], first)[0] == common.OK
    other = FakeChip(synth.stock_flash(seed=8), mac="24:58:7c:00:00:01")
    code, out = run_cli(["backup", "--work-dir", work], other)
    assert code == common.STOP and "24:58:7c:00:00:01" in out
    assert other.full_reads == 0 and other.writes == []


def test_backup_reads_differ(tmp_path):
    chip = FakeChip(synth.stock_flash(), faults=Faults(flip_full_read=(2, 0x345678)))
    work = str(tmp_path)
    assert run_cli(["identify", "--work-dir", work], chip)[0] == common.OK
    code, out = run_cli(["backup", "--work-dir", work], chip)
    assert code == common.STOP
    assert "THE TWO READS DIFFER" in out and "0x340000" in out and "another USB cable" in out
    files = os.listdir(work)
    assert len([f for f in files if f.endswith(".read1.bin") or f.endswith(".read2.bin")]) == 2
    mismatch = [f for f in files if f.endswith("-MISMATCH.json")]
    assert len(mismatch) == 1
    report = json.load(open(os.path.join(work, mismatch[0])))
    assert report["differing_64k_blocks"] == ["0x340000"]
    assert workdir.latest_verified_backup(work, chip.mac) is None
    assert chip.writes == []


def test_backup_identify_drift_is_not_kept(tmp_path):
    # Both full reads agree with each other but not with what identify read moments before.
    chip = FakeChip(synth.stock_flash())
    work = str(tmp_path)
    assert run_cli(["identify", "--work-dir", work], chip)[0] == common.OK

    def factory(port, ui=None):
        dev = chip.factory(port, ui)
        real = dev.read

        def read(off, length, progress=None):
            data = real(off, length, progress)
            if length == 16 * 1024 * 1024:
                data = data[:0x100] + bytes([data[0x100] ^ 1]) + data[0x101:]
            return data

        dev.read = read
        return dev

    ui = UI(out=io.StringIO(), ask_fn=scripted([]))
    code = cli.main(["backup", "--work-dir", work], device_factory=factory, port_resolver=lambda r, u: "COM7", ui=ui)
    out = "\n".join(ui.lines)
    assert code == common.STOP and "differs from the bootloader/table read" in out
    assert workdir.latest_verified_backup(work, chip.mac) is None


def test_backup_unacknowledged_is_verified_but_flagged(tmp_path):
    chip = FakeChip(synth.stock_flash())
    work = str(tmp_path)
    assert run_cli(["identify", "--work-dir", work], chip)[0] == common.OK
    code, out = run_cli(["backup", "--work-dir", work], chip, answers=["wrong", "wrong", "wrong"])
    assert code == common.OK
    assert "not yet confirmed" in out
    side, rec = workdir.latest_verified_backup(work, chip.mac)
    assert rec["verified"] is True and rec["owner_ack"] is None


def test_backup_disconnect_marks_nothing(tmp_path):
    chip = FakeChip(synth.stock_flash())
    work = str(tmp_path)
    assert run_cli(["identify", "--work-dir", work], chip)[0] == common.OK
    chip.faults.disconnect_on_read = chip.reads + 5  # during the first full read
    code, out = run_cli(["backup", "--work-dir", work], chip)
    assert code == common.NOT_REACHED and "No backup was marked as verified" in out
    assert workdir.latest_verified_backup(work, chip.mac) is None


def test_backup_dry_run_touches_nothing(tmp_path):
    chip = FakeChip(synth.stock_flash())
    work = str(tmp_path)
    assert run_cli(["identify", "--work-dir", work], chip)[0] == common.OK
    chip.ops.clear()
    code, out = run_cli(["backup", "--dry-run", "--work-dir", work], chip)
    assert code == common.OK and chip.ops == []
    assert "read_flash(esp, 0x0, 0x1000000" in out
