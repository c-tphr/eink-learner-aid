"""docs/DEVICE_SESSION.md (and, for links and commands, docs/DEVICE_EARLY_CHECK.md) must stay true.

The checklist marks what it quotes with an HTML comment just before an inline code span, as the
early-check sheet does (test_early_check_doc.py):

* ``<!--cmd-->`...` ``   a command the owner types: a real session.ps1 command with real options;
* ``<!--q-->`...` ``     something piccoflash prints: must appear in the real output of the scenarios
                          below, run against the fake device;
* ``<!--ps-->`...` ``    something session.ps1 prints: must appear in session.ps1;
* ``<!--type-->`...` ``  a confirmation phrase the owner types: must be one of the tool's phrases.

It also checks: every relative link and picture in the two sheets and the results template
resolves (anchors included); the twelve guided-acceptance steps it prints are the firmware's
(src/activities/devicetest/Acceptance.cpp, WALK_STEPS: id, title, instructions, expected); every
step and stop condition of docs/PLAN.md section 10 is in its mapping appendix; its simulator
pictures equal the scenario references they were copied from.
"""

import hashlib
import os
import re
import shlex

import pytest

import synth
from fakedevice import FakeChip, Faults
from helpers import identify_and_backup, make_kit, run_cli
from piccoflash import cli, common, layout, ports, workdir
from piccoflash.device import MonitorPort

HERE = os.path.dirname(os.path.abspath(__file__))
DEVICE = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(DEVICE))
SESSION_DOC = os.path.join(REPO, "docs", "DEVICE_SESSION.md")
EARLY_DOC = os.path.join(REPO, "docs", "DEVICE_EARLY_CHECK.md")
TEMPLATE = os.path.join(REPO, ".github", "ISSUE_TEMPLATE", "device-session-results.md")
SESSION_PS1 = os.path.join(DEVICE, "session.ps1")
ACCEPTANCE_CPP = os.path.join(REPO, "src", "activities", "devicetest", "Acceptance.cpp")
PLAN = os.path.join(REPO, "docs", "PLAN.md")
PICTURES = os.path.join(REPO, "docs", "device-session")
SCENARIO_REFS = os.path.join(REPO, "tools", "simtest", "scenarios", "device_session_screens", "refs")

MARK = re.compile(r"<!--(cmd|q|ps|type)-->`([^`]+)`")
PHRASES = {common.FLASH_PHRASE, common.RESTORE_PHRASE, common.OTHER_DEVICE_PHRASE}


def read(path):
    with open(path, encoding="utf-8") as fp:
        return fp.read()


def marked(path, kind=None):
    found = MARK.findall(read(path))
    return [s for k, s in found if kind is None or k == kind] if kind else found


def norm(s):
    return " ".join(s.split())


# --- real output -----------------------------------------------------------------------------

class ScriptedPort(MonitorPort):
    """A serial port that plays bytes, vanishes once ('GONE'), then ends as Ctrl+C would."""

    def __init__(self, script):
        self.script = script

    def open(self):
        pass

    def read(self):
        item = self.script.pop(0) if self.script else "STOP"
        if item == "GONE":
            raise OSError("ClearCommError failed (PermissionError)")
        if item == "STOP":
            raise KeyboardInterrupt
        return item

    def close(self):
        pass

    def pulse_reset(self):
        pass


@pytest.fixture(scope="module")
def real_output(tmp_path_factory, monkeypatch_module):
    """Every outcome the checklist quotes, run through the real CLI against the fake device."""
    out = []

    def go(argv, chip=None, answers=(), **kw):
        code, text = run_cli(argv, chip, answers=answers, **kw)
        out.append(text)
        return code

    def session(name):
        base = str(tmp_path_factory.mktemp(name))
        chip = FakeChip(synth.stock_flash())
        kit = make_kit(base)
        work = os.path.join(base, "work")
        side, rec = identify_and_backup(chip, work)
        return chip, kit, work, os.path.join(work, rec["file"]), base

    # identify and backup outcomes (as in the early-check sheet)
    base = str(tmp_path_factory.mktemp("read-only"))
    good = FakeChip(synth.stock_flash())
    w = os.path.join(base, "ok")
    go(["identify", "--work-dir", w], good)
    go(["backup", "--work-dir", w], good, answers=[hashlib.sha256(bytes(good.flash)).hexdigest()[-8:]])
    w = os.path.join(base, "unacked")
    go(["identify", "--work-dir", w], FakeChip(synth.stock_flash()))
    go(["backup", "--work-dir", w], FakeChip(synth.stock_flash()), answers=["x", "y", "z"])
    go(["identify", "--work-dir", os.path.join(base, "stop")], FakeChip(synth.stock_flash(), secure_boot=True))
    conv = FakeChip(synth.stock_flash())
    conv.flash[0x8000:0x8C00] = layout.encode_table(layout.TWO_SLOT_TABLE)
    go(["identify", "--work-dir", os.path.join(base, "conv")], conv)
    go(["identify", "--work-dir", os.path.join(base, "unreached")],
       FakeChip(synth.stock_flash(), faults=Faults(unreachable=True)))
    esp = ports.PortInfo("COM5", 0x303A, 0x1001, "USB JTAG/serial debug unit")
    for name, plist in (("noport", []), ("twoports", [esp, ports.PortInfo("COM9", 0x303A, 0x1001, "")])):
        go(["identify", "--work-dir", os.path.join(base, name)], FakeChip(synth.stock_flash()),
           resolver=lambda r, u, pl=plist: ports.choose_port(r, pl))
    go(["backup", "--work-dir", os.path.join(base, "noidentify")], FakeChip(synth.stock_flash()))
    mis = FakeChip(synth.stock_flash(), faults=Faults(flip_full_read=(2, 0x10)))
    w = os.path.join(base, "mismatch")
    go(["identify", "--work-dir", w], mis)
    go(["backup", "--work-dir", w], mis)
    drop = FakeChip(synth.stock_flash())
    w = os.path.join(base, "drop")
    go(["identify", "--work-dir", w], drop)
    drop.faults.disconnect_on_read = drop.reads + 5
    go(["backup", "--work-dir", w], drop)
    w = os.path.join(base, "full")
    go(["identify", "--work-dir", w], good)
    monkeypatch_module.setattr(workdir, "free_bytes", lambda p: 0)
    go(["backup", "--work-dir", w], good)
    monkeypatch_module.undo()

    # plan, dry runs, flash: wrong phrase, success, then identify and restore on the converted chip
    chip, kit, work, backup, base = session("flash")
    go(["plan", "--plan", kit])
    go(["flash", "--dry-run", "--work-dir", work, "--plan", kit])
    go(["flash", "--dry-run", "--work-dir", os.path.join(base, "empty"), "--plan", kit])
    go(["flash", "--work-dir", work, "--plan", kit], chip, answers=["replace onyx"])
    go(["flash", "--work-dir", work, "--plan", kit], chip, answers=[common.FLASH_PHRASE])
    go(["identify", "--work-dir", work], chip)
    go(["flash", "--work-dir", work, "--plan", kit], chip, answers=[common.FLASH_PHRASE])  # preconditions
    go(["restore", "--dry-run", "--work-dir", work, backup], chip)
    go(["restore", "--work-dir", work, backup], chip, answers=["restore"])
    chip.faults.disconnect_on_write = True
    go(["restore", "--work-dir", work, backup], chip, answers=[common.RESTORE_PHRASE])
    chip.faults.disconnect_on_write = False
    go(["restore", "--work-dir", work, backup], chip, answers=[common.RESTORE_PHRASE])
    chip.mac = "24:58:7c:00:00:02"
    go(["restore", "--work-dir", work, backup], chip, answers=[common.RESTORE_PHRASE, "no"])
    os.rename(workdir.sidecar_for(backup), workdir.sidecar_for(backup) + ".moved")
    go(["restore", "--work-dir", work, backup], chip, answers=[common.RESTORE_PHRASE])
    go(["summary", "--work-dir", work])

    # flash fails part-way; the Picco changed since the backup; the chip cannot be reached
    chip, kit, work, backup, _ = session("partway")
    chip.faults.fail_write_at = (0x8000, 100)
    go(["flash", "--work-dir", work, "--plan", kit], chip, answers=[common.FLASH_PHRASE])
    chip, kit, work, backup, _ = session("changed")
    chip.flash[0x100] ^= 0x01
    go(["flash", "--work-dir", work, "--plan", kit], chip, answers=[common.FLASH_PHRASE])
    chip, kit, work, backup, _ = session("unreached-flash")
    chip.faults.unreachable = True
    go(["flash", "--work-dir", work, "--plan", kit], chip, answers=[common.FLASH_PHRASE])
    # a work folder whose backup is not yet confirmed: the dry run's "[ask]" line
    base = str(tmp_path_factory.mktemp("ask"))
    chip = FakeChip(synth.stock_flash())
    kit = make_kit(base)
    w = os.path.join(base, "work")
    go(["identify", "--work-dir", w], chip)
    go(["backup", "--work-dir", w], chip, answers=["x", "y", "z"])
    go(["flash", "--dry-run", "--work-dir", w, "--plan", kit])

    # monitor (a port that vanishes once, then Ctrl+C), its dry run, and the port list
    monkeypatch_module.setattr("piccoflash.monitor.time.sleep", lambda s: None)
    mon = str(tmp_path_factory.mktemp("monitor"))
    script = [b"ESP-ROM:esp32s3\r\n", "GONE", b"[EPD] Picco panel DEPG0397\r\n", "STOP"]
    go(["monitor", "--reset", "--work-dir", mon], None, monitor_port_factory=lambda name: ScriptedPort(script))
    go(["monitor", "--dry-run", "--work-dir", mon])
    monkeypatch_module.setattr(ports, "list_ports", lambda: [esp])
    go(["ports"])
    monkeypatch_module.undo()
    return norm("\n".join(out))


@pytest.fixture(scope="module")
def monkeypatch_module():
    mp = pytest.MonkeyPatch()
    yield mp
    mp.undo()


# --- quotes, commands and phrases ----------------------------------------------------------------

def test_checklist_has_markers():
    kinds = [k for k, _ in marked(SESSION_DOC)]
    assert kinds.count("cmd") >= 10 and kinds.count("q") >= 40 and kinds.count("ps") >= 3
    assert kinds.count("type") >= 3


def test_quoted_piccoflash_output_is_real(real_output):
    missing = [s for s in marked(SESSION_DOC, "q") if norm(s) not in real_output]
    assert not missing, "not printed by the tool: %r" % missing


def test_quoted_session_ps1_output_is_in_the_script():
    source = read(SESSION_PS1)
    for s in marked(SESSION_DOC, "ps"):
        assert s in source, "not in session.ps1: %r" % s


@pytest.mark.parametrize("doc", [SESSION_DOC, EARLY_DOC], ids=["session", "early-check"])
def test_quoted_tool_results_are_marked(doc):
    text = read(doc)
    unmarked = re.findall(r"(?<!-->)`((?:DRY RUN )?RESULT:[^`]*|REFUSED[:.][^`]*|STOP:[^`]*|DONE:[^`]*|"
                          r"VERIFIED:[^`]*|BACKUP VERIFIED[^`]*|FLASHING DID NOT[^`]*|RESTORE DID NOT[^`]*)`", text)
    assert not unmarked, "quoted tool output without a marker: %s" % unmarked


@pytest.mark.parametrize("doc", [SESSION_DOC, EARLY_DOC], ids=["session", "early-check"])
def test_commands_are_real_session_ps1_commands(doc):
    source = read(SESSION_PS1)
    allowed = re.search(r"ValidateSet\(([^)]*)\)", source).group(1)
    commands = set(re.findall(r"'([a-z]+)'", allowed))
    params = set(re.findall(r"\[(?:string|switch)\]\$(\w+)", source))
    subs = cli.build_parser()._subparsers._group_actions[0].choices
    only_for = {"Reset": {"monitor"}, "DryRun": {"flash", "restore", "identify", "backup", "monitor", "summary",
                                                 "plan"}}
    cmds = marked(doc, "cmd")
    assert cmds
    for s in cmds:
        words = shlex.split(s, posix=False)
        assert words[:5] == ["powershell", "-ExecutionPolicy", "Bypass", "-File", ".\\session.ps1"], s
        command, rest = words[5], words[6:]
        assert command in commands, s
        assert command == "setup" or command in subs, s
        positional = [w for i, w in enumerate(rest) if not w.startswith("-") and
                      not (i > 0 and rest[i - 1] in ("-Port", "-Plan", "-WorkDir"))]
        assert len(positional) == (1 if command == "restore" else 0), s
        for word in rest:
            if word.startswith("-"):
                assert word[1:] in params and word[1:] != "PythonExe", s
                assert command in only_for.get(word[1:], {command}), s


@pytest.mark.parametrize("doc", [SESSION_DOC, EARLY_DOC], ids=["session", "early-check"])
def test_no_line_starts_with_a_marker(doc):
    """A line that starts with "<!--" is an HTML block in Markdown (CommonMark and GitHub): its code
    span would print with literal backticks, which PowerShell reads as escape characters."""
    bad = [line for line in read(doc).splitlines() if re.match(r"^\s{0,3}<!--", line)]
    assert not bad, "start the line with a word, e.g. 'Command: <!--cmd-->`...`': %s" % bad


@pytest.mark.parametrize("doc", [SESSION_DOC, EARLY_DOC], ids=["session", "early-check"])
def test_every_typed_session_command_is_marked(doc):
    text = read(doc)
    unmarked = re.findall(r"(?<!-->)`(powershell [^`]*session\.ps1[^`]*)`", text)
    assert not unmarked, "a session.ps1 command without the <!--cmd--> marker: %s" % unmarked


@pytest.mark.parametrize("doc", [SESSION_DOC, EARLY_DOC], ids=["session", "early-check"])
def test_every_other_powershell_line_is_marked(doc):
    """PowerShell lines that are not session.ps1 commands carry <!--pwsh-->: the device-tool-windows
    job runs each of them (.github/scripts/device_doc_powershell.py)."""
    text = read(doc)
    unmarked = re.findall(r"(?<!<!--pwsh-->)`(\(?(?:Get-|Set-|Remove-|Test-|Expand-)[^`]*)`", text)
    assert not unmarked, "a PowerShell line without the <!--pwsh--> marker: %s" % unmarked


def test_typed_phrases_are_the_tools_phrases():
    typed = marked(SESSION_DOC, "type")
    assert set(typed) == PHRASES, typed
    text = read(SESSION_DOC)
    for phrase in PHRASES:
        assert "<!--type-->`%s`" % phrase in text


# --- links and pictures ----------------------------------------------------------------------

def slug(heading):
    """GitHub's anchor for a Markdown heading."""
    text = re.sub(r"[`*_]", "", heading.strip().lower())
    return re.sub(r"[^\w\- ]", "", text).replace(" ", "-")


def anchors(path):
    seen, out = {}, set()
    in_fence = False
    for line in read(path).splitlines():
        if line.startswith("```"):
            in_fence = not in_fence
        m = re.match(r"^#{1,6}\s+(.*?)\s*#*\s*$", line)
        if m and not in_fence:
            base = slug(m.group(1))
            n = seen.get(base, 0)
            out.add(base if n == 0 else "%s-%d" % (base, n))
            seen[base] = n + 1
    return out


def links(path):
    text = re.sub(r"`[^`\n]*`", "", read(path))
    return re.findall(r"!?\[[^\]]*\]\(([^)\s]+)\)", text) + re.findall(r"<img [^>]*src=\"([^\"]+)\"", text)


@pytest.mark.parametrize("doc", [SESSION_DOC, EARLY_DOC, TEMPLATE], ids=["session", "early-check", "template"])
def test_relative_links_and_pictures_resolve(doc):
    for target in links(doc):
        if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.I):
            continue
        path, _, anchor = target.partition("#")
        dest = os.path.normpath(os.path.join(os.path.dirname(doc), path)) if path else doc
        assert os.path.isfile(dest), "%s: broken link %s" % (os.path.basename(doc), target)
        if anchor:
            assert anchor in anchors(dest), "%s: no heading for #%s in %s" % (
                os.path.basename(doc), anchor, os.path.basename(dest))


def test_every_picture_is_from_the_simulator_and_says_so():
    lines = read(SESSION_DOC).splitlines()
    pictures = [i for i, line in enumerate(lines) if line.startswith(("![", "<img "))]
    assert len(pictures) >= 25
    assert not [line for line in lines if "![" in line], "use <img ... width=...> so the printed copy stays short"
    for i in pictures:
        near = " ".join(lines[i - 2:i + 3])
        assert "imulator" in near and "e-ink" in near, "picture without the simulator caption: %s" % lines[i]


def test_session_pictures_equal_the_scenario_references():
    names = sorted(os.listdir(PICTURES))
    assert names and names == sorted(os.listdir(SCENARIO_REFS))
    text = read(SESSION_DOC)
    for name in names:
        with open(os.path.join(PICTURES, name), "rb") as a, open(os.path.join(SCENARIO_REFS, name), "rb") as b:
            assert a.read() == b.read(), "docs/device-session/%s differs from the scenario's reference" % name
        assert 'src="device-session/%s"' % name in text, "docs/device-session/%s is not used" % name


# --- the guided acceptance steps -------------------------------------------------------------------

def walk_steps():
    """WALK_STEPS of Acceptance.cpp: (id, title, instructions, expected) for each step, in order."""
    source = read(ACCEPTANCE_CPP)
    body = re.search(r"const WalkStep WALK_STEPS\[WALK_STEP_COUNT\] = \{(.*?)\n\};", source, re.S).group(1)
    steps = []
    for entry in re.findall(r"\n    \{(.*?)\},(?=\n    \{|\n?$)", body + "\n", re.S):
        groups, last_end = [], None
        for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', entry):
            value = m.group(1).replace('\\"', '"').replace("\\\\", "\\")
            if last_end is not None and not entry[last_end:m.start()].strip():
                groups[-1] += value
            else:
                groups.append(value)
            last_end = m.end()
        assert len(groups) == 6, entry  # id, purpose, depends on, title, instructions, expected
        steps.append((groups[0], groups[3], groups[4], groups[5]))
    return steps


def printed_steps():
    text = read(SESSION_DOC)
    out = []
    for m in re.finditer(r"^\*\*Step (\d+) of 12: (.+?)\*\* <!--step:([a-z0-9-]+)-->\n\nInstructions: (.+)\n\n"
                         r"Expected: (.+)\n", text, re.M):
        out.append((int(m.group(1)), m.group(3), m.group(2), m.group(4), m.group(5)))
    return out


def test_printed_steps_are_the_firmwares():
    firmware = walk_steps()
    printed = printed_steps()
    assert len(firmware) == 12
    assert [p[0] for p in printed] == list(range(1, 13))
    for (n, sid, title, instructions, expected), (fid, ftitle, finstr, fexp) in zip(printed, firmware):
        assert sid == fid, "step %d" % n
        assert title == ftitle, "step %d title" % n
        assert instructions == finstr, "step %d instructions" % n
        assert expected == fexp, "step %d expected" % n


# --- the mapping to plan section 10 ------------------------------------------------------------

def plan_section_10():
    text = read(PLAN)
    return text[text.index("## 10. On-device test protocol"):text.index("## 11. ")]


def test_mapping_covers_every_step_and_stop_condition():
    section = plan_section_10()
    steps = re.findall(r"^(\d+)\. (\*\*[^*]+\*\*)", section[section.index("### Session steps"):], re.M)
    stops_block = section[section.index("### Stop conditions and recovery"):]
    stops = [row.split("|")[1].strip() for row in stops_block.splitlines()
             if row.startswith("| ") and not row.startswith(("| If ", "| ---"))]
    doc = read(SESSION_DOC)
    mapping = doc[doc.index("## Appendix D: mapping to the plan"):doc.index("## Appendix E")]
    assert [int(n) for n, _ in steps] == list(range(1, 13))
    for n, title in steps:
        assert "| Step %s. %s |" % (n, title) in mapping, "plan step %s has no line" % n
    assert len(stops) == 6
    for stop in stops:
        assert "| Stop: %s |" % stop in mapping, "stop condition without a line: %s" % stop
        assert "| %s |" % stop in doc[:doc.index("## Before the session day")], "not in the quick table: %s" % stop
    for row in mapping.splitlines():
        if row.startswith("| ") and not row.startswith(("| Plan section 10 |", "| ---")):
            assert "](#" in row, "a mapping row without a link to the checklist: %s" % row


# --- the results template ----------------------------------------------------------------------

def test_results_template():
    text = read(TEMPLATE)
    assert 'title: "Device session results"' in text and "DEVICE_SESSION.md" in text
    head = text[:text.index("Instructions:")]
    assert "NEVER attach" in head and ".bin" in head and "PUBLIC" in head
    for field in ("Onyx firmware version", "Identify result", "two reads matched", "Did the flash complete",
                  "First boot", "BTN_UP", "Settings changed", "SD-card update", "Benchmark file", "Acceptance file",
                  "Profile chosen", "Anything that went wrong", "slide switch", "palm"):
        assert field.lower() in text.lower(), field
    assert "template=device-session-results.md" in read(SESSION_DOC)
