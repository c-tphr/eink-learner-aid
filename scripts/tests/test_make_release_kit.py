"""Tests for scripts/make_release_kit.py and the release version (task A0, docs/RELEASING.md).

Every build here is SYNTHETIC: the images come from tools/device/tests/synth.py and the envdump is
written by hand. The kit builder runs for real on them (make_device_kit.py, the guard, the zip, the
unpack-and-rehearse step against the fake device). Run with the device tool's test requirements:

    python3 -m pip install -r tools/device/requirements-test.txt
    python3 -m pytest scripts/tests -q
"""

import configparser
import hashlib
import os
import shutil
import zipfile
from collections import namedtuple

import pytest

import make_release_kit as mrk
import release_version
import synth
from piccoflash import layout

Build = namedtuple("Build", "dir envdump license")
VERSION = "2.0.1"
BUILT = "2.0.1-picco2"


def sha(path):
    with open(path, "rb") as fp:
        return hashlib.sha256(fp.read()).hexdigest()


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as fp:
        fp.write(data)


def make_build(root, crosspoint_version=BUILT, embedded=None):
    """A synthetic picco_2slot build folder, its envdump and a licence file."""
    build = os.path.join(root, "pio", "build", "picco_2slot")
    framework = os.path.join(root, "framework", "tools", "partitions")
    write(os.path.join(build, "bootloader.bin"), synth.bootloader())
    write(os.path.join(build, "partitions.bin"), layout.encode_table(layout.TWO_SLOT_TABLE))
    write(os.path.join(framework, "boot_app0.bin"), layout.boot_app0_bytes())
    name = (embedded if embedded is not None else crosspoint_version).encode()
    write(os.path.join(build, "firmware.bin"), synth.app(300_000, name=name))
    # Also present in a real build folder; the kit must never pick it up.
    write(os.path.join(build, "firmware.factory.bin"), b"\xe9" + b"\x00" * 4095)
    envdump = os.path.join(root, "envdump.txt")
    with open(envdump, "w", encoding="utf-8") as fp:
        fp.write("{ 'BUILD_DIR': '%s',\n" % build)
        fp.write("  'CPPDEFINES': [ ('CROSSPOINT_VERSION', '\\\\\"%s\\\\\"'),\n" % crosspoint_version)
        fp.write("                  ('CROSSPOINT_RSVP', 1)],\n")
        fp.write("  'FLASH_EXTRA_IMAGES': [ ( '0x0000',\n                            '%s/bootloader.bin'),\n" % build)
        fp.write("                          ( '0x8000',\n                            '%s/partitions.bin'),\n" % build)
        fp.write("                          ( '0x15000',\n                            '%s/boot_app0.bin')],\n"
                 % framework)
        fp.write("  'PIOENV': 'picco_2slot'}\n")
    licence = os.path.join(root, "LICENSES.txt")
    with open(licence, "w", encoding="utf-8") as fp:
        fp.write("synthetic licence notices for tests\n")
    return Build(build, envdump, licence)


def run_kit(out, build, *extra, version=VERSION, kind="preview", allow_dirty=True):
    argv = ["--version", version, "--kind", kind, "--out", str(out), "--build-dir", build.dir,
            "--license", build.license] + list(extra)
    if "--device-kit" not in extra:
        argv += ["--envdump", build.envdump]
    if allow_dirty:
        argv.append("--allow-dirty")
    try:
        code = mrk.main(argv)
    except SystemExit as exc:
        code = exc.code
    return code, "\n".join(mrk.LINES)


@pytest.fixture(scope="module")
def build(tmp_path_factory):
    return make_build(str(tmp_path_factory.mktemp("synthetic-build")))


@pytest.fixture(scope="module")
def made(tmp_path_factory, build):
    out = tmp_path_factory.mktemp("made") / "release-kit"
    code, log = run_kit(out, build)
    assert code == 0, log
    dist = out / "dist"
    zf = zipfile.ZipFile(str(dist / mrk.zip_name(VERSION)))
    return {"out": out, "dist": dist, "zip": zf, "log": log, "names": zf.namelist()}


# --- the kit ------------------------------------------------------------------------------------

def tool_files():
    pkg = os.path.join(mrk.REPO, "tools", "device", "piccoflash")
    return sorted("piccoflash/" + f for f in os.listdir(pkg) if f.endswith(".py"))


def test_layout_is_exactly_the_kit(made):
    expected = sorted(tool_files() + [
        "KIT-README.txt", "LICENSES.txt", "MANIFEST.sha256", "kit-info.json",
        "session.ps1", "requirements.txt", "requirements-build.txt",
        "kit/flash-plan.json", "kit/bootloader.bin", "kit/partitions.bin", "kit/boot_app0.bin", "kit/firmware.bin",
        "sd-card/update.bin", "sd-card/books/fixture.epub", "sd-card/books/fixture.prepared.epub",
        "sd-card/books/paine-essays.epub", "sd-card/books/paine-essays.prepared.epub",
        "docs/DEVICE_EARLY_CHECK.md", "docs/paine-essays.validation-report.md", "docs/paine-essays.LICENSE.md",
    ] + (["docs/DEVICE_SESSION.md"] if os.path.isfile(os.path.join(mrk.REPO, "docs", "DEVICE_SESSION.md")) else []))
    assert made["names"] == expected
    assert sorted(os.listdir(str(made["dist"]))) == sorted([
        "picco-2slot-2.0.1.zip", "picco-2slot-2.0.1.zip.sha256", "eink-learner-aid-v2.0.1-picco2.bin",
        "eink-learner-aid-v2.0.1-picco2.bin.sha256", "kit-info.json", "release-notes.md"])


def test_manifest_lists_and_verifies_every_file(made, tmp_path):
    made["zip"].extractall(str(tmp_path))
    text = (tmp_path / "MANIFEST.sha256").read_text(encoding="ascii")
    assert mrk.verify_manifest(str(tmp_path), text) == []
    listed = [line.split("  ", 1)[1] for line in text.splitlines()]
    assert listed == [n for n in made["names"] if n != "MANIFEST.sha256"]
    # A changed file is caught.
    (tmp_path / "docs" / "DEVICE_EARLY_CHECK.md").write_text("changed", encoding="utf-8")
    assert mrk.verify_manifest(str(tmp_path), text) == ["docs/DEVICE_EARLY_CHECK.md does not match its "
                                                        "MANIFEST.sha256 hash"]


def test_images_are_exactly_what_the_plan_hashes(made, build):
    zf = made["zip"]
    import json
    plan = json.loads(zf.read("kit/flash-plan.json"))
    assert {i["file"] for i in plan["images"]} == set(mrk.IMAGES)
    for img in plan["images"]:
        assert hashlib.sha256(zf.read("kit/" + img["file"])).hexdigest() == img["sha256"]
    firmware = zf.read("kit/firmware.bin")
    assert firmware == open(os.path.join(build.dir, "firmware.bin"), "rb").read()
    assert zf.read("sd-card/update.bin") == firmware
    wifi = made["dist"] / "eink-learner-aid-v2.0.1-picco2.bin"
    assert wifi.read_bytes() == firmware
    assert (made["dist"] / "eink-learner-aid-v2.0.1-picco2.bin.sha256").read_text() == \
        "%s  eink-learner-aid-v2.0.1-picco2.bin\n" % hashlib.sha256(firmware).hexdigest()
    z = made["dist"] / "picco-2slot-2.0.1.zip"
    assert (made["dist"] / "picco-2slot-2.0.1.zip.sha256").read_text() == "%s  picco-2slot-2.0.1.zip\n" % sha(str(z))


def test_tool_is_byte_identical_to_tools_device(made):
    zf = made["zip"]
    pairs = [("tools/device/" + n, n) for n in ("session.ps1", "requirements.txt", "requirements-build.txt")]
    pairs += [("tools/device/" + n, n) for n in tool_files()]
    for src, dst in pairs:
        with open(os.path.join(mrk.REPO, src), "rb") as fp:
            assert zf.read(dst) == fp.read(), dst
    assert sorted(n for n in made["names"] if n.startswith("piccoflash/")) == tool_files()


def test_nothing_forbidden_in_the_zip(made):
    names = made["names"]
    assert mrk.tree_problems(names) == []
    assert [n for n in names if n.lower().endswith(".bin")] == sorted(mrk.ALLOWED_BIN)
    for n in names:
        low = n.lower()
        assert "factory" not in low and "merged" not in low and "picco-backup" not in low
        assert not set(low.split("/")[:-1]) & {"work", "runtime", ".pio", "__pycache__", "build"}
        assert len(n) <= 179 and mrk.windows_name_problems(n) == []


def test_zip_entries_are_deterministic(made):
    infos = made["zip"].infolist()
    assert [i.filename for i in infos] == sorted(i.filename for i in infos)
    assert {i.compress_type for i in infos} == {zipfile.ZIP_STORED}
    assert len({i.date_time for i in infos}) == 1
    assert {(i.create_system, i.external_attr) for i in infos} == {(0, 0o644 << 16)}


def test_same_build_gives_the_same_zip(made, build, tmp_path):
    device_kit = str(made["out"] / "stage" / "device-kit")
    code, log = run_kit(tmp_path / "again", build, "--device-kit", device_kit)
    assert code == 0, log
    assert sha(str(tmp_path / "again" / "dist" / "picco-2slot-2.0.1.zip")) == \
        sha(str(made["dist"] / "picco-2slot-2.0.1.zip"))


def test_pack_ignores_file_times(made, tmp_path):
    tree = str(made["out"] / "kit")
    files = mrk.walk(tree)
    mrk.pack(tree, files, str(tmp_path / "a.zip"), 1_700_000_000)
    os.utime(os.path.join(tree, "KIT-README.txt"), (1, 1))
    mrk.pack(tree, files, str(tmp_path / "b.zip"), 1_700_000_000)
    assert sha(str(tmp_path / "a.zip")) == sha(str(tmp_path / "b.zip"))


def test_unpacked_kit_was_rehearsed(made):
    log = made["log"]
    assert "rehearsal uses the unpacked package: <unpacked>/piccoflash/__init__.py" in log
    assert "DRY RUN RESULT: READY" in log
    assert log.rstrip().endswith("RELEASE KIT MADE: %s (v2.0.1, preview)"
                                 % mrk.rel(str(made["dist"] / "picco-2slot-2.0.1.zip")))


def test_kit_readme_and_info(made):
    text = made["zip"].read("KIT-README.txt").decode("ascii")
    assert "\r\n" in text and "\n" not in text.replace("\r\n", "")
    assert "Version:  2.0.1 (firmware 2.0.1-picco2), PREVIEW kit" in text
    commit = mrk.repo_state()[0]
    assert "Commit:   %s" % commit in text
    assert "Only the full\r\nbackup made by 'session.ps1 backup' can return the device to its stock Onyx" in text
    assert "Open first: docs\\DEVICE_" in text
    import json
    info = json.loads(made["zip"].read("kit-info.json"))
    assert info["version"] == "2.0.1" and info["tag"] == "v2.0.1" and info["firmware_version"] == BUILT
    assert info["commit"] == commit and info["kind"] == "preview"
    assert [i["role"] for i in info["images"]] == ["bootloader", "partition_table", "boot_app0", "app"]
    assert info["wifi_asset"]["name"] == "eink-learner-aid-v2.0.1-picco2.bin"
    assert info["tools"]["piccoflash"] and info["tools"]["esptool"] == "5.6.0"
    assert (made["dist"] / "kit-info.json").read_bytes() == made["zip"].read("kit-info.json")
    notes = (made["dist"] / "release-notes.md").read_text(encoding="utf-8")
    assert "eink-learner-aid-v2.0.1-picco2.bin" in notes and "Not yet verified on hardware" in notes
    assert sha(str(made["dist"] / "picco-2slot-2.0.1.zip")) in notes


# --- path rules -------------------------------------------------------------------------------

@pytest.mark.parametrize("path, why", [
    ("kit/firmware.factory.bin", "merged, factory"),
    ("kit/merged-firmware.bin", "merged, factory"),
    ("work/picco-backup-24587c123456-x.bin", "forbidden folder"),
    ("picco-backup-24587c123456-x.json", "merged, factory, backup"),
    ("runtime/py313/tools/python.exe", "forbidden folder"),
    (".pio/build/picco_2slot/firmware.elf", "forbidden folder"),
    ("piccoflash/__pycache__/guard.cpython-313.pyc", "forbidden folder"),
    ("sd-card/firmware.bin", "the only .bin files allowed"),
    ("docs/CON.md", "reserved Windows name"),
    ("docs/lpt1", "reserved Windows name"),
    ("docs/a:b.md", "contains :"),
    ("docs/notes.", "ends with a dot"),
    ("docs/notes ", "ends with a dot or space"),
    ("docs/café.md", "printable ASCII"),
    ("docs/" + "x" * 180, "characters, the limit is 179"),
    ("docs\\x.md", "forward slashes"),
])
def test_tree_rules_refuse(path, why):
    good = sorted(mrk.ALLOWED_BIN)
    problems = mrk.tree_problems(good + [path])
    assert any(why in p for p in problems), problems


def test_tree_rules_refuse_case_collisions_and_missing_images():
    good = sorted(mrk.ALLOWED_BIN)
    assert any("differ only in case" in p for p in mrk.tree_problems(good + ["docs/a.md", "Docs/A.md"]))
    assert "kit/boot_app0.bin is missing" in mrk.tree_problems([b for b in good if "boot_app0" not in b])


# --- refusals ---------------------------------------------------------------------------------

def copy_build(build, root):
    new = make_build(str(root))
    for name in ("bootloader.bin", "partitions.bin", "firmware.bin"):
        shutil.copyfile(os.path.join(build.dir, name), os.path.join(new.dir, name))
    return new


def test_refuses_a_build_the_guard_refuses(build, tmp_path):
    bad = copy_build(build, tmp_path / "b")
    fw = os.path.join(bad.dir, "firmware.bin")
    data = bytearray(open(fw, "rb").read())
    data[5000] ^= 0xFF  # breaks the image's checksum and appended SHA-256
    write(fw, bytes(data))
    code, log = run_kit(tmp_path / "out", bad)
    assert code == 1
    assert "KIT NOT MADE: the guard refused the images" in log
    assert log.rstrip().endswith("RELEASE KIT NOT MADE: scripts/make_device_kit.py refused the build (exit 1); "
                                 "see its reasons above")
    assert not (tmp_path / "out" / "dist").exists()


def test_refuses_an_added_factory_image(made, build, tmp_path):
    dk = tmp_path / "device-kit"
    shutil.copytree(str(made["out"] / "stage" / "device-kit"), str(dk))
    shutil.copyfile(str(dk / "kit" / "firmware.bin"), str(dk / "kit" / "firmware.factory.bin"))
    code, log = run_kit(tmp_path / "out", build, "--device-kit", str(dk))
    assert code == 1
    assert "must hold exactly boot_app0.bin, bootloader.bin, firmware.bin, flash-plan.json, partitions.bin; it " \
           "holds boot_app0.bin, bootloader.bin, firmware.bin, firmware.factory.bin" in log


def test_refuses_a_tampered_image(made, build, tmp_path):
    dk = tmp_path / "device-kit"
    shutil.copytree(str(made["out"] / "stage" / "device-kit"), str(dk))
    data = bytearray((dk / "kit" / "bootloader.bin").read_bytes())
    data[100] ^= 1
    (dk / "kit" / "bootloader.bin").write_bytes(bytes(data))
    code, log = run_kit(tmp_path / "out", build, "--device-kit", str(dk))
    assert code == 1
    assert "kit/bootloader.bin differs from" in log and "the kit must come from this one build" in log


def test_refuses_an_image_that_no_longer_matches_the_plan(made, build, tmp_path):
    dk = tmp_path / "device-kit"
    shutil.copytree(str(made["out"] / "stage" / "device-kit"), str(dk))
    (dk / "kit" / "boot_app0.bin").write_bytes(b"\x00" * 8192)
    code, log = run_kit(tmp_path / "out", build, "--device-kit", str(dk))
    assert code == 1
    assert "`piccoflash plan` refused" in log


def test_refuses_a_version_mismatch(build, tmp_path):
    code, log = run_kit(tmp_path / "out", build, version="2.0.2")
    assert code == 1
    assert "version mismatch: the build's CROSSPOINT_VERSION is '2.0.1-picco2', the requested version needs " \
           "'2.0.2-picco2'" in log


def test_refuses_a_development_build(tmp_path):
    dev = make_build(str(tmp_path / "b"), crosspoint_version="2.0.0-dev-picco2")
    code, log = run_kit(tmp_path / "out", dev, version="2.0.1")
    assert code == 1 and "version mismatch: the build's CROSSPOINT_VERSION is '2.0.0-dev-picco2'" in log


def test_refuses_a_firmware_without_the_version_string(tmp_path):
    odd = make_build(str(tmp_path / "b"), embedded="crosspoint")
    code, log = run_kit(tmp_path / "out", odd)
    assert code == 1 and "firmware.bin does not contain the string '2.0.1-picco2'" in log


def test_refuses_a_version_not_above_the_development_version(tmp_path):
    old = make_build(str(tmp_path / "b"), crosspoint_version="2.0.0-picco2")
    code, log = run_kit(tmp_path / "out", old, version="2.0.0")
    assert code == 1 and "version 2.0.0 is not above 2.0.0" in log


@pytest.mark.parametrize("version", ["2.0", "v2.0.1", "02.0.1", "2.0.1-rc1", "1234567890.0.0", ""])
def test_refuses_a_malformed_version(build, tmp_path, version):
    code, _ = run_kit(tmp_path / "out", build, version=version)
    assert code == 2


def test_final_kit_needs_the_session_checklist(build, tmp_path, monkeypatch):
    monkeypatch.setattr(mrk, "SESSION_DOC", (str(tmp_path / "missing.md"), "docs/DEVICE_SESSION.md"))
    code, log = run_kit(tmp_path / "out", build, kind="final")
    assert code == 1 and "a final kit needs" in log
    doc = tmp_path / "DEVICE_SESSION.md"
    doc.write_text("# checklist\n", encoding="utf-8")
    monkeypatch.setattr(mrk, "SESSION_DOC", (str(doc), "docs/DEVICE_SESSION.md"))
    code, log = run_kit(tmp_path / "out2", build, kind="final")
    assert code == 0, log
    with zipfile.ZipFile(str(tmp_path / "out2" / "dist" / "picco-2slot-2.0.1.zip")) as zf:
        assert zf.read("docs/DEVICE_SESSION.md") == b"# checklist\n"
        assert "Open first: docs\\DEVICE_SESSION.md" in zf.read("KIT-README.txt").decode()


def test_refuses_a_modified_checkout(build, tmp_path, monkeypatch):
    head = mrk.repo_state()[0]
    monkeypatch.setattr(mrk, "repo_state", lambda: (head, True))
    code, log = run_kit(tmp_path / "out", build, allow_dirty=False)
    assert code == 1 and "the checkout has modified tracked files" in log


def test_check_zip_catches_a_changed_or_extra_entry(made, tmp_path):
    tree = str(made["out"] / "kit")
    files = mrk.walk(tree)
    changed = str(tmp_path / "changed.zip")
    with zipfile.ZipFile(changed, "w") as zf:
        for f in files:
            data = open(os.path.join(tree, f), "rb").read()
            zf.writestr(f, data + b"x" if f == "docs/DEVICE_EARLY_CHECK.md" else data)
    with pytest.raises(SystemExit):
        mrk.check_zip(changed, tree, files)
    assert "docs/DEVICE_EARLY_CHECK.md differs between the zip and the kit tree" in mrk.LINES[-1]
    extra = str(tmp_path / "extra.zip")
    with zipfile.ZipFile(extra, "w") as zf:
        for f in files + ["work/x.txt"]:
            zf.writestr(f, b"")
    with pytest.raises(SystemExit):
        mrk.check_zip(extra, tree, files)
    assert "not exactly the kit's files" in mrk.LINES[-1]


# --- the updater's naming and version rules ------------------------------------------------------

@pytest.fixture(scope="module")
def tool(tmp_path_factory):
    return mrk.naming_tool(str(tmp_path_factory.mktemp("naming")))


@pytest.mark.parametrize("version", ["2.0.1", "2.1.0", "10.20.30", "999.999.999"])
def test_published_asset_name_is_what_the_updater_looks_for(tool, version):
    code, name = mrk.updater(tool, "asset-name", "v" + version)
    assert code == 0 and name == mrk.wifi_asset_name(version)


def test_updater_finds_nothing_for_a_tag_too_long_for_its_buffer(tool):
    assert mrk.updater(tool, "asset-name", "v123456789.123456789.1") == (1, "none")


def test_naming_tool_uses_the_updaters_buffer_size():
    with open(os.path.join(mrk.REPO, "src", "network", "OtaUpdater.cpp"), encoding="utf-8") as fp:
        assert mrk.UPDATER_BUFFER_RE.search(fp.read())
    with open(os.path.join(mrk.REPO, "scripts", "release_naming_cli.cpp"), encoding="utf-8") as fp:
        assert "constexpr size_t kAssetNameCapacity = 48;" in fp.read()


@pytest.mark.parametrize("tag, running, verdict", [
    ("v2.0.1", "2.0.0-picco2", "install"),       # devices on the D4 builds
    ("v2.0.1", "2.0.0-dev-picco2", "install"),   # a development build
    ("v2.0.1", "2.0.1-picco2", "no update"),     # the release build itself
    ("v2.0.0", "2.0.1-picco2", "no update"),     # an older tag
    ("v2.0.2", "2.0.1-picco2", "install"),
    ("v2.0.10", "2.0.9-picco2", "install"),
])
def test_updater_verdicts(tool, tag, running, verdict):
    assert mrk.updater(tool, "is-newer", tag, running) == (0, verdict)


def test_updater_parses_tag_and_built_string_to_the_same_numbers(tool):
    assert mrk.updater(tool, "version", "v2.0.1") == (0, "2.0.1")
    assert mrk.updater(tool, "version", release_version.release_string("2.0.1")) == (0, "2.0.1")
    assert mrk.updater(tool, "version", "2.0.1") == (0, "2.0.1")


# --- the version mechanism ----------------------------------------------------------------------

def test_release_version_rules():
    assert release_version.release_string("2.0.1") == "2.0.1-picco2"
    assert release_version.release_string("10.0.123456789") == "10.0.123456789-picco2"
    for bad in ("2.0", "2.0.1.1", "v2.0.1", "02.0.1", "2.0.1-rc1", " 2.0.1", "1234567890.0.0", ""):
        with pytest.raises(ValueError):
            release_version.release_string(bad)


def test_release_version_replaces_the_one_define():
    flags = ["-DFOO=1", '-DCROSSPOINT_VERSION=\\"2.0.0-dev-picco2\\"', "-DBAR"]
    assert release_version.replace_define(flags, "2.0.1-picco2") == \
        ["-DFOO=1", '-DCROSSPOINT_VERSION=\\"2.0.1-picco2\\"', "-DBAR"]
    with pytest.raises(ValueError):
        release_version.replace_define(["-DFOO"], "2.0.1-picco2")
    with pytest.raises(ValueError):
        release_version.replace_define(flags + flags, "2.0.1-picco2")


def test_picco_2slot_uses_the_release_version_script():
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.read(os.path.join(mrk.REPO, "platformio.project.ini"), encoding="utf-8")
    env = cfg["env:picco_2slot"]
    assert "pre:scripts/release_version.py" in env["extra_scripts"].split()
    defines = [f.strip() for f in env["build_flags"].splitlines() if "CROSSPOINT_VERSION" in f]
    assert defines == ['-DCROSSPOINT_VERSION=\\"${crosspoint.version}-dev-picco2\\"']


# --- the workflow's version check (.github/scripts/release_preflight.py) ----------------------------

def load_preflight():
    import importlib.util
    path = os.path.join(mrk.REPO, ".github", "scripts", "release_preflight.py")
    spec = importlib.util.spec_from_file_location("release_preflight", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


@pytest.mark.parametrize("tags, releases, code", [
    ([], [], 0),
    (["v2.0.0"], [{"tag_name": "v2.0.0"}], 0),
    (["upstream-sync", "latest"], [], 0),                    # names that are not versions are ignored
    (["v2.0.1"], [], 1),                                     # the tag exists
    (["2.0.1"], [], 1),                                      # the same version without the v
    ([], [{"tag_name": "v2.0.1", "draft": True}], 1),        # a draft release holds the version
    ([], [{"tag_name": "v2.1.0", "prerelease": True}], 1),   # a higher version exists
    (["v2.0.10"], [], 1),                                    # numeric, not text, order
])
def test_release_preflight(monkeypatch, capsys, tags, releases, code):
    mod = load_preflight()
    monkeypatch.setenv("GITHUB_REPOSITORY", "owner/repo")
    monkeypatch.setattr(mod, "api_list", lambda path: [{"name": t} for t in tags] if "/tags" in path else releases)
    assert mod.main(["release_preflight.py", "2.0.1"]) == code, capsys.readouterr().out
