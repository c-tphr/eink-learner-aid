"""identify: read-only facts about the connected chip, and the go/no-go verdict.

Reads only. The decision (``evaluate``) is a pure function of what was read,
so every stop condition is unit-tested without a device.

Verdicts:

* ``PASS``: an ESP32-S3 with 16 MB quad flash, no Secure Boot, no flash
  encryption, download mode unrestricted, unsigned stock bootloader, and a
  partition table equal to partitions_picco.csv row for row.
* ``ALREADY_CONVERTED``: all of the above except that the table is already
  our two-slot table. ``backup`` and ``flash`` refuse such a device (updates
  go through the SD card); ``restore`` still works.
* ``STOP``: anything else. Nothing has been changed.
"""

from __future__ import annotations

import os
from hashlib import sha256
from dataclasses import asdict, dataclass, field
from typing import List, Optional

from . import PINNED_ESPTOOL_VERSION, __version__, espimage, esptool_calls, layout, workdir
from .device import ChipInfo, Device, SecurityInfo
from .ui import UI

PASS = "PASS"
ALREADY_CONVERTED = "ALREADY_CONVERTED"
STOP = "STOP"

BOOTLOADER_READ = (0x0, 0x8000)
TABLE_READ = (layout.TABLE_OFFSET, layout.TABLE_MAX_LEN)
OTADATA_READ = (0x15000, layout.OTADATA_LEN)

# eFuse key purposes that mean flash encryption or Secure Boot keys are burned.
SECURITY_KEY_PURPOSES = {2: "XTS_AES_256_KEY_1", 3: "XTS_AES_256_KEY_2", 4: "XTS_AES_128_KEY",
                         9: "SECURE_BOOT_DIGEST0", 10: "SECURE_BOOT_DIGEST1", 11: "SECURE_BOOT_DIGEST2"}


@dataclass
class Check:
    name: str
    ok: bool
    detail: str
    stop_if_failed: bool = True


@dataclass
class Evaluation:
    verdict: str
    checks: List[Check] = field(default_factory=list)
    table_kind: Optional[str] = None
    bootloader: Optional[dict] = None
    otadata: Optional[dict] = None

    @property
    def stops(self) -> List[Check]:
        return [c for c in self.checks if not c.ok and c.stop_if_failed]


def evaluate(chip: ChipInfo, sec: SecurityInfo, boot_blob: Optional[bytes], table_blob: Optional[bytes],
             ota_blob: Optional[bytes]) -> Evaluation:
    checks: List[Check] = []

    def add(name: str, ok: bool, detail: str, stop: bool = True) -> None:
        checks.append(Check(name, bool(ok), detail, stop))

    add("chip", chip.chip_name == "ESP32-S3", "chip is %s (%s)" % (chip.chip_name, chip.description))
    add("secure download mode", not sec.secure_download_mode,
        "secure download mode is %s" % ("ON: flash cannot be read or verified" if sec.secure_download_mode else "off"))
    add("flash size", chip.flash_size == "16MB", "flash chip reports %s" % (chip.flash_size or "an unknown size"))
    add("flash type", chip.flash_type_efuse == "quad",
        "eFuse FLASH_TYPE says %s flash" % (chip.flash_type_efuse or "unknown"))
    sb = sec.secure_boot_enabled or bool(sec.efuse_secure_boot_enabled)
    add("secure boot", not sb, "Secure Boot is %s" % ("ENABLED" if sb else "disabled"))
    fe = sec.flash_crypt_cnt != 0 or bool(sec.efuse_flash_encryption_enabled)
    add("flash encryption", not fe, "flash encryption is %s (SPI_BOOT_CRYPT_CNT 0x%X)" % (
        "ENABLED or was enabled" if fe else "disabled", sec.flash_crypt_cnt))
    restricted = sec.dis_download_icache or sec.dis_download_dcache
    add("download mode", not restricted, "download-mode cache access is %s" % (
        "RESTRICTED" if restricted else "unrestricted"))
    keys = [SECURITY_KEY_PURPOSES[p] for p in sec.key_purposes if p in SECURITY_KEY_PURPOSES]
    add("security keys", not keys, "eFuse key blocks: %s" % (", ".join(keys) if keys else "no encryption or "
                                                                                       "Secure Boot keys"))
    add("USB access", True, "USB disable flag: %s; connected via %s" % (
        "set" if sec.dis_usb else "not set", chip.usb_mode or "unknown"), stop=False)
    add("PSRAM", True, chip.psram or "not reported by the chip's eFuses", stop=False)

    ev = Evaluation(STOP, checks)
    if boot_blob is None or table_blob is None:
        add("flash contents", False, "not read, because an earlier check failed")
        return _finish(ev)

    info = espimage.parse_image(boot_blob)
    signed = info.image_length is not None and espimage.signature_block_after(boot_blob, info.image_length)
    ev.bootloader = dict(info.summary(), signature_block=signed)
    boot_ok = info.ok and info.chip_id == espimage.CHIP_ID_ESP32S3
    add("stock bootloader", boot_ok, "bootloader at 0x0: %s" % (
        "ESP32-S3 image, %s, %s, %s, %d bytes" % (info.flash_mode, info.flash_freq, info.flash_size,
                                                  info.image_length or 0) if boot_ok
        else "not a valid ESP32-S3 image (%s)" % "; ".join(info.errors or ["chip id %s" % info.chip_id])))
    add("bootloader signature", not signed, "Secure Boot signature block after the bootloader: %s" % (
        "PRESENT" if signed else "none"))

    parsed = layout.parse_table(table_blob)
    kind = layout.classify_table(parsed.rows) if parsed.ok else "unreadable"
    ev.table_kind = kind
    if kind == "stock":
        add("partition table", True, "partition table equals partitions_picco.csv (%d rows%s)" % (
            len(parsed.rows), ", MD5 ok" if parsed.md5_present else ", no MD5 entry"))
    elif kind == "two-slot":
        add("partition table", True, "partition table is ALREADY the two-slot table", stop=False)
    else:
        diffs = parsed.errors + layout.diff_rows(parsed.rows, layout.STOCK_TABLE)
        add("partition table", False, "partition table differs from partitions_picco.csv: " + "; ".join(diffs))

    if ota_blob is not None:
        recs = layout.parse_otadata(ota_blob)
        ev.otadata = {"records": [dict(asdict(r), state_name=r.state_name) for r in recs],
                      "boot_slot_if_one_ota_slot": layout.otadata_boot_slot(ota_blob, 1)}
    return _finish(ev)


def _finish(ev: Evaluation) -> Evaluation:
    if ev.stops:
        ev.verdict = STOP
    elif ev.table_kind == "two-slot":
        ev.verdict = ALREADY_CONVERTED
    elif ev.table_kind == "stock":
        ev.verdict = PASS
    else:
        ev.verdict = STOP
    return ev


@dataclass
class IdentifyResult:
    evaluation: Evaluation
    chip: ChipInfo
    security: SecurityInfo
    boot_blob: Optional[bytes]
    table_blob: Optional[bytes]
    ota_blob: Optional[bytes]
    record_path: Optional[str] = None
    record: Optional[dict] = None

    @property
    def verdict(self) -> str:
        return self.evaluation.verdict


def read_and_evaluate(dev: Device, ui: UI) -> IdentifyResult:
    """Read chip facts and the three small flash regions. Read-only."""
    for call in esptool_calls.chip_info(dev.port) + esptool_calls.security_info(dev.port):
        ui.log_only("esptool: %s   [CLI: %s]" % (call.api(), call.cli))
    chip = dev.chip_info()
    if chip.chip_name == "ESP32-S3":
        sec = dev.security_info()
    else:  # GET_SECURITY_INFO is not available on every chip; identify stops on the chip check anyway
        sec = SecurityInfo(flags=0, flash_crypt_cnt=0, key_purposes=[], secure_boot_enabled=False,
                           secure_download_mode=False, dis_download_icache=False, dis_download_dcache=False,
                           dis_usb=False, raw={"note": "not read: not an ESP32-S3"})
    boot = table = ota = None
    if chip.chip_name == "ESP32-S3" and not sec.secure_download_mode:
        reads = []
        for off, length in (BOOTLOADER_READ, TABLE_READ, OTADATA_READ):
            call = esptool_calls.read(dev.port, off, length, "identify-0x%X.bin" % off)
            ui.log_only("esptool: %s   [CLI: %s]" % (call.api(), call.cli))
            reads.append(dev.read(off, length))
        boot, table, ota = reads
    ev = evaluate(chip, sec, boot, table, ota)
    return IdentifyResult(ev, chip, sec, boot, table, ota)


def save_record(res: IdentifyResult, work: str, port: str, stamp: Optional[str] = None) -> str:
    stamp = stamp or workdir.timestamp()
    base = os.path.join(workdir.ensure(work), "%s%s" % (workdir.IDENTIFY_PREFIX, stamp))
    blobs = {}
    for name, blob in (("bootloader", res.boot_blob), ("table", res.table_blob), ("otadata", res.ota_blob)):
        if blob is not None:
            path = "%s-%s.bin" % (base, name)
            workdir.write_bytes(path, blob)
            blobs[name] = {"file": os.path.basename(path), "size": len(blob), "sha256": sha256(blob).hexdigest()}
    parsed = layout.parse_table(res.table_blob) if res.table_blob is not None else None
    rec = {
        "format": "picco-identify",
        "version": 1,
        "time_utc": stamp,
        "port": port,
        "verdict": res.verdict,
        "checks": [asdict(c) for c in res.evaluation.checks],
        "chip": res.chip.as_dict(),
        "security": res.security.as_dict(),
        "bootloader_header": res.evaluation.bootloader,
        "partition_table": {
            "kind": res.evaluation.table_kind,
            "rows": [r.as_dict() for r in parsed.rows] if parsed else None,
            "md5_present": parsed.md5_present if parsed else None,
            "errors": parsed.errors if parsed else None,
        },
        "otadata": res.evaluation.otadata,
        "blobs": blobs,
        "tool": {"piccoflash": __version__, "esptool": PINNED_ESPTOOL_VERSION},
    }
    path = base + ".json"
    workdir.write_json_atomic(path, rec)
    res.record_path, res.record = path, rec
    return path


def print_summary(res: IdentifyResult, ui: UI) -> None:
    ui.heading("What the Picco's chip reports")
    c = res.chip
    ui.say("Chip:          %s" % c.description)
    ui.say("MAC address:   %s" % c.mac)
    ui.say("Features:      %s" % ", ".join(c.features))
    ui.say("Crystal:       %s MHz" % c.crystal_mhz)
    if c.flash_manufacturer is not None:
        ui.say("Flash chip:    manufacturer 0x%02X, device 0x%04X, %s, %s" % (
            c.flash_manufacturer, c.flash_device or 0, c.flash_size, c.flash_type_efuse))
    ui.say("Connected via: %s" % (c.usb_mode or "unknown"))
    ui.heading("Checks")
    for chk in res.evaluation.checks:
        mark = "ok  " if chk.ok else ("STOP" if chk.stop_if_failed else "note")
        ui.say("[%s] %-21s %s" % (mark, chk.name, chk.detail))
    ui.say("")
    if res.verdict == PASS:
        ui.say("RESULT: PASS. This is a stock Picco that matches what the tools expect.")
        ui.say("Nothing on the device was changed. Next step: backup.")
    elif res.verdict == ALREADY_CONVERTED:
        ui.say("RESULT: ALREADY CONVERTED. This Picco already has the two-slot layout.")
        ui.say("Nothing was changed. 'flash' will refuse this device: install updates from the SD card")
        ui.say("(Settings > SD Card Firmware Update). 'restore <backup>' still works.")
    else:
        ui.say("RESULT: STOP. Do not continue with backup or flash.")
        for chk in res.evaluation.stops:
            ui.say("  - %s: %s" % (chk.name, chk.detail))
        ui.say("Nothing on the device was changed. Report this output (and the work folder) in the issue.")
