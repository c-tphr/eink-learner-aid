# First extra script of [env:picco_2slot] (platformio.project.ini; task D4, docs/TWO_SLOT.md).
#
# 1. Refuses every target that would talk to a device (flash, erase, read back, debug-load), before
#    the platform script runs, so no serial port is opened and no tool is started. The picco_2slot
#    images are written once, by the guarded tool in tools/device/ (session.ps1 / piccoflash), and
#    afterwards only by the firmware's own SD-card or Wi-Fi update. scripts/picco_2slot_post.py
#    replaces the upload and erase commands as a second line of defence.
# 2. Fails the build if [env:picco_2slot] build_flags drift from [env:picco] in anything but the
#    CROSSPOINT_VERSION define, so the two Picco builds stay the same firmware.
Import("env")  # noqa: F821  (SCons)

import sys

ENV_NAME = "picco_2slot"
BASE_ENV = "env:picco"
DEVICE_TARGETS = {
    "upload",
    "uploadfs",
    "uploadfsota",
    "erase",
    "erase_upload",
    "download_fs",
    "coredump",
    "coredump-only",
    "__debug",
}
REFUSAL = (
    "\n*** picco_2slot has no upload, erase or debug target. ***\n"
    "This build is flashed exactly once, with the guarded tool in tools/device/\n"
    "(session.ps1 flash, from the kit made by scripts/make_device_kit.py), and later updated\n"
    "from the device itself (SD card or Wi-Fi). Nothing was built, no serial port was opened\n"
    "and no tool was started. See docs/TWO_SLOT.md.\n"
)

from SCons.Script import COMMAND_LINE_TARGETS  # noqa: E402

requested = sorted(set(COMMAND_LINE_TARGETS) & DEVICE_TARGETS)
if requested:
    sys.stderr.write(REFUSAL)
    sys.stderr.write("Refused target(s): %s\n" % ", ".join(requested))
    env.Exit(1)  # noqa: F821


def _without_version(flags):
    return [f for f in flags if not f.replace(" ", "").startswith("-DCROSSPOINT_VERSION=")]


config = env.GetProjectConfig()  # noqa: F821
base_flags = _without_version(config.get(BASE_ENV, "build_flags"))
own_flags = _without_version(config.get("env:" + ENV_NAME, "build_flags"))
missing = [f for f in base_flags if f not in own_flags]
extra = [f for f in own_flags if f not in base_flags]
if missing or extra:
    sys.stderr.write(
        "picco_2slot build_flags differ from [%s] beyond CROSSPOINT_VERSION:\n  missing: %s\n  extra: %s\n"
        "Keep the two lists the same (platformio.project.ini).\n" % (BASE_ENV, missing, extra)
    )
    env.Exit(1)  # noqa: F821
