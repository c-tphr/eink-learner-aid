"""summary: one text file, safe to post in a public issue: what identify, backup, flash and restore did.

Read-only: it reads the work folder (records and logs) and never contacts the
device. It contains parsed fields, SHA-256 hashes and the tool's own messages,
never bytes of the backup or of the blobs identify saved. The MAC address has
its last three bytes masked, and user names in file paths are replaced by <user>.
It works with whatever the work folder holds, for example only identify and backup.
"""

from __future__ import annotations

import datetime
import glob
import os
import platform
import re
from typing import List, Optional

from . import PINNED_ESPTOOL_VERSION, __version__, guard, identify, workdir
from .common import EXIT_NAMES, FLASH_FAILED, NOT_REACHED, OK, RESTORE_FAILED, STOP, Context

TAIL_LINES = 40
FILE_PREFIX = "session-summary-"  # earlier kits wrote "early-check-summary-<time>.txt"
FLASH_RESULT_PREFIX = "flash-result-"
VERDICT_EXIT = {identify.PASS: 0, identify.ALREADY_CONVERTED: 3, identify.STOP: 1}

_MAC_RE = re.compile(r"\b([0-9A-Fa-f]{2}):([0-9A-Fa-f]{2}):([0-9A-Fa-f]{2}):[0-9A-Fa-f]{2}:[0-9A-Fa-f]{2}:[0-9A-Fa-f]{2}\b")
_TAG_RE = re.compile(r"(picco-backup-[0-9a-f]{6})[0-9a-f]{6}(?=-)")
_WIN_USER_RE = re.compile(r"(?i)([A-Z]:\\Users\\)[^\\\r\n\"']+")
_POSIX_USER_RE = re.compile(r"(/(?:home|Users)/)[^/\s\"']+")
_EXIT_RE = re.compile(r"^\[exit (\d+): ([A-Z_?]+)\]$")


def mask_mac(mac: Optional[str]) -> str:
    if not mac:
        return "unknown"
    return _MAC_RE.sub(lambda m: "%s:%s:%s:xx:xx:xx" % m.groups(), mac)


def sanitize(text: str, macs: List[str] = ()) -> str:
    """Mask MAC addresses (and the MAC in backup file names) and user names in paths."""
    for mac in macs:
        tag = workdir.mac_tag(mac)
        if len(tag) == 12:
            text = text.replace(tag, tag[:6] + "xxxxxx")
    text = _MAC_RE.sub(lambda m: "%s:%s:%s:xx:xx:xx" % m.groups(), text)
    text = _TAG_RE.sub(lambda m: m.group(1) + "xxxxxx", text)
    text = _WIN_USER_RE.sub(lambda m: m.group(1) + "<user>", text)
    text = _POSIX_USER_RE.sub(lambda m: m.group(1) + "<user>", text)
    for var in ("USERNAME", "USER"):
        name = os.environ.get(var, "")
        if len(name) >= 3:
            text = re.sub(r"(?<=[\\/])%s(?=[\\/])" % re.escape(name), "<user>", text, flags=re.IGNORECASE)
    return text


def _installed(dist: str) -> str:
    try:
        from importlib import metadata

        return metadata.version(dist)
    except Exception:
        return "not installed"


def _log_runs(work: str) -> List[tuple]:
    """(command, time, exit line) for each command log in work/logs."""
    runs = []
    for path in sorted(glob.glob(os.path.join(work, "logs", "*.log"))):
        name = os.path.basename(path)[:-4]
        cmd, _, stamp = name.rpartition("-")
        exit_line = "no exit line (interrupted?)"
        try:
            with open(path, encoding="utf-8", errors="replace") as fp:
                for line in fp:
                    m = _EXIT_RE.match(line.strip())
                    if m:
                        exit_line = "exit %s %s" % (m.group(1), m.group(2))
        except OSError:
            continue
        runs.append((stamp, cmd, exit_line, path))
    return sorted(runs)


def _tail(path: str, n: int = TAIL_LINES) -> List[str]:
    """The last n lines the owner saw (the per-call esptool descriptions, log-only, are left out)."""
    with open(path, encoding="utf-8", errors="replace") as fp:
        lines = [line.rstrip("\n") for line in fp if not line.startswith("esptool: ")]
    return lines[-n:]


def _hash_check(work: str, info: dict) -> str:
    path = os.path.join(work, info.get("file", ""))
    if not os.path.isfile(path):
        return "file not present"
    size, digest = workdir.file_sha256(path)
    return "re-checked, matches" if (size, digest) == (info.get("size"), info.get("sha256")) else "FILE CHANGED"


def _flash_results(work: str) -> List[str]:
    """One block per flash-result-<time>.json: each image in the order written, and how it ended."""
    out: List[str] = []
    for path in sorted(glob.glob(os.path.join(work, FLASH_RESULT_PREFIX + "*.json"))):
        try:
            rec = workdir.read_json(path)
        except (OSError, ValueError):
            out.append("  %s: unreadable record" % os.path.basename(path))
            continue
        result = rec.get("result")
        version = rec.get("firmware_version")
        out.append("  %s: result %s%s" % (os.path.basename(path), result,
                                          ", firmware %s" % version if version else ""))
        steps = {s.get("role"): s for s in rec.get("steps") or []}
        for n, role in enumerate(guard.FLASH_ORDER, 1):
            step = steps.get(role)
            status = (step or {}).get("status")
            if step is None:
                what = "not written"
            elif status == "verified":
                what = "written and verified (read back SHA-256 %s...)" % step.get("readback_sha256", "")[:16]
                if step.get("tail_not_erased"):
                    what += "; bytes after it in its last sector not erased (harmless)"
            elif status == "verify-failed":
                what = "STOPPED HERE: written, but the read-back differs (SHA-256 %s...)" % (
                    step.get("readback_sha256", "")[:16])
            elif result == "REFUSED":
                what = "not written: refused before the first write"
            else:
                what = "STOPPED HERE: not verified; the write may be incomplete"
            region = guard.REGION_BY_ROLE[role]
            size = (step or {}).get("size")
            out.append("    [%d/4] %-15s at 0x%-6X %-14s %s" % (
                n, role, region.offset, "%d bytes" % size if size is not None else "", what))
        if result == "OK":
            out.append("    The tool said: DONE: all four images written and verified.")
        else:
            out.append("    Reason: %s" % rec.get("reason"))
            if result == "FAILED":
                out.append("    The tool said: FLASHING DID NOT COMPLETE, and showed the restore command (exit %d)."
                           % FLASH_FAILED)
            else:
                out.append("    The tool said: Nothing was written to the Picco (exit %d)." % STOP)
    if not out:
        out.append("  no flash result: flash never reached its first write (see 'Commands run' for its exit code)")
    return out


def _restore_results(runs: List[tuple]) -> List[str]:
    """One line per restore that was really run (not a dry run), from its log."""
    out: List[str] = []
    for stamp, cmd, exit_line, path in runs:
        if cmd != "restore":
            continue
        backup_name, verified, why = "?", None, None
        try:
            with open(path, encoding="utf-8", errors="replace") as fp:
                for line in fp:
                    line = line.rstrip("\n")
                    if line.startswith("Backup file: "):
                        backup_name = re.split(r"[\\/]", line)[-1]
                    elif line.startswith("VERIFIED: the whole flash now equals the backup"):
                        verified = line
                    elif line.startswith("RESTORE DID NOT COMPLETE: "):
                        why = line[len("RESTORE DID NOT COMPLETE: "):]
        except OSError:
            continue
        m = re.match(r"exit (\d+)", exit_line)
        code = int(m.group(1)) if m else None
        if code == OK and verified:
            what = "the whole flash was written, read back and verified: it equals the backup"
        elif code == RESTORE_FAILED:
            what = "did NOT complete (%s); the tool asked to run the restore again" % why
        elif code in (STOP, NOT_REACHED):
            what = "nothing was written (refused, cancelled or the chip not reached)"
        else:
            what = "unclear: no verified read-back in the log"
        out.append("  %s  backup %s" % (stamp, backup_name))
        out.append("    %s; %s" % (what, exit_line if code is None else "exit %d %s" % (code, EXIT_NAMES.get(code, "?"))))
    if not out:
        out.append("  no restore was run")
    return out


def build(work: str, now: Optional[datetime.datetime] = None) -> str:
    now = now or datetime.datetime.now(datetime.timezone.utc)
    out: List[str] = []
    say = out.append
    macs: List[str] = []

    say("piccoflash session summary")
    say("==========================")
    say("Every line of this file is safe to post in a public issue: parsed fields, SHA-256 hashes, the")
    say("commands run and the tool's own messages; never flash contents. The MAC address is partly masked")
    say("and user names are removed from paths.")
    say("Generated (UTC):  %s" % now.strftime("%Y-%m-%d %H:%M:%S"))
    say("Tools:            piccoflash %s; esptool %s installed (%s pinned); pyserial %s; Python %s" % (
        __version__, _installed("esptool"), PINNED_ESPTOOL_VERSION, _installed("pyserial"),
        platform.python_version()))
    say("OS:               %s" % platform.platform())
    say("")

    say("Commands run (from the logs in the work folder)")
    say("-----------------------------------------------")
    runs = _log_runs(work)
    for stamp, cmd, exit_line, _ in runs:
        say("  %-26s %-16s %s" % (stamp, cmd, exit_line))
    if not runs:
        say("  (no command logs found)")
    say("")

    records = []
    for path in workdir.identify_records(work):
        try:
            records.append((path, workdir.read_json(path)))
        except (OSError, ValueError):
            continue
    say("Identify")
    say("--------")
    if not records:
        say("  no identify record (identify never reached the chip, or was not run)")
    for path, rec in records[:-1]:
        say("  earlier: %s  verdict %s" % (os.path.basename(path), rec.get("verdict")))
    if records:
        path, rec = records[-1]
        chip, sec = rec.get("chip", {}), rec.get("security", {})
        macs.append(chip.get("mac", ""))
        verdict = rec.get("verdict")
        say("  latest:  %s  verdict %s (exit %s)" % (os.path.basename(path), verdict, VERDICT_EXIT.get(verdict, "?")))
        say("  port:    %s" % rec.get("port"))
        say("  chip:    %s; %s" % (chip.get("chip_name"), chip.get("description")))
        say("  MAC:     %s" % mask_mac(chip.get("mac")))
        say("  features: %s; crystal %s MHz; PSRAM %s; connected via %s" % (
            ", ".join(chip.get("features") or []), chip.get("crystal_mhz"), chip.get("psram") or "not reported",
            chip.get("usb_mode")))
        fm, fd = chip.get("flash_manufacturer"), chip.get("flash_device")
        say("  flash:   manufacturer %s, device %s, size %s, eFuse FLASH_TYPE %s" % (
            "0x%02X" % fm if fm is not None else "?", "0x%04X" % fd if fd is not None else "?",
            chip.get("flash_size"), chip.get("flash_type_efuse")))
        say("  security: flags 0x%08X, SPI_BOOT_CRYPT_CNT 0x%X, key purposes %s" % (
            sec.get("flags") or 0, sec.get("flash_crypt_cnt") or 0, sec.get("key_purposes")))
        say("            Secure Boot %s (eFuse %s), flash encryption %s (eFuse %s), secure download mode %s," % (
            sec.get("secure_boot_enabled"), sec.get("efuse_secure_boot_enabled"), sec.get("flash_encryption_enabled"),
            sec.get("efuse_flash_encryption_enabled"), sec.get("secure_download_mode")))
        say("            download cache restricted I/D %s/%s, USB disable flag %s" % (
            sec.get("dis_download_icache"), sec.get("dis_download_dcache"), sec.get("dis_usb")))
        say("  checks:")
        for c in rec.get("checks", []):
            mark = "ok  " if c.get("ok") else ("STOP" if c.get("stop_if_failed") else "note")
            say("    [%s] %-21s %s" % (mark, c.get("name"), c.get("detail")))
        bh = rec.get("bootloader_header") or {}
        if bh:
            say("  bootloader header at 0x0: magic ok %s, %s segments, %s / %s / %s, chip id %s, chip rev %s..%s," % (
                bh.get("magic_ok"), bh.get("segments"), bh.get("flash_mode"), bh.get("flash_size"),
                bh.get("flash_freq"), bh.get("chip_id"), bh.get("min_rev_full"), bh.get("max_rev_full")))
            say("            hash appended %s, image length %s, checksum ok %s, SHA-256 ok %s, signature block %s" % (
                bh.get("hash_appended"), bh.get("image_length"), bh.get("checksum_ok"), bh.get("sha256_ok"),
                bh.get("signature_block")))
        pt = rec.get("partition_table") or {}
        say("  partition table at 0x8000: %s, MD5 entry %s%s" % (
            pt.get("kind"), pt.get("md5_present"), "; errors: %s" % "; ".join(pt["errors"]) if pt.get("errors") else ""))
        for r in pt.get("rows") or []:
            say("    %-10s %-5s %-9s offset %-9s size %-9s flags %s" % (
                r.get("name"), r.get("type"), r.get("subtype"), r.get("offset"), r.get("size"), r.get("flags")))
        ota = rec.get("otadata") or {}
        for i, r in enumerate(ota.get("records") or []):
            say("  otadata record %d: seq %s, state %s, CRC ok %s" % (i, r.get("seq"), r.get("state_name"),
                                                                    r.get("crc_ok")))
        for name, info in sorted((rec.get("blobs") or {}).items()):
            say("  %-10s blob: %6d bytes, SHA-256 %s (%s)" % (name, info.get("size"), info.get("sha256"),
                                                              _hash_check(work, info)))
    say("")

    say("Backup")
    say("------")
    sidecars = workdir.backup_sidecars(work)
    for side in sidecars:
        try:
            rec = workdir.read_json(side)
        except (OSError, ValueError):
            say("  %s: unreadable record" % os.path.basename(side))
            continue
        macs.append(rec.get("mac", ""))
        reads = [r.get("sha256") for r in rec.get("reads", [])]
        say("  %s" % rec.get("file"))
        say("    size %s, SHA-256 %s" % (rec.get("size"), rec.get("sha256")))
        say("    verified (two identical reads) %s; reads equal %s; owner confirmed the copy %s; file %s" % (
            rec.get("verified"), len(set(reads)) == 1 and len(reads) == 2, bool(rec.get("owner_ack")),
            _hash_check(work, rec)))
        say("    taken from table: %s; at %s" % (rec.get("partition_table"), rec.get("created_utc")))
    for path in sorted(glob.glob(os.path.join(work, workdir.BACKUP_PREFIX + "*-MISMATCH.json"))):
        try:
            rep = workdir.read_json(path)
        except (OSError, ValueError):
            continue
        blocks = rep.get("differing_64k_blocks", [])
        say("  MISMATCH %s: read 1 %s, read 2 %s, %d differing 64 KiB blocks: %s%s" % (
            os.path.basename(path), rep.get("read1", {}).get("sha256", "")[:16],
            rep.get("read2", {}).get("sha256", "")[:16], len(blocks), ", ".join(blocks[:16]),
            " ..." if len(blocks) > 16 else ""))
    if not sidecars:
        say("  no backup record")
    say("")

    say("Flash (from the flash-result files in the work folder; images in the order written)")
    say("-----------------------------------------------------------------------------------")
    out.extend(_flash_results(work))
    say("")

    say("Restore (from the restore logs in the work folder)")
    say("--------------------------------------------------")
    out.extend(_restore_results(runs))
    say("")

    for cmd in ("identify", "backup", "flash", "restore"):
        latest = [r for r in runs if r[1] == cmd]
        if latest:
            say("Last %d lines of the latest %s log (%s)" % (TAIL_LINES, cmd, os.path.basename(latest[-1][3])))
            say("-" * 60)
            out.extend("  | " + line for line in _tail(latest[-1][3]))
            say("")
    say("End of summary. Never attach the .bin files from the work folder: they hold the Picco's own data.")
    return sanitize("\n".join(out) + "\n", [m for m in macs if m])


def run(ctx: Context) -> int:
    ui = ctx.ui
    work = ctx.work
    ui.heading("Summary (read-only; the Picco is not contacted)")
    if not os.path.isdir(work):
        ui.say("There is no work folder at %s yet. Run identify first:" % work)
        ui.say("    %s" % ctx.command_hint("identify"))
        return 1
    text = build(work)
    if ctx.dry_run:
        ui.say("DRY RUN: the summary would be written to the work folder. Its contents:")
        ui.say(text)
        return OK
    path = os.path.join(work, "%s%s.txt" % (FILE_PREFIX, workdir.timestamp()))
    with open(path, "w", encoding="utf-8", newline="\n") as fp:
        fp.write(text)
    ui.say("Summary written: %s" % os.path.abspath(path))
    ui.say("Attach this one file to the GitHub issue. Never attach the .bin files.")
    return OK
