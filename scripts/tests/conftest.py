"""pytest configuration for scripts/tests: import the release scripts and the device tool's test helpers."""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
for p in (os.path.join(REPO, "scripts"), os.path.join(REPO, "tools", "device"),
          os.path.join(REPO, "tools", "device", "tests")):
    if p not in sys.path:
        sys.path.insert(0, p)
