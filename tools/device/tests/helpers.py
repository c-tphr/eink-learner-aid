"""Shared test helpers: run the CLI against a fake chip with scripted answers."""

from __future__ import annotations

import hashlib
import io
import os
from typing import Iterable, Optional, Tuple

import synth
from fakedevice import FakeChip
from piccoflash import cli, common
from piccoflash.ui import UI

PORT = "COM7"


def no_device(port, ui=None):
    raise AssertionError("this command must not create a device (port %r)" % port)


def scripted(answers: Iterable[str]):
    queue = list(answers)

    def ask(prompt: str) -> str:
        if not queue:
            raise EOFError
        return queue.pop(0)

    return ask


def run_cli(argv, chip: Optional[FakeChip] = None, answers: Iterable[str] = (), port: str = PORT,
            resolver=None, **kw) -> Tuple[int, str]:
    argv = list(argv)
    if argv[0] in ("identify", "backup", "flash", "restore", "monitor"):
        assert "--work-dir" in argv, "tests must not write into the current directory"
    ui = UI(out=io.StringIO(), ask_fn=scripted(answers))
    code = cli.main(argv, device_factory=chip.factory if chip is not None else no_device,
                    port_resolver=resolver or (lambda req, ui: req or port), ui=ui, **kw)
    return code, "\n".join(ui.lines)


def make_kit(tmp: str, app_size: int = 900_000) -> str:
    src = synth.kit_sources(os.path.join(tmp, "build"), app_size)
    kit = os.path.join(tmp, "kit")
    code, out = run_cli(["make-plan", "--bootloader", src["bootloader"], "--partitions", src["partition_table"],
                         "--boot-app0", src["boot_app0"], "--app", src["app"], "--version", "0.1.0-test",
                         "--build-id", "synthetic", "-o", kit])
    assert code == common.OK, out
    return kit


def identify_and_backup(chip: FakeChip, work: str) -> Tuple[str, dict]:
    """Run identify and backup (answering the acknowledgement) against the fake; return the sidecar."""
    from piccoflash import workdir

    code, out = run_cli(["identify", "--work-dir", work], chip)
    assert code == common.OK, out
    ack = hashlib.sha256(bytes(chip.flash)).hexdigest()[-8:]
    code, out = run_cli(["backup", "--work-dir", work], chip, answers=[ack])
    assert code == common.OK, out
    found = workdir.latest_verified_backup(work, chip.mac)
    assert found is not None and found[1]["owner_ack"]["typed"] == ack
    return found
