"""Every command the tool prints for the owner to type is the full form the checklists use.

The owner types what the tool shows, often at a bad moment (a flash that stopped part-way). A PC
that blocks scripts refuses ``.\\session.ps1 ...``; the checklists always write
``powershell -ExecutionPolicy Bypass -File .\\session.ps1 <command> ...``. One helper,
``common.session_command``, writes that text; session.ps1 holds the same launcher text.

The walk below reaches every message that prints a command (most of them failure paths) through
the real command line and the fake device, from a work folder whose path has a space, an
apostrophe and a dollar sign, and checks every mention of session.ps1 in what was shown. The
static checks catch a message the walk does not reach.
"""

import ast
import glob
import io
import os
import re

import pytest

import synth
from fakedevice import FakeChip, Faults
from helpers import identify_and_backup, make_kit, run_cli
from piccoflash import common, esptool_device, layout, ports, workdir
from piccoflash.device import DeviceError, DeviceNotReached
from piccoflash.ui import UI

HERE = os.path.dirname(os.path.abspath(__file__))
DEVICE = os.path.dirname(HERE)
PACKAGE = os.path.join(DEVICE, "piccoflash")
SESSION_PS1 = os.path.join(DEVICE, "session.ps1")

COMMANDS = ("setup", "identify", "backup", "plan", "flash", "monitor", "restore", "ports", "summary")
FULL = re.compile(r"powershell -ExecutionPolicy Bypass -File \.\\session\.ps1 (%s)\b" % "|".join(COMMANDS))
# A path like the owner's, made harder: a space, an apostrophe and a dollar sign.
AWKWARD = "kit folder's $copy"


def read(path):
    with open(path, encoding="utf-8") as fp:
        return fp.read()


def ps_words(line):
    """Split a typed PowerShell command line into its arguments, as PowerShell would (bare words,
    "double-quoted" without $ or backtick, 'single-quoted' with doubled quotes)."""
    words, i = [], 0
    singles = "'\u2018\u2019\u201a\u201b"
    while i < len(line):
        c = line[i]
        if c == " ":
            i += 1
        elif c == '"':
            end = line.index('"', i + 1)
            body = line[i + 1:end]
            assert not set(body) & set("$`\u201c\u201d\u201e"), "would be expanded or end early: %r" % body
            words.append(body)
            i = end + 1
        elif c in singles:
            out, i = [], i + 1
            while True:
                if line[i] in singles:
                    if i + 1 < len(line) and line[i + 1] in singles:
                        out.append(line[i])
                        i += 2
                        continue
                    i += 1
                    break
                out.append(line[i])
                i += 1
            words.append("".join(out))
        else:
            end = line.find(" ", i)
            end = len(line) if end < 0 else end
            word = line[i:end]
            assert not set(word) & set("\"'$`(){};,@&|<>#"), "a bare word PowerShell would read differently: %r" % word
            words.append(word)
            i = end
    return words


def owner_commands(text):
    """Every line that shows a session.ps1 command, and the command text on it."""
    found = []
    for line in text.splitlines():
        if "session.ps1" in line:
            m = FULL.search(line)
            assert m, "a session.ps1 command not in the full form: %r" % line
            assert line.count("session.ps1") == 1, line
            found.append(line[m.start():].strip())
    return found


# --- the helper ---------------------------------------------------------------------------------

@pytest.mark.parametrize("arg", [
    r"C:\picco\picco-2slot-2.0.1\work\picco-backup-24587c123456-20261119T120000.123Z.bin",
    r"C:\picco session\work\x.bin",
    r"C:\Users\Jane O'Brien\picco\work\x.bin",
    r"D:\backup $1\x.bin",
    "D:\\it\u2019s `here`\\x.bin",
    "D:\\\u201cquoted\u201d\\x.bin",
    "<your backup file>",
    "COM5",
    "-Reset",
])
def test_ps_arg_round_trips(arg):
    line = common.session_command("restore", arg)
    assert line.startswith(common.SESSION_LAUNCHER + " restore ")
    assert ps_words(line)[6:] == [arg]


def test_paths_are_always_quoted():
    assert common.ps_arg(r"C:\picco\work\x.bin") == r'"C:\picco\work\x.bin"'
    assert common.ps_arg("COM5") == "COM5" and common.ps_arg("-Reset") == "-Reset"
    assert common.ps_arg("-Reset;x") != "-Reset;x"


def test_launcher_is_the_same_in_session_ps1():
    source = read(SESSION_PS1)
    m = re.search(r"^\$Launcher = '([^']+)'$", source, re.M)
    assert m and m.group(1) == common.SESSION_LAUNCHER
    assert "--launcher" not in source


def test_session_ps1_prints_only_full_commands():
    """Every session.ps1 command in the script (its help and its messages) is the full form.
    Line comments are not shown to the owner; the help block is (Get-Help, when no command is given)."""
    for n, line in enumerate(read(SESSION_PS1).splitlines(), 1):
        if line.lstrip().startswith("#"):
            continue
        for m in re.finditer(r"session\.ps1", line):
            before = line[:m.start()]
            assert before.endswith("powershell -ExecutionPolicy Bypass -File .\\"), (n, line)
        if "Write-Host" in line and re.search(r"\b(restore|identify|backup|flash|summary)\b.*\.bin", line):
            assert "$Launcher" in line, (n, line)


def string_constants(path):
    """(line, text) of every string literal in a module, docstrings left out."""
    tree = ast.parse(read(path))
    docstrings = set()
    for node in ast.walk(tree):
        if isinstance(node, (ast.Module, ast.FunctionDef, ast.ClassDef)) and node.body:
            first = node.body[0]
            if isinstance(first, ast.Expr) and isinstance(first.value, ast.Constant):
                docstrings.add(id(first.value))
    return [(n.lineno, n.value) for n in ast.walk(tree)
            if isinstance(n, ast.Constant) and isinstance(n.value, str) and id(n) not in docstrings]


def test_no_message_writes_a_command_by_hand():
    """Only common.SESSION_LAUNCHER names session.ps1; no message holds a short form or a quoted
    command name to run ('Run 'identify' first', 'run session.ps1')."""
    for path in sorted(glob.glob(os.path.join(PACKAGE, "*.py"))):
        for line, text in string_constants(path):
            if "session.ps1" in text:
                assert text == common.SESSION_LAUNCHER and path.endswith("common.py"), (path, line, text)
            assert not re.search(r"python -m piccoflash (%s)\b" % "|".join(COMMANDS), text), (path, line, text)
            assert not re.search(r"(?i)\brun '(%s)\b" % "|".join(COMMANDS), text), (path, line, text)


# --- every message that prints a command, reached through the real command line --------------

@pytest.fixture(scope="module")
def walk(tmp_path_factory):
    """(scenario, exit code, output) for each path that tells the owner to run something."""
    root = str(tmp_path_factory.mktemp("hints"))
    base = os.path.join(root, AWKWARD)
    os.makedirs(base)
    out = []

    def go(name, argv, chip=None, answers=(), **kw):
        code, text = run_cli(argv, chip, answers=answers, **kw)
        out.append((name, code, text))
        return code

    def session(name):
        b = os.path.join(base, name)
        os.makedirs(b)
        chip = FakeChip(synth.stock_flash())
        kit = make_kit(b)
        work = os.path.join(b, "work")
        side, rec = identify_and_backup(chip, work)
        return chip, kit, work, os.path.join(work, rec["file"])

    def w(name):
        return os.path.join(base, name, "work")

    good = FakeChip(synth.stock_flash())
    go("identify PASS", ["identify", "--work-dir", w("ok")], good)
    go("identify STOP", ["identify", "--work-dir", w("stop")], FakeChip(synth.stock_flash(), secure_boot=True))
    conv = FakeChip(synth.stock_flash())
    conv.flash[0x8000:0x8C00] = layout.encode_table(layout.TWO_SLOT_TABLE)
    go("identify ALREADY CONVERTED", ["identify", "--work-dir", w("conv")], conv)
    go("identify not reached", ["identify", "--work-dir", w("unreached")],
       FakeChip(synth.stock_flash(), faults=Faults(unreachable=True)))
    esp = ports.PortInfo("COM5", 0x303A, 0x1001, "USB JTAG/serial debug unit")
    for name, plist in (("no port", []), ("two ports", [esp, ports.PortInfo("COM9", 0x303A, 0x1001, "")])):
        go("identify " + name, ["identify", "--work-dir", w(name)], FakeChip(synth.stock_flash()),
           resolver=lambda r, u, pl=plist: ports.choose_port(r, pl))
    go("backup without identify", ["backup", "--work-dir", w("noidentify")], FakeChip(synth.stock_flash()))
    other = FakeChip(synth.stock_flash())
    other.mac = "24:58:7c:00:00:09"
    go("backup of another Picco", ["backup", "--work-dir", w("ok")], other)
    mis = FakeChip(synth.stock_flash(), faults=Faults(flip_full_read=(2, 0x10)))
    go("identify for mismatch", ["identify", "--work-dir", w("mismatch")], mis)
    go("backup reads differ", ["backup", "--work-dir", w("mismatch")], mis)
    drop = FakeChip(synth.stock_flash())
    go("identify for drop", ["identify", "--work-dir", w("drop")], drop)
    drop.faults.disconnect_on_read = drop.reads + 5
    go("backup connection drops", ["backup", "--work-dir", w("drop")], drop)
    go("summary without work folder", ["summary", "--work-dir", w("none")])

    chip, kit, work, backup = session("flash")
    go("flash dry run, empty work folder", ["flash", "--dry-run", "--work-dir", w("empty"), "--plan", kit])
    go("flash, empty work folder", ["flash", "--work-dir", w("empty"), "--plan", kit], chip,
       answers=[common.FLASH_PHRASE])
    go("flash DONE", ["flash", "--work-dir", work, "--plan", kit], chip, answers=[common.FLASH_PHRASE])
    chip.faults.disconnect_on_write = True
    go("restore did not complete", ["restore", "--work-dir", work, backup], chip, answers=[common.RESTORE_PHRASE])

    chip, kit, work, backup = session("partway")
    chip.faults.fail_write_at = (0x8000, 100)
    go("flash stops part-way", ["flash", "--work-dir", work, "--plan", kit], chip, answers=[common.FLASH_PHRASE])

    chip, kit, work, backup = session("noident")
    for p in workdir.identify_records(work):
        os.remove(p)
    go("flash dry run, identify record gone", ["flash", "--dry-run", "--work-dir", work, "--plan", kit])

    chip, kit, work, backup = session("converted")
    chip.flash[0x8000:0x8C00] = layout.encode_table(layout.TWO_SLOT_TABLE)
    go("flash on a converted Picco", ["flash", "--work-dir", work, "--plan", kit], chip,
       answers=[common.FLASH_PHRASE])
    return {name: (code, text) for name, code, text in out}


EXPECTED = {
    # scenario: the commands it must show the owner (restore with the backup's path: checked below)
    "identify PASS": ["backup"],
    "identify STOP": ["summary"],
    "identify ALREADY CONVERTED": ["restore"],
    "backup without identify": ["identify"],
    "backup of another Picco": ["identify"],
    "backup reads differ": ["backup"],
    "backup connection drops": ["backup"],
    "summary without work folder": ["identify"],
    "flash dry run, empty work folder": ["identify", "backup"] * 2,  # under Preconditions and the result
    "flash, empty work folder": ["identify", "backup"],
    "flash DONE": ["monitor"],
    "restore did not complete": ["restore"],
    "flash stops part-way": ["restore"],
    "flash dry run, identify record gone": ["identify"] * 2,
}


@pytest.mark.parametrize("scenario", sorted(EXPECTED))
def test_each_hint_is_the_full_command(walk, scenario):
    code, text = walk[scenario]
    shown = owner_commands(text)
    assert [FULL.match(c).group(1) for c in shown] == EXPECTED[scenario], text
    for line in text.splitlines():
        if "session.ps1" in line:
            assert line == "    " + line.strip(), "a command goes on a line of its own, indented: %r" % line


def test_every_scenario_was_reached(walk):
    codes = {name: code for name, (code, _) in walk.items()}
    assert codes["flash stops part-way"] == common.FLASH_FAILED
    assert codes["restore did not complete"] == common.RESTORE_FAILED
    assert codes["flash DONE"] == common.OK
    assert codes["identify ALREADY CONVERTED"] == common.ALREADY_CONVERTED
    assert codes["flash on a converted Picco"] == common.STOP
    assert all("session.ps1" not in walk[n][1] for n in ("identify not reached", "identify no port",
                                                         "identify two ports"))


def test_no_output_has_a_short_form(walk):
    for name, (code, text) in walk.items():
        owner_commands(text)  # asserts the form of each
        assert "python -m piccoflash" not in text, name
        assert not re.search(r"(?<!-File )\.\\session\.ps1", text), name


@pytest.mark.parametrize("scenario", ["flash stops part-way", "restore did not complete"])
def test_restore_hint_holds_the_backup_path_exactly(walk, scenario):
    code, text = walk[scenario]
    (line,) = owner_commands(text)
    words = ps_words(line)
    assert words[:6] == ["powershell", "-ExecutionPolicy", "Bypass", "-File", ".\\session.ps1", "restore"]
    (path,) = words[6:]
    assert AWKWARD in path and os.path.isfile(path) and path == os.path.abspath(path)
    assert os.path.isfile(workdir.sidecar_for(path))


def test_monitor_hint_matches_the_checklist(walk):
    code, text = walk["flash DONE"]
    assert "    " + common.session_command("monitor", "-Reset") in text.splitlines()


def test_already_converted_names_the_menu_as_the_device_shows_it(walk):
    for name in ("identify ALREADY CONVERTED", "flash on a converted Picco"):
        assert "Settings > System > SD Card Firmware Update" in walk[name][1], name


def test_esptool_problems_name_the_setup_command(monkeypatch):
    import esptool

    monkeypatch.setattr(esptool, "__version__", "0.0.1")
    with pytest.raises(DeviceError) as err:
        esptool_device.load_esptool(UI(out=io.StringIO()))
    assert owner_commands(str(err.value)) == [common.session_command("setup")]

    def missing(ui):
        raise ImportError("No module named 'esptool'")

    monkeypatch.setattr(esptool_device, "load_esptool", missing)
    with pytest.raises(DeviceNotReached) as err:
        esptool_device.factory("COM7", UI(out=io.StringIO())).connect()
    assert owner_commands(str(err.value)) == [common.session_command("setup")]
