"""docs/DEVICE_EARLY_CHECK.md must quote the tool exactly.

The sheet marks every quoted string with an HTML comment just before an inline
code span (invisible when GitHub renders it):

* ``<!--cmd-->`...` ``  a command the owner types: checked against session.ps1's
  parameters and against piccoflash's command line;
* ``<!--q-->`...` ``    something piccoflash prints: must appear in the real output
  of the scenarios below, run against the fake device;
* ``<!--ps-->`...` ``   something session.ps1 prints: must appear in session.ps1
  (its set-up cannot run here).
"""

import os
import re
import shlex

import pytest

import synth
from fakedevice import FakeChip, Faults
from helpers import run_cli
from piccoflash import cli, ports, workdir

HERE = os.path.dirname(os.path.abspath(__file__))
DEVICE = os.path.dirname(HERE)
SHEET = os.path.join(DEVICE, "..", "..", "docs", "DEVICE_EARLY_CHECK.md")
SESSION = os.path.join(DEVICE, "session.ps1")
MARK = re.compile(r"<!--(cmd|q|ps)-->`([^`]+)`")


def marked():
    with open(SHEET, encoding="utf-8") as fp:
        text = fp.read()
    found = MARK.findall(text)
    unmarked_quotes = re.findall(r"(?<!-->)`(RESULT:[^`]*|REFUSED:[^`]*|STOP:[^`]*)`", text)
    assert not unmarked_quotes, "quoted tool output without a marker: %s" % unmarked_quotes
    return found


def norm(s):
    return " ".join(s.split())


@pytest.fixture(scope="module")
def real_output(tmp_path_factory):
    """Run the read-only commands through every outcome the sheet describes; return all output."""
    out = []
    base = tmp_path_factory.mktemp("sheet")

    def go(name, argv, chip=None, answers=(), **kw):
        work = str(base / name)
        out.append(run_cli([argv[0], "--work-dir", work] + argv[1:], chip, answers=answers, **kw)[1])
        return work

    good = FakeChip(synth.stock_flash())
    work = go("ok", ["identify"], good)
    import hashlib

    ack = hashlib.sha256(bytes(good.flash)).hexdigest()[-8:]
    out.append(run_cli(["backup", "--work-dir", work], good, answers=[ack])[1])
    out.append(run_cli(["summary", "--work-dir", work])[1])
    w2 = go("unacked", ["identify"], FakeChip(synth.stock_flash()))
    out.append(run_cli(["backup", "--work-dir", w2], FakeChip(synth.stock_flash()), answers=["x", "y", "z"])[1])
    go("stop", ["identify"], FakeChip(synth.stock_flash(), secure_boot=True))
    conv = FakeChip(synth.stock_flash())
    from piccoflash import layout

    conv.flash[0x8000:0x8C00] = layout.encode_table(layout.TWO_SLOT_TABLE)
    go("conv", ["identify"], conv)
    go("unreached", ["identify"], FakeChip(synth.stock_flash(), faults=Faults(unreachable=True)))
    esp = ports.PortInfo("COM5", 0x303A, 0x1001, "USB JTAG/serial debug unit")
    for name, plist in (("noport", []), ("twoports", [esp, ports.PortInfo("COM9", 0x303A, 0x1001, "")])):
        go(name, ["identify"], FakeChip(synth.stock_flash()), resolver=lambda r, u, pl=plist: ports.choose_port(r, pl))
    go("noidentify", ["backup"], FakeChip(synth.stock_flash()))
    mis = FakeChip(synth.stock_flash(), faults=Faults(flip_full_read=(2, 0x10)))
    w3 = go("mismatch", ["identify"], mis)
    out.append(run_cli(["backup", "--work-dir", w3], mis)[1])
    drop = FakeChip(synth.stock_flash())
    w4 = go("drop", ["identify"], drop)
    drop.faults.disconnect_on_read = drop.reads + 5
    out.append(run_cli(["backup", "--work-dir", w4], drop)[1])
    w5 = go("full", ["identify"], good)
    original = workdir.free_bytes
    workdir.free_bytes = lambda p: 0
    try:
        out.append(run_cli(["backup", "--work-dir", w5], good)[1])
    finally:
        workdir.free_bytes = original
    return norm("\n".join(out))


def test_sheet_has_markers():
    kinds = [k for k, _ in marked()]
    assert kinds.count("cmd") >= 5 and kinds.count("q") >= 15 and kinds.count("ps") >= 5


def test_quoted_piccoflash_output_is_real(real_output):
    for kind, s in marked():
        if kind == "q":
            assert norm(s) in real_output, "not printed by the tool: %r" % s


def test_quoted_session_ps1_output_is_in_the_script():
    source = open(SESSION, encoding="utf-8").read()
    for kind, s in marked():
        if kind == "ps":
            assert s in source, "not in session.ps1: %r" % s


def test_quoted_commands_exist():
    source = open(SESSION, encoding="utf-8").read()
    allowed = re.search(r"ValidateSet\(([^)]*)\)", source).group(1)
    commands = set(re.findall(r"'([a-z]+)'", allowed))
    params = set(re.findall(r"\[(?:string|switch)\]\$(\w+)", source))
    subs = cli.build_parser()._subparsers._group_actions[0].choices
    for kind, s in marked():
        if kind != "cmd":
            continue
        words = shlex.split(s, posix=False)
        assert words[:5] == ["powershell", "-ExecutionPolicy", "Bypass", "-File", ".\\session.ps1"], s
        command, rest = words[5], words[6:]
        assert command in commands, s
        assert command == "setup" or command in subs, s
        for word in rest:
            if word.startswith("-"):
                assert word[1:] in params, s


def test_issue_template_points_back_to_the_sheet():
    path = os.path.join(DEVICE, "..", "..", ".github", "ISSUE_TEMPLATE", "device-early-check.md")
    text = open(path, encoding="utf-8").read()
    assert "DEVICE_EARLY_CHECK.md" in text and ".bin" in text and "session-summary" in text
    assert "device-early-check.md" in open(SHEET, encoding="utf-8").read()
