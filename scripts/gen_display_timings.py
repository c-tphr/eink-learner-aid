# PlatformIO pre-script for [env:simulator_picco] (platformio.project.ini).
#
# Compiles docs/device/picco-timings.json into the simulator's timing model: it validates the file
# and writes DisplayTimingsFile.generated.h into the build directory (scripts/display_timings.py).
# A missing file means "built-in defaults"; a malformed file stops the build here, before anything
# compiles. The device build does not run this: the device measures its own refresh times.

import os
import sys

Import("env")  # noqa: F821  (SCons)

# PlatformIO runs this without __file__.
_project_dir = env.subst("$PROJECT_DIR")  # noqa: F821
sys.path.insert(0, os.path.join(_project_dir, "scripts"))

import display_timings  # noqa: E402
from pathlib import Path  # noqa: E402

_out_dir = Path(env.subst("$BUILD_DIR")) / "display_timings"  # noqa: F821
try:
    _header, _summary = display_timings.generate(Path(_project_dir), _out_dir)
except display_timings.TimingsError as e:
    sys.stderr.write(f"*** Display timings: {e}\n")
    env.Exit(1)  # noqa: F821
else:
    print(f"Display timings: {_summary}")
    env.Append(CPPPATH=[str(_out_dir)])  # noqa: F821
