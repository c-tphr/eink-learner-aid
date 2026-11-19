"""Lay out an owner-style session folder for the offline session.ps1 checks in CI.

    python .github/scripts/device_session_fixture.py <session folder>

Used by the `device-tool-windows` job of .github/workflows/project-ci.yml (and
runnable anywhere). It never touches a device and changes nothing in the
repository. It creates, in <session folder> (which may contain spaces):

  session.ps1, piccoflash/, requirements.txt, requirements-build.txt
      copied from tools/device/, the files the owner receives (tools/device/README.md,
      "For whoever builds the kit");
  kit/flash-plan.json and the four images
      made by `python -m piccoflash make-plan` from the SYNTHETIC images of
      tools/device/tests/synth.py (no real firmware);
  work/
      a passing identify and a verified, acknowledged backup, recorded by running
      identify and backup in-process against the in-memory fake device
      (tools/device/tests/fakedevice.py), exactly as tools/device/tests/make_examples.py does.

So `session.ps1 plan`, `flash -DryRun`, `restore <backup> -DryRun` and `summary`
can then run from that folder with their default -Plan and -WorkDir, as the
owner would run them. Prints `backup=<path of the backup .bin>` and, under
GitHub Actions, writes the same line to $GITHUB_OUTPUT.
"""

import os
import shutil
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEVICE = os.path.join(REPO, "tools", "device")
TESTS = os.path.join(DEVICE, "tests")
sys.path[:0] = [DEVICE, TESTS]

import helpers  # noqa: E402
import synth  # noqa: E402
from fakedevice import FakeChip  # noqa: E402

SHIPPED = ("session.ps1", "requirements.txt", "requirements-build.txt")


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    session = os.path.abspath(argv[1])
    if os.path.exists(session):
        print("refusing to reuse an existing folder: %s" % session)
        return 2
    os.makedirs(session)
    for name in SHIPPED:
        shutil.copy2(os.path.join(DEVICE, name), os.path.join(session, name))
    shutil.copytree(os.path.join(DEVICE, "piccoflash"), os.path.join(session, "piccoflash"),
                    ignore=shutil.ignore_patterns("__pycache__"))

    build = os.path.join(session, "synthetic-build")
    src = synth.kit_sources(build, app_size=5_386_144)
    kit = os.path.join(session, "kit")
    code, out = helpers.run_cli(["make-plan", "--bootloader", src["bootloader"], "--partitions", src["partition_table"],
                                 "--boot-app0", src["boot_app0"], "--app", src["app"], "--version",
                                 "0.1.0-synthetic", "--build-id", "ci-synthetic-images", "-o", kit])
    print(out)
    if code != 0:
        print("make-plan failed with exit %d" % code)
        return 1
    shutil.rmtree(build)

    work = os.path.join(session, "work")
    chip = FakeChip(synth.stock_flash())
    _sidecar, record = helpers.identify_and_backup(chip, work)
    backup_bin = os.path.join(work, record["file"])
    print("session folder: %s" % session)
    for root, _dirs, files in os.walk(session):
        if "piccoflash" in root.split(os.sep):
            continue
        for f in sorted(files):
            p = os.path.join(root, f)
            print("  %10d  %s" % (os.path.getsize(p), os.path.relpath(p, session)))
    line = "backup=%s" % backup_bin
    print(line)
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as fp:
            fp.write(line + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
