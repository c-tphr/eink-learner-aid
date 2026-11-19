"""restore <backup file>: write a verified 16 MB backup at 0x0 and verify it by reading it all back.

Does not need a passing identify: after a failed flash the table may be
anything. It needs the backup's record (the .json beside the .bin), a file
whose size and SHA-256 equal the record, an ESP32-S3 with 16 MB flash and no
flash encryption or secure download mode, the same MAC as the backup (or the
owner typing an override phrase), and the owner typing the restore phrase.
"""

from __future__ import annotations

import os

from . import esptool_calls, guard, layout, workdir
from .common import (NOT_REACHED, OK, OTHER_DEVICE_PHRASE, RESTORE_FAILED, RESTORE_PHRASE, STOP, Context,
                     PortProblem, not_reached_help)
from .device import DeviceError, DeviceNotReached


def run(ctx: Context, backup_file: str) -> int:
    ui = ctx.ui
    ui.heading("Restore: write a full backup back to the Picco")
    side = workdir.sidecar_for(backup_file)
    problems = []
    rec = None
    if not os.path.isfile(backup_file):
        problems.append("no backup file at %s" % backup_file)
    if not os.path.isfile(side):
        problems.append("no backup record at %s (the .json written next to the backup)" % side)
    else:
        try:
            rec = workdir.read_json(side)
        except (OSError, ValueError) as exc:
            problems.append("cannot read the backup record %s: %s" % (side, exc))
    if rec is not None:
        if rec.get("format") != "picco-backup":
            problems.append("%s is not a piccoflash backup record" % side)
        if rec.get("verified") is not True:
            problems.append("the backup record does not say the backup was verified by two identical reads")
        if os.path.basename(backup_file) != rec.get("file"):
            problems.append("the record is for %r, not %r" % (rec.get("file"), os.path.basename(backup_file)))
    data = b""
    approved = None
    if not problems:
        with open(backup_file, "rb") as fp:
            data = fp.read()
        try:
            approved = guard.approve_restore_write(data, rec.get("size"), rec.get("sha256"))
        except guard.GuardError as exc:
            problems += exc.reasons
    if problems:
        ui.say("REFUSED. Nothing was written.")
        for p in problems:
            ui.say("  - %s" % p)
        return STOP

    ui.say("Backup file: %s" % os.path.abspath(backup_file))
    ui.say("Taken from:  MAC %s at %s" % (rec.get("mac"), rec.get("created_utc")))
    ui.say("SHA-256:     %s (matches the record)" % approved.sha256)
    table = layout.parse_table(data[0x8000:0x8000 + layout.TABLE_MAX_LEN])
    ui.say("Its partition table is: %s" % layout.classify_table(table.rows))
    ui.say("Guard: one image of exactly 16,777,216 bytes at 0x0. APPROVED.")

    port_for_text = ctx.port or "<port>"
    if ctx.dry_run:
        try:
            port_for_text = ctx.port_resolver(ctx.port, _quiet(ui))
        except PortProblem:
            pass
        ui.heading("DRY RUN: the device is not contacted and nothing is written")
        ui.say("A real run would:")
        ui.say("  1. ask you to type exactly: %s" % RESTORE_PHRASE)
        ui.say("  2. connect:")
        for call in esptool_calls.connect(port_for_text):
            ui.say("       %s" % call.api())
        ui.say("  3. check: ESP32-S3, 16 MB flash, no flash encryption, no secure download mode,")
        ui.say("     MAC %s (a different MAC needs the phrase: %s)" % (rec.get("mac"), OTHER_DEVICE_PHRASE))
        call = esptool_calls.write(port_for_text, 0, data, os.path.basename(backup_file))
        ui.say("  4. write: %s" % call.api())
        ui.say("     CLI:   %s" % call.cli)
        ui.say("  5. read back: %s and compare SHA-256" % esptool_calls.read(port_for_text, 0, len(data)).api())
        ui.say("  6. %s" % esptool_calls.hard_reset(port_for_text).api())
        ui.say("")
        ui.say("DRY RUN RESULT: READY. The backup passes the guard.")
        return OK

    if not ui.confirm_phrase(RESTORE_PHRASE, "overwrite the whole Picco flash with this backup"):
        return STOP
    try:
        port = ctx.port_resolver(ctx.port, ui)
    except PortProblem as exc:
        ui.say(str(exc))
        return NOT_REACHED
    dev = ctx.device_factory(port, ui)
    try:
        dev.connect()
    except DeviceNotReached as exc:
        not_reached_help(ui, str(exc))
        return NOT_REACHED
    try:
        return _restore_connected(ctx, dev, approved, rec, backup_file)
    finally:
        dev.close()


def _restore_connected(ctx: Context, dev, approved, rec: dict, backup_file: str) -> int:
    ui = ctx.ui
    try:
        chip = dev.chip_info()
        sec = dev.security_info()
    except DeviceError as exc:
        ui.say("The connection failed while checking the chip: %s. Nothing was written." % exc)
        return NOT_REACHED
    problems = []
    if chip.chip_name != "ESP32-S3":
        problems.append("the chip is %s, not an ESP32-S3" % chip.chip_name)
    if sec.secure_download_mode:
        problems.append("secure download mode is on; flash cannot be written and verified")
    if chip.flash_size != "16MB":
        problems.append("the flash chip reports %s, not 16MB" % chip.flash_size)
    if sec.flash_encryption_enabled or sec.efuse_flash_encryption_enabled:
        problems.append("flash encryption is enabled; writing a plain backup would not restore the device")
    if problems:
        ui.say("REFUSED. Nothing was written.")
        for p in problems:
            ui.say("  - %s" % p)
        return STOP
    if chip.mac != rec.get("mac"):
        ui.say("")
        ui.say("WARNING: this backup was taken from MAC %s, but the connected chip is MAC %s." % (
            rec.get("mac"), chip.mac))
        ui.say("Writing another device's backup copies that device's data and calibration onto this one.")
        if not ui.confirm_phrase(OTHER_DEVICE_PHRASE, "restore a backup from a DIFFERENT device"):
            return STOP

    call = esptool_calls.write(dev.port, 0, approved.data, os.path.basename(backup_file))
    ui.say("Writing 16 MB (this takes several minutes; do not unplug)...")
    ui.log_only("esptool: %s   [CLI: %s]" % (call.api(), call.cli))
    try:
        dev.write(approved)
    except guard.GuardError as exc:
        ui.say("REFUSED by the guard: %s. Nothing was written." % "; ".join(exc.reasons))
        return STOP
    except (DeviceError, KeyboardInterrupt, Exception) as exc:
        return _failed(ctx, backup_file, "the write did not complete: %s" % (str(exc) or "interrupted"))
    ui.say("Reading all 16 MB back to verify...")
    rcall = esptool_calls.read(dev.port, 0, len(approved.data))
    ui.log_only("esptool: %s" % rcall.api())
    try:
        back = dev.read(0, len(approved.data))
    except (DeviceError, KeyboardInterrupt) as exc:
        return _failed(ctx, backup_file, "the read-back did not complete: %s" % (str(exc) or "interrupted"))
    got = guard.sha256(back)
    if got != approved.sha256:
        return _failed(ctx, backup_file, "the flash read back has SHA-256 %s, expected %s" % (got, approved.sha256))
    ui.say("VERIFIED: the whole flash now equals the backup (SHA-256 %s)." % got)
    ui.log_only("esptool: %s" % esptool_calls.hard_reset(dev.port).api())
    try:
        dev.hard_reset()
        ui.say("The Picco has been reset and should start as before.")
    except DeviceError as exc:
        ui.say("The automatic reset failed (%s). Restart the Picco with its power button." % exc)
    ui.say("DONE.")
    return OK


def _failed(ctx: Context, backup_file: str, why: str) -> int:
    ui = ctx.ui
    ui.say("")
    ui.say("RESTORE DID NOT COMPLETE: %s" % why)
    ui.say("Your backup file is unchanged. Do not unplug the Picco.")
    ui.say("Run the restore again; if it fails again, try another USB cable or port:")
    ui.say("    %s" % ctx.command_hint("restore", os.path.abspath(backup_file)))
    return RESTORE_FAILED


def _quiet(ui):
    from .ui import UI

    class _N:
        def write(self, s):
            pass

        def flush(self):
            pass

    return UI(out=_N())
