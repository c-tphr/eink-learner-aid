"""flash end to end against the fake device, every stop condition, dry run, restore after failure."""

import hashlib
import os
import re

import pytest

import synth
from fakedevice import FakeChip
from helpers import identify_and_backup, make_kit, run_cli
from piccoflash import common, guard, layout, plan as planmod

PHRASE = common.FLASH_PHRASE


@pytest.fixture
def setup(tmp_path):
    chip = FakeChip(synth.stock_flash())
    original = bytes(chip.flash)
    kit = make_kit(str(tmp_path))
    work = str(tmp_path / "work")
    side, rec = identify_and_backup(chip, work)
    chip.ops.clear()
    return chip, original, kit, work, os.path.join(work, rec["file"])


def flash(chip, kit, work, answers=(PHRASE,), *extra):
    return run_cli(["flash", "--work-dir", work, "--plan", kit, *extra], chip, answers=list(answers))


def assert_flashed(flash_bytes, original, plan):
    """Stock everywhere except the four regions; in each region the image, then erased bytes, then stock."""
    f = bytes(flash_bytes)
    covered = []
    for img in plan.images:
        data = planmod.read_image_bytes(plan, img)
        start, end = guard.touched_span(img.offset, len(data))
        assert f[img.offset:img.offset + len(data)] == data, img.role
        assert f[img.offset + len(data):end] == b"\xff" * (end - img.offset - len(data)), img.role
        covered.append((start, end))
    pos = 0
    for start, end in sorted(covered) + [(layout.FLASH_SIZE, layout.FLASH_SIZE)]:
        assert f[pos:start] == original[pos:start], "changed outside the regions at 0x%X-0x%X" % (pos, start)
        pos = end
    assert f[0x9000:0x15000] == original[0x9000:0x15000]  # NVS untouched


def test_happy_path_identify_backup_flash_restore(setup):
    chip, original, kit, work, backup_path = setup
    code, out = flash(chip, kit, work)
    assert code == common.OK, out
    assert "DONE: all four images written and verified" in out
    plan = planmod.load_and_verify(kit)
    assert_flashed(chip.flash, original, plan)
    assert [(op[1], op[2]) for op in chip.writes] == [
        (0x20000, plan.image("app").size), (0x15000, 0x2000), (0x8000, 0xC00), (0x0, plan.image("bootloader").size)]
    assert chip.ops[-2:] == [("reset",), ("close",)]
    parsed = layout.parse_table(bytes(chip.flash[0x8000:0x8C00]))
    assert layout.classify_table(parsed.rows) == "two-slot"
    assert layout.otadata_boot_slot(bytes(chip.flash[0x15000:0x17000]), 2) == 0
    results = [f for f in os.listdir(work) if f.startswith("flash-result-")]
    assert len(results) == 1

    # identify now reports the distinct already-converted result, and flash refuses to run again
    code, out = run_cli(["identify", "--work-dir", work], chip)
    assert code == common.ALREADY_CONVERTED
    writes_before = len(chip.writes)
    code, out = flash(chip, kit, work)
    assert code == common.STOP and len(chip.writes) == writes_before

    # restore puts back every byte
    code, out = run_cli(["restore", "--work-dir", work, backup_path], chip, answers=[common.RESTORE_PHRASE])
    assert code == common.OK, out
    assert bytes(chip.flash) == original
    assert "VERIFIED" in out


def test_wrong_phrase_writes_nothing(setup):
    chip, original, kit, work, _ = setup
    for answer in ("replace onyx", "y", "yes", "", "REPLACE ONYX NOW"):
        code, out = flash(chip, kit, work, (answer,))
        assert code == common.STOP and "Cancelled" in out
    assert chip.ops == [] and bytes(chip.flash) == original


def test_no_backup_refuses(tmp_path):
    chip = FakeChip(synth.stock_flash())
    kit = make_kit(str(tmp_path))
    work = str(tmp_path / "work")
    assert run_cli(["identify", "--work-dir", work], chip)[0] == common.OK
    code, out = flash(chip, kit, work)
    assert code == common.STOP and "no verified backup" in out
    assert chip.writes == []


def test_backup_file_altered_refuses(setup):
    chip, original, kit, work, backup_path = setup
    with open(backup_path, "r+b") as fp:
        fp.seek(0x123456)
        fp.write(b"\x00")
    code, out = flash(chip, kit, work)
    assert code == common.STOP and "does not match the hash recorded" in out
    assert chip.ops == []


def test_backup_file_missing_refuses(setup):
    chip, original, kit, work, backup_path = setup
    os.remove(backup_path)
    code, out = flash(chip, kit, work)
    assert code == common.STOP and "is missing" in out and chip.ops == []


def test_device_changed_between_backup_and_flash(setup):
    chip, original, kit, work, _ = setup
    other = FakeChip(bytearray(original), mac="24:58:7c:99:99:99")
    code, out = flash(other, kit, work)
    assert code == common.STOP and "different device" in out
    assert other.writes == [] and bytes(other.flash) == original


def test_layout_changed_between_backup_and_flash(setup):
    chip, original, kit, work, _ = setup
    chip.flash[0x8000:0x8C00] = layout.encode_table(layout.TWO_SLOT_TABLE)
    code, out = flash(chip, kit, work)
    assert code == common.STOP and "already has the two-slot table" in out
    assert chip.writes == []


def test_bootloader_changed_since_backup(setup):
    chip, original, kit, work, _ = setup
    chip.flash[0x100] ^= 0x01  # still a valid-looking header region? not necessarily; any change is refused
    code, out = flash(chip, kit, work)
    assert code == common.STOP and chip.writes == []
    assert "bootloader" in out


def test_security_turned_on_refuses(setup):
    chip, original, kit, work, _ = setup
    chip.secure_boot = True
    code, out = flash(chip, kit, work)
    assert code == common.STOP and "Secure Boot is ENABLED" in out and chip.writes == []


def test_unreachable_at_flash_time(setup):
    chip, original, kit, work, _ = setup
    chip.faults.unreachable = True
    code, out = flash(chip, kit, work)
    assert code == common.NOT_REACHED and chip.writes == [] and bytes(chip.flash) == original


def test_flash_fails_part_way_then_restore_succeeds(setup):
    chip, original, kit, work, backup_path = setup
    chip.faults.fail_write_at = (0x20000, 300_000)
    code, out = flash(chip, kit, work)
    assert code == common.FLASH_FAILED
    assert "FLASHING DID NOT COMPLETE" in out and "restore" in out and backup_path in out
    assert [op[1] for op in chip.writes] == [0x20000]  # nothing after the failed region
    assert bytes(chip.flash) != original
    assert bytes(chip.flash[0:0x9000]) == original[0:0x9000]  # bootloader and table not reached
    chip.faults.fail_write_at = None
    code, out = run_cli(["restore", "--work-dir", work, backup_path], chip, answers=[common.RESTORE_PHRASE])
    assert code == common.OK, out
    assert bytes(chip.flash) == original


def test_verification_mismatch_stops_before_later_regions(setup):
    chip, original, kit, work, backup_path = setup
    chip.faults.corrupt_after_write_at = 0x15000
    code, out = flash(chip, kit, work)
    assert code == common.FLASH_FAILED and "verification failed" in out
    assert [op[1] for op in chip.writes] == [0x20000, 0x15000]
    assert bytes(chip.flash[0:0x9000]) == original[0:0x9000]
    res = [f for f in os.listdir(work) if f.startswith("flash-result-")]
    assert len(res) == 1 and "FAILED" in open(os.path.join(work, res[0])).read()


def test_disconnect_during_write(setup):
    chip, original, kit, work, backup_path = setup
    chip.faults.disconnect_on_write = True
    code, out = flash(chip, kit, work)
    assert code == common.FLASH_FAILED and "restore" in out


def test_file_changed_during_flash_is_refused_at_the_next_write(setup):
    chip, original, kit, work, _ = setup
    table = os.path.join(kit, "partitions.bin")

    def hook(c, approved):
        if approved.offset == 0x20000:  # after the plan was verified and the app approved
            with open(table, "wb") as fp:
                fp.write(layout.encode_table(layout.STOCK_TABLE))

    chip.faults.before_write_hook = hook
    code, out = flash(chip, kit, work)
    assert code == common.FLASH_FAILED and "the guard refused" in out
    assert [op[1] for op in chip.writes] == [0x20000, 0x15000]  # the table and bootloader were not written


def test_plan_with_wrong_offset_is_refused_before_anything(setup):
    chip, original, kit, work, _ = setup
    import json

    p = os.path.join(kit, "flash-plan.json")
    doc = json.load(open(p))
    doc["images"][2]["offset"] = "0xE000"
    json.dump(doc, open(p, "w"))
    code, out = flash(chip, kit, work)
    assert code == common.STOP and "0xE000" in out and chip.ops == []


def test_missing_acknowledgement_is_asked_for(tmp_path):
    chip = FakeChip(synth.stock_flash())
    original = bytes(chip.flash)
    kit = make_kit(str(tmp_path))
    work = str(tmp_path / "work")
    assert run_cli(["identify", "--work-dir", work], chip)[0] == common.OK
    assert run_cli(["backup", "--work-dir", work], chip, answers=[])[0] == common.OK  # no answer: EOF
    chip.ops.clear()
    code, out = flash(chip, kit, work, ("nope", "nope", "nope"))
    assert code == common.STOP and "not confirmed" in out and chip.ops == []
    ack = hashlib.sha256(original).hexdigest()[-8:]
    code, out = flash(chip, kit, work, (ack, PHRASE))
    assert code == common.OK, out


def _write_offsets_in(text):
    return {int(m, 16) for m in re.findall(r"write_flash\(esp, \[\((0x[0-9A-F]+),", text)} | \
        {int(m, 16) for m in re.findall(r"write-flash [^\n]*? (0x[0-9A-F]+) \S+\n", text + "\n")}


def test_dry_run_writes_nothing_and_prints_only_planned_offsets(setup):
    chip, original, kit, work, _ = setup
    code, out = run_cli(["flash", "--dry-run", "--work-dir", work, "--plan", kit], chip)
    assert code == common.OK, out
    assert chip.ops == []  # the fake was never even connected
    assert bytes(chip.flash) == original
    assert "DRY RUN RESULT: READY" in out
    planned = {0x0, 0x8000, 0x15000, 0x20000}
    assert _write_offsets_in(out) == planned
    assert out.count("write_flash(esp, [(") == 4
    assert "0xE000" not in out and "0xe000" not in out.lower().replace("0x0e000", "")
    for line in out.splitlines():
        if "write_flash" in line or "write-flash" in line:
            assert "flash_mode='keep'" in line or "--flash-mode keep" in line


def test_dry_run_without_backup_reports_not_ready(tmp_path):
    chip = FakeChip(synth.stock_flash())
    kit = make_kit(str(tmp_path))
    code, out = run_cli(["flash", "--dry-run", "--work-dir", str(tmp_path / "work"), "--plan", kit], chip)
    assert code == common.STOP and "NOT READY" in out and chip.ops == []
    assert _write_offsets_in(out) == {0x0, 0x8000, 0x15000, 0x20000}


def test_dry_run_shows_guard_refusal_of_a_changed_file(setup):
    chip, original, kit, work, _ = setup
    with open(os.path.join(kit, "boot_app0.bin"), "r+b") as fp:
        fp.write(b"\x02")
    code, out = run_cli(["flash", "--dry-run", "--work-dir", work, "--plan", kit], chip)
    assert code == common.STOP and "REFUSED" in out and chip.ops == []


def test_committed_dry_run_transcripts():
    examples = os.path.join(os.path.dirname(__file__), "..", "examples")
    text = open(os.path.join(examples, "dry-run-flash.txt")).read()
    assert "DRY RUN RESULT: READY" in text
    assert _write_offsets_in(text) == {0x0, 0x8000, 0x15000, 0x20000}
    assert text.count("write_flash(esp, [(") == 4
    text = open(os.path.join(examples, "dry-run-restore.txt")).read()
    assert "DRY RUN RESULT: READY" in text and _write_offsets_in(text) == {0x0}
