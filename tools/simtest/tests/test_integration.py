"""The committed scenarios against the real simulator.

Which binary: pytest --binary PATH, else $SIMTEST_BINARY, else the default
.pio/build/simulator_picco/program. A binary that was named (option or
variable) must exist, or the tests fail. Only when nothing was named and the
default does not exist are the tests skipped, with a loud reason.
"""

import os
from pathlib import Path

import pytest

from tools.simtest import __main__ as cli
from tools.simtest import scenario


@pytest.fixture(scope="module")
def binary(pytestconfig):
    named = pytestconfig.getoption("--binary") or os.environ.get("SIMTEST_BINARY")
    if named:
        path = Path(named).resolve()
        assert path.is_file() and os.access(path, os.X_OK), f"simulator binary {path} not found or not executable"
        return path
    if cli.DEFAULT_BINARY.is_file():
        return cli.DEFAULT_BINARY
    pytest.skip(f"NO SIMULATOR BINARY: {cli.DEFAULT_BINARY} does not exist, so the scenarios were NOT run. "
                "Build it (./scripts/cloud-setup.sh && pio run -e simulator_picco) or pass --binary PATH")


@pytest.mark.parametrize("name", sorted(scenario.discover()))
def test_scenario_passes(binary, name, tmp_path, capsys):
    code = cli.main(["run", name, "--binary", str(binary), "--report-dir", str(tmp_path / "report")])
    out = capsys.readouterr().out
    assert code == 0, f"{name} failed; report in {tmp_path / 'report'}\n{out}"
