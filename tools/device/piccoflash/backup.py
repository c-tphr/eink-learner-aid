"""backup: read the whole 16 MB twice, keep it only if both reads are identical. Read-only."""

from __future__ import annotations

import datetime
import os
from typing import List, Optional, Tuple

from . import PINNED_ESPTOOL_VERSION, __version__, esptool_calls, guard, identify, layout, workdir
from .common import NOT_REACHED, OK, STOP, Context, PortProblem, not_reached_help
from .device import DeviceError, DeviceNotReached
from .ui import UI

FULL = layout.FLASH_SIZE
BLOCK = 0x10000  # mismatch reports are per 64 KiB block


def differing_blocks(a: bytes, b: bytes, block: int = BLOCK) -> List[int]:
    return [off for off in range(0, max(len(a), len(b)), block) if a[off:off + block] != b[off:off + block]]


def base_name(work: str, mac: str, stamp: str) -> str:
    return os.path.join(work, "%s%s-%s" % (workdir.BACKUP_PREFIX, workdir.mac_tag(mac), stamp))


def run(ctx: Context) -> int:
    ui = ctx.ui
    ui.heading("Backup (read-only): two full reads of the 16 MB flash")
    work = workdir.ensure(ctx.work)
    if not any(_verdict(p) == identify.PASS for p in workdir.identify_records(work)):
        ui.say("REFUSED: there is no passing 'identify' in the work folder %s." % work)
        ui.say("Run identify first:")
        ui.say("    %s" % ctx.command_hint("identify"))
        return STOP

    if ctx.dry_run:
        return _dry_run(ctx)

    if workdir.free_bytes(work) < 2 * FULL + (8 << 20):
        ui.say("REFUSED: the work folder needs at least 40 MB of free disk space.")
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
        return _backup(ctx, dev, work)
    except DeviceError as exc:
        ui.say("")
        ui.say("The connection to the chip failed during the backup: %s" % exc)
        ui.say("Nothing was written to the Picco. No backup was marked as verified.")
        ui.say("Check the cable and run the backup again:")
        ui.say("    %s" % ctx.command_hint("backup"))
        return NOT_REACHED
    finally:
        dev.close()


def _backup(ctx: Context, dev, work: str) -> int:
    ui = ctx.ui
    res = identify.read_and_evaluate(dev, ui)
    mac = res.chip.mac
    found = workdir.latest_identify(work, mac)
    if found is None or found[1].get("verdict") != identify.PASS:
        ui.say("REFUSED: the connected Picco (MAC %s) has no passing 'identify' in %s." % (mac, work))
        ui.say("Run identify first, with this Picco connected:")
        ui.say("    %s" % ctx.command_hint("identify"))
        _reset(dev, ui)
        return STOP
    if res.verdict != identify.PASS:
        identify.print_summary(res, ui)
        ui.say("REFUSED: the device no longer passes identify (see above). Nothing was read or written.")
        _reset(dev, ui)
        return STOP
    ui.say("Device %s still passes identify (record %s)." % (mac, os.path.basename(found[0])))

    stamp = workdir.timestamp()
    base = base_name(work, mac, stamp)
    reads: List[Tuple[str, bytes, str]] = []
    for n in (1, 2):
        path = "%s.read%d.bin" % (base, n)
        call = esptool_calls.read(dev.port, 0, FULL, os.path.basename(path))
        ui.say("")
        ui.say("Read %d of 2: all 16 MB (this may take several minutes)..." % n)
        ui.log_only("esptool: %s   [CLI: %s]" % (call.api(), call.cli))
        data = dev.read(0, FULL, _progress(ui))
        if len(data) != FULL:
            raise DeviceError("read returned %d bytes instead of %d" % (len(data), FULL))
        workdir.write_bytes(path, data)
        size, disk_digest = workdir.file_sha256(path)
        digest = guard.sha256(data)
        if size != FULL or disk_digest != digest:
            ui.say("REFUSED: %s did not save correctly to disk (size %d, hash differs). Check free space and the disk."
                   % (path, size))
            _reset(dev, ui)
            return STOP
        ui.say("Read %d SHA-256: %s" % (n, digest))
        reads.append((path, data, digest))
    _reset(dev, ui)

    (p1, d1, h1), (p2, d2, h2) = reads
    consistent = d1[0:0x8000] == res.boot_blob and d1[0x8000:0x8000 + layout.TABLE_MAX_LEN] == res.table_blob
    if h1 != h2 or not consistent:
        blocks = differing_blocks(d1, d2)
        report = {
            "format": "picco-backup-mismatch", "version": 1, "time_utc": stamp, "mac": mac,
            "read1": {"file": os.path.basename(p1), "sha256": h1},
            "read2": {"file": os.path.basename(p2), "sha256": h2},
            "differing_64k_blocks": ["0x%06X" % b for b in blocks],
            "matches_identify_reads": consistent,
        }
        workdir.write_json_atomic(base + "-MISMATCH.json", report)
        ui.say("")
        ui.say("STOP: THE TWO READS DIFFER. No backup was marked as verified. Do not flash.")
        if blocks:
            ui.say("Differing 64 KiB blocks (%d): %s" % (len(blocks), ", ".join("0x%06X" % b for b in blocks[:32])
                                                         + (" ..." if len(blocks) > 32 else "")))
        if not consistent:
            ui.say("The full read also differs from the bootloader/table read moments earlier by identify.")
        ui.say("Both reads were kept for diagnosis: %s and %s" % (p1, p2))
        ui.say("Try again with another USB cable or another USB port on the PC:")
        ui.say("    %s" % ctx.command_hint("backup"))
        return STOP

    final = base + ".bin"
    os.replace(p1, final)
    os.remove(p2)
    size, final_digest = workdir.file_sha256(final)
    if size != FULL or final_digest != h1:
        ui.say("REFUSED: the kept backup file no longer hashes correctly. Run the backup again:")
        ui.say("    %s" % ctx.command_hint("backup"))
        return STOP
    sidecar = base + ".json"
    rec = {
        "format": "picco-backup",
        "version": 1,
        "file": os.path.basename(final),
        "size": FULL,
        "sha256": h1,
        "mac": mac,
        "chip": res.chip.as_dict(),
        "security": res.security.as_dict(),
        "partition_table": res.evaluation.table_kind,
        "identify_record": os.path.basename(found[0]),
        "reads": [{"sha256": h1}, {"sha256": h2}],
        "verified": True,
        "created_utc": stamp,
        "tool": {"piccoflash": __version__, "esptool": PINNED_ESPTOOL_VERSION},
        "owner_ack": None,
    }
    workdir.write_json_atomic(sidecar, rec)
    ui.say("")
    ui.say("BACKUP VERIFIED: both reads are identical.")
    ui.say("File:    %s" % os.path.abspath(final))
    ui.say("Record:  %s" % os.path.abspath(sidecar))
    ask_ack(ui, sidecar, rec)
    return OK


def ask_ack(ui: UI, sidecar_path: str, rec: dict) -> bool:
    """Ask the owner to copy the backup elsewhere and type the hash's last 8 characters."""
    sha = rec["sha256"]
    ui.say("")
    ui.say("This file is the ONLY way to put the Picco back to its original Onyx firmware.")
    ui.say("Copy it now, together with this record, to a second place you control (a USB stick or another")
    ui.say("disk). Never upload it or attach it anywhere: it holds the Picco's own data and Onyx's firmware.")
    ui.say("    %s" % os.path.abspath(os.path.join(os.path.dirname(sidecar_path), rec["file"])))
    ui.say("    %s" % os.path.abspath(sidecar_path))
    ui.say("Its SHA-256 is:")
    ui.say("    %s" % " ".join(sha[i:i + 8] for i in range(0, 64, 8)))
    for attempt in range(3):
        answer = ui.ask("When the copy is done, type the LAST 8 characters of the SHA-256 above: ")
        if answer is None:
            break
        if answer.strip().lower() == sha[-8:]:
            rec["owner_ack"] = {"typed": answer.strip().lower(),
                                "time_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")}
            workdir.write_json_atomic(sidecar_path, rec)
            ui.say("Thank you. The backup is confirmed.")
            return True
        ui.say("That does not match the last 8 characters (%d of 3 tries)." % (attempt + 1))
    ui.say("The backup is verified but not yet confirmed. 'flash' will ask for this confirmation again.")
    return False


def _dry_run(ctx: Context) -> int:
    ui = ctx.ui
    port = _dry_port(ctx)
    ui.say("DRY RUN: the device is not contacted. A real run would:")
    for call in esptool_calls.connect(port):
        ui.say("  connect:  %s" % call.api())
    ui.say("  re-check identify (chip, eFuses, reads of 0x0+0x8000, 0x8000+0xC00, 0x15000+0x2000)")
    for n in (1, 2):
        call = esptool_calls.read(port, 0, FULL, "picco-backup-<mac>-<time>.read%d.bin" % n)
        ui.say("  read %d:   %s" % (n, call.api()))
        ui.say("            CLI: %s" % call.cli)
    ui.say("  compare the two SHA-256 hashes; keep one file only if they are equal")
    ui.say("  %s" % esptool_calls.hard_reset(port).api())
    ui.say("Nothing is ever written to the Picco by backup.")
    return OK


def _dry_port(ctx: Context) -> str:
    try:
        return ctx.port_resolver(ctx.port, UI(out=_Null()))
    except PortProblem:
        return ctx.port or "<port>"


class _Null:
    def write(self, s):
        pass

    def flush(self):
        pass


def _verdict(path: str) -> Optional[str]:
    try:
        return workdir.read_json(path).get("verdict")
    except (OSError, ValueError):
        return None


def _reset(dev, ui: UI) -> None:
    ui.log_only("esptool: %s" % esptool_calls.hard_reset(dev.port).api())
    try:
        dev.hard_reset()
    except DeviceError as exc:
        ui.say("(The device could not be reset automatically: %s. It is unchanged; restart it by hand.)" % exc)


def _progress(ui: UI):
    state = {"last": -1}

    def cb(done: int, total: int) -> None:
        pct = int(100 * done / total) if total else 100
        step = pct // 10
        if step != state["last"]:
            state["last"] = step
            ui.say("  %3d%%" % (step * 10))

    return cb


def find_backup_for_flash(work: str) -> Optional[Tuple[str, dict]]:
    """The newest verified backup in the work folder, any MAC."""
    best = None
    for path in workdir.backup_sidecars(work):
        try:
            rec = workdir.read_json(path)
        except (OSError, ValueError):
            continue
        if rec.get("verified") is True and (best is None or str(rec.get("created_utc")) >= str(best[1].get("created_utc"))):
            best = (path, rec)
    return best
