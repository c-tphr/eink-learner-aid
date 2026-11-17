"""The checks behind the device-tool-windows step that runs the sheets' PowerShell lines
(.github/scripts/device_doc_powershell.py). Running the lines needs Windows; this checks, on any
system, that the script finds every line, and that the kit zip it builds is a real kit: its
MANIFEST.sha256 and .sha256 files are the kit builder's, and the fields the lines read are where
they expect them."""

import hashlib
import importlib.util
import os
import zipfile

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
SCRIPT = os.path.join(REPO, ".github", "scripts", "device_doc_powershell.py")

spec = importlib.util.spec_from_file_location("device_doc_powershell", SCRIPT)
ddp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ddp)

import make_release_kit as mrk  # noqa: E402  (scripts/ is on the path through the script)


def test_every_marked_line_is_a_known_kind():
    found = [(sheet, ddp.classify(line)) for sheet, line in ddp.doc_lines()]
    assert sorted(set(k for _, k in found)) == sorted(ddp.KINDS)
    assert all(sheet == "DEVICE_SESSION.md" for sheet, _ in found)


def test_an_unknown_line_stops_the_script():
    with pytest.raises(SystemExit):
        ddp.classify("Get-ChildItem")


def test_placeholders_are_filled():
    filled = ddp.fill(ddp.KINDS["zip-hash"], r"D:\stick")
    assert "<" not in filled and filled.count("picco-2slot-%s.zip" % ddp.VERSION) == 2
    filled = ddp.fill(ddp.KINDS["backup-copy-hash"], r"D:\stick")
    assert filled == "Get-FileHash " + os.path.join(r"D:\stick", ddp.STICK_NAME)


def test_the_sheet_still_says_what_the_checks_expect():
    with open(ddp.SHEETS[0], encoding="utf-8") as fp:
        text = fp.read()
    for words in ddp.SHEET_WORDS:
        assert words in text


def test_kit_zip_is_a_real_kit(tmp_path):
    zip_path = ddp.make_kit_zip(str(tmp_path / "made"))
    assert os.path.basename(zip_path) == mrk.zip_name(ddp.VERSION)
    with open(zip_path, "rb") as fp:
        digest = hashlib.sha256(fp.read()).hexdigest()
    with open(zip_path + ".sha256", encoding="ascii") as fp:
        sha_text = fp.read()
    # (Get-Content <zip>.sha256).Split(' ')[0]: the first space-separated word
    assert sha_text.split(" ")[0] == digest and sha_text == "%s  %s\n" % (digest, os.path.basename(zip_path))
    unpacked = tmp_path / "unpacked"
    with zipfile.ZipFile(zip_path) as zf:
        names = zf.namelist()
        zf.extractall(str(unpacked))
    assert not mrk.tree_problems(names)
    assert {"session.ps1", "kit/flash-plan.json", "docs/DEVICE_SESSION.md", "MANIFEST.sha256"} <= set(names)
    manifest = (unpacked / "MANIFEST.sha256").read_text(encoding="ascii")
    assert mrk.verify_manifest(str(unpacked), manifest) == []
    for line in manifest.splitlines():
        # $_.Split(' ', 3): the hash, an empty field, then the path (spaces in it kept)
        p = line.split(" ", 2)
        assert p[1] == "" and (unpacked / p[2]).is_file()
        assert hashlib.sha256((unpacked / p[2]).read_bytes()).hexdigest() == p[0]
