"""flash: the one destructive command. Writes the four planned images, verifies each.

Order of events (README, "Write order and failure analysis"):

1. Offline: the plan verifies and every file passes the guard; this work
   folder holds a passing identify and a verified backup for one MAC whose
   file is present and still hashes to the recorded value.
2. The owner has confirmed the backup copy (last 8 hex digits of its hash).
3. The plan and the list of what is destroyed are shown; the owner types the
   exact phrase.
4. Connect and re-identify: same MAC as the backup, still the stock table,
   and the bootloader and table bytes on the chip equal those in the backup.
5. Write app, then boot_app0, then partition table, then bootloader. Before
   each write the file is read again and the guard approves those exact
   bytes; the device re-checks them right before esptool. After each write
   the region is read back and its SHA-256 compared. Any failure stops at
   once: later regions are not written, and the restore command is printed.
6. Hard reset only after all four regions verified.
"""

from __future__ import annotations

import os
from typing import List, Optional, Tuple

from . import backup, esptool_calls, guard, identify, layout, plan as planmod, workdir
from .common import (FLASH_FAILED, FLASH_PHRASE, NOT_REACHED, OK, STOP, Context, PortProblem, not_reached_help,
                     restore_instructions)
from .device import DeviceError, DeviceNotReached
from .ui import UI


def destroyed_lines(plan: planmod.Plan) -> List[str]:
    app = plan.image("app")
    app_end = guard.touched_span(app.offset, app.size)[1]
    lines = [
        "Onyx's second-stage bootloader            0x000000-0x007FFF",
        "the stock partition table                 0x008000-0x008FFF",
        "the stock otadata (which Onyx app boots)  0x015000-0x016FFF",
        "the vendor recovery app 'safeboot'        0x020000-0x0DFFFF",
    ]
    if app_end > 0xE0000:
        lines.append("the start of the Onyx main app            0x0E0000-0x%06X" % (app_end - 1))
    lines.append("(the rest of the Onyx app and its spiffs data stay as unused bytes; NVS 0x9000-0x14FFF is kept)")
    return lines


def _preconditions(ctx: Context, plan: planmod.Plan) -> Tuple[List[str], List[str], Optional[tuple]]:
    """(problems, ok_lines, (sidecar_path, record, backup_path)). Offline only."""
    work = ctx.work
    problems: List[str] = []
    ok: List[str] = []
    found = backup.find_backup_for_flash(work) if os.path.isdir(work) else None
    if found is None:
        problems.append("no verified backup in the work folder %s. Run identify, then backup:\n    %s\n    %s"
                        % (work, ctx.command_hint("identify"), ctx.command_hint("backup")))
        return problems, ok, None
    side_path, rec = found
    mac = rec.get("mac")
    idf = workdir.latest_identify(work, mac)
    if idf is None or idf[1].get("verdict") != identify.PASS:
        problems.append("no passing identify for MAC %s in %s. Run identify with this Picco connected:\n    %s"
                        % (mac, work, ctx.command_hint("identify")))
    else:
        ok.append("identify passed for MAC %s (%s)" % (mac, os.path.basename(idf[0])))
    bpath = os.path.join(os.path.dirname(side_path), rec.get("file", ""))
    if not os.path.isfile(bpath):
        problems.append("the backup file %s is missing" % bpath)
        return problems, ok, (side_path, rec, bpath)
    size, digest = workdir.file_sha256(bpath)
    errs = guard.check_restore(size, digest, rec.get("size"), rec.get("sha256"))
    if errs:
        problems += ["backup %s: %s" % (os.path.basename(bpath), e) for e in errs]
    else:
        ok.append("verified backup %s, SHA-256 re-checked: %s" % (os.path.basename(bpath), digest))
    if rec.get("partition_table") != "stock":
        problems.append("the backup was not taken from a stock table")
    return problems, ok, (side_path, rec, bpath)


def run(ctx: Context) -> int:
    ui = ctx.ui
    ui.heading("Flash: replace Onyx with the two-slot firmware")
    try:
        plan = planmod.load_and_verify(ctx.plan_path)
    except planmod.PlanError as exc:
        ui.say("REFUSED: the flash plan does not pass the guard. Nothing was written.")
        for r in exc.reasons:
            ui.say("  - %s" % r)
        return STOP
    for line in planmod.format_plan_header(plan) + [""] + planmod.format_table(plan):
        ui.say(line)
    ui.say("Guard: the plan and all four files pass (%s)." % ", ".join(guard.ROLES))

    problems, ok_lines, bk = _preconditions(ctx, plan)
    ui.heading("Preconditions in the work folder %s" % os.path.abspath(ctx.work))
    for line in ok_lines:
        ui.say("[ok]   %s" % line)
    for line in problems:
        ui.say("[STOP] %s" % line)
    if bk and not problems:
        if bk[1].get("owner_ack"):
            ui.say("[ok]   owner confirmed the backup copy")
        else:
            ui.say("[ask]  owner has not yet confirmed the backup copy; a real run asks for it first")

    if ctx.dry_run:
        return _dry_run(ctx, plan, problems)
    if problems:
        ui.say("REFUSED: the preconditions above are not met. Nothing was written.")
        return STOP
    side_path, rec, bpath = bk
    if not rec.get("owner_ack"):
        if not backup.ask_ack(ui, side_path, rec):
            ui.say("REFUSED: the backup copy was not confirmed. Nothing was written.")
            return STOP

    ui.heading("What will be destroyed")
    for line in destroyed_lines(plan):
        ui.say("  " + line)
    ui.say("Only your backup (%s) can bring these back." % os.path.basename(bpath))
    if not ui.confirm_phrase(FLASH_PHRASE, "replace Onyx on this Picco"):
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
        return _flash_connected(ctx, dev, plan, rec, bpath)
    finally:
        dev.close()


def _flash_connected(ctx: Context, dev, plan: planmod.Plan, rec: dict, bpath: str) -> int:
    ui = ctx.ui
    with open(bpath, "rb") as fp:
        backup_bytes = fp.read()
    if guard.sha256(backup_bytes) != rec["sha256"]:
        ui.say("REFUSED: the backup file changed. Nothing was written.")
        return STOP
    ui.heading("Re-identifying the connected Picco (read-only)")
    try:
        res = identify.read_and_evaluate(dev, ui)
    except DeviceError as exc:
        ui.say("The connection failed while re-identifying: %s. Nothing was written." % exc)
        return NOT_REACHED
    problems = []
    if res.chip.mac != rec["mac"]:
        problems.append("the connected Picco has MAC %s, but the backup is of MAC %s: a different device" % (
            res.chip.mac, rec["mac"]))
    if res.verdict == identify.ALREADY_CONVERTED:
        problems.append("this Picco already has the two-slot table; install updates from the SD card instead "
                        "(Settings > System > SD Card Firmware Update)")
    elif res.verdict != identify.PASS:
        problems += ["%s: %s" % (c.name, c.detail) for c in res.evaluation.stops]
    if res.boot_blob is not None and res.boot_blob != backup_bytes[0:0x8000]:
        problems.append("the bootloader on the chip differs from the one in the backup")
    if res.table_blob is not None and res.table_blob != backup_bytes[0x8000:0x8000 + layout.TABLE_MAX_LEN]:
        problems.append("the partition table on the chip differs from the one in the backup")
    if problems:
        ui.say("REFUSED: the device does not match the checks made before. Nothing was written.")
        for p in problems:
            ui.say("  - %s" % p)
        _reset_quietly(dev, ui)
        return STOP
    ui.say("Same device (MAC %s), still stock, and its bootloader and table equal the backup." % res.chip.mac)
    return write_all(ctx, dev, plan, bpath, rec["sha256"])


def write_all(ctx: Context, dev, plan: planmod.Plan, bpath: Optional[str], bsha: Optional[str]) -> int:
    ui = ctx.ui
    stamp = workdir.timestamp()
    steps = []
    started = False
    result_path = os.path.join(ctx.work, "flash-result-%s.json" % stamp)
    ui.heading("Writing (do not unplug the Picco until this says DONE)")
    try:
        for n, role in enumerate(guard.FLASH_ORDER, 1):
            img = plan.image(role)
            step = {"role": role, "offset": "0x%X" % img.offset, "size": img.size, "status": "started"}
            steps.append(step)
            data = planmod.read_image_bytes(plan, img)  # read the file again, now
            approved = guard.approve_flash_write(img, data)  # GuardError if anything changed
            call = esptool_calls.write(dev.port, img.offset, approved.data, img.file_name)
            span_start, span_end = guard.touched_span(img.offset, img.size)
            ui.say("[%d/4] %s: writing %d bytes at 0x%X (erases 0x%X-0x%X)..." % (
                n, role, img.size, img.offset, span_start, span_end - 1))
            ui.log_only("esptool: %s   [CLI: %s]" % (call.api(), call.cli))
            started = True
            dev.write(approved)
            rcall = esptool_calls.read(dev.port, img.offset, span_end - img.offset, "readback.bin")
            ui.log_only("esptool: %s" % rcall.api())
            back = dev.read(img.offset, span_end - img.offset)
            got = guard.sha256(back[:img.size])
            step["readback_sha256"] = got
            if got != approved.sha256:
                step["status"] = "verify-failed"
                raise _VerifyFailed("%s at 0x%X: read back SHA-256 %s, expected %s" % (
                    role, img.offset, got[:16], approved.sha256[:16]))
            if back[img.size:].strip(b"\xff"):
                ui.say("      note: bytes after the image in its last sector are not erased (harmless; logged)")
                step["tail_not_erased"] = True
            step["status"] = "verified"
            ui.say("      verified: SHA-256 of the bytes read back equals the plan (%s...)" % got[:16])
    except guard.GuardError as exc:
        return _failed(ctx, steps, result_path, started, bpath, bsha,
                       "the guard refused: " + "; ".join(exc.reasons))
    except _VerifyFailed as exc:
        return _failed(ctx, steps, result_path, True, bpath, bsha, "verification failed: %s" % exc)
    except (DeviceError, OSError) as exc:
        return _failed(ctx, steps, result_path, started, bpath, bsha, "error: %s" % exc)
    except KeyboardInterrupt:
        return _failed(ctx, steps, result_path, started, bpath, bsha, "interrupted (Ctrl+C)")
    except Exception as exc:  # a bug must not hide that writes may have started
        return _failed(ctx, steps, result_path, started, bpath, bsha,
                       "unexpected error: %s: %s" % (type(exc).__name__, exc))

    workdir.write_json_atomic(result_path, {"format": "picco-flash-result", "version": 1, "time_utc": stamp,
                                            "result": "OK", "steps": steps, "plan": plan.path,
                                            "firmware_version": plan.doc["firmware_version"]})
    ui.log_only("esptool: %s" % esptool_calls.hard_reset(dev.port).api())
    try:
        dev.hard_reset()
        reset_note = "The Picco has been reset and should now start the new firmware."
    except DeviceError as exc:
        reset_note = "All four regions are written and verified, but the automatic reset failed (%s). " \
                     "Restart the Picco with its power button." % exc
    ui.say("")
    ui.say("DONE: all four images written and verified.")
    ui.say(reset_note)
    ui.say("Next: record the first start of the new firmware in the serial log. Type:")
    ui.say("    %s" % ctx.command_hint("monitor", "-Reset"))
    return OK


class _VerifyFailed(Exception):
    pass


def _failed(ctx: Context, steps, result_path: str, started: bool, bpath, bsha, why: str) -> int:
    ui = ctx.ui
    ui.say("")
    ui.say("STOPPED: %s" % why)
    workdir.write_json_atomic(result_path, {"format": "picco-flash-result", "version": 1,
                                            "result": "FAILED" if started else "REFUSED",
                                            "reason": why, "steps": steps})
    if not started:
        ui.say("Nothing was written to the Picco.")
        return STOP
    ui.say("Later regions were NOT written. Steps: %s" % ", ".join("%s=%s" % (s["role"], s["status"]) for s in steps))
    restore_instructions(ui, ctx, bpath, bsha)
    return FLASH_FAILED


def _reset_quietly(dev, ui: UI) -> None:
    try:
        dev.hard_reset()
    except DeviceError as exc:
        ui.log_only("reset failed: %s" % exc)


def _dry_run(ctx: Context, plan: planmod.Plan, problems: List[str]) -> int:
    ui = ctx.ui
    port = backup._dry_port(ctx)
    ui.heading("DRY RUN: the device is not contacted and nothing is written")
    ui.say("What a real run would destroy:")
    for line in destroyed_lines(plan):
        ui.say("  " + line)
    ui.say("A real run would then:")
    ui.say("  1. ask you to type exactly: %s" % FLASH_PHRASE)
    ui.say("  2. connect:")
    for call in esptool_calls.connect(port):
        ui.say("       %s" % call.api())
    ui.say("  3. re-identify read-only: same MAC as the backup, still the stock table, bootloader and table")
    ui.say("     equal to the backup (reads 0x0+0x8000, 0x8000+0xC00, 0x15000+0x2000)")
    ui.say("  4. write and verify, in this order, stopping at the first failure:")
    refused = []
    for n, role in enumerate(guard.FLASH_ORDER, 1):
        img = plan.image(role)
        data = planmod.read_image_bytes(plan, img)
        try:
            approved = guard.approve_flash_write(img, data)
            verdict = "APPROVED (%d bytes, sha256 %s...)" % (approved.length, approved.sha256[:16])
        except guard.GuardError as exc:
            verdict = "REFUSED: " + "; ".join(exc.reasons)
            refused.append(role)
        start, end = guard.touched_span(img.offset, img.size)
        call = esptool_calls.write(port, img.offset, data, img.file_name)
        rcall = esptool_calls.read(port, img.offset, end - img.offset, "readback.bin")
        ui.say("     [%d/4] %s -> 0x%X (erases 0x%X-0x%X); guard: %s" % (n, role, img.offset, start, end - 1, verdict))
        ui.say("           %s" % call.api())
        ui.say("           CLI: %s" % call.cli)
        ui.say("           then %s and compare SHA-256" % rcall.api())
    ui.say("  5. %s" % esptool_calls.hard_reset(port).api())
    ui.say("")
    if problems or refused:
        ui.say("DRY RUN RESULT: NOT READY. A real run would stop before writing anything:")
        for p in problems:
            ui.say("  - %s" % p)
        for r in refused:
            ui.say("  - guard refuses %s" % r)
        return STOP
    ui.say("DRY RUN RESULT: READY. A real run would ask for the phrase and then write the 4 images above.")
    return OK
