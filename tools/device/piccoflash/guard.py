"""The offset guard: the single authority on what may be written to the Picco.

This module is pure: no file, device or network access. Its policy is hard
coded for this one device and does not come from the flash plan, so a wrong
or tampered plan cannot widen it. There is no override and no force flag.

What may be written
-------------------
``flash`` writes exactly four images, each at exactly one offset:

======== ================= ========= ============= ========================
role     file              offset    region end    length
======== ================= ========= ============= ========================
app      firmware.bin      0x20000   0x800000      1 .. 0x7E0000
boot_app0 boot_app0.bin    0x15000   0x17000       exactly 0x2000
partition_table partitions.bin 0x8000 0x9000       exactly our table (0xC00)
bootloader bootloader.bin  0x0       0x8000        < 0x8000
======== ================= ========= ============= ========================

A write erases whole 4 KiB sectors, so the guard checks the sector-rounded
span ``[offset, round_up(offset + length, 0x1000))``, not just the bytes of
the file. That span must lie inside the role's region. Nothing may touch the
stock NVS (0x9000-0x14FFF); 0xE000, the framework's default ``boot_app0``
offset, is called out by name.

``restore`` writes exactly one image: 16,777,216 bytes at 0x0 whose SHA-256
equals the hash recorded when the backup was taken.

Every write goes through ``approve_*`` (which returns an ``ApprovedWrite``
holding the exact bytes) and, immediately before esptool is called, through
``recheck`` on those same bytes. Device implementations refuse anything else.
"""

from __future__ import annotations

import hashlib
import re
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

from . import espimage, layout

SECTOR = layout.SECTOR
FLASH_SIZE = layout.FLASH_SIZE


class GuardError(Exception):
    """Raised when the guard refuses. ``reasons`` lists every problem found."""

    def __init__(self, reasons: Sequence[str]):
        self.reasons = list(reasons)
        super().__init__("; ".join(self.reasons))


@dataclass(frozen=True)
class Region:
    role: str
    file_name: str
    offset: int
    end: int  # exclusive; the sector-rounded write span must end at or before it
    min_len: int
    max_len: int
    description: str


# The write order used by ``flash`` is FLASH_ORDER below; this tuple is in
# address order for display.
FLASH_REGIONS: Tuple[Region, ...] = (
    Region("bootloader", "bootloader.bin", 0x0, 0x8000, espimage.HEADER_LEN, 0x7FFF, "second-stage bootloader"),
    Region("partition_table", "partitions.bin", 0x8000, 0x9000, layout.TABLE_MAX_LEN, layout.TABLE_MAX_LEN,
           "partition table (two-slot)"),
    Region("boot_app0", "boot_app0.bin", 0x15000, 0x17000, layout.OTADATA_LEN, layout.OTADATA_LEN,
           "otadata initial image (boot app0)"),
    Region("app", "firmware.bin", 0x20000, 0x800000, espimage.HEADER_LEN, 0x7E0000, "application, slot app0 (ota_0)"),
)
REGION_BY_ROLE: Dict[str, Region] = {r.role: r for r in FLASH_REGIONS}
ROLES = tuple(r.role for r in FLASH_REGIONS)

# App first, bootloader last. See README "Write order and failure analysis".
FLASH_ORDER: Tuple[str, ...] = ("app", "boot_app0", "partition_table", "bootloader")

# Named forbidden ranges, for clear refusal messages. Anything outside the
# four regions is refused as well; these only make the reason specific.
FORBIDDEN: Tuple[Tuple[int, int, str], ...] = (
    (0x9000, 0x15000, "the stock NVS partition (0x9000-0x14FFF), which the two-slot layout keeps"),
    (0xE000, 0x10000, "0xE000, the framework's default boot_app0 offset, which is inside the stock NVS"),
)

RESTORE_OFFSET = 0x0
RESTORE_LEN = FLASH_SIZE

# Image budget from D4's CI gate (slot minus 10%); exceeding it is a warning,
# exceeding the slot itself is a refusal.
APP_BUDGET = 7431782

_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
_BAD_NAME_RE = re.compile(r"factory|merged|combined|full", re.IGNORECASE)

# Seal for ApprovedWrite: only this module holds it, so only the guard can approve.
_SEAL = object()


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def touched_span(offset: int, length: int) -> Tuple[int, int]:
    """Bytes a write of ``length`` at ``offset`` erases: whole sectors."""
    start = offset - (offset % SECTOR)
    end = -(-(offset + length) // SECTOR) * SECTOR
    return start, end


def allowed_spans() -> List[Tuple[int, int]]:
    return [(r.offset, r.end) for r in FLASH_REGIONS]


def _forbidden_hits(start: int, end: int) -> List[str]:
    return ["it would touch %s" % why for (fs, fe, why) in FORBIDDEN if start < fe and fs < end]


def check_extent(role: str, offset: int, length: int) -> List[str]:
    """Where and how long. Returns a list of reasons; empty means allowed."""
    region = REGION_BY_ROLE.get(role)
    if region is None:
        return ["unknown image role %r; the only roles are %s" % (role, ", ".join(ROLES))]
    errs = []
    label = "%s (%s)" % (role, region.file_name)
    if not isinstance(offset, int) or not isinstance(length, int) or offset < 0 or length < 0:
        return ["%s: offset and length must be non-negative integers" % label]
    if offset != region.offset:
        errs.append("%s: offset 0x%X is not the allowed offset 0x%X" % (label, offset, region.offset))
    if offset % SECTOR:
        errs.append("%s: offset 0x%X is not 4 KiB aligned" % (label, offset))
    if length < region.min_len:
        errs.append("%s: %d bytes is shorter than the minimum %d" % (label, length, region.min_len))
    if length > region.max_len:
        errs.append("%s: %d bytes (0x%X) is longer than the maximum %d (0x%X)" % (
            label, length, length, region.max_len, region.max_len))
    start, end = touched_span(offset, length)
    if start < region.offset or end > region.end:
        errs.append("%s: the write would erase 0x%X-0x%X, outside its region 0x%X-0x%X" % (
            label, start, end - 1, region.offset, region.end - 1))
    if end > FLASH_SIZE:
        errs.append("%s: the write would run past the end of the 16 MB flash" % label)
    for hit in _forbidden_hits(start, end):
        errs.append("%s: %s" % (label, hit))
    return errs


def check_image_content(role: str, data: bytes) -> List[str]:
    """What. Cheap sanity checks on the exact bytes of one image."""
    errs: List[str] = []
    if role not in REGION_BY_ROLE:
        return ["unknown image role %r" % role]
    label = "%s (%s)" % (role, REGION_BY_ROLE[role].file_name)
    if not data:
        return ["%s: file is empty" % label]
    if data.count(0xFF) == len(data) or data.count(0) == len(data):
        errs.append("%s: file is blank (all 0xFF or all 0x00)" % label)
    if role in ("bootloader", "app"):
        info = espimage.parse_image(data)
        errs += ["%s: %s" % (label, e) for e in info.errors]
        if info.magic_ok and info.chip_id != espimage.CHIP_ID_ESP32S3:
            errs.append("%s: image is for chip id %s, not ESP32-S3 (9)" % (label, info.chip_id))
        if info.image_length is not None and info.image_length != len(data):
            errs.append("%s: the file is %d bytes but holds a %d-byte image; it is not exactly one image "
                        "(a merged firmware.factory.bin or a padded file?)" % (label, len(data), info.image_length))
        if info.ok and not info.hash_appended:
            errs.append("%s: image has no appended SHA-256" % label)
        if role == "bootloader" and info.ok:
            if (info.flash_mode, info.flash_size, info.flash_freq) != ("DIO", "16MB", "80m"):
                errs.append("%s: header says %s/%s/%s; the Picco bootloader must be DIO/16MB/80m" % (
                    label, info.flash_mode, info.flash_size, info.flash_freq))
            if not _has_bootloader_desc(data, info):
                errs.append("%s: no ESP-IDF bootloader description; this does not look like a bootloader" % label)
        if role == "app" and info.ok:
            if not _has_app_desc(data, info):
                errs.append("%s: no ESP-IDF application description in the first segment; "
                            "this does not look like an application image" % label)
    elif role == "partition_table":
        expected = layout.encode_table(layout.TWO_SLOT_TABLE)
        if data != expected:
            parsed = layout.parse_table(data)
            kind = layout.classify_table(parsed.rows)
            if kind == "stock":
                errs.append("%s: this is the STOCK Onyx table, not the two-slot table" % label)
            elif kind == "two-slot":
                errs.append("%s: rows match the two-slot table but the bytes differ (MD5 entry or padding)" % label)
            else:
                errs.append("%s: not the two-slot table" % label)
                errs += ["%s: %s" % (label, d) for d in layout.diff_rows(parsed.rows, layout.TWO_SLOT_TABLE)]
            errs += ["%s: %s" % (label, e) for e in parsed.errors]
    elif role == "boot_app0":
        if len(data) != layout.OTADATA_LEN:
            errs.append("%s: must be exactly 8192 bytes, is %d" % (label, len(data)))
        else:
            slot = layout.otadata_boot_slot(data, 2)
            if slot != 0:
                errs.append("%s: does not select slot app0 (ota_0); selects %s" % (label, slot))
            if sha256(data) != layout.BOOT_APP0_SHA256:
                errs.append("%s: SHA-256 %s is not the known Arduino boot_app0.bin %s" % (
                    label, sha256(data)[:16], layout.BOOT_APP0_SHA256[:16]))
    return errs


def _has_app_desc(data: bytes, info: espimage.ImageInfo) -> bool:
    if not info.segments:
        return False
    load, length = info.segments[0]
    pos = espimage.HEADER_LEN + 8
    return 0x3C000000 <= load < 0x3E000000 and length >= 4 and data[pos:pos + 4] == b"\x32\x54\xcd\xab"


def _has_bootloader_desc(data: bytes, info: espimage.ImageInfo) -> bool:
    pos = espimage.HEADER_LEN
    for load, length in info.segments:
        if 0x3FC88000 <= load < 0x3FD00000 and length >= 80 and data[pos + 8] == 0x50:
            return True
        pos += 8 + length
    return False


@dataclass(frozen=True)
class PlannedImage:
    """One row of a flash plan, as the guard sees it."""

    role: str
    file_name: str
    offset: int
    size: int
    sha256: str


def check_plan_set(images: Sequence[PlannedImage]) -> List[str]:
    """The whole set: exactly the four images, nothing else, no overlaps."""
    errs: List[str] = []
    roles = [i.role for i in images]
    for role in ROLES:
        n = roles.count(role)
        if n == 0:
            errs.append("missing image: %s (%s at 0x%X)" % (role, REGION_BY_ROLE[role].file_name,
                                                             REGION_BY_ROLE[role].offset))
        elif n > 1:
            errs.append("duplicate image role: %s appears %d times" % (role, n))
    for role in roles:
        if role not in REGION_BY_ROLE:
            errs.append("extra image with unknown role %r; a flash writes exactly four images" % role)
    if len(images) != len(ROLES):
        errs.append("a flash writes exactly %d images, the plan has %d" % (len(ROLES), len(images)))
    offsets = [i.offset for i in images]
    for off in sorted(set(o for o in offsets if offsets.count(o) > 1)):
        errs.append("duplicate offset 0x%X" % off)
    for img in images:
        errs += check_extent(img.role, img.offset, img.size)
        if img.role in REGION_BY_ROLE and img.file_name != REGION_BY_ROLE[img.role].file_name:
            errs.append("%s: file must be named %s, not %r" % (img.role, REGION_BY_ROLE[img.role].file_name,
                                                               img.file_name))
        if _BAD_NAME_RE.search(img.file_name):
            errs.append("%s: %r looks like a merged image; flash the four separate files" % (img.role, img.file_name))
        if not _SHA256_RE.match(img.sha256 or ""):
            errs.append("%s: sha256 must be 64 lowercase hex digits" % img.role)
    spans = sorted((touched_span(i.offset, i.size) + (i.role,)) for i in images if i.size >= 0)
    for (s0, e0, r0), (s1, e1, r1) in zip(spans, spans[1:]):
        if s1 < e0:
            errs.append("images overlap: %s (to 0x%X) and %s (from 0x%X)" % (r0, e0 - 1, r1, s1))
    return _dedupe(errs)


def check_extents(writes: Iterable[Tuple[int, int]]) -> List[Optional[str]]:
    """For each (offset, length), the role it may be written as, or None.

    Used by the property test: a write is only ever allowed if it matches one
    region exactly in offset and fits it after sector rounding.
    """
    out: List[Optional[str]] = []
    for offset, length in writes:
        role = next((r.role for r in FLASH_REGIONS if r.offset == offset), None)
        out.append(role if role is not None and not check_extent(role, offset, length) else None)
    return out


class ApprovedWrite:
    """Bytes the guard has approved for one write. Only this module makes them."""

    __slots__ = ("_seal", "kind", "role", "offset", "data", "sha256")

    def __init__(self, seal: object, kind: str, role: str, offset: int, data: bytes, digest: str):
        if seal is not _SEAL:
            raise GuardError(["ApprovedWrite can only be created by the guard"])
        object.__setattr__(self, "_seal", seal)
        object.__setattr__(self, "kind", kind)
        object.__setattr__(self, "role", role)
        object.__setattr__(self, "offset", offset)
        object.__setattr__(self, "data", bytes(data))
        object.__setattr__(self, "sha256", digest)

    def __setattr__(self, name, value):
        raise AttributeError("ApprovedWrite is read-only")

    @property
    def length(self) -> int:
        return len(self.data)

    def __repr__(self) -> str:
        return "ApprovedWrite(%s %s at 0x%X, %d bytes, sha256 %s)" % (
            self.kind, self.role, self.offset, len(self.data), self.sha256[:16])


def approve_flash_write(image: PlannedImage, data: bytes) -> ApprovedWrite:
    """Approve writing ``data`` (the bytes just read from the kit) for ``image``."""
    data = bytes(data)
    errs = check_extent(image.role, image.offset, len(data))
    errs += check_image_content(image.role, data)
    digest = sha256(data)
    if digest != image.sha256:
        errs.append("%s: the file's SHA-256 is %s but the plan says %s; the file changed after the plan "
                    "was made or verified" % (image.role, digest[:16], (image.sha256 or "")[:16]))
    if len(data) != image.size:
        errs.append("%s: the file is %d bytes but the plan says %d" % (image.role, len(data), image.size))
    if image.role in REGION_BY_ROLE and image.file_name != REGION_BY_ROLE[image.role].file_name:
        errs.append("%s: unexpected file name %r" % (image.role, image.file_name))
    if errs:
        raise GuardError(_dedupe(errs))
    return ApprovedWrite(_SEAL, "flash", image.role, image.offset, data, digest)


def check_restore(data_len: int, digest: str, recorded_size: int, recorded_sha256: str) -> List[str]:
    errs = []
    if data_len != RESTORE_LEN:
        errs.append("backup file is %d bytes; a full backup is exactly %d bytes (16 MiB)" % (data_len, RESTORE_LEN))
    if recorded_size != RESTORE_LEN:
        errs.append("the backup record says %r bytes; expected %d" % (recorded_size, RESTORE_LEN))
    if not _SHA256_RE.match(recorded_sha256 or ""):
        errs.append("the backup record has no valid SHA-256")
    elif digest != recorded_sha256:
        errs.append("backup file SHA-256 %s does not match the hash recorded when it was taken (%s)" % (
            digest[:16], recorded_sha256[:16]))
    return errs


def approve_restore_write(data: bytes, recorded_size: int, recorded_sha256: str) -> ApprovedWrite:
    data = bytes(data)
    digest = sha256(data)
    errs = check_restore(len(data), digest, recorded_size, recorded_sha256)
    if errs:
        raise GuardError(errs)
    return ApprovedWrite(_SEAL, "restore", "full_backup", RESTORE_OFFSET, data, digest)


def recheck(approved: object) -> None:
    """Called by every Device.write immediately before the esptool call."""
    if not isinstance(approved, ApprovedWrite) or getattr(approved, "_seal", None) is not _SEAL:
        raise GuardError(["write refused: it was not approved by the guard"])
    digest = sha256(approved.data)
    if digest != approved.sha256:
        raise GuardError(["write refused: the approved bytes changed after approval"])
    if approved.kind == "flash":
        errs = check_extent(approved.role, approved.offset, len(approved.data))
        errs += check_image_content(approved.role, approved.data)
        if errs:
            raise GuardError(errs)
    elif approved.kind == "restore":
        if approved.offset != RESTORE_OFFSET or len(approved.data) != RESTORE_LEN:
            raise GuardError(["write refused: a restore writes exactly 16 MiB at 0x0"])
    else:
        raise GuardError(["write refused: unknown write kind %r" % approved.kind])


def _dedupe(items: List[str]) -> List[str]:
    seen = set()
    out = []
    for i in items:
        if i not in seen:
            seen.add(i)
            out.append(i)
    return out
