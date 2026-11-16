"""Read an EPUB's zip container and write a copy with a few members changed.

``zipfile`` cannot copy an entry without recompressing it, so this module
reads the central directory itself. ``rebuild`` copies every untouched entry
byte for byte (local header, extra fields, compressed data and data
descriptor), rewrites only the entries it is given, and appends new ones.
Entry order, compression methods, timestamps, attributes, extra fields and
the archive comment are kept.

Not supported (refused with ``ZipFormatError``): ZIP64, encrypted entries,
multi-disk archives, and rewriting an entry whose method is not stored or
deflate (such entries can still be copied unchanged).
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

LOCAL_SIG = 0x04034B50
CENTRAL_SIG = 0x02014B50
EOCD_SIG = 0x06054B50
DESCRIPTOR_SIG = 0x08074B50
ZIP64_EOCD_LOCATOR_SIG = 0x07064B50

STORED = 0
DEFLATED = 8
FLAG_ENCRYPTED = 0x0001
FLAG_DATA_DESCRIPTOR = 0x0008
FLAG_UTF8 = 0x0800

_LOCAL = struct.Struct("<IHHHHHIIIHH")  # 30 bytes
_CENTRAL = struct.Struct("<IHHHHHHIIIHHHHHII")  # 46 bytes
_EOCD = struct.Struct("<IHHHHIIH")  # 22 bytes


class ZipFormatError(ValueError):
    """The archive cannot be read or rewritten safely."""


@dataclass(frozen=True)
class Entry:
    name: str
    flags: int
    method: int
    dos_time: int
    dos_date: int
    crc: int
    compressed_size: int
    size: int
    local_offset: int
    data_offset: int  # first byte of the compressed data
    end_offset: int  # one past the data descriptor, or past the data
    central_record: bytes  # the whole central directory record


class Archive:
    """A parsed zip archive held in memory."""

    def __init__(self, data: bytes):
        self.data = data
        self.comment = b""
        self.entries: list[Entry] = []
        self._parse()
        self.by_name = {e.name: e for e in self.entries}
        if len(self.by_name) != len(self.entries):
            raise ZipFormatError("the archive has two entries with the same name")

    # -- reading ----------------------------------------------------------

    def _find_eocd(self) -> int:
        data = self.data
        start = max(0, len(data) - (_EOCD.size + 0xFFFF))
        pos = data.rfind(struct.pack("<I", EOCD_SIG), start)
        while pos >= 0:
            if pos + _EOCD.size <= len(data):
                comment_len = struct.unpack_from("<H", data, pos + 20)[0]
                if pos + _EOCD.size + comment_len == len(data):
                    return pos
            pos = data.rfind(struct.pack("<I", EOCD_SIG), start, pos)
        raise ZipFormatError("not a zip archive (no end of central directory record)")

    def _parse(self) -> None:
        data = self.data
        eocd = self._find_eocd()
        _, disk, cd_disk, n_disk, n_total, cd_size, cd_offset, comment_len = _EOCD.unpack_from(data, eocd)
        if eocd >= 20 and struct.unpack_from("<I", data, eocd - 20)[0] == ZIP64_EOCD_LOCATOR_SIG:
            raise ZipFormatError("ZIP64 archives are not supported")
        if disk != 0 or cd_disk != 0 or n_disk != n_total:
            raise ZipFormatError("multi-disk archives are not supported")
        if 0xFFFF in (n_total,) or 0xFFFFFFFF in (cd_size, cd_offset):
            raise ZipFormatError("ZIP64 archives are not supported")
        if cd_offset + cd_size > eocd:
            raise ZipFormatError("central directory lies outside the archive")
        self.comment = data[eocd + _EOCD.size : eocd + _EOCD.size + comment_len]
        pos = cd_offset
        for _ in range(n_total):
            if pos + _CENTRAL.size > len(data):
                raise ZipFormatError("truncated central directory")
            (sig, _made_by, _needed, flags, method, dos_time, dos_date, crc, csize, usize,
             name_len, extra_len, comment_len, _disk_start, _iattr, _eattr, offset) = _CENTRAL.unpack_from(data, pos)
            if sig != CENTRAL_SIG:
                raise ZipFormatError(f"bad central directory record at byte {pos}")
            record_end = pos + _CENTRAL.size + name_len + extra_len + comment_len
            raw_name = data[pos + _CENTRAL.size : pos + _CENTRAL.size + name_len]
            name = raw_name.decode("utf-8" if flags & FLAG_UTF8 else "cp437")
            if 0xFFFFFFFF in (csize, usize, offset):
                raise ZipFormatError(f"{name}: ZIP64 entries are not supported")
            if flags & FLAG_ENCRYPTED:
                raise ZipFormatError(f"{name}: encrypted entries are not supported")
            if offset + _LOCAL.size > len(data):
                raise ZipFormatError(f"{name}: local header outside the archive")
            (lsig, _v, _f, _m, _t, _d, _c, _cs, _us, lname_len, lextra_len) = _LOCAL.unpack_from(data, offset)
            if lsig != LOCAL_SIG:
                raise ZipFormatError(f"{name}: bad local header at byte {offset}")
            data_offset = offset + _LOCAL.size + lname_len + lextra_len
            end = data_offset + csize
            if flags & FLAG_DATA_DESCRIPTOR:
                has_sig = struct.unpack_from("<I", data, end)[0] == DESCRIPTOR_SIG if end + 4 <= len(data) else False
                end += 16 if has_sig else 12
            if end > len(data):
                raise ZipFormatError(f"{name}: data runs past the end of the archive")
            self.entries.append(
                Entry(name, flags, method, dos_time, dos_date, crc, csize, usize, offset, data_offset, end,
                      data[pos:record_end])
            )
            pos = record_end

    def names(self) -> list[str]:
        return [e.name for e in self.entries]

    def raw(self, name: str) -> bytes:
        """The compressed bytes of an entry."""
        e = self.by_name[name]
        return self.data[e.data_offset : e.data_offset + e.compressed_size]

    def read(self, name: str) -> bytes:
        """The uncompressed bytes of an entry, CRC-checked."""
        e = self.by_name.get(name)
        if e is None:
            raise KeyError(name)
        raw = self.raw(name)
        if e.method == STORED:
            out = raw
        elif e.method == DEFLATED:
            try:
                out = zlib.decompress(raw, -15)
            except zlib.error as exc:
                raise ZipFormatError(f"{name}: cannot inflate: {exc}") from exc
        else:
            raise ZipFormatError(f"{name}: compression method {e.method} is not supported")
        if len(out) != e.size or (zlib.crc32(out) & 0xFFFFFFFF) != e.crc:
            raise ZipFormatError(f"{name}: CRC or size mismatch")
        return out

    # -- writing ----------------------------------------------------------

    def rebuild(self, replace: dict[str, bytes], add: list[tuple[str, bytes, str]]) -> bytes:
        """Return a new archive.

        ``replace`` maps existing entry names to new uncompressed contents.
        ``add`` lists ``(name, contents, template)`` for new entries, appended
        after every existing entry; each copies the method, timestamp and
        attributes of the existing entry ``template``.
        """
        unknown = set(replace) - set(self.by_name)
        if unknown:
            raise KeyError(f"cannot replace missing entries {sorted(unknown)}")
        for name, _, template in add:
            if name in self.by_name:
                raise ZipFormatError(f"{name} already exists in the archive")
            if template not in self.by_name:
                raise KeyError(template)
        out = bytearray()
        central: dict[str, bytes] = {}
        for e in sorted(self.entries, key=lambda e: e.local_offset):
            offset = len(out)
            if e.name in replace:
                local, record = _rewrite(e, self.data, replace[e.name], offset)
                out += local
            else:
                out += self.data[e.local_offset : e.end_offset]
                record = e.central_record[:42] + struct.pack("<I", offset) + e.central_record[46:]
            central[e.name] = record
        records = [central[e.name] for e in self.entries]
        for name, contents, template in add:
            local, record = _new_entry(self.by_name[template], name, contents, len(out))
            out += local
            records.append(record)
        cd_offset = len(out)
        for record in records:
            out += record
        cd_size = len(out) - cd_offset
        if len(records) > 0xFFFE or cd_offset > 0xFFFFFFFE:
            raise ZipFormatError("the rebuilt archive would need ZIP64")
        out += _EOCD.pack(EOCD_SIG, 0, 0, len(records), len(records), cd_size, cd_offset, len(self.comment))
        out += self.comment
        return bytes(out)


def _compress(method: int, contents: bytes, name: str) -> bytes:
    if method == STORED:
        return contents
    if method == DEFLATED:
        c = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
        return c.compress(contents) + c.flush()
    raise ZipFormatError(f"{name}: cannot write compression method {method}")


def _rewrite(e: Entry, data: bytes, contents: bytes, offset: int) -> tuple[bytes, bytes]:
    """New local header + data and central record for a replaced entry."""
    (sig, version, flags, method, dos_time, dos_date, _crc, _cs, _us, name_len, extra_len) = _LOCAL.unpack_from(
        data, e.local_offset
    )
    name_and_extra = data[e.local_offset + _LOCAL.size : e.local_offset + _LOCAL.size + name_len + extra_len]
    compressed = _compress(e.method, contents, e.name)
    crc = zlib.crc32(contents) & 0xFFFFFFFF
    flags &= ~FLAG_DATA_DESCRIPTOR
    local = _LOCAL.pack(sig, version, flags, method, dos_time, dos_date, crc, len(compressed), len(contents),
                        name_len, extra_len) + name_and_extra + compressed
    rec = bytearray(e.central_record)
    central_flags = struct.unpack_from("<H", rec, 8)[0] & ~FLAG_DATA_DESCRIPTOR
    struct.pack_into("<H", rec, 8, central_flags)
    struct.pack_into("<III", rec, 16, crc, len(compressed), len(contents))
    struct.pack_into("<I", rec, 42, offset)
    return local, bytes(rec)


def _new_entry(template: Entry, name: str, contents: bytes, offset: int) -> tuple[bytes, bytes]:
    raw_name = name.encode("ascii")
    compressed = _compress(template.method, contents, name)
    crc = zlib.crc32(contents) & 0xFFFFFFFF
    made_by, needed = struct.unpack_from("<HH", template.central_record, 4)
    internal_attr, external_attr = struct.unpack_from("<HI", template.central_record, 36)
    local = _LOCAL.pack(LOCAL_SIG, needed, 0, template.method, template.dos_time, template.dos_date, crc,
                        len(compressed), len(contents), len(raw_name), 0) + raw_name + compressed
    record = _CENTRAL.pack(CENTRAL_SIG, made_by, needed, 0, template.method, template.dos_time, template.dos_date,
                           crc, len(compressed), len(contents), len(raw_name), 0, 0, 0, internal_attr,
                           external_attr, offset) + raw_name
    return local, record
