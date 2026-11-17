# Last extra script of [env:picco_2slot] (platformio.project.ini; task D4, docs/TWO_SLOT.md).
#
# Runs after the platform script, so it sees what the build really resolved:
# 1. The extra flash images must be exactly 0x0 bootloader.bin, 0x8000 partitions.bin and
#    0x15000 boot_app0.bin (never the framework default 0xE000, inside the stock NVS), and the
#    partition table must be partitions_picco_2slot.csv with app0 (ota_0) at 0x20000. Otherwise
#    the build fails here, long before anything reaches a device.
# 2. The upload and erase commands are replaced by a refusal, and every device target gets a
#    refusing pre-action (scripts/picco_2slot_pre.py already stops them at start; this is the
#    second line). UPLOADERFLAGS is left as the platform built it, so `pio run -e picco_2slot -t
#    envdump` still shows the offsets a stock upload would have written.
# 3. After firmware.bin is written, the build fails if it does not fit the 0x7E0000-byte slot.
#    The CI gate (slot minus 10%) is .github/scripts/firmware-size.sh --gate.
Import("env")  # noqa: F821  (SCons)

import os
import sys

SLOT_SIZE = 0x7E0000
BUDGET = SLOT_SIZE * 9 // 10  # 7,431,782 bytes, the CI gate
EXPECTED_TABLE = "partitions_picco_2slot.csv"
EXPECTED_APP_OFFSET = 0x20000
EXPECTED_EXTRA_IMAGES = [(0x0, "bootloader.bin"), (0x8000, "partitions.bin"), (0x15000, "boot_app0.bin")]
DEVICE_TARGETS = (
    "upload",
    "uploadfs",
    "uploadfsota",
    "erase",
    "erase_upload",
    "download_fs",
    "coredump",
    "coredump-only",
    "__debug",
)
REFUSE_CMD = (
    '"$PYTHONEXE" -c "import sys; sys.stderr.write(\'picco_2slot: upload and erase are disabled; '
    "flash with tools/device (docs/TWO_SLOT.md)\\n'); sys.exit(1)\""
)


def fail(msg):
    sys.stderr.write("picco_2slot: %s\n" % msg)
    env.Exit(1)  # noqa: F821


def ota0_from_csv(path):
    with open(path, encoding="utf-8") as fp:
        for line in fp:
            line = line.split("#", 1)[0].strip()
            cells = [c.strip() for c in line.split(",")]
            if len(cells) >= 5 and cells[1] == "app" and cells[2] == "ota_0":
                return int(cells[3], 0), int(cells[4], 0)
    return None, None


# 1. What the build resolved.
images = [(int(str(off), 16), os.path.basename(env.subst(path))) for off, path in env.get("FLASH_EXTRA_IMAGES", [])]  # noqa: F821
if images != EXPECTED_EXTRA_IMAGES:
    fail(
        "FLASH_EXTRA_IMAGES is %s, expected %s"
        % ([("0x%X" % o, n) for o, n in images], [("0x%X" % o, n) for o, n in EXPECTED_EXTRA_IMAGES])
    )
table = env.subst("$PARTITIONS_TABLE_CSV")  # noqa: F821
if os.path.basename(table) != EXPECTED_TABLE:
    fail("partition table is %s, expected %s" % (table, EXPECTED_TABLE))
app_offset, app_size = ota0_from_csv(table)
if (app_offset, app_size) != (EXPECTED_APP_OFFSET, SLOT_SIZE):
    fail("ota_0 in %s is %s/%s, expected 0x%X/0x%X" % (table, app_offset, app_size, EXPECTED_APP_OFFSET, SLOT_SIZE))

# 2. No way to the device from this environment.
env.Replace(UPLOADCMD=REFUSE_CMD, ERASECMD=REFUSE_CMD)  # noqa: F821


def _refuse(target, source, env):
    sys.stderr.write("picco_2slot: device targets are disabled; flash with tools/device (docs/TWO_SLOT.md)\n")
    return 1


for name in DEVICE_TARGETS:
    env.AddPreAction(name, env.VerboseAction(_refuse, "picco_2slot: refusing '%s'" % name))  # noqa: F821


# 3. The image must fit its slot.
def _check_image_size(target, source, env):
    path = str(target[0])
    size = os.path.getsize(path)
    print(
        "picco_2slot: %s is %d bytes: %.1f%% of the 0x%X-byte slot, %.1f%% of the %d-byte budget"
        % (os.path.basename(path), size, 100.0 * size / SLOT_SIZE, SLOT_SIZE, 100.0 * size / BUDGET, BUDGET)
    )
    if size > SLOT_SIZE:
        sys.stderr.write("picco_2slot: %s (%d bytes) does not fit the %d-byte app slot\n" % (path, size, SLOT_SIZE))
        return 1
    return 0


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", env.VerboseAction(_check_image_size, "Checking image size against the app slot"))  # noqa: F821
