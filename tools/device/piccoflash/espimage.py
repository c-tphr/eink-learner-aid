"""Minimal parser for ESP32-S3 firmware images (bootloader.bin, firmware.bin).

Written from the published format (esptool documentation, "Firmware Image
Format"); it does not use esptool. The guard uses it to check that a file is
exactly one well-formed ESP image of the right chip, so a merged
``firmware.factory.bin`` or a truncated download is refused.

Layout::

    0      magic 0xE9
    1      segment count
    2      SPI flash mode (0 QIO, 1 QOUT, 2 DIO, 3 DOUT)
    3      high nibble flash size, low nibble flash frequency
    4..7   entry point
    8..23  extended header: WP pin, drive settings, chip id (u16 at 12),
           min rev (14), min/max rev full (15..18), reserved, hash-appended (23)
    24..   segments: load address u32, length u32, data
    then   zero padding so that the checksum byte ends on a 16-byte boundary;
           the checksum is 0xEF XOR every segment data byte
    then   if hash-appended: SHA-256 of everything before it (32 bytes)
"""

from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

ESP_IMAGE_MAGIC = 0xE9
CHIP_ID_ESP32S3 = 9
HEADER_LEN = 24
MAX_SEGMENTS = 16

FLASH_MODES = {0: "QIO", 1: "QOUT", 2: "DIO", 3: "DOUT"}
FLASH_SIZES = {0: "1MB", 1: "2MB", 2: "4MB", 3: "8MB", 4: "16MB", 5: "32MB", 6: "64MB", 7: "128MB"}
# ESP32-S3 frequency codes (esptool targets/esp32s3.py FLASH_FREQUENCY).
FLASH_FREQS_S3 = {0x0: "40m", 0x1: "26m", 0x2: "20m", 0xF: "80m"}


@dataclass
class ImageInfo:
    magic_ok: bool = False
    segment_count: int = 0
    flash_mode: Optional[str] = None
    flash_size: Optional[str] = None
    flash_freq: Optional[str] = None
    chip_id: Optional[int] = None
    min_rev_full: Optional[int] = None
    max_rev_full: Optional[int] = None
    hash_appended: bool = False
    segments: List[Tuple[int, int]] = field(default_factory=list)  # (load address, length)
    image_length: Optional[int] = None  # bytes the image occupies, incl. checksum and hash
    checksum_ok: Optional[bool] = None
    sha256_ok: Optional[bool] = None
    errors: List[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return not self.errors

    def summary(self) -> dict:
        return {
            "magic_ok": self.magic_ok,
            "segments": self.segment_count,
            "flash_mode": self.flash_mode,
            "flash_size": self.flash_size,
            "flash_freq": self.flash_freq,
            "chip_id": self.chip_id,
            "min_rev_full": self.min_rev_full,
            "max_rev_full": self.max_rev_full,
            "hash_appended": self.hash_appended,
            "image_length": self.image_length,
            "checksum_ok": self.checksum_ok,
            "sha256_ok": self.sha256_ok,
            "errors": list(self.errors),
        }


# For each bit position, a table mapping a byte to 1 if that bit is set.
_BIT_TABLES = [bytes((b >> bit) & 1 for b in range(256)) for bit in range(8)]


def xor_all(data: bytes) -> int:
    """XOR of every byte, computed per bit position at C speed."""
    out = 0
    for bit, table in enumerate(_BIT_TABLES):
        if data.translate(table).count(1) & 1:
            out |= 1 << bit
    return out


def parse_image(data: bytes) -> ImageInfo:
    """Parse ``data`` as one ESP image. Never raises; see ``errors``."""
    info = ImageInfo()
    if len(data) < HEADER_LEN:
        info.errors.append("too short for an ESP image header (%d bytes)" % len(data))
        return info
    magic, count, mode, size_freq, _entry = struct.unpack("<BBBBI", data[:8])
    info.magic_ok = magic == ESP_IMAGE_MAGIC
    if not info.magic_ok:
        info.errors.append("first byte is 0x%02X, not the ESP image magic 0xE9" % magic)
        return info
    info.segment_count = count
    info.flash_mode = FLASH_MODES.get(mode, "0x%02X" % mode)
    info.flash_size = FLASH_SIZES.get(size_freq >> 4, "0x%X" % (size_freq >> 4))
    info.flash_freq = FLASH_FREQS_S3.get(size_freq & 0x0F, "0x%X" % (size_freq & 0x0F))
    info.chip_id = struct.unpack("<H", data[12:14])[0]
    info.min_rev_full, info.max_rev_full = struct.unpack("<HH", data[15:19])
    info.hash_appended = data[23] == 1
    if count == 0 or count > MAX_SEGMENTS:
        info.errors.append("implausible segment count %d" % count)
        return info
    pos = HEADER_LEN
    checksum = 0xEF
    for i in range(count):
        if pos + 8 > len(data):
            info.errors.append("segment %d header runs past the end of the file" % i)
            return info
        load, length = struct.unpack("<II", data[pos : pos + 8])
        pos += 8
        if pos + length > len(data):
            info.errors.append("segment %d (%d bytes) runs past the end of the file" % (i, length))
            return info
        checksum ^= xor_all(data[pos : pos + length])
        info.segments.append((load, length))
        pos += length
    # Padding: the checksum byte sits at the last byte of a 16-byte block.
    checksum_pos = pos + (15 - (pos % 16))
    if checksum_pos >= len(data):
        info.errors.append("file ends before the image checksum")
        return info
    info.checksum_ok = data[checksum_pos] == checksum
    if not info.checksum_ok:
        info.errors.append("image checksum mismatch")
    end = checksum_pos + 1
    if info.hash_appended:
        if end + 32 > len(data):
            info.errors.append("file ends before the appended SHA-256")
            return info
        info.sha256_ok = hashlib.sha256(data[:end]).digest() == data[end : end + 32]
        if not info.sha256_ok:
            info.errors.append("appended SHA-256 does not match the image")
        end += 32
    info.image_length = end
    return info


def signature_block_after(data: bytes, image_length: int) -> bool:
    """True if a Secure Boot V2 signature sector follows the image.

    The signature block starts at the next 4 KiB boundary after the image and
    begins with magic 0xE7 followed by a version byte (0x02 RSA, 0x03 ECDSA).
    """
    pos = (image_length + 0xFFF) & ~0xFFF
    return pos + 2 <= len(data) and data[pos] == 0xE7 and data[pos + 1] in (0x02, 0x03)
