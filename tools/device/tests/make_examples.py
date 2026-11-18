"""Regenerate tools/device/examples/*.txt (run from the repo root):

    python3 tools/device/tests/make_examples.py

1. Builds synthetic images and a kit with `python -m piccoflash make-plan`.
2. Runs identify and backup in-process against the fake device (tests/fakedevice.py),
   so the work folder holds a passing identify and a verified, confirmed backup.
3. Runs the real command line, `python -m piccoflash flash --dry-run` and
   `restore --dry-run`, as subprocesses (a dry run never opens a port), plus
   make-plan with a wrong offset and with an altered file.
The temporary folder is shown as <tmp> in the transcripts.
"""

import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEVICE = os.path.dirname(HERE)
sys.path[:0] = [DEVICE, HERE]

import helpers  # noqa: E402
import synth  # noqa: E402
from fakedevice import FakeChip  # noqa: E402


def cli(tmp, *args):
    cmd = [sys.executable, "-m", "piccoflash", *args]
    p = subprocess.run(cmd, cwd=DEVICE, capture_output=True, text=True)
    shown = " ".join(a.replace(tmp, "<tmp>") for a in ["python", "-m", "piccoflash", *args])
    text = "$ %s\n%s[process exit code %d]\n" % (shown, (p.stdout + p.stderr).replace(tmp, "<tmp>"), p.returncode)
    return text


def main():
    tmp = tempfile.mkdtemp(prefix="piccoflash-examples-")
    try:
        src = synth.kit_sources(os.path.join(tmp, "build"), app_size=5_386_144)
        kit = os.path.join(tmp, "kit")
        made = cli(tmp, "make-plan", "--bootloader", src["bootloader"], "--partitions", src["partition_table"],
                   "--boot-app0", src["boot_app0"], "--app", src["app"], "--version", "0.1.0-synthetic",
                   "--build-id", "synthetic-images", "-o", kit)
        work = os.path.join(tmp, "work")
        chip = FakeChip(synth.stock_flash())
        side, rec = helpers.identify_and_backup(chip, work)
        backup_bin = os.path.join(work, rec["file"])
        header = ("# Transcript produced by tools/device/tests/make_examples.py against SYNTHETIC images\n"
                  "# (no real firmware, no device). identify and backup were run beforehand against the\n"
                  "# in-memory fake device, so the work folder holds a passing identify and a verified backup.\n\n")
        flash = cli(tmp, "flash", "--dry-run", "--port", "COM5", "--work-dir", work, "--plan", kit)
        restore = cli(tmp, "restore", "--dry-run", "--port", "COM5", "--work-dir", work, backup_bin)
        out = os.path.join(DEVICE, "examples")
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, "dry-run-flash.txt"), "w") as fp:
            fp.write(header + made + "\n" + flash)
        with open(os.path.join(out, "dry-run-restore.txt"), "w") as fp:
            fp.write(header + restore)

        refused = []
        bad = os.path.join(tmp, "kit-bad-offset")
        refused.append(cli(tmp, "make-plan", "--bootloader", src["bootloader"], "--partitions", src["partition_table"],
                           "--boot-app0", src["boot_app0"], "--app", src["app"], "--version", "0.1.0",
                           "--expect-offset", "boot_app0=0xE000", "-o", bad))
        altered = os.path.join(tmp, "build", "boot_app0.bin")
        data = bytearray(open(altered, "rb").read())
        data[0x100] ^= 0x01
        open(altered, "wb").write(bytes(data))
        refused.append(cli(tmp, "make-plan", "--bootloader", src["bootloader"], "--partitions", src["partition_table"],
                           "--boot-app0", altered, "--app", src["app"], "--version", "0.1.0", "-o", bad))
        # an altered offset inside an existing plan, then an altered image file in the kit
        import json

        plan_path = os.path.join(kit, "flash-plan.json")
        doc = json.load(open(plan_path))
        doc["images"][2]["offset"] = "0xE000"
        json.dump(doc, open(plan_path, "w"), indent=2)
        refused.append(cli(tmp, "plan", "--plan", kit))
        doc["images"][2]["offset"] = "0x15000"
        json.dump(doc, open(plan_path, "w"), indent=2)
        fw = os.path.join(kit, "firmware.bin")
        data = bytearray(open(fw, "rb").read())
        data[1000] ^= 0x01
        open(fw, "wb").write(bytes(data))
        refused.append(cli(tmp, "plan", "--plan", kit))
        with open(os.path.join(out, "refusals.txt"), "w") as fp:
            fp.write("# make-plan and plan refusing a wrong offset and altered files (synthetic images).\n\n"
                     + "\n".join(refused))
        print("examples written to", out)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
