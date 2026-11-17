#!/usr/bin/env python3
"""Assemble the release device kit from ONE finished `pio run -e picco_2slot` build (task A0).

    export EINK_RELEASE_VERSION=2.0.1
    pio run -e picco_2slot -t envdump > build/envdump.txt   # before the build (docs/CI.md)
    pio run -e picco_2slot
    python3 scripts/make_release_kit.py --version 2.0.1 --kind preview --envdump build/envdump.txt \\
        --out build/release-kit

Run it with a Python that has tools/device/requirements-test.txt installed (the release workflow
and Project CI use a virtualenv). The release workflow is .github/workflows/release-kit.yml;
docs/RELEASING.md explains the whole process. Never touches a device.

Steps, each of which stops the script with "RELEASE KIT NOT MADE: <reason>":

1. scripts/make_device_kit.py on the build: offsets from the build's own envdump, every image
   through the guard (`piccoflash make-plan` with --expect-offset, then `piccoflash plan`), and
   update.bin. A build whose images the guard refuses stops here. With --device-kit, an existing
   make_device_kit.py output is used instead, after checking it holds exactly the four images and
   the plan, that they equal the build's images byte for byte, and that `piccoflash plan` passes.
2. Version: --version must be MAJOR.MINOR.PATCH above upstream's [crosspoint] version; the plan's
   firmware version must be "<version>-picco2" and firmware.bin must contain that string. The
   Wi-Fi updater's own code (lib/UpdatePolicy/ReleaseNaming.cpp, compiled from
   scripts/release_naming_cli.cpp) must parse the tag v<version> and the built string to the same
   numbers, and must name the Wi-Fi asset exactly as this script does.
3. The kit tree (<out>/kit): the device tool byte-identical to tools/device, kit/ with the plan
   and four images, sd-card/ with books and update.bin, docs/ (with every picture the documents
   link to, at the same relative path; a link to anything else outside the kit refuses the kit),
   KIT-README.txt, LICENSES.txt, kit-info.json and MANIFEST.sha256. Every path is checked: Windows-safe names, under 180
   characters, no .bin other than the four images and update.bin, no merged or factory image,
   no work/, runtime/, backup or build folder.
4. The zip (<out>/dist/picco-2slot-<version>.zip): entries sorted, stored (no compression), all
   with the commit's time, so the same kit tree always gives the same bytes.
5. The zip is unpacked into a temporary folder whose name has a space; every file is compared with
   the tree and MANIFEST.sha256; the UNPACKED piccoflash runs `plan` and `flash --dry-run`
   against a work folder made with the fake device of tools/device/tests. Anything but
   "DRY RUN RESULT: READY" stops the script.
6. <out>/dist also gets the Wi-Fi asset eink-learner-aid-v<version>-picco2.bin (= firmware.bin),
   a .sha256 file for it and for the zip, kit-info.json and release-notes.md.

Exit status: 0 when the kit was made; 1 when any check refused (the message says which);
2 on a usage error.
"""

import argparse
import configparser
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPTS = os.path.join(REPO, "scripts")
DEVICE = os.path.join(REPO, "tools", "device")
ENV_NAME = "picco_2slot"
PROJECT = "eink-learner-aid"

sys.path.insert(0, SCRIPTS)
import release_version  # noqa: E402  (scripts/release_version.py: the build's version rule)

# What the owner's folder holds besides kit/: (repository path, path in the kit).
TOOL_FILES = [("tools/device/session.ps1", "session.ps1"),
              ("tools/device/requirements.txt", "requirements.txt"),
              ("tools/device/requirements-build.txt", "requirements-build.txt")]
TOOL_PACKAGE = ("tools/device/piccoflash", "piccoflash")
BOOKS = [("fixtures/books/fixture.epub", "sd-card/books/fixture.epub"),
         ("fixtures/books/fixture.prepared.epub", "sd-card/books/fixture.prepared.epub"),
         ("fixtures/books/paine-essays.epub", "sd-card/books/paine-essays.epub"),
         ("fixtures/books/paine-essays.prepared.epub", "sd-card/books/paine-essays.prepared.epub")]
DOCS = [("docs/DEVICE_EARLY_CHECK.md", "docs/DEVICE_EARLY_CHECK.md"),
        ("fixtures/books/paine-essays.validation-report.md", "docs/paine-essays.validation-report.md"),
        ("fixtures/books/paine-essays.LICENSE.md", "docs/paine-essays.LICENSE.md")]
SESSION_DOC = ("docs/DEVICE_SESSION.md", "docs/DEVICE_SESSION.md")
# Files a kit document may link to besides another kit document: its pictures. They are copied
# into the kit at the same path relative to the document, so the kit's copy is complete offline.
DOC_ASSET_EXTENSIONS = (".png", ".jpg", ".jpeg")
DOC_LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)\s]+)\)|<img\s[^>]*?src=\"([^\"]+)\"")
CODE_SPAN_RE = re.compile(r"`[^`\n]*`")
URL_RE = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")
IMAGES = ("bootloader.bin", "partitions.bin", "boot_app0.bin", "firmware.bin")
PLAN_FILE = "flash-plan.json"
SD_UPDATE = "sd-card/update.bin"
ALLOWED_BIN = frozenset(["kit/" + n for n in IMAGES] + [SD_UPDATE])
# Folder names that must never appear in the kit: the tool's own output, the private Python,
# build folders and caches.
FORBIDDEN_DIRS = frozenset(["work", "runtime", ".pio", "build", "stage", "rehearsal", "__pycache__", ".git"])
FORBIDDEN_NAME_RE = re.compile(r"factory|merged|picco-backup|MISMATCH|\.pyc$|\.read[12]\.bin$", re.I)
MAX_PATH = 179  # characters from the kit root, so the extracted path stays well under Windows' 260
WINDOWS_RESERVED = frozenset(["CON", "PRN", "AUX", "NUL"] + ["COM%d" % i for i in range(1, 10)]
                             + ["LPT%d" % i for i in range(1, 10)])
WINDOWS_BAD_CHARS = set('<>:"/\\|?*')
# The updater's asset-name buffer (src/network/OtaUpdater.cpp); release_naming_cli.cpp uses it.
UPDATER_BUFFER_RE = re.compile(r"char assetName\[48\]")

LINES = []


def say(text=""):
    print(text, flush=True)
    LINES.append(text)


def die(text):
    say("RELEASE KIT NOT MADE: %s" % text)
    raise SystemExit(1)


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    with open(path, "rb") as fp:
        return sha256_bytes(fp.read())


def rel(path):
    return path.replace(REPO + os.sep, "")


# --- names --------------------------------------------------------------------------------------

def zip_name(version):
    return "picco-2slot-%s.zip" % version


def wifi_asset_name(version):
    """lib/UpdatePolicy/ReleaseNaming.h: "eink-learner-aid-" + tag + "-picco2.bin", tag v<version>."""
    return "%s-v%s-picco2.bin" % (PROJECT, version)


def windows_name_problems(path):
    """Why Windows (Explorer's Extract All, then PowerShell) could not hold this kit path."""
    problems = []
    if not path or path.startswith("/") or "\\" in path:
        return ["%r: not a relative path with forward slashes" % path]
    if len(path) > MAX_PATH:
        problems.append("%s: %d characters, the limit is %d" % (path, len(path), MAX_PATH))
    if any(ord(c) < 0x21 and c != " " or ord(c) > 0x7E for c in path):
        problems.append("%r: only printable ASCII is allowed (zip names without the UTF-8 flag)" % path)
    for part in path.split("/"):
        if part in ("", ".", ".."):
            problems.append("%s: empty, '.' or '..' path component" % path)
            continue
        bad = sorted(set(part) & WINDOWS_BAD_CHARS)
        if bad:
            problems.append("%s: %r contains %s" % (path, part, " ".join(bad)))
        if part[-1] in ". ":
            problems.append("%s: %r ends with a dot or space" % (path, part))
        if part.split(".")[0].upper().rstrip(" ") in WINDOWS_RESERVED:
            problems.append("%s: %r is a reserved Windows name" % (path, part))
    return problems


def tree_problems(paths):
    """Every rule the kit's file list must meet; [] when it is clean."""
    problems = []
    seen = {}
    for p in paths:
        problems += windows_name_problems(p)
        low = p.lower()
        if low in seen:
            problems.append("%s and %s differ only in case" % (seen[low], p))
        seen[low] = p
        parts = p.split("/")
        hit = sorted(set(d.lower() for d in parts[:-1]) & FORBIDDEN_DIRS)
        if hit:
            problems.append("%s: inside a forbidden folder (%s)" % (p, ", ".join(hit)))
        if FORBIDDEN_NAME_RE.search(parts[-1]):
            problems.append("%s: a merged, factory, backup or cache file must never be in the kit" % p)
        if low.endswith(".bin") and p not in ALLOWED_BIN:
            problems.append("%s: the only .bin files allowed are %s" % (p, ", ".join(sorted(ALLOWED_BIN))))
    for need in sorted(ALLOWED_BIN):
        if need not in paths:
            problems.append("%s is missing" % need)
    return problems


def walk(root):
    """Relative paths (forward slashes) of every file under root, sorted."""
    out = []
    for dirpath, dirnames, files in os.walk(root):
        dirnames.sort()
        for f in files:
            out.append(os.path.relpath(os.path.join(dirpath, f), root).replace(os.sep, "/"))
    return sorted(out)


# --- the build and its device kit ----------------------------------------------------------------

def run(cmd, cwd=REPO, show=True):
    if show:
        say("$ " + " ".join(rel(c) for c in cmd))
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if show:
        for line in (proc.stdout + proc.stderr).rstrip("\n").splitlines():
            say("  " + rel(line))
        say("  [exit %d]" % proc.returncode)
    return proc


def repo_state():
    """(HEAD commit, tracked files modified?)"""
    try:
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=REPO, capture_output=True, text=True,
                              check=True).stdout.strip()
        dirty = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=REPO,
                               capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as exc:
        die("cannot read the git state of %s: %s" % (REPO, exc))
    return head, bool(dirty)


def commit_time(commit):
    proc = subprocess.run(["git", "show", "-s", "--format=%ct", commit], cwd=REPO, capture_output=True, text=True)
    if proc.returncode != 0 or not proc.stdout.strip().isdigit():
        die("cannot read the time of commit %s" % commit)
    return int(proc.stdout.strip())


def make_device_kit(out, build_dir, envdump):
    cmd = [sys.executable, os.path.join(SCRIPTS, "make_device_kit.py"), "--out", out, "--build-dir", build_dir]
    if envdump:
        cmd += ["--envdump", envdump]
    proc = run(cmd)
    if proc.returncode != 0:
        die("scripts/make_device_kit.py refused the build (exit %d); see its reasons above" % proc.returncode)


def check_device_kit(device_kit, build_dir):
    """The make_device_kit.py output is complete, from this build, and passes `piccoflash plan`."""
    kit = os.path.join(device_kit, "kit")
    have = sorted(os.listdir(kit)) if os.path.isdir(kit) else []
    want = sorted(IMAGES + (PLAN_FILE,))
    if have != want:
        die("%s must hold exactly %s; it holds %s" % (rel(kit), ", ".join(want), ", ".join(have) or "nothing"))
    for name in ("bootloader.bin", "partitions.bin", "firmware.bin"):
        built = os.path.join(build_dir, name)
        if not os.path.isfile(built):
            die("%s not found in %s; run `pio run -e %s` first" % (name, rel(build_dir), ENV_NAME))
        if sha256_file(built) != sha256_file(os.path.join(kit, name)):
            die("kit/%s differs from %s: the kit must come from this one build" % (name, rel(built)))
    update = os.path.join(device_kit, "update.bin")
    if not os.path.isfile(update) or sha256_file(update) != sha256_file(os.path.join(kit, "firmware.bin")):
        die("update.bin is missing or differs from kit/firmware.bin")
    proc = run([sys.executable, "-m", "piccoflash", "plan", "--plan", kit], cwd=DEVICE)
    if proc.returncode != 0:
        die("`piccoflash plan` refused %s (exit %d); see its reasons above" % (rel(kit), proc.returncode))
    with open(os.path.join(kit, PLAN_FILE), encoding="utf-8") as fp:
        plan = json.load(fp)
    for img in plan["images"]:
        if sha256_file(os.path.join(kit, img["file"])) != img["sha256"]:
            die("kit/%s does not match the hash in %s" % (img["file"], PLAN_FILE))
    return plan


# --- version -------------------------------------------------------------------------------------

def base_version():
    """Upstream's [crosspoint] version, which development builds carry (platformio.ini)."""
    cfg = configparser.ConfigParser()
    cfg.read(os.path.join(REPO, "platformio.ini"), encoding="utf-8")
    return cfg.get("crosspoint", "version")


def naming_tool(workdir):
    """Compile scripts/release_naming_cli.cpp with the updater's ReleaseNaming.cpp; return its path."""
    with open(os.path.join(REPO, "src", "network", "OtaUpdater.cpp"), encoding="utf-8") as fp:
        if not UPDATER_BUFFER_RE.search(fp.read()):
            die("src/network/OtaUpdater.cpp no longer declares `char assetName[48]`; update "
                "kAssetNameCapacity in scripts/release_naming_cli.cpp to match")
    exe = os.path.join(workdir, "release_naming_cli")
    cxx = os.environ.get("CXX", "c++")
    cmd = [cxx, "-std=c++17", "-O1", "-Wall", "-Werror", "-I", os.path.join(REPO, "lib", "UpdatePolicy"),
           os.path.join(SCRIPTS, "release_naming_cli.cpp"), os.path.join(REPO, "lib", "UpdatePolicy", "ReleaseNaming.cpp"),
           "-o", exe]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True)
    except OSError as exc:
        die("cannot run the C++ compiler %r (set CXX): %s" % (cxx, exc))
    if proc.returncode != 0:
        die("compiling scripts/release_naming_cli.cpp failed:\n%s" % (proc.stdout + proc.stderr))
    return exe


def updater(tool, *args):
    proc = subprocess.run([tool] + list(args), capture_output=True, text=True)
    return proc.returncode, proc.stdout.strip()


def check_version(version, plan, firmware, tool):
    """The requested version, the built string, the tag and the updater agree; returns the facts."""
    try:
        built = release_version.release_string(version)
    except ValueError as exc:
        die("--version %s" % str(exc).split("=", 1)[-1])
    tag = "v" + version
    say("requested version %s, tag %s, expected CROSSPOINT_VERSION %s" % (version, tag, built))
    if plan["firmware_version"] != built:
        die("version mismatch: the build's CROSSPOINT_VERSION is %r, the requested version needs %r. Build with "
            "EINK_RELEASE_VERSION=%s (scripts/release_version.py)" % (plan["firmware_version"], built, version))
    with open(firmware, "rb") as fp:
        data = fp.read()
    if built.encode() + b"\0" not in data:
        die("firmware.bin does not contain the string %r" % built)
    if b"-dev-picco2\0" in data:
        die("firmware.bin contains a development version string (-dev-picco2)")
    say("firmware.bin contains %r" % built)

    base = base_version()
    facts = {"tag": tag, "firmware_version": built}
    for text in (tag, built):
        code, parsed = updater(tool, "version", text)
        say("updater parseVersion(%r) -> %s" % (text, parsed))
        if code != 0 or parsed != version:
            die("the updater parses %r as %r, not %s" % (text, parsed, version))
    code, asset = updater(tool, "asset-name", tag)
    say("updater formatAssetName(%r) -> %s" % (tag, asset))
    if code != 0 or asset != wifi_asset_name(version):
        die("the updater looks for %r for tag %s, but the release would publish %r"
            % (asset, tag, wifi_asset_name(version)))
    facts["asset_name"] = asset
    verdicts = {}
    for running in (built, base + "-dev-picco2", base + "-picco2"):
        _, verdict = updater(tool, "is-newer", tag, running)
        verdicts[running] = verdict
        say("updater isNewerRelease(%r, running %r) -> %s" % (tag, running, verdict))
    if verdicts[built] != "no update":
        die("a device running this build would install the same release again")
    if verdicts[base + "-dev-picco2"] != "install" or verdicts[base + "-picco2"] != "install":
        die("version %s is not above %s, the version development builds carry ([crosspoint] version in "
            "platformio.ini); choose a higher version" % (version, base))
    facts["updater_verdicts"] = verdicts
    return facts


# --- the kit tree ------------------------------------------------------------------------------

def tool_entries():
    entries = list(TOOL_FILES)
    src_dir, dst_dir = TOOL_PACKAGE
    for p in walk(os.path.join(REPO, src_dir)):
        if "__pycache__" in p.split("/") or p.endswith(".pyc"):
            continue
        entries.append(("%s/%s" % (src_dir, p), "%s/%s" % (dst_dir, p)))
    return entries


def doc_link_targets(text):
    """Relative link and image targets in a Markdown text (Markdown links and HTML <img> tags),
    without their #anchor (URLs, in-page anchors and anything inside code spans are left out)."""
    out = []
    for link, img in DOC_LINK_RE.findall(CODE_SPAN_RE.sub("", text)):
        target = link or img
        path = target.split("#", 1)[0]
        if URL_RE.match(target) or not path or path in out:
            continue
        out.append(path)
    return out


def doc_assets(doc_entries):
    """(source, kit path) of every picture the kit documents link to, at the same relative path.

    A link to another kit document is fine. Anything else must be a picture under the kit's docs/
    folder that exists in the repository; otherwise the kit's copy of the document would be
    incomplete, and the kit is refused."""
    kit_docs = set(dst for _, dst in doc_entries)
    assets = []
    for src, dst in doc_entries:
        src_path = os.path.join(REPO, src)
        with open(src_path, encoding="utf-8") as fp:
            targets = doc_link_targets(fp.read())
        for target in targets:
            kit_path = os.path.normpath(os.path.join(os.path.dirname(dst), target)).replace(os.sep, "/")
            if kit_path in kit_docs:
                continue
            source = os.path.normpath(os.path.join(os.path.dirname(src_path), target))
            if not kit_path.startswith("docs/") or "\\" in target:
                die("%s links to %s, which would lie outside the kit's docs folder" % (dst, target))
            if not kit_path.lower().endswith(DOC_ASSET_EXTENSIONS):
                die("%s links to %s, which is not in the kit (only kit documents and pictures can be linked)"
                    % (dst, target))
            if not os.path.isfile(source):
                die("%s links to %s, which does not exist" % (dst, target))
            if all(kit_path != have for _, have in assets):
                assets.append((source, kit_path))
    return sorted(assets, key=lambda a: a[1])


def session_python_version():
    with open(os.path.join(DEVICE, "session.ps1"), encoding="utf-8") as fp:
        m = re.search(r"^\$PinnedPythonVersion\s*=\s*'([^']+)'", fp.read(), re.M)
    return m.group(1) if m else "unknown"


def pio_version():
    try:
        proc = subprocess.run(["pio", "--version"], capture_output=True, text=True)
    except OSError:
        return "unknown"
    return proc.stdout.strip() or "unknown"


def run_info():
    run_id = os.environ.get("GITHUB_RUN_ID")
    if not run_id:
        return {"run_id": None, "run_url": None, "note": "built outside GitHub Actions"}
    url = "%s/%s/actions/runs/%s" % (os.environ.get("GITHUB_SERVER_URL", "https://github.com"),
                                      os.environ.get("GITHUB_REPOSITORY", ""), run_id)
    return {"run_id": run_id, "run_attempt": os.environ.get("GITHUB_RUN_ATTEMPT"), "run_url": url,
            "workflow": os.environ.get("GITHUB_WORKFLOW")}


def kit_readme(info, has_session_doc):
    first = "docs\\DEVICE_SESSION.md" if has_session_doc else "docs\\DEVICE_EARLY_CHECK.md"
    kind = "FINAL kit" if info["kind"] == "final" else "PREVIEW kit (for checking the kit, not for the device session)"
    lines = [
        "eink-learner-aid device kit for the BOOX Picco",
        "",
        "Version:  %s (firmware %s), %s" % (info["version"], info["firmware_version"], kind),
        "Commit:   %s" % info["commit"],
        "Built:    %s%s" % (info["built_utc"], "  " + info["build"]["run_url"] if info["build"].get("run_url") else ""),
        "",
        "What this is: everything the Picco session needs. session.ps1 is the only program to run;",
        "kit\\ holds the four images it writes, sd-card\\ the books and update.bin for the SD card,",
        "docs\\ the instructions. Nothing in it has been tested on a real Picco yet.",
        "",
        "Open first: %s" % first,
    ]
    if not has_session_doc:
        lines.append("(The session checklist docs\\DEVICE_SESSION.md is not in this preview kit.)")
    lines += [
        "Open PowerShell in THIS folder (it replaces tools\\device in the early-check sheet) and type:",
        "    powershell -ExecutionPolicy Bypass -File .\\session.ps1 setup",
        "If you did the early check in another folder, copy its work folder here first.",
        "",
        "WARNING: flashing replaces the Picco's Onyx bootloader and partition table. Only the full",
        "backup made by 'session.ps1 backup' can return the device to its stock Onyx firmware.",
        "Keep two copies of it and never upload or share it.",
        "",
        "Every file's SHA-256 is in MANIFEST.sha256; details are in kit-info.json; licences in LICENSES.txt.",
    ]
    return "\r\n".join(lines) + "\r\n"


def write_sha256_file(path):
    """<path>.sha256 as published beside a release asset: "<sha256>  <file name>" and a newline.
    The session checklist's PowerShell line reads the first word of it."""
    with open(path + ".sha256", "w", encoding="ascii", newline="\n") as fp:
        fp.write("%s  %s\n" % (sha256_file(path), os.path.basename(path)))


def manifest_text(tree, files):
    return "".join("%s  %s\n" % (sha256_file(os.path.join(tree, f)), f) for f in files)


def verify_manifest(tree, text):
    """Problems with MANIFEST.sha256 against the files under tree ([] when every line and file agree)."""
    problems = []
    listed = {}
    for n, line in enumerate(text.splitlines(), 1):
        m = re.match(r"^([0-9a-f]{64})  (\S.*)$", line)
        if not m:
            problems.append("MANIFEST.sha256 line %d is malformed" % n)
            continue
        listed[m.group(2)] = m.group(1)
    actual = [f for f in walk(tree) if f != "MANIFEST.sha256"]
    for f in actual:
        if f not in listed:
            problems.append("%s is not in MANIFEST.sha256" % f)
        elif sha256_file(os.path.join(tree, f)) != listed[f]:
            problems.append("%s does not match its MANIFEST.sha256 hash" % f)
    for f in sorted(set(listed) - set(actual)):
        problems.append("MANIFEST.sha256 lists %s, which is missing" % f)
    return problems


def copy_exact(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)
    if sha256_file(src) != sha256_file(dst):
        die("copying %s changed its bytes" % rel(src))


# --- zip -----------------------------------------------------------------------------------------

def zip_time(epoch):
    t = datetime.datetime.fromtimestamp(max(epoch, 315532800), datetime.timezone.utc)  # zip starts in 1980
    return (t.year, t.month, t.day, t.hour, t.minute, t.second - t.second % 2)


def pack(tree, files, out_zip, epoch):
    """Deterministic zip: sorted entries, stored, one fixed time, fixed attributes, no directories."""
    with zipfile.ZipFile(out_zip, "x", compression=zipfile.ZIP_STORED) as zf:
        for f in sorted(files):
            info = zipfile.ZipInfo(f, date_time=zip_time(epoch))
            info.compress_type = zipfile.ZIP_STORED
            info.create_system = 0  # MS-DOS: Windows reads only the low byte (0 = normal file)
            info.external_attr = 0o644 << 16  # set explicitly; writestr would otherwise pick 0o600
            with open(os.path.join(tree, f), "rb") as fp:
                zf.writestr(info, fp.read())


def make_fake_work(work):
    """A passing identify and a verified backup of a FAKE stock Picco (tools/device/tests)."""
    tests = os.path.join(DEVICE, "tests")
    for p in (DEVICE, tests):
        if p not in sys.path:
            sys.path.insert(0, p)
    import helpers  # noqa: E402  (tools/device/tests)
    import synth  # noqa: E402
    from fakedevice import FakeChip  # noqa: E402

    helpers.identify_and_backup(FakeChip(synth.stock_flash()), work)


def check_zip(zip_path, tree, files):
    """Unpack into a folder with a space in its name, compare, and rehearse with the UNPACKED tool."""
    say("")
    say("== Unpack the zip and rehearse with the unpacked tool")
    with zipfile.ZipFile(zip_path) as zf:
        names = zf.namelist()
        if names != sorted(files):
            die("the zip's entries are not exactly the kit's files in order")
        problems = tree_problems(names)
        if problems:
            die("the zip breaks the kit rules: %s" % "; ".join(problems))
        tmp = tempfile.mkdtemp(prefix="picco kit ")
        try:
            target = os.path.join(tmp, "unpacked kit")
            zf.extractall(target)
            for f in names:
                if sha256_file(os.path.join(target, f)) != sha256_file(os.path.join(tree, f)):
                    die("%s differs between the zip and the kit tree" % f)
            with open(os.path.join(target, "MANIFEST.sha256"), encoding="utf-8") as fp:
                problems = verify_manifest(target, fp.read())
            if problems:
                die("MANIFEST.sha256 does not verify after unpacking: %s" % "; ".join(problems))
            say("unpacked %d files into a temporary folder; all equal the tree; MANIFEST.sha256 verifies" % len(names))
            py = [sys.executable, "-E", "-s"]
            where = run(py + ["-c", "import piccoflash; print(piccoflash.__file__)"], cwd=target, show=False)
            loaded = os.path.realpath(where.stdout.strip())
            if where.returncode != 0 or not loaded.startswith(os.path.realpath(target) + os.sep):
                die("the rehearsal would not use the unpacked piccoflash (it loads %r)" % where.stdout.strip())
            say("rehearsal uses the unpacked package: <unpacked>/%s" % os.path.relpath(loaded, os.path.realpath(target)))
            plan = os.path.join(target, "kit", PLAN_FILE)
            proc = run(py + ["-m", "piccoflash", "plan", "--plan", plan], cwd=target)
            if proc.returncode != 0:
                die("`piccoflash plan` on the unpacked kit failed (exit %d)" % proc.returncode)
            work = os.path.join(tmp, "fake device work")
            say("(work folder: identify and backup of a FAKE stock Picco, tools/device/tests)")
            make_fake_work(work)
            proc = run(py + ["-m", "piccoflash", "flash", "--dry-run", "--port", "COM5", "--work-dir", work,
                             "--plan", plan], cwd=target)
            if proc.returncode != 0 or "DRY RUN RESULT: READY" not in proc.stdout:
                die("flash --dry-run on the unpacked kit did not report READY (exit %d)" % proc.returncode)
            say("UNPACKED KIT: plan verified, DRY RUN RESULT: READY")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


# --- release notes -----------------------------------------------------------------------------

def release_notes(info, zip_file, zip_sha, notes):
    preview = info["kind"] == "preview"
    out = []
    if preview:
        out.append("**Preview kit.** For checking the kit and the workflow; not for the device session.\n")
    out += [
        "Device kit for eink-learner-aid **%s** (firmware `%s`), built from commit `%s`%s." % (
            info["tag"], info["firmware_version"], info["commit"],
            " by [run %s](%s)" % (info["build"]["run_id"], info["build"]["run_url"]) if info["build"].get("run_url")
            else ""),
        "",
    ]
    if notes:
        out += [notes.strip(), ""]
    out += [
        "## Session entry point",
        "",
        "Download `%s`, right-click it and choose **Extract All**, open the extracted folder, open PowerShell "
        "there and type `powershell -ExecutionPolicy Bypass -File .\\session.ps1 setup`. Then follow %s. "
        "`KIT-README.txt` says the same." % (zip_file, "`docs\\DEVICE_SESSION.md`" if info["has_session_doc"]
                                               else "`docs\\DEVICE_EARLY_CHECK.md` (this preview has no session "
                                               "checklist)"),
        "",
        "Only the full backup made by `session.ps1 backup` can return the Picco to its stock Onyx firmware.",
        "",
        "## Assets",
        "",
        "| Asset | Bytes | SHA-256 |",
        "| --- | ---: | --- |",
        "| `%s` | %d | `%s` |" % (zip_file, info["zip"]["size"], zip_sha),
        "| `%s` (Wi-Fi update; = `kit/firmware.bin` = `sd-card/update.bin`) | %d | `%s` |" % (
            info["wifi_asset"]["name"], info["wifi_asset"]["size"], info["wifi_asset"]["sha256"]),
        "| `kit-info.json` (also inside the zip) | %d | `%s` |" % (info["kit_info"]["size"], info["kit_info"]["sha256"]),
        "",
        "## Flash images (`kit/flash-plan.json`)",
        "",
        "| Role | File | Offset | Bytes | SHA-256 |",
        "| --- | --- | --- | ---: | --- |",
    ]
    for img in info["images"]:
        out.append("| %s | `%s` | %s | %d | `%s` |" % (img["role"], img["file"], img["offset"], img["size"],
                                                     img["sha256"]))
    out += [
        "",
        "## Not yet verified on hardware",
        "",
        "Nothing in this kit has run on a Picco. In particular (tools/device/README.md, \"What could not be "
        "tested here\"): reaching the stock Picco over USB and esptool's reset into download mode; read and write "
        "times; the real eFuses and Onyx's bootloader and table; whether this firmware boots after the flash; "
        "the SD-card and Wi-Fi updates on the device; the display profiles. The kit was checked by the guard, a "
        "dry run against a fake device, and the offline `session.ps1` commands on Windows PowerShell 5.1 and "
        "PowerShell 7 (the release workflow creates a draft only after all of these passed).",
        "",
        "## Wi-Fi update",
        "",
        "Devices running an older release (or a development build of %s) are offered this release only after it "
        "is published as a normal release (not a draft, not a pre-release) and is the repository's latest; they "
        "download `%s`. See docs/RELEASING.md." % (info["base_version"], info["wifi_asset"]["name"]),
    ]
    return "\n".join(out) + "\n"


# --- main ----------------------------------------------------------------------------------------

def main(argv=None):
    del LINES[:]
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--version", required=True, help="release version MAJOR.MINOR.PATCH (tag v<version>)")
    ap.add_argument("--kind", required=True, choices=("preview", "final"))
    ap.add_argument("--out", default=os.path.join(REPO, "build", "release-kit"))
    ap.add_argument("--build-dir", default=os.path.join(REPO, ".pio", "build", ENV_NAME))
    ap.add_argument("--envdump", help="saved `pio run -e picco_2slot -t envdump` output (else it is run)")
    ap.add_argument("--device-kit", help="use this scripts/make_device_kit.py output of the same build")
    ap.add_argument("--license", help="the build's licence notices (default: scripts/build_release_license.py)")
    ap.add_argument("--notes", default="", help="text added to release-notes.md")
    ap.add_argument("--allow-dirty", action="store_true",
                    help="accept a build from a modified checkout (local trials only; the workflow never does)")
    args = ap.parse_args(argv)
    out = os.path.abspath(args.out)
    if os.path.exists(out):
        print("refusing to reuse %s; remove it or pass another --out" % out)
        return 2
    build_dir = os.path.abspath(args.build_dir)
    try:
        release_version.release_string(args.version)
    except ValueError as exc:
        print("--version: %s" % str(exc).split("=", 1)[-1])
        return 2

    commit, dirty = repo_state()
    if dirty and not args.allow_dirty:
        die("the checkout has modified tracked files (git status); a release kit is built from a clean commit")
    build_id = commit[:12] + ("-dirty" if dirty else "")
    os.makedirs(out)
    stage = os.path.join(out, "stage")
    os.makedirs(stage)

    say("== Device kit from the %s build in %s" % (ENV_NAME, rel(build_dir)))
    if args.device_kit:
        device_kit = os.path.abspath(args.device_kit)
        say("using the existing device kit %s" % rel(device_kit))
    else:
        device_kit = os.path.join(stage, "device-kit")
        make_device_kit(device_kit, build_dir, args.envdump and os.path.abspath(args.envdump))
    plan = check_device_kit(device_kit, build_dir)
    if plan["build_id"] != build_id:
        die("the plan's build id is %r but this checkout is %r; build and assemble from the same clean commit"
            % (plan["build_id"], build_id))

    say("")
    say("== Version")
    tool = naming_tool(stage)
    facts = check_version(args.version, plan, os.path.join(device_kit, "kit", "firmware.bin"), tool)

    say("")
    say("== Kit tree")
    session_src = os.path.join(REPO, SESSION_DOC[0])
    has_session_doc = os.path.isfile(session_src)
    if args.kind == "final" and not has_session_doc:
        die("a final kit needs %s, which does not exist yet; make a preview kit or add the checklist" % SESSION_DOC[0])
    docs = DOCS + ([SESSION_DOC] if has_session_doc else [])
    assets = doc_assets(docs)
    say("%d pictures linked from the kit's documents: %s" % (len(assets), ", ".join(dst for _, dst in assets) or "none"))
    entries = tool_entries() + BOOKS + docs + assets
    tree = os.path.join(out, "kit")
    for src, dst in entries:
        copy_exact(os.path.join(REPO, src), os.path.join(tree, dst))
    for name in IMAGES + (PLAN_FILE,):
        copy_exact(os.path.join(device_kit, "kit", name), os.path.join(tree, "kit", name))
    copy_exact(os.path.join(device_kit, "update.bin"), os.path.join(tree, SD_UPDATE))
    licence = args.license and os.path.abspath(args.license)
    if not licence:
        licence = os.path.join(stage, "LICENSES.txt")
        proc = run([sys.executable, os.path.join(SCRIPTS, "build_release_license.py"), "--env", ENV_NAME,
                    "--repository", os.environ.get("GITHUB_REPOSITORY", "c-tphr/eink-learner-aid"),
                    "--output", licence])
        if proc.returncode != 0:
            die("scripts/build_release_license.py failed (exit %d)" % proc.returncode)
    copy_exact(licence, os.path.join(tree, "LICENSES.txt"))

    with open(os.path.join(tree, "kit", "firmware.bin"), "rb") as fp:
        firmware = fp.read()
    epoch = commit_time(commit)
    images = [dict(img) for img in sorted(plan["images"], key=lambda i: int(i["offset"], 16))]
    info = {
        "format": "eink-learner-aid-kit-info",
        "version_format": 1,
        "version": args.version,
        "tag": facts["tag"],
        "kind": args.kind,
        "firmware_version": facts["firmware_version"],
        "base_version": base_version(),
        "commit": commit,
        "dirty": dirty,
        "commit_time_utc": datetime.datetime.fromtimestamp(epoch, datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "built_utc": plan["created_utc"],
        "build": run_info(),
        "images": images,
        "sd_update": {"path": SD_UPDATE, "size": len(firmware), "sha256": sha256_bytes(firmware)},
        "wifi_asset": {"name": facts["asset_name"], "size": len(firmware), "sha256": sha256_bytes(firmware)},
        "updater": {"asset_name_for_tag": facts["asset_name"], "verdicts": facts["updater_verdicts"]},
        "has_session_doc": has_session_doc,
        "books": [{"path": dst, "sha256": sha256_file(os.path.join(tree, dst))} for _, dst in BOOKS],
        "tools": {"piccoflash": plan["tools"]["piccoflash"], "esptool": plan["tools"]["esptool"],
                  "kit_builder_python": plan["tools"]["python"], "session_python": session_python_version(),
                  "platformio": pio_version()},
    }
    with open(os.path.join(tree, "KIT-README.txt"), "w", encoding="ascii", newline="") as fp:
        fp.write(kit_readme(info, has_session_doc))
    kit_info_text = json.dumps(info, indent=2, sort_keys=True) + "\n"
    with open(os.path.join(tree, "kit-info.json"), "w", encoding="utf-8", newline="\n") as fp:
        fp.write(kit_info_text)

    files = walk(tree)
    problems = tree_problems(files)
    if problems:
        die("the kit tree breaks the kit rules: %s" % "; ".join(problems))
    expected = sorted([dst for _, dst in entries] + ["kit/" + n for n in IMAGES + (PLAN_FILE,)]
                      + [SD_UPDATE, "LICENSES.txt", "KIT-README.txt", "kit-info.json"])
    if files != expected:
        die("the kit tree holds unexpected files: %s" % sorted(set(files) ^ set(expected)))
    for src, dst in tool_entries():
        if sha256_file(os.path.join(REPO, src)) != sha256_file(os.path.join(tree, dst)):
            die("%s is not byte-identical to %s" % (dst, src))
    for img in plan["images"]:
        if sha256_file(os.path.join(tree, "kit", img["file"])) != img["sha256"]:
            die("kit/%s is not the image the plan hashes" % img["file"])
    if sha256_file(os.path.join(tree, SD_UPDATE)) != sha256_file(os.path.join(tree, "kit", "firmware.bin")):
        die("%s differs from kit/firmware.bin" % SD_UPDATE)
    with open(os.path.join(tree, "MANIFEST.sha256"), "w", encoding="ascii", newline="\n") as fp:
        fp.write(manifest_text(tree, files))
    files = walk(tree)
    for f in files:
        say("  %10d  %s" % (os.path.getsize(os.path.join(tree, f)), f))
    say("%d files; every path Windows-safe and at most %d characters; MANIFEST.sha256 written" % (len(files), MAX_PATH))

    say("")
    say("== Zip")
    dist = os.path.join(out, "dist")
    os.makedirs(dist)
    zip_file = zip_name(args.version)
    zip_path = os.path.join(dist, zip_file)
    pack(tree, files, zip_path, epoch)
    zip_sha = sha256_file(zip_path)
    info["zip"] = {"name": zip_file, "size": os.path.getsize(zip_path), "sha256": zip_sha}
    info["kit_info"] = {"size": len(kit_info_text.encode()), "sha256": sha256_bytes(kit_info_text.encode())}
    say("%s: %d bytes, SHA-256 %s (entries stored, sorted, dated %s)" % (
        zip_file, info["zip"]["size"], zip_sha, info["commit_time_utc"]))
    check_zip(zip_path, tree, files)

    say("")
    say("== Release assets in %s" % rel(dist))
    wifi = os.path.join(dist, facts["asset_name"])
    with open(wifi, "wb") as fp:
        fp.write(firmware)
    for path in (zip_path, wifi):
        write_sha256_file(path)
    with open(os.path.join(dist, "kit-info.json"), "w", encoding="utf-8", newline="\n") as fp:
        fp.write(kit_info_text)
    with open(os.path.join(dist, "release-notes.md"), "w", encoding="utf-8", newline="\n") as fp:
        fp.write(release_notes(info, zip_file, zip_sha, args.notes))
    for f in sorted(os.listdir(dist)):
        say("  %10d  %s" % (os.path.getsize(os.path.join(dist, f)), f))
    if sha256_file(wifi) != sha256_file(os.path.join(tree, "kit", "firmware.bin")):
        die("the Wi-Fi asset differs from kit/firmware.bin")

    say("")
    say("RELEASE KIT MADE: %s (%s, %s)" % (rel(zip_path), info["tag"], args.kind))
    with open(os.path.join(out, "release-kit-report.txt"), "w", encoding="utf-8") as fp:
        fp.write("\n".join(LINES) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
