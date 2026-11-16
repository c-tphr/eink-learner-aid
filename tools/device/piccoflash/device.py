"""The one interface through which piccoflash touches the device.

Two implementations exist: ``esptool_device.EsptoolDevice`` (the real chip,
through the pinned esptool's Python API) and the in-memory fake used by the
tests (tests/fakedevice.py). Command code never imports esptool directly.

Contract for implementations:

* ``write`` takes a ``guard.ApprovedWrite`` and must call ``guard.recheck`` on
  it immediately before writing; it writes exactly ``approved.data`` at
  ``approved.offset`` with flash mode, frequency and size kept as they are.
* Nothing else in the interface can change flash contents.
* ``read`` returns exactly ``length`` bytes or raises ``DeviceError``.
"""

from __future__ import annotations

import abc
from dataclasses import asdict, dataclass, field
from typing import Callable, Dict, List, Optional

ProgressFn = Optional[Callable[[int, int], None]]


class DeviceError(Exception):
    """Communication with the chip failed (nothing is known about the write state)."""


class DeviceNotReached(DeviceError):
    """Could not connect at all. Nothing was read or written."""


@dataclass
class ChipInfo:
    chip_name: str  # esptool CHIP_NAME, e.g. "ESP32-S3"
    description: str  # e.g. "ESP32-S3 (QFN56) (revision v0.2)"
    mac: str  # "aa:bb:cc:dd:ee:ff"
    features: List[str] = field(default_factory=list)
    crystal_mhz: Optional[int] = None
    flash_manufacturer: Optional[int] = None
    flash_device: Optional[int] = None
    flash_size: Optional[str] = None  # from the flash chip's JEDEC id, e.g. "16MB"
    flash_type_efuse: Optional[str] = None  # "quad", "octal" or None if unknown
    psram: Optional[str] = None
    usb_mode: Optional[str] = None  # "USB-Serial/JTAG", "USB-OTG", "UART bridge"
    stub: bool = False

    def as_dict(self) -> dict:
        return asdict(self)


@dataclass
class SecurityInfo:
    flags: int
    flash_crypt_cnt: int
    key_purposes: List[int]
    secure_boot_enabled: bool  # from the ROM's GET_SECURITY_INFO flags
    secure_download_mode: bool
    dis_download_icache: bool
    dis_download_dcache: bool
    dis_usb: bool
    efuse_secure_boot_enabled: Optional[bool] = None  # second source: the eFuse register
    efuse_flash_encryption_enabled: Optional[bool] = None
    chip_id: Optional[int] = None
    raw: Dict[str, object] = field(default_factory=dict)

    @property
    def flash_encryption_enabled(self) -> bool:
        return bin(self.flash_crypt_cnt).count("1") % 2 == 1

    def as_dict(self) -> dict:
        d = asdict(self)
        d["flash_encryption_enabled"] = self.flash_encryption_enabled
        return d


class Device(abc.ABC):
    """A connection to one chip. Use as a context manager."""

    port: str = ""

    def __enter__(self) -> "Device":
        self.connect()
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    @abc.abstractmethod
    def connect(self) -> None:
        """Reset the chip into ROM download mode and connect (raises DeviceNotReached)."""

    @abc.abstractmethod
    def close(self) -> None:
        """Close the port. Leaves the chip in whatever state it is in."""

    @abc.abstractmethod
    def chip_info(self) -> ChipInfo:
        ...

    @abc.abstractmethod
    def security_info(self) -> SecurityInfo:
        ...

    @abc.abstractmethod
    def read(self, offset: int, length: int, progress: ProgressFn = None) -> bytes:
        ...

    @abc.abstractmethod
    def write(self, approved) -> None:
        """Write a guard.ApprovedWrite. Must call guard.recheck(approved) first."""

    @abc.abstractmethod
    def hard_reset(self) -> None:
        """Reset the chip so it boots from flash."""


class MonitorPort(abc.ABC):
    """Read-only serial access for ``monitor``."""

    @abc.abstractmethod
    def open(self) -> None:
        ...

    @abc.abstractmethod
    def read(self) -> bytes:
        """Return available bytes (possibly empty). Raises OSError if the port vanished."""

    @abc.abstractmethod
    def close(self) -> None:
        ...

    @abc.abstractmethod
    def pulse_reset(self) -> None:
        """Toggle the reset control line (no data is sent)."""
