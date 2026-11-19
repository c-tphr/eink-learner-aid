"""Run, on Windows, the PowerShell lines the device sheets ask the owner to type, and the commands
the session tool prints for the owner.

    python .github/scripts/device_doc_powershell.py --shell powershell.exe --scratch <folder>
        [--session <folder made by device_session_fixture.py, with session.ps1 set up>]
    python .github/scripts/device_doc_powershell.py --list

Used by the `device-tool-windows` job of .github/workflows/project-ci.yml, under Windows
PowerShell 5.1 and PowerShell 7. Never touches a device and changes nothing in the repository.

1. The lines: every code span marked <!--pwsh--> in docs/DEVICE_SESSION.md and
   docs/DEVICE_EARLY_CHECK.md, taken from the documents as written. Each must be one of the kinds
   below (a new line needs a check here first). Placeholders are filled in: <version> with the
   kit's version, and in the backup copy check E:\\ with a folder standing in for the USB stick.
2. A real kit zip, made for this from SYNTHETIC images (tools/device/tests/synth.py) with the kit
   builder's own layout, MANIFEST.sha256, zip and .sha256 code (scripts/make_release_kit.py).
3. Each line runs in the shell given, as typed at its prompt (-EncodedCommand: no quoting is
   changed), in the folder the sheet says: on the intact download and kit (must report success),
   and on damaged ones (a changed zip, a wrong .sha256, a changed and a missing kit file: must
   report the mismatch in the words the sheet tells the owner to look for).
4. With --session: the commands the tool prints (common.session_command), typed as shown, with
   -DryRun added so that nothing needs a device, from the session folder, including a restore of
   a backup whose path holds a space, an apostrophe and a dollar sign.

Exit status 0 when every check passed, 1 otherwise.
"""

import argparse
import base64
import hashlib
import os
import re
import shutil
import subprocess
import sys
import zipfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEVICE = os.path.join(REPO, "tools", "device")
SHEETS = [os.path.join(REPO, "docs", "DEVICE_SESSION.md"), os.path.join(REPO, "docs", "DEVICE_EARLY_CHECK.md")]
sys.path[:0] = [os.path.join(REPO, "scripts"), DEVICE, os.path.join(DEVICE, "tests")]

MARK = re.compile(r"<!--pwsh-->`([^`]+)`")
VERSION = "2.0.1"
STICK_NAME = "picco-backup-24587cxxxxxx-20261119T120000.000Z.bin"

# kind: (the line as the sheet writes it, matched exactly after the placeholders are kept)
KINDS = {
    "zip-hash": r"(Get-FileHash .\picco-2slot-<version>.zip).Hash -eq "
                r"(Get-Content .\picco-2slot-<version>.zip.sha256).Split(' ')[0]",
    "zip-hash-by-eye": r"Get-FileHash .\picco-2slot-<version>.zip",
    "manifest": r"Get-Content .\MANIFEST.sha256 | ForEach-Object { $p = $_.Split(' ', 3); "
                r"if ((Get-FileHash $p[2]).Hash -ne $p[0]) { 'DIFFERENT: ' + $p[2] } }; 'Check done.'",
    "backup-copy-hash": r"Get-FileHash E:\picco-backup-<...>.bin",
}
# What the checklist tells the owner to look for; the checks below expect exactly these words.
SHEET_WORDS = ["It must print `True`. If it prints `False`", "It must print only `Check done.`",
               "A line starting `DIFFERENT:`"]


def doc_lines():
    """[(sheet file name, line)] for every <!--pwsh--> span, in document order."""
    out = []
    for path in SHEETS:
        with open(path, encoding="utf-8") as fp:
            out += [(os.path.basename(path), m.group(1)) for m in MARK.finditer(fp.read())]
    return out


def classify(line):
    for kind, text in KINDS.items():
        if line == text:
            return kind
    raise SystemExit("an unknown <!--pwsh--> line (add a check for it to %s): %s" % (__file__, line))


def fill(line, stick):
    return line.replace("<version>", VERSION).replace("E:\\picco-backup-<...>.bin",
                                                      os.path.join(stick, STICK_NAME))


# --- a kit zip from synthetic images ---------------------------------------------------------------

def make_kit_zip(out):
    """Kit tree, zip and .sha256 in out (as make_release_kit.py lays them out); returns the zip path."""
    import helpers
    import make_release_kit as mrk
    import synth

    tree = os.path.join(out, "kit tree")
    images = os.path.join(out, "synthetic images")
    src = synth.kit_sources(images, app_size=300_000)
    code, text = helpers.run_cli(["make-plan", "--bootloader", src["bootloader"], "--partitions",
                                  src["partition_table"], "--boot-app0", src["boot_app0"], "--app", src["app"],
                                  "--version", VERSION + "-picco2", "--build-id", "ci-synthetic", "-o",
                                  os.path.join(tree, "kit")])
    if code != 0:
        raise SystemExit("make-plan failed:\n" + text)
    docs = mrk.DOCS + [mrk.SESSION_DOC]
    for src_path, dst in mrk.tool_entries() + mrk.BOOKS + docs + mrk.doc_assets(docs):
        mrk.copy_exact(os.path.join(REPO, src_path), os.path.join(tree, dst))
    mrk.copy_exact(os.path.join(tree, "kit", "firmware.bin"), os.path.join(tree, mrk.SD_UPDATE))
    for name, text in (("LICENSES.txt", "synthetic kit for CI\n"), ("KIT-README.txt", "synthetic kit for CI\r\n"),
                       ("kit-info.json", '{"kind": "synthetic"}\n')):
        with open(os.path.join(tree, name), "w", encoding="ascii", newline="") as fp:
            fp.write(text)
    files = mrk.walk(tree)
    problems = mrk.tree_problems(files)
    if problems:
        raise SystemExit("the synthetic kit breaks the kit rules: %s" % "; ".join(problems))
    with open(os.path.join(tree, "MANIFEST.sha256"), "w", encoding="ascii", newline="\n") as fp:
        fp.write(mrk.manifest_text(tree, files))
    files = mrk.walk(tree)
    dist = os.path.join(out, "dist")
    os.makedirs(dist)
    zip_path = os.path.join(dist, mrk.zip_name(VERSION))
    mrk.pack(tree, files, zip_path, 1_763_000_000)
    mrk.write_sha256_file(zip_path)
    shutil.rmtree(images)
    return zip_path


# --- running a line as typed -------------------------------------------------------------------------

def run_ps(shell, text, cwd):
    """(exit code, stdout lines, stderr) of `text` typed at a prompt of `shell` in `cwd`."""
    encoded = base64.b64encode(text.encode("utf-16-le")).decode("ascii")
    proc = subprocess.run([shell, "-NoProfile", "-NonInteractive", "-EncodedCommand", encoded], cwd=cwd,
                          capture_output=True)
    out = proc.stdout.decode("utf-8", "replace").replace("\r\n", "\n")
    err = proc.stderr.decode("utf-8", "replace").replace("\r\n", "\n")
    return proc.returncode, [line.rstrip() for line in out.split("\n") if line.strip()], err


class Checks:
    def __init__(self):
        self.failed = []

    def check(self, name, ok, shown):
        print("::group::%s: %s" % ("PASS" if ok else "FAIL", name))
        print(shown)
        print("::endgroup::")
        print("%s  %s" % ("PASS" if ok else "FAIL", name))
        if not ok:
            self.failed.append(name)


def show(line, cwd, code, out, err):
    return "folder: %s\nPS> %s\n%s\n[exit %d]%s" % (cwd, line, "\n".join(out), code,
                                                     "\nstderr:\n" + err.strip() if err.strip() else "")


def check_lines(shell, scratch, checks):
    lines = {}
    for sheet, line in doc_lines():
        lines.setdefault(classify(line), line)
        print("%s: %-16s %s" % (sheet, classify(line), line))
    missing = sorted(set(KINDS) - set(lines))
    if missing:
        raise SystemExit("the sheets no longer hold these lines: %s" % ", ".join(missing))
    with open(SHEETS[0], encoding="utf-8") as fp:
        session_sheet = fp.read()
    for words in SHEET_WORDS:
        if words not in session_sheet:
            raise SystemExit("docs/DEVICE_SESSION.md no longer says %r; update the checks here" % words)

    zip_path = make_kit_zip(os.path.join(scratch, "made"))
    zip_name = os.path.basename(zip_path)
    with open(zip_path + ".sha256", encoding="ascii") as fp:
        published = fp.read().split()[0]
    stick = os.path.join(scratch, "stick")

    def download(name, damage=None):
        folder = os.path.join(scratch, name, "Downloads")
        os.makedirs(folder)
        shutil.copyfile(zip_path, os.path.join(folder, zip_name))
        shutil.copyfile(zip_path + ".sha256", os.path.join(folder, zip_name + ".sha256"))
        if damage == "zip":
            with open(os.path.join(folder, zip_name), "r+b") as fp:
                fp.seek(os.path.getsize(zip_path) // 2)
                byte = fp.read(1)
                fp.seek(-1, 1)
                fp.write(bytes([byte[0] ^ 0x01]))
        elif damage == "sha256":
            with open(os.path.join(folder, zip_name + ".sha256"), "w", encoding="ascii", newline="\n") as fp:
                fp.write("%s  %s\n" % ("0" * 64, zip_name))
        return folder

    def run(kind, cwd):
        line = fill(lines[kind], stick)
        code, out, err = run_ps(shell, line, cwd)
        return code, out, err, show(line, cwd, code, out, err)

    folder = download("Jane Doe")
    code, out, err, shown = run("zip-hash", folder)
    checks.check("zip hash line, intact download: prints True", out == ["True"], shown)
    code, out, err, shown = run("zip-hash-by-eye", folder)
    checks.check("Get-FileHash of the zip shows its 64-character hash",
                 code == 0 and any(published.upper() in line.upper() for line in out), shown)
    for name, damage in (("changed zip", "zip"), ("wrong sha256 file", "sha256")):
        code, out, err, shown = run("zip-hash", download(name, damage))
        checks.check("zip hash line, %s: prints False" % name, out == ["False"], shown)

    def kit(name, change=None):
        folder = os.path.join(scratch, name, "picco-2slot-%s" % VERSION)
        with zipfile.ZipFile(zip_path) as zf:
            zf.extractall(folder)
        if change:
            change(folder)
        return folder

    code, out, err, shown = run("manifest", kit("picco kit"))
    checks.check("MANIFEST line, intact kit: prints only 'Check done.'",
                 out == ["Check done."] and not re.search(r"Get-FileHash|Cannot find", err), shown)

    def changed(folder):
        with open(os.path.join(folder, "kit", "flash-plan.json"), "ab") as fp:
            fp.write(b" ")

    code, out, err, shown = run("manifest", kit("changed kit", changed))
    checks.check("MANIFEST line, one file changed: names it after 'DIFFERENT:'",
                 out == ["DIFFERENT: kit/flash-plan.json", "Check done."], shown)

    def removed(folder):
        os.remove(os.path.join(folder, "sd-card", "books", "fixture.epub"))

    code, out, err, shown = run("manifest", kit("incomplete kit", removed))
    checks.check("MANIFEST line, one file missing: names it after 'DIFFERENT:' (and PowerShell shows an error)",
                 out[-2:] == ["DIFFERENT: sd-card/books/fixture.epub", "Check done."]
                 and not [line for line in out if line.startswith("DIFFERENT:")][1:], shown)

    os.makedirs(stick)
    data = os.urandom(1 << 20)
    with open(os.path.join(stick, STICK_NAME), "wb") as fp:
        fp.write(data)
    want = hashlib.sha256(data).hexdigest().upper()
    code, out, err, shown = run("backup-copy-hash", scratch)
    checks.check("Get-FileHash of the backup copy shows its 64-character hash",
                 code == 0 and any(want in line.upper() for line in out), shown)


def check_tool_commands(shell, session, checks):
    """The commands piccoflash prints, typed as shown (plus -DryRun), from the session folder."""
    from piccoflash import common, workdir  # tools/device/piccoflash, which the session folder holds a copy of

    work = os.path.join(session, "work")
    sidecars = workdir.backup_sidecars(work)
    rec = workdir.read_json(sidecars[-1])
    commands = []
    for folder in ("backup copy", "owner's copy $1"):
        dest = os.path.join(session, folder)
        os.makedirs(dest)
        for name in (rec["file"], os.path.basename(sidecars[-1])):
            shutil.copyfile(os.path.join(work, name), os.path.join(dest, name))
        commands.append((common.session_command("restore", os.path.join(dest, rec["file"])),
                         "DRY RUN RESULT: READY. The backup passes the guard."))
    commands += [(common.session_command("monitor", "-Reset"), "DRY RUN: would open the serial port"),
                 (common.session_command("identify"), "DRY RUN: the device is not contacted"),
                 (common.session_command("backup"), "Nothing is ever written to the Picco by backup."),
                 (common.session_command("summary"), "piccoflash session summary")]
    for command, expect in commands:
        typed = command + " -DryRun"
        code, out, err = run_ps(shell, typed + "; exit $LASTEXITCODE", session)
        checks.check("tool's command, typed as shown: %s" % typed, code == 0 and expect in "\n".join(out),
                     show(typed, session, code, out, err))
    typed = common.session_command("restore")
    code, out, err = run_ps(shell, typed + "; exit $LASTEXITCODE", session)
    hint = "    %s restore \"work\\picco-backup-<...>.bin\"" % common.SESSION_LAUNCHER
    checks.check("session.ps1 restore without a file: exit 2 and the full command", code == 2 and hint in out,
                 show(typed, session, code, out, err))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--shell", help="powershell.exe or pwsh.exe")
    ap.add_argument("--scratch", help="an empty or missing folder whose path has no space")
    ap.add_argument("--session", help="a session folder from device_session_fixture.py, set up")
    ap.add_argument("--list", action="store_true", help="only print the lines and their kinds")
    args = ap.parse_args(argv)
    if args.list:
        for sheet, line in doc_lines():
            print("%s: %-16s %s" % (sheet, classify(line), line))
        return 0
    if not args.shell or not args.scratch:
        ap.error("--shell and --scratch are needed")
    scratch = os.path.abspath(args.scratch)
    if " " in scratch or (os.path.exists(scratch) and os.listdir(scratch)):
        ap.error("--scratch must be empty and its path must have no space (it stands in for drive E:)")
    os.makedirs(scratch, exist_ok=True)
    checks = Checks()
    check_lines(args.shell, scratch, checks)
    if args.session:
        check_tool_commands(args.shell, os.path.abspath(args.session), checks)
    print("")
    if checks.failed:
        print("FAILED: %s" % "; ".join(checks.failed))
        return 1
    print("every checklist line and every printed command did what the sheets say")
    return 0


if __name__ == "__main__":
    sys.exit(main())
