"""Flash layout facts for the BOOX Picco, and the partition-table binary format.

Everything here is a constant or a pure function. The numbers come from
``partitions_picco.csv`` (the stock table read from a retail unit) and from
spike S3 (``docs/spikes/S3-two-slot.md``); the tests compare them with those
files so the two cannot drift apart silently.

Partition table binary format (ESP-IDF ``gen_esp32part.py``, documented at
docs.espressif.com, "Partition Tables"): the table lives at 0x8000 and is at
most 0xC00 bytes. Each entry is 32 bytes, little endian::

    magic   2 bytes  0xAA 0x50
    type    1 byte
    subtype 1 byte
    offset  4 bytes
    size    4 bytes
    name   16 bytes  NUL padded
    flags   4 bytes

An optional MD5 entry follows the last partition: 0xEB 0xEB, 14 bytes 0xFF,
then the 16-byte MD5 of all preceding entries. The rest is 0xFF.
"""

from __future__ import annotations

import binascii
import hashlib
import struct
from dataclasses import dataclass
from typing import List, Optional, Sequence, Tuple

FLASH_SIZE = 0x1000000  # 16 MiB, the only flash size this tool accepts
SECTOR = 0x1000  # smallest erasable unit; every write erases whole sectors

TABLE_OFFSET = 0x8000
TABLE_MAX_LEN = 0xC00  # gen_esp32part MAX_PARTITION_LENGTH
ENTRY_LEN = 32
ENTRY_MAGIC = b"\xaa\x50"
MD5_MAGIC = b"\xeb\xeb"

TYPES = {"app": 0x00, "data": 0x01}
SUBTYPES = {
    "app": {"factory": 0x00, "test": 0x20, **{"ota_%d" % i: 0x10 + i for i in range(16)}},
    "data": {
        "ota": 0x00,
        "phy": 0x01,
        "nvs": 0x02,
        "coredump": 0x03,
        "nvs_keys": 0x04,
        "efuse": 0x05,
        "undefined": 0x06,
        "esphttpd": 0x80,
        "fat": 0x81,
        "spiffs": 0x82,
        "littlefs": 0x83,
    },
}


@dataclass(frozen=True)
class Row:
    """One partition table row, as in a CSV file."""

    name: str
    type: str
    subtype: str
    offset: int
    size: int
    flags: int = 0

    @property
    def end(self) -> int:
        return self.offset + self.size

    def as_dict(self) -> dict:
        return {
            "name": self.name,
            "type": self.type,
            "subtype": self.subtype,
            "offset": "0x%X" % self.offset,
            "size": "0x%X" % self.size,
            "flags": self.flags,
        }

    @classmethod
    def from_dict(cls, d: dict) -> "Row":
        return cls(d["name"], d["type"], d["subtype"], int(d["offset"], 16), int(d["size"], 16), int(d["flags"]))


# partitions_picco.csv, rows 5-10 (the retail unit's table).
STOCK_TABLE: Tuple[Row, ...] = (
    Row("nvs", "data", "nvs", 0x9000, 0xC000),
    Row("otadata", "data", "ota", 0x15000, 0x2000),
    Row("safeboot", "app", "factory", 0x20000, 0xC0000),
    Row("app", "app", "ota_0", 0xE0000, 0xE70000),
    Row("spiffs", "data", "spiffs", 0xF50000, 0x80000),
    Row("coredump", "data", "coredump", 0xFD0000, 0x10000),
)

# docs/spikes/partitions_picco_2slot.proposed.csv (spike S3, section 3).
TWO_SLOT_TABLE: Tuple[Row, ...] = (
    Row("nvs", "data", "nvs", 0x9000, 0xC000),
    Row("otadata", "data", "ota", 0x15000, 0x2000),
    Row("app0", "app", "ota_0", 0x20000, 0x7E0000),
    Row("app1", "app", "ota_1", 0x800000, 0x7E0000),
    Row("coredump", "data", "coredump", 0xFF0000, 0x10000),
)

# SHA-256 of the gen_esp32part.py output for TWO_SLOT_TABLE, measured in spike S3
# with both the arduino-esp32 3.3.11 and the ESP-IDF v5.5.5 copy of the tool.
TWO_SLOT_TABLE_SHA256 = "0d3fad517e6421219fb3754b58c1e1153505655aba69dac3fc40cbd528d4a332"

# SHA-256 of framework-arduinoespressif32 3.3.11 tools/partitions/boot_app0.bin,
# measured in spike S3. boot_app0_bytes() below reproduces it exactly.
BOOT_APP0_SHA256 = "f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0"
OTADATA_LEN = 0x2000


def _subtype_code(ptype: str, subtype: str) -> int:
    return SUBTYPES[ptype][subtype]


def _subtype_name(type_code: int, sub_code: int) -> str:
    for tname, tcode in TYPES.items():
        if tcode == type_code:
            for sname, scode in SUBTYPES[tname].items():
                if scode == sub_code:
                    return sname
    return "0x%02X" % sub_code


def _type_name(type_code: int) -> str:
    for tname, tcode in TYPES.items():
        if tcode == type_code:
            return tname
    return "0x%02X" % type_code


def encode_table(rows: Sequence[Row], md5: bool = True) -> bytes:
    """Encode rows the way gen_esp32part.py does (MD5 entry on, 0xFF padding)."""
    out = b""
    for r in rows:
        name = r.name.encode("ascii")
        if len(name) > 16:
            raise ValueError("partition name too long: %r" % r.name)
        out += struct.pack(
            "<2sBBLL16sL", ENTRY_MAGIC, TYPES[r.type], _subtype_code(r.type, r.subtype), r.offset, r.size, name, r.flags
        )
    if md5:
        out += MD5_MAGIC + b"\xff" * 14 + hashlib.md5(out).digest()
    if len(out) > TABLE_MAX_LEN:
        raise ValueError("partition table too long")
    return out + b"\xff" * (TABLE_MAX_LEN - len(out))


@dataclass
class ParsedTable:
    rows: List[Row]
    md5_present: bool
    md5_ok: Optional[bool]
    errors: List[str]

    @property
    def ok(self) -> bool:
        return not self.errors


def parse_table(blob: bytes) -> ParsedTable:
    """Parse a partition table blob read from 0x8000.

    Never raises on bad input; problems are reported in ``errors``. Rows are
    returned as far as they could be decoded.
    """
    rows: List[Row] = []
    errors: List[str] = []
    md5_present = False
    md5_ok: Optional[bool] = None
    if len(blob) < ENTRY_LEN:
        return ParsedTable(rows, False, None, ["partition table blob is only %d bytes" % len(blob)])
    pos = 0
    end_marker_seen = False
    while pos + ENTRY_LEN <= min(len(blob), TABLE_MAX_LEN):
        entry = blob[pos : pos + ENTRY_LEN]
        magic = entry[:2]
        if magic == ENTRY_MAGIC:
            _, tcode, scode, offset, size, name, flags = struct.unpack("<2sBBLL16sL", entry)
            raw_name = name.split(b"\x00", 1)[0]
            try:
                text_name = raw_name.decode("ascii")
            except UnicodeDecodeError:
                text_name = raw_name.decode("latin-1")
                errors.append("entry %d has a non-ASCII name" % (pos // ENTRY_LEN))
            rows.append(Row(text_name, _type_name(tcode), _subtype_name(tcode, scode), offset, size, flags))
        elif magic == MD5_MAGIC:
            md5_present = True
            if entry[2:16] != b"\xff" * 14:
                errors.append("MD5 entry has unexpected padding")
            md5_ok = hashlib.md5(blob[:pos]).digest() == entry[16:32]
            if not md5_ok:
                errors.append("partition table MD5 does not match its entries (table corrupt)")
            pos += ENTRY_LEN
            end_marker_seen = True
            break
        elif entry == b"\xff" * ENTRY_LEN:
            end_marker_seen = True
            break
        else:
            errors.append("unexpected bytes at table entry %d: %s" % (pos // ENTRY_LEN, entry[:4].hex()))
            break
        pos += ENTRY_LEN
    if not end_marker_seen and not errors:
        errors.append("partition table has no end marker within 0x%X bytes" % TABLE_MAX_LEN)
    rest = blob[pos:TABLE_MAX_LEN]
    if end_marker_seen and rest.strip(b"\xff"):
        errors.append("non-0xFF bytes after the end of the partition table")
    if not rows:
        errors.append("partition table has no entries")
    return ParsedTable(rows, md5_present, md5_ok, errors)


def parse_csv(text: str) -> List[Row]:
    """Parse a gen_esp32part-style CSV (only what the two Picco tables use)."""
    rows = []
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        cells = [c.strip() for c in line.split(",")]
        name, ptype, subtype, offset, size = cells[:5]
        flags = cells[5] if len(cells) > 5 else ""
        rows.append(Row(name, ptype, subtype, int(offset, 0), _size(size), 1 if "encrypted" in flags else 0))
    return rows


def _size(text: str) -> int:
    t = text.strip().upper()
    if t.endswith("K"):
        return int(t[:-1], 0) * 1024
    if t.endswith("M"):
        return int(t[:-1], 0) * 1024 * 1024
    return int(t, 0)


def classify_table(rows: Sequence[Row]) -> str:
    """Return "stock", "two-slot" or "other"."""
    if tuple(rows) == STOCK_TABLE:
        return "stock"
    if tuple(rows) == TWO_SLOT_TABLE:
        return "two-slot"
    return "other"


def diff_rows(actual: Sequence[Row], expected: Sequence[Row]) -> List[str]:
    """Human-readable row-by-row differences."""
    out = []
    for i in range(max(len(actual), len(expected))):
        a = actual[i] if i < len(actual) else None
        e = expected[i] if i < len(expected) else None
        if a != e:
            out.append("row %d: found %s, expected %s" % (i + 1, _fmt_row(a), _fmt_row(e)))
    return out


def _fmt_row(r: Optional[Row]) -> str:
    if r is None:
        return "(nothing)"
    return "%s %s/%s 0x%X+0x%X flags=%d" % (r.name, r.type, r.subtype, r.offset, r.size, r.flags)


# --- otadata (boot_app0.bin) -------------------------------------------------

OTA_STATES = {
    0x0: "NEW",
    0x1: "PENDING_VERIFY",
    0x2: "VALID",
    0x3: "INVALID",
    0x4: "ABORTED",
    0xFFFFFFFF: "UNDEFINED",
}


def otadata_crc(seq: int) -> int:
    """ESP-IDF bootloader_common_ota_select_crc: crc32_le(UINT32_MAX, &ota_seq, 4)."""
    return binascii.crc32(struct.pack("<I", seq), 0xFFFFFFFF) & 0xFFFFFFFF


def boot_app0_bytes() -> bytes:
    """The 8 KiB otadata image the Arduino framework ships as boot_app0.bin.

    Record 0: ota_seq 1, label 0xFF, state 0xFFFFFFFF, valid CRC. Record 1:
    ota_seq 0, CRC 0xFFFFFFFF (which is the CRC of sequence 0, so the record is
    valid but loses to record 0's higher sequence). Everything else 0xFF. Its
    SHA-256 equals BOOT_APP0_SHA256 (checked by the tests).
    """
    rec0 = struct.pack("<I", 1) + b"\xff" * 20 + struct.pack("<II", 0xFFFFFFFF, otadata_crc(1))
    rec1 = struct.pack("<I", 0) + b"\xff" * 20 + struct.pack("<II", 0xFFFFFFFF, 0xFFFFFFFF)
    return rec0 + b"\xff" * (SECTOR - len(rec0)) + rec1 + b"\xff" * (SECTOR - len(rec1))


@dataclass
class OtaRecord:
    seq: int
    state: int
    crc: int
    crc_ok: bool

    @property
    def state_name(self) -> str:
        return OTA_STATES.get(self.state, "0x%08X" % self.state)


def parse_otadata(blob: bytes) -> List[OtaRecord]:
    recs = []
    for base in (0, SECTOR):
        chunk = blob[base : base + 32]
        if len(chunk) < 32:
            continue
        seq = struct.unpack("<I", chunk[0:4])[0]
        state, crc = struct.unpack("<II", chunk[24:32])
        recs.append(OtaRecord(seq, state, crc, crc == otadata_crc(seq) and seq != 0xFFFFFFFF))
    return recs


def otadata_boot_slot(blob: bytes, slot_count: int) -> Optional[int]:
    """Which ota_N the ESP-IDF bootloader would pick, or None (no valid record).

    Same rule as bootloader_utility.c: the valid record with the highest
    sequence number wins and maps to slot (seq - 1) % slot_count. Records in
    state INVALID or ABORTED are skipped.
    """
    best = None
    for r in parse_otadata(blob):
        if not r.crc_ok or r.state in (0x3, 0x4):
            continue
        if best is None or r.seq > best.seq:
            best = r
    if best is None:
        return None
    return (best.seq - 1) % slot_count
