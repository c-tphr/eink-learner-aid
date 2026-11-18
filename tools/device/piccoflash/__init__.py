"""piccoflash: guarded identify, backup, flash, monitor and restore for the BOOX Picco.

Run as ``python -m piccoflash <command>``. See tools/device/README.md.
"""

__version__ = "1.0.0"

# The esptool release every behaviour in this package was checked against
# (tools/device/requirements.txt pins it with hashes). The real device driver
# refuses to run with any other version.
PINNED_ESPTOOL_VERSION = "5.6.0"
PINNED_PYSERIAL_VERSION = "3.5"
