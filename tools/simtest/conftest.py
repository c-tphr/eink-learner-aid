"""pytest options for the harness's own tests (python3 -m pytest tools/simtest -q)."""


def pytest_terminal_summary(terminalreporter):
    """Make a skipped integration run impossible to miss, even with -q."""
    for rep in terminalreporter.stats.get("skipped", []):
        if "test_integration" in rep.nodeid:
            reason = rep.longrepr[2] if isinstance(rep.longrepr, tuple) else str(rep.longrepr)
            terminalreporter.write_sep("!", "simulator scenarios were skipped", yellow=True, bold=True)
            terminalreporter.write_line(reason)
            break


def pytest_addoption(parser):
    parser.addoption("--binary", action="store", default=None,
                     help="simulator program for the integration tests (default: "
                          ".pio/build/simulator_picco/program, or $SIMTEST_BINARY)")
