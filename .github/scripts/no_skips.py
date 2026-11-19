"""Fail when a pytest JUnit report records a skipped test (or no tests at all).

    python .github/scripts/no_skips.py build/prep-junit.xml

The prep and device test suites skip tests when something they need is
missing (Java or the EPUBCheck download for tools/prep). In CI that would turn
a missing dependency into a green run, so a skip fails the job instead.
"""

import sys
import xml.etree.ElementTree as ET


def main(path):
    root = ET.parse(path).getroot()
    suites = [root] if root.tag == "testsuite" else list(root.iter("testsuite"))
    total = sum(int(s.get("tests", 0)) for s in suites)
    skipped = []
    for case in root.iter("testcase"):
        for s in case.findall("skipped"):
            skipped.append("%s::%s: %s" % (case.get("classname"), case.get("name"), s.get("message", "")))
    print("%s: %d tests, %d skipped" % (path, total, len(skipped)))
    if total == 0:
        print("::error::no tests ran")
        return 1
    if skipped:
        for line in skipped:
            print("::error::skipped: %s" % line)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
