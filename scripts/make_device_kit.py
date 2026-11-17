#!/usr/bin/env python3
"""Assemble the device kit from a finished `pio run -e picco_2slot` build (task D4).

    pio run -e picco_2slot
    python3 scripts/make_device_kit.py [--out build/device-kit] [--envdump FILE] [--rehearse]

What it does, and where each fact comes from:

1. Takes the image offsets from the build's own environment: FLASH_EXTRA_IMAGES of
   `pio run -e picco_2slot -t envdump` (bootloader, partitions.bin, boot_app0.bin and their
   paths) and the ota_0 row of the BUILT .pio/build/picco_2slot/partitions.bin (the app offset,
   which the platform takes from the same table). The version is the build's CROSSPOINT_VERSION.
2. Runs `python -m piccoflash make-plan` (tools/device) on exactly bootloader.bin,
   partitions.bin, the framework's boot_app0.bin and firmware.bin, passing every offset from
   step 1 as --expect-offset, so a disagreement between the build and the guard stops here.
   The guard's content checks run on the real images; nothing is loosened.
3. Runs `python -m piccoflash plan` on the new kit (the owner's "session.ps1 plan").
4. Copies firmware.bin as update.bin, the SD-card update file (any *.bin name works: the
   firmware's SD update lets the user pick the file; plan section 10 calls it update.bin).
5. With --rehearse: prepares a work folder against the IN-MEMORY FAKE device of
   tools/device/tests (a passing identify and a verified backup of a synthetic stock Picco) and
   runs `python -m piccoflash flash --dry-run` with the real kit. A dry run never opens a port.

Output (--out, which must not exist yet):
    <out>/kit/            flash-plan.json + the four images (what session.ps1 flashes)
    <out>/update.bin      the SD-card update file (= firmware.bin)
    <out>/kit-report.txt  everything printed, for the pull request and docs/evidence
    <out>/rehearsal/      only with --rehearse: fake-device work folder (not for shipping)

Exit status: 0 when every step passed; 1 when any check, the guard, or a piccoflash command
refused (the message says which); 2 on a usage error. Never touches a device.
"""

import argparse
import ast
import hashlib
import os
import re
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEVICE = os.path.join(REPO, "tools", "device")
ENV_NAME = "picco_2slot"
ROLE_BY_FILE = {"bootloader.bin": "bootloader", "partitions.bin": "partition_table", "boot_app0.bin": "boot_app0"}

sys.path.insert(0, DEVICE)
from piccoflash import layout  # noqa: E402

LINES = []


def say(text=""):
    print(text, flush=True)
    LINES.append(text)


def die(text):
    say("KIT NOT MADE: %s" % text)
    raise SystemExit(1)


def sha256(path):
    with open(path, "rb") as fp:
        return hashlib.sha256(fp.read()).hexdigest()


def envdump_text(path):
    if path:
        with open(path, encoding="utf-8", errors="replace") as fp:
            return fp.read()
    say("$ pio run -e %s -t envdump" % ENV_NAME)
    env = dict(os.environ, PLATFORMIO_SETTING_ENABLE_TELEMETRY="No")
    proc = subprocess.run(["pio", "run", "-e", ENV_NAME, "-t", "envdump"], cwd=REPO, env=env,
                          capture_output=True, text=True)
    if proc.returncode != 0:
        say(proc.stdout[-3000:] + proc.stderr[-3000:])
        die("envdump failed (exit %d)" % proc.returncode)
    return proc.stdout


def dump_value(text, key):
    """The Python literal after 'KEY': in a pprint-ed SCons environment dump."""
    m = re.search(r"^  '%s': " % re.escape(key), text, re.M)
    if not m:
        die("envdump has no %s" % key)
    start = m.end()
    for end in range(start + 1, len(text) + 1):
        if text[end - 1] not in ",\n":
            continue
        try:
            return ast.literal_eval(text[start:end].rstrip(",\n"))
        except (SyntaxError, ValueError):
            continue
    die("cannot read %s from the envdump" % key)


def crosspoint_version(text):
    m = re.search(r"\('CROSSPOINT_VERSION', '\\\\\"([^\\\"]+)\\\\\"'\)", text)
    if not m:
        die("envdump has no CROSSPOINT_VERSION define")
    return m.group(1)


def run_piccoflash(args):
    cmd = [sys.executable, "-m", "piccoflash"] + args
    say("$ python -m piccoflash " + " ".join(a.replace(REPO + os.sep, "") for a in args))
    proc = subprocess.run(cmd, cwd=DEVICE, capture_output=True, text=True)
    for line in (proc.stdout + proc.stderr).rstrip("\n").splitlines():
        say("  " + line.replace(REPO + os.sep, ""))
    say("  [exit %d]" % proc.returncode)
    return proc.returncode, proc.stdout


def git_build_id():
    try:
        head = subprocess.run(["git", "rev-parse", "--short=12", "HEAD"], cwd=REPO, capture_output=True,
                              text=True, check=True).stdout.strip()
        dirty = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=REPO,
                               capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"
    return head + ("-dirty" if dirty else "")


def rehearse(kit, out):
    sys.path.insert(0, os.path.join(DEVICE, "tests"))
    import helpers  # noqa: E402  (tools/device/tests)
    import synth  # noqa: E402
    from fakedevice import FakeChip  # noqa: E402

    work = os.path.join(out, "rehearsal")
    say("")
    say("== Rehearsal: flash --dry-run with the real kit, against a FAKE device's work folder")
    say("(identify and backup of a synthetic stock Picco, run in-process by tools/device/tests/helpers.py)")
    helpers.identify_and_backup(FakeChip(synth.stock_flash()), work)
    code, stdout = run_piccoflash(["flash", "--dry-run", "--port", "COM5", "--work-dir", work, "--plan", kit])
    if code != 0 or "DRY RUN RESULT: READY" not in stdout:
        die("flash --dry-run with the real images did not report READY")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", default=os.path.join(REPO, "build", "device-kit"))
    ap.add_argument("--build-dir", default=os.path.join(REPO, ".pio", "build", ENV_NAME))
    ap.add_argument("--envdump", help="use this saved `pio run -e picco_2slot -t envdump` output")
    ap.add_argument("--rehearse", action="store_true", help="also run flash --dry-run against a fake device")
    args = ap.parse_args(argv)
    out = os.path.abspath(args.out)
    if os.path.exists(out):
        print("refusing to reuse %s; remove it or pass another --out" % out)
        return 2

    say("== Images of the %s build" % ENV_NAME)
    build = os.path.abspath(args.build_dir)
    for name in ("bootloader.bin", "partitions.bin", "firmware.bin"):
        if not os.path.isfile(os.path.join(build, name)):
            die("%s not found in %s; run `pio run -e %s` first" % (name, build, ENV_NAME))

    dump = envdump_text(args.envdump)
    extra = dump_value(dump, "FLASH_EXTRA_IMAGES")
    sources, expect = {}, {}
    for offset, path in extra:
        role = ROLE_BY_FILE.get(os.path.basename(path))
        if role is None or role in sources:
            die("unexpected FLASH_EXTRA_IMAGES entry %s %s" % (offset, path))
        sources[role], expect[role] = path, int(offset, 16)
    if set(sources) != set(ROLE_BY_FILE.values()):
        die("FLASH_EXTRA_IMAGES lists %s, expected bootloader, partitions and boot_app0" % sorted(sources))
    for role, name in (("bootloader", "bootloader.bin"), ("partition_table", "partitions.bin")):
        if os.path.realpath(sources[role]) != os.path.realpath(os.path.join(build, name)):
            die("the build's %s is %s, not the one in %s" % (role, sources[role], build))
    say("FLASH_EXTRA_IMAGES (envdump):")
    for role in ("bootloader", "partition_table", "boot_app0"):
        say("  0x%-6X %-15s %s" % (expect[role], role, sources[role].replace(REPO + os.sep, "")))

    with open(os.path.join(build, "partitions.bin"), "rb") as fp:
        table = layout.parse_table(fp.read())
    say("partitions.bin, parsed:")
    say("  %-9s %-5s %-9s %-10s %-10s" % ("Name", "Type", "SubType", "Offset", "Size"))
    for r in table.rows:
        say("  %-9s %-5s %-9s 0x%-8X 0x%-8X" % (r.name, r.type, r.subtype, r.offset, r.size))
    say("  MD5 entry present: %s, correct: %s" % (table.md5_present, table.md5_ok))
    if table.errors:
        die("partitions.bin: %s" % "; ".join(table.errors))
    same = tuple(table.rows) == layout.TWO_SLOT_TABLE
    say("  equal to the two-slot table row for row: %s" % ("yes" if same else "NO"))
    if not same:
        die("the built partition table is not the two-slot table: %s" % layout.diff_rows(table.rows, layout.TWO_SLOT_TABLE))
    ota0 = [r for r in table.rows if r.type == "app" and r.subtype == "ota_0"]
    if len(ota0) != 1:
        die("partitions.bin has %d ota_0 rows" % len(ota0))
    sources["app"], expect["app"] = os.path.join(build, "firmware.bin"), ota0[0].offset
    say("app offset (ota_0 of the built table, as the platform uses it): 0x%X" % expect["app"])

    version = crosspoint_version(dump)
    build_id = git_build_id()
    say("CROSSPOINT_VERSION: %s   build id: %s" % (version, build_id))

    say("")
    say("== make-plan (the guard checks every image)")
    kit = os.path.join(out, "kit")
    os.makedirs(out)
    plan_args = ["make-plan", "--bootloader", sources["bootloader"], "--partitions", sources["partition_table"],
                 "--boot-app0", sources["boot_app0"], "--app", sources["app"], "--version", version,
                 "--build-id", build_id, "-o", kit]
    for role in ("bootloader", "partition_table", "boot_app0", "app"):
        plan_args += ["--expect-offset", "%s=0x%X" % (role, expect[role])]
    code, _ = run_piccoflash(plan_args)
    if code != 0:
        die("the guard refused the images (make-plan exit %d); nothing was loosened, see the reasons above" % code)

    say("")
    say("== plan (what the owner's `session.ps1 plan` shows)")
    code, _ = run_piccoflash(["plan", "--plan", kit])
    if code != 0:
        die("plan verification failed (exit %d)" % code)

    say("")
    say("== SD-card update file")
    update = os.path.join(out, "update.bin")
    shutil.copyfile(sources["app"], update)
    if sha256(update) != sha256(os.path.join(kit, "firmware.bin")):
        die("update.bin differs from the kit's firmware.bin")
    say("update.bin = firmware.bin, %d bytes, SHA-256 %s" % (os.path.getsize(update), sha256(update)))

    if args.rehearse:
        rehearse(kit, out)

    say("")
    say("KIT MADE: %s" % out.replace(REPO + os.sep, ""))
    with open(os.path.join(out, "kit-report.txt"), "w", encoding="utf-8") as fp:
        fp.write("\n".join(LINES) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
