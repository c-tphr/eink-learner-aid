"""Shared helpers: write scenario files into a temporary scenarios folder, make a stub binary."""

from __future__ import annotations

import sys
import textwrap
from pathlib import Path

from tools.simtest import scenario

STUB = Path(__file__).resolve().parent / "stub_sim.py"


def write_scenario(root: Path, name: str, body: str) -> Path:
    folder = root / name
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / "scenario.toml"
    path.write_text(textwrap.dedent(body).lstrip(), encoding="utf-8")
    return path


def load(root: Path, name: str, body: str) -> scenario.Scenario:
    return scenario.load(write_scenario(root, name, body))


def stub_binary(tmp: Path) -> Path:
    """A shell wrapper so the stub runs with this interpreter (and its Pillow)
    even though the harness gives the simulator a minimal PATH."""
    exe = tmp / "stub-program"
    exe.write_text(f'#!/bin/sh\nexec "{sys.executable}" "{STUB}" "$@"\n', encoding="utf-8")
    exe.chmod(0o755)
    return exe
