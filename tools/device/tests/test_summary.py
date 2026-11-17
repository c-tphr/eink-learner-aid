"""summary: one text file, safe to post publicly; never bytes of the backup or of identify's blobs.
It reports identify, backup, and (when they ran) each flash image and each restore."""

import binascii
import glob
import os
import re

import pytest

import synth
from fakedevice import FakeChip, Faults
from helpers import identify_and_backup, make_kit, run_cli
from piccoflash import common, summary

SECRET = b"WIFI-SSID=OwnersHomeNetwork;PSK=correct-horse-battery-staple;"


@pytest.fixture
def done(tmp_path):
    flash = synth.stock_flash()
    flash[0xA000:0xA000 + len(SECRET)] = SECRET  # something private in the Onyx NVS area
    chip = FakeChip(flash)
    work = str(tmp_path / "work")
    side, rec = identify_and_backup(chip, work)
    chip.ops.clear()
    code, out = run_cli(["summary", "--work-dir", work])
    assert code == common.OK, out
    files = glob.glob(os.path.join(work, "session-summary-*.txt"))
    assert len(files) == 1
    return chip, work, rec, open(files[0], "rb").read()


def _windows(data, n):
    return {data[i:i + n] for i in range(len(data) - n + 1)}


def assert_no_raw_or_hex_run(text, data, n):
    """No run of 2n-1 or more bytes of `data` appears in `text`, raw or as hex.

    Any such run contains a whole n-byte window of `data` starting at a multiple of n,
    so checking the aligned windows of `data` against every n-byte window of the
    text is exhaustive for runs of that length.
    """
    raw = _windows(text, n)
    hexes = set()
    lower = text.lower()
    for m in re.finditer(rb"[0-9a-f]{%d,}" % (2 * n), lower):
        run = m.group(0)
        for i in range(len(run) - 2 * n + 1):
            hexes.add(binascii.unhexlify(run[i:i + 2 * n]))
    for off in range(0, len(data) - n + 1, n):
        w = data[off:off + n]
        assert w not in raw, "raw bytes from offset 0x%X appear in the summary" % off
        assert w not in hexes, "hex of bytes from offset 0x%X appears in the summary" % off


def test_summary_contains_the_facts(done):
    chip, work, rec, text = done
    s = text.decode("ascii")  # plain ASCII text
    for needle in ("verdict PASS (exit 0)", "ESP32-S3 (QFN56) (revision v0.2)", "24:58:7c:xx:xx:xx",
                   "manufacturer 0x20, device 0x4018, size 16MB, eFuse FLASH_TYPE quad",
                   "SPI_BOOT_CRYPT_CNT 0x0", "Secure Boot False", "partition table at 0x8000: stock",
                   "safeboot   app   factory   offset 0x20000", "DIO / 16MB / 80m",
                   "identify         exit 0 OK", "backup           exit 0 OK",
                   rec["sha256"], "owner confirmed the copy True", "file re-checked, matches",
                   "BACKUP VERIFIED: both reads are identical.", "RESULT: PASS."):
        assert needle in s, needle
    ident = sorted(glob.glob(os.path.join(work, "identify-*.json")))[-1]
    import json

    for blob in json.load(open(ident))["blobs"].values():
        assert blob["sha256"] in s and blob["file"] not in s.replace("xxxxxx", "")


def test_summary_masks_the_mac(done):
    chip, work, rec, text = done
    assert chip.mac.encode() not in text
    assert b"24587c123456" not in text
    assert b"24:58:7c:xx:xx:xx" in text and b"24587cxxxxxx" in text


def test_summary_holds_no_bytes_of_the_backup_or_blobs(done):
    chip, work, rec, text = done
    assert SECRET not in text and binascii.hexlify(SECRET) not in text.lower()
    backup = open(os.path.join(work, rec["file"]), "rb").read()
    assert SECRET in backup  # the private data really is in the backup
    assert_no_raw_or_hex_run(text, backup, 16)  # exhaustive for runs of 31 bytes or more
    blobs = glob.glob(os.path.join(work, "identify-*-*.bin"))
    assert len(blobs) == 3
    for blob in blobs:
        # Runs of 23 bytes or more. Parsed partition names legitimately appear in the summary, but
        # the stock names are at most 8 characters, NUL-padded, so none fills a 12-byte window.
        assert_no_raw_or_hex_run(text, open(blob, "rb").read(), 12)


def test_summary_never_contacts_the_device(done):
    chip, work, rec, text = done
    assert chip.ops == []


def test_summary_reports_stops_mismatches_and_unreached_runs(tmp_path):
    work = str(tmp_path)
    chip = FakeChip(synth.stock_flash(), secure_boot=True)
    assert run_cli(["identify", "--work-dir", work], chip)[0] == common.STOP
    gone = FakeChip(synth.stock_flash(), faults=Faults(unreachable=True))
    assert run_cli(["identify", "--work-dir", work], gone)[0] == common.NOT_REACHED
    good = FakeChip(synth.stock_flash(), faults=Faults(flip_full_read=(2, 0x10)))
    assert run_cli(["identify", "--work-dir", work], good)[0] == common.OK
    assert run_cli(["backup", "--work-dir", work], good)[0] == common.STOP
    code, out = run_cli(["summary", "--work-dir", work])
    assert code == common.OK
    s = open(glob.glob(os.path.join(work, "session-summary-*.txt"))[0]).read()
    assert "exit 1 STOP" in s and "exit 4 NOT_REACHED" in s and "exit 0 OK" in s
    assert "earlier:" in s and "verdict STOP" in s
    assert "MISMATCH" in s and "1 differing 64 KiB blocks: 0x000000" in s
    assert "no backup record" in s


def test_summary_dry_run_writes_nothing(tmp_path):
    work = str(tmp_path)
    chip = FakeChip(synth.stock_flash())
    assert run_cli(["identify", "--work-dir", work], chip)[0] == common.OK
    code, out = run_cli(["summary", "--dry-run", "--work-dir", work])
    assert code == common.OK and "verdict PASS" in out
    assert not glob.glob(os.path.join(work, "session-summary-*"))


def test_summary_without_work_folder(tmp_path):
    code, out = run_cli(["summary", "--work-dir", str(tmp_path / "missing")])
    assert code == common.STOP and "Run identify first:" in out
    assert "    " + common.session_command("identify") in out.splitlines()


@pytest.mark.parametrize("raw,clean", [
    (r"Saved: C:\Users\Jane Doe\Downloads\picco\work\x.json", r"Saved: C:\Users\<user>\Downloads\picco\work\x.json"),
    (r"File: c:\users\jdoe\OneDrive - Home\p\work", r"File: c:\users\<user>\OneDrive - Home\p\work"),
    ("Log: /home/alice/picco/work/logs/a.log", "Log: /home/<user>/picco/work/logs/a.log"),
    ("MAC address:   aa:bb:cc:dd:ee:ff", "MAC address:   aa:bb:cc:xx:xx:xx"),
    ("picco-backup-aabbccddeeff-20261118T1.bin", "picco-backup-aabbccxxxxxx-20261118T1.bin"),
])
def test_sanitize(raw, clean):
    assert summary.sanitize(raw) == clean


def test_sanitize_username_segment(monkeypatch):
    monkeypatch.setenv("USERNAME", "jdoe")
    assert summary.sanitize(r"D:\data\jdoe\picco\work") == r"D:\data\<user>\picco\work"


# --- flash and restore ---------------------------------------------------------------------------

def _session(tmp_path):
    flash = synth.stock_flash()
    flash[0xA000:0xA000 + len(SECRET)] = SECRET
    chip = FakeChip(flash)
    kit = make_kit(str(tmp_path))
    work = str(tmp_path / "work")
    side, rec = identify_and_backup(chip, work)
    return chip, kit, work, os.path.join(work, rec["file"])


def _summary(work):
    for old in glob.glob(os.path.join(work, "session-summary-*.txt")):
        os.remove(old)
    code, out = run_cli(["summary", "--work-dir", work])
    assert code == common.OK, out
    (path,) = glob.glob(os.path.join(work, "session-summary-*.txt"))
    return open(path, "rb").read()


def _section(text, title):
    start = text.index("\n%s" % title)
    return text[start:text.index("\n\n", start + 1)]


def test_summary_without_flash_or_restore_says_so(done):
    chip, work, rec, text = done
    s = text.decode("ascii")
    assert "no flash result: flash never reached its first write" in _section(s, "Flash (")
    assert "no restore was run" in _section(s, "Restore (")
    assert "Every line of this file is safe to post in a public issue" in s


def test_summary_of_a_completed_flash(tmp_path):
    chip, kit, work, backup = _session(tmp_path)
    assert run_cli(["flash", "--work-dir", work, "--plan", kit], chip, answers=[common.FLASH_PHRASE])[0] == common.OK
    s = _summary(work).decode("ascii")
    flash = _section(s, "Flash (")
    rows = [line.split()[1] for line in flash.splitlines() if re.match(r"^    \[\d/4\]", line)]
    assert rows == ["app", "boot_app0", "partition_table", "bootloader"]  # the order written
    assert flash.count("written and verified (read back SHA-256 ") == 4
    assert "result OK, firmware 0.1.0-test" in flash
    assert "The tool said: DONE: all four images written and verified." in flash
    assert "flash            exit 0 OK" in s
    assert "Last 40 lines of the latest flash log" in s


def test_summary_of_a_flash_stopped_part_way_and_two_restores(tmp_path):
    chip, kit, work, backup = _session(tmp_path)
    chip.faults.fail_write_at = (0x8000, 100)
    code, out = run_cli(["flash", "--work-dir", work, "--plan", kit], chip, answers=[common.FLASH_PHRASE])
    assert code == common.FLASH_FAILED
    chip.faults.fail_write_at = None
    chip.faults.disconnect_on_write = True
    assert run_cli(["restore", "--work-dir", work, backup], chip,
                   answers=[common.RESTORE_PHRASE])[0] == common.RESTORE_FAILED
    chip.faults.disconnect_on_write = False
    assert run_cli(["restore", "--work-dir", work, backup], chip, answers=["no"])[0] == common.STOP
    assert run_cli(["restore", "--dry-run", "--work-dir", work, backup], chip)[0] == common.OK
    assert run_cli(["restore", "--work-dir", work, backup], chip, answers=[common.RESTORE_PHRASE])[0] == common.OK
    text = _summary(work)
    s = text.decode("ascii")
    flash = _section(s, "Flash (")
    assert "result FAILED" in flash
    assert re.search(r"\[1/4\] app .* written and verified", flash)
    assert re.search(r"\[2/4\] boot_app0 .* written and verified", flash)
    assert re.search(r"\[3/4\] partition_table .* STOPPED HERE: not verified; the write may be incomplete", flash)
    assert re.search(r"\[4/4\] bootloader .* not written", flash)
    assert "Reason: error: Serial data stream stopped" in flash
    assert "The tool said: FLASHING DID NOT COMPLETE, and showed the restore command (exit 5)." in flash
    restore = _section(s, "Restore (").splitlines()
    outcomes = [line.strip() for line in restore if line.startswith("    ")]
    assert len(outcomes) == 3  # the dry run is not a restore
    assert outcomes[0].startswith("did NOT complete (the write did not complete: The device disconnected")
    assert outcomes[0].endswith("exit 6 RESTORE_FAILED")
    assert outcomes[1] == "nothing was written (refused, cancelled or the chip not reached); exit 1 STOP"
    assert outcomes[2] == ("the whole flash was written, read back and verified: it equals the backup; "
                           "exit 0 OK")
    assert "backup picco-backup-24587cxxxxxx-" in _section(s, "Restore (")
    # what the tool told the owner, from the end of the flash log: the restore command
    tail = s[s.index("Last 40 lines of the latest flash log"):]
    assert "FLASHING DID NOT COMPLETE" in tail and "session.ps1 restore" in tail
    # still private: MAC masked, no backup bytes
    assert chip.mac.encode() not in text and b"24587c123456" not in text
    assert SECRET not in text
    assert_no_raw_or_hex_run(text, open(backup, "rb").read(), 16)


def test_summary_of_a_flash_the_guard_stopped_at_the_second_image(tmp_path):
    chip, kit, work, backup = _session(tmp_path)

    def change_file(chip_, approved):
        if approved.offset == 0x20000:
            with open(os.path.join(kit, "boot_app0.bin"), "r+b") as fp:
                fp.write(b"\x00")

    chip.faults.before_write_hook = change_file
    assert run_cli(["flash", "--work-dir", work, "--plan", kit], chip,
                   answers=[common.FLASH_PHRASE])[0] == common.FLASH_FAILED
    flash = _section(_summary(work).decode("ascii"), "Flash (")
    assert re.search(r"\[1/4\] app .* written and verified", flash)
    assert re.search(r"\[2/4\] boot_app0 .* STOPPED HERE", flash)
    assert "Reason: the guard refused" in flash
