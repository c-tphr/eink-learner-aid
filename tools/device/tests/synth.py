"""Synthetic but well-formed images and a synthetic stock Picco flash, for tests and examples.

Nothing here comes from a real device or a real build. The ESP image layout
follows esptool's documented "Firmware Image Format"; test_espimage.py
cross-checks these images with esptool's own parser.
"""

from __future__ import annotations

import functools
import hashlib
import os
import random
import struct
from typing import Dict, List, Tuple

from piccoflash import layout
from piccoflash.espimage import xor_all

DROM = 0x3C000020
IROM = 0x42000020
DRAM = 0x3FC88000 + 0x5A810  # 0x3FCE2810, inside the S3 DRAM range
IRAM = 0x403C8700


def esp_image(segments: List[Tuple[int, bytes]], chip_id: int = 9, mode: int = 2, size_freq: int = 0x4F,
              hash_appended: bool = True, entry: int = 0x403C8924) -> bytes:
    out = struct.pack("<BBBBI", 0xE9, len(segments), mode, size_freq, entry)
    out += struct.pack("<B3sHBHH4sB", 0xEE, b"\x00\x00\x00", chip_id, 0, 0, 99, b"\x00" * 4, 1 if hash_appended else 0)
    checksum = 0xEF
    for load, data in segments:
        assert len(data) % 4 == 0
        out += struct.pack("<II", load, len(data)) + data
        checksum ^= xor_all(data)
    out += b"\x00" * (15 - len(out) % 16)
    out += bytes([checksum])
    if hash_appended:
        out += hashlib.sha256(out).digest()
    return out


def _rand(rng: random.Random, n: int) -> bytes:
    return bytes(rng.getrandbits(8) for _ in range(n)) if n < 4096 else rng.randbytes(n)


def bootloader(seed: int = 1, idf: bytes = b"v5.5.5", length: int = 18720) -> bytes:
    """A bootloader-shaped image: DRAM segment starting with the bootloader description."""
    rng = random.Random(seed)
    desc = struct.pack("<B3sI32s24s16s", 0x50, b"\x00" * 3, 1, idf, b"Nov 18 2026 00:00:00", b"\x00" * 16)
    dram = desc + _rand(rng, 0x2F0 - len(desc))
    overhead = 24 + 8 + len(dram) + 8 + 16 + 32
    iram_len = max(4, (length - overhead) // 4 * 4)
    img = esp_image([(DRAM, dram), (IRAM, _rand(rng, iram_len))])
    return img


def app(size: int = 600_000, seed: int = 2, name: bytes = b"crosspoint") -> bytes:
    """An application-shaped image: DROM segment starting with esp_app_desc_t."""
    rng = random.Random(seed)
    desc = struct.pack("<II8s32s32s16s16s32s32sHHB3s72s", 0xABCD5432, 0, b"\x00" * 8, b"rsvp-test", name,
                       b"00:00:00", b"Nov 18 2026", b"v5.5.5", b"\x11" * 32, 0, 99, 16, b"\x00" * 3, b"\x00" * 72)
    rest = max(1024, size - len(desc) - 24 - 3 * 8 - 48)
    drom = desc + _rand(rng, (rest // 3) // 4 * 4)
    irom = _rand(rng, (rest // 3) // 4 * 4)
    dram = _rand(rng, (rest - len(drom) - len(irom) + len(desc)) // 4 * 4)
    return esp_image([(DROM, drom), (IROM, irom), (DRAM, dram)], entry=0x403C98D4)


def kit_sources(directory: str, app_size: int = 900_000) -> Dict[str, str]:
    """Write the four synthetic images; return role -> path (make-plan input)."""
    os.makedirs(directory, exist_ok=True)
    files = {
        "bootloader": ("bootloader.bin", bootloader()),
        "partition_table": ("partitions.bin", layout.encode_table(layout.TWO_SLOT_TABLE)),
        "boot_app0": ("boot_app0.bin", layout.boot_app0_bytes()),
        "app": ("firmware.bin", app(app_size)),
    }
    out = {}
    for role, (name, data) in files.items():
        p = os.path.join(directory, name)
        with open(p, "wb") as fp:
            fp.write(data)
        out[role] = p
    return out


def stock_flash(seed: int = 7) -> bytearray:
    """A synthetic 16 MB flash laid out like a stock Picco (a fresh copy each call)."""
    return bytearray(_stock_flash(seed))


@functools.lru_cache(maxsize=4)
def _stock_flash(seed: int) -> bytes:
    rng = random.Random(seed)
    f = bytearray(b"\xff" * layout.FLASH_SIZE)

    def put(off: int, data: bytes) -> None:
        f[off:off + len(data)] = data

    put(0x0, bootloader(seed=101, idf=b"v4.4-onyx", length=22000))
    put(0x8000, layout.encode_table(layout.STOCK_TABLE))
    put(0x9000, rng.randbytes(0xC000))  # Onyx NVS data
    rec = struct.pack("<I", 3) + b"\xff" * 20 + struct.pack("<II", 2, layout.otadata_crc(3))
    put(0x15000, rec)
    put(0x20000, app(700_000, seed=102, name=b"onyx-safeboot"))
    put(0xE0000, app(3_000_000, seed=103, name=b"onyx"))
    put(0xF50000, rng.randbytes(0x80000))  # spiffs
    return bytes(f)
