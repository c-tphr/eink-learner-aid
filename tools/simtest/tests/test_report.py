"""Report generation: results.json, junit.xml and index.html."""

import json
import xml.etree.ElementTree as ET

from tools.simtest import report
from tools.simtest.runner import ScenarioResult, StepResult


def sample_results(tmp_path):
    ok = ScenarioResult("good", "good", "a passing flow", status="pass", duration=1.5,
                        steps=[StepResult(1, "wait_log", '{ wait_log = "Home" }', 1, "pass", 0.4, 0.4, "matched")],
                        shots={"home": {"png": "good/shots/home.png", "sha256": "ab" * 32}},
                        runs=[{"number": 1, "pid": 10, "sd": "/tmp/x", "end": "end", "exit": 0}],
                        log_file="good/log.txt",
                        sd_listing=[{"path": "books/", "size": None},
                                    {"path": ".crosspoint/state.json", "size": 9, "text": '{"a": "<b>"}'},
                                    {"path": "p.bin", "size": 2, "hex": "01 00"}])
    bad = ScenarioResult("bad", "bad", "a <failing> flow", status="fail", duration=2.0,
                         error="step 2 (compare): differs\n--- last lines ---\nboom",
                         steps=[StepResult(1, "shot", '{ shot = "x" }', 1, "pass"),
                                StepResult(2, "compare", '{ compare = "x" }', None, "fail", None, None,
                                           "shot 'x' differs: 4 pixel(s) differ",
                                           {"diff": "bad/diff/x.diff.png", "reference": "bad/diff/x.reference.png",
                                            "actual": "bad/diff/x.actual.png"})],
                         log_file="bad/log.txt")
    for r in (ok, bad):
        (tmp_path / r.label).mkdir()
        (tmp_path / r.log_file).write_text(f"log of {r.label} <tag>\n")
    return [ok, bad]


def test_write_all(tmp_path):
    results = sample_results(tmp_path)
    report.write_all(results, tmp_path, {"binary": "/b/program", "started": "now", "duration": 3.5})

    data = json.loads((tmp_path / "results.json").read_text())
    assert data["passed"] == 1 and data["failed"] == 1
    assert data["scenarios"][1]["steps"][1]["images"]["diff"] == "bad/diff/x.diff.png"

    suite = ET.parse(tmp_path / "junit.xml").getroot()
    assert suite.tag == "testsuite" and suite.get("tests") == "2" and suite.get("failures") == "1"
    cases = {c.get("name"): c for c in suite.findall("testcase")}
    assert cases["good"].find("failure") is None
    failure = cases["bad"].find("failure")
    assert failure.get("message") == "step 2 (compare): differs"
    assert "boom" in failure.text

    page = (tmp_path / "index.html").read_text()
    assert "1 of 2 passed" in page
    assert "a &lt;failing&gt; flow" in page          # escaped
    assert "log of bad &lt;tag&gt;" in page          # log embedded and escaped
    assert "bad/diff/x.diff.png" in page             # diff shown
    assert "good/shots/home.png" in page             # screenshot embedded
    assert "{&quot;a&quot;: &quot;&lt;b&gt;&quot;}" in page  # small JSON file content
    assert "01 00" in page                           # small binary file as hex
