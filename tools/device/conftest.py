"""pytest configuration for tools/device: make `piccoflash` and the test helpers importable."""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
for p in (HERE, os.path.join(HERE, "tests")):
    if p not in sys.path:
        sys.path.insert(0, p)
