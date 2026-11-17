#!/usr/bin/env python3
"""The results files the firmware's encoder writes (DeviceTestSamples) match the JSON Schema.

Run by ctest after DeviceTestSamplesWrite. Standard library only: the schema is applied by
tools/simtest/jsonchecks.py (the validator the simulator scenarios use) and the consistency rules
by tools/device/piccoflash/results.py (its checks that need no jsonschema). The schema's
"additionalProperties": false everywhere means every member the encoder writes is in the schema,
and its "required" lists mean every member the schema names is written.
"""

import json
import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "simtest"))
sys.path.insert(0, os.path.join(REPO, "tools", "device"))

import jsonchecks  # noqa: E402
from piccoflash import results  # noqa: E402

SCHEMA = os.path.join(REPO, "tools", "device", "schema", "rsvp-bench.schema.json")
EXPECTED = ("0001-bench.json", "0002-controls.json", "0012-bench.json", "0005-acceptance.json",
            "0014-acceptance.json", "0015-choice.json")


def main(sample_dir: str) -> int:
    with open(SCHEMA, encoding="utf-8") as f:
        schema = json.load(f)
    failures = 0
    for name in EXPECTED:
        path = os.path.join(sample_dir, name)
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
        problems = jsonchecks.validate(doc, schema)
        problems += results.consistency_problems(doc)
        if problems:
            failures += 1
            print("FAIL %s" % name)
            for p in problems[:40]:
                print("  " + p)
        else:
            print("ok   %s: %s" % (name, results.describe(doc)))

    # A broken file is caught: a member the schema does not know, and a count that does not match.
    with open(os.path.join(sample_dir, "0012-bench.json"), encoding="utf-8") as f:
        doc = json.load(f)
    doc["profiles"][0]["steps"][0]["extra"] = 1
    if not jsonchecks.validate(doc, schema):
        failures += 1
        print("FAIL an unknown member passed the schema")
    del doc["profiles"][0]["steps"][0]["extra"]
    doc["profiles"][0]["steps"][0]["updates"]["count"] = 99
    if not results.consistency_problems(doc):
        failures += 1
        print("FAIL a word band of 99 updates passed the consistency checks")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
