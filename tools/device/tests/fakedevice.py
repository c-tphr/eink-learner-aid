"""An in-memory 16 MB ESP32-S3 for tests, with injectable faults.

FakeChip is the persistent device (flash contents, identity, eFuses, faults);
FakeDevice is one connection to it, made by ``chip.factory(port, ui)``, the
same signature as the real driver's factory. Writes behave like esptool's:
the guard's ``recheck`` runs first, then every 4 KiB sector the write touches
is erased to 0xFF, then the data is written.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional, Tuple

from piccoflash import guard, layout
from piccoflash.device import ChipInfo, Device, DeviceError, DeviceNotReached, SecurityInfo


@dataclass
class Faults:
    unreachable: bool = False
    # Flip one byte in the Nth full-flash read (1-based), at this offset.
    flip_full_read: Optional[Tuple[int, int]] = None
    # Raise after writing this many bytes of the write at this offset.
    fail_write_at: Optional[Tuple[int, int]] = None
    # After a successful write at this offset, corrupt one byte (read-back mismatch).
    corrupt_after_write_at: Optional[int] = None
    # Disconnect (DeviceError) on the Nth read call overall (1-based).
    disconnect_on_read: Optional[int] = None
    # Disconnect on any write.
    disconnect_on_write: bool = False
    # Called before each write with (chip, approved): lets a test change files mid-flash.
    before_write_hook: Optional[object] = None


@dataclass
class FakeChip:
    flash: bytearray
    mac: str = "24:58:7c:12:34:56"
    chip_name: str = "ESP32-S3"
    flash_size: str = "16MB"
    flash_type: str = "quad"
    secure_boot: bool = False
    efuse_secure_boot: bool = False
    flash_crypt_cnt: int = 0
    secure_download_mode: bool = False
    dis_download_icache: bool = False
    key_purposes: List[int] = field(default_factory=lambda: [0, 0, 0, 0, 0, 0, 0])
    faults: Faults = field(default_factory=Faults)
    ops: List[tuple] = field(default_factory=list)  # every operation, in order
    full_reads: int = 0
    reads: int = 0
    connections: int = 0

    def factory(self, port, ui=None) -> "FakeDevice":
        return FakeDevice(self, port)

    @property
    def writes(self) -> List[tuple]:
        return [op for op in self.ops if op[0] == "write"]


class FakeDevice(Device):
    def __init__(self, chip: FakeChip, port: str):
        self.chip = chip
        self.port = port
        self.connected = False

    def connect(self) -> None:
        self.chip.ops.append(("connect", self.port))
        if self.chip.faults.unreachable:
            raise DeviceNotReached("Failed to connect to ESP32-S3: No serial data received.")
        self.connected = True
        self.chip.connections += 1

    def close(self) -> None:
        self.chip.ops.append(("close",))
        self.connected = False

    def _need(self):
        if not self.connected:
            raise DeviceError("not connected")

    def chip_info(self) -> ChipInfo:
        self._need()
        c = self.chip
        self.chip.ops.append(("chip_info",))
        return ChipInfo(chip_name=c.chip_name, description="%s (QFN56) (revision v0.2)" % c.chip_name, mac=c.mac,
                        features=["Wi-Fi", "BT 5 (LE)", "Dual Core + LP Core", "240MHz",
                                  "Embedded PSRAM 8MB (AP_1v8)"],
                        crystal_mhz=40, flash_manufacturer=0x20, flash_device=0x4018, flash_size=c.flash_size,
                        flash_type_efuse=c.flash_type, psram="Embedded PSRAM 8MB (AP_1v8)",
                        usb_mode="USB-Serial/JTAG", stub=True)

    def security_info(self) -> SecurityInfo:
        self._need()
        c = self.chip
        self.chip.ops.append(("security_info",))
        return SecurityInfo(flags=(1 if c.secure_boot else 0) | (4 if c.secure_download_mode else 0),
                            flash_crypt_cnt=c.flash_crypt_cnt, key_purposes=list(c.key_purposes),
                            secure_boot_enabled=c.secure_boot, secure_download_mode=c.secure_download_mode,
                            dis_download_icache=c.dis_download_icache, dis_download_dcache=False, dis_usb=False,
                            efuse_secure_boot_enabled=c.efuse_secure_boot,
                            efuse_flash_encryption_enabled=bin(c.flash_crypt_cnt).count("1") % 2 == 1, chip_id=9)

    def read(self, offset: int, length: int, progress=None) -> bytes:
        self._need()
        c = self.chip
        c.reads += 1
        c.ops.append(("read", offset, length))
        if c.faults.disconnect_on_read == c.reads:
            raise DeviceError("The device disconnected (simulated)")
        if offset < 0 or offset + length > len(c.flash):
            raise DeviceError("read out of range")
        data = bytearray(c.flash[offset:offset + length])
        if offset == 0 and length == layout.FLASH_SIZE:
            c.full_reads += 1
            flip = c.faults.flip_full_read
            if flip and flip[0] == c.full_reads:
                data[flip[1]] ^= 0x01
        if progress:
            progress(length, length)
        return bytes(data)

    def write(self, approved) -> None:
        self._need()
        guard.recheck(approved)
        c = self.chip
        if c.faults.before_write_hook:
            c.faults.before_write_hook(c, approved)
        c.ops.append(("write", approved.offset, len(approved.data)))
        if c.faults.disconnect_on_write:
            raise DeviceError("The device disconnected during the write (simulated)")
        start, end = guard.touched_span(approved.offset, len(approved.data))
        c.flash[start:end] = b"\xff" * (end - start)
        data = approved.data
        fail = c.faults.fail_write_at
        if fail and fail[0] == approved.offset:
            n = fail[1]
            c.flash[approved.offset:approved.offset + n] = data[:n]
            raise DeviceError("Serial data stream stopped: Possible serial noise or corruption (simulated)")
        c.flash[approved.offset:approved.offset + len(data)] = data
        if c.faults.corrupt_after_write_at == approved.offset:
            c.flash[approved.offset + len(data) // 2] ^= 0xFF

    def hard_reset(self) -> None:
        self._need()
        self.chip.ops.append(("reset",))
