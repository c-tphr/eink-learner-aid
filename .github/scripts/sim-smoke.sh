#!/usr/bin/env bash
# Headless smoke run of the simulator_picco binary (job "simulator" of
# .github/workflows/project-ci.yml), the run docs/BASE.md ("Simulator") and the
# F2 evidence (docs/evidence/f2/simulator_picco-home.png) describe:
#
#   .github/scripts/sim-smoke.sh [binary] [output dir]
#
# defaults: .pio/build/simulator_picco/program and build/sim-smoke.
#
# Starts the binary under xvfb-run with an empty simulated SD card and the
# scripted input
#     0:WAIT:Entering activity: Home;1500:SHOT:<out>/home.bmp;2500:QUIT
# i.e. wait until the firmware logs that it entered the Home activity (the
# simulator ends the run with exit 1 if that does not happen within 30 s),
# take a screenshot, quit. Then converts the screenshot to PNG (Pillow) and
# reports whether it is pixel-identical to the F2 evidence image
# (informational only: the Home screen may legitimately change).
#
# Needs: xvfb-run (xvfb, xauth), the SDL2 and OpenSSL runtime libraries,
# python3 with Pillow. Exit status: 0 when the binary exited 0, the WAIT
# matched and a screenshot was written; non-zero otherwise.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="$(realpath "${1:-$ROOT/.pio/build/simulator_picco/program}")"
OUT="${2:-$ROOT/build/sim-smoke}"
mkdir -p "$OUT"
OUT="$(realpath "$OUT")"
rm -rf "$OUT/sd" "$OUT/home.bmp" "$OUT/home.png"
mkdir -p "$OUT/sd"

SCRIPT="0:WAIT:Entering activity: Home;1500:SHOT:$OUT/home.bmp;2500:QUIT"
echo "binary: $BIN ($(stat -c %s "$BIN") bytes)"
echo "CROSSPOINT_SIM_INPUT_SCRIPT=$SCRIPT"

set +e
(cd "$OUT" && CROSSPOINT_SIM_SD="$OUT/sd" CROSSPOINT_SIM_INPUT_SCRIPT="$SCRIPT" \
  timeout 180 xvfb-run -a "$BIN") >"$OUT/simulator.log" 2>&1
rc=$?
set -e
echo "simulator exit status: $rc"
grep -E '^\[SIM\]' "$OUT/simulator.log" || true

fail=0
if [[ $rc -ne 0 ]]; then
  echo "::error::simulator exited with status $rc (last lines of the log follow)"
  tail -40 "$OUT/simulator.log"
  fail=1
fi
if ! grep -q 'WAIT matched' "$OUT/simulator.log"; then
  echo "::error::the simulator never logged 'Entering activity: Home'"
  fail=1
fi
if [[ ! -s "$OUT/home.bmp" ]]; then
  echo "::error::no screenshot at $OUT/home.bmp"
  fail=1
fi
if [[ $fail -ne 0 ]]; then
  exit 1
fi

python3 - "$OUT/home.bmp" "$OUT/home.png" "$ROOT/docs/evidence/f2/simulator_picco-home.png" <<'EOF'
import sys
from PIL import Image, ImageChops

bmp, png, ref = sys.argv[1:4]
img = Image.open(bmp)
print(f"screenshot: {img.size[0]} x {img.size[1]}, mode {img.mode}")
img.save(png)
extrema = img.convert("L").getextrema()
if extrema[0] == extrema[1]:
    print(f"::error::the screenshot is a single flat colour {extrema}: nothing was drawn")
    sys.exit(1)
try:
    want = Image.open(ref).convert("RGB")
    got = Image.open(png).convert("RGB")
    same = want.size == got.size and ImageChops.difference(want, got).getbbox() is None
    print("pixel-identical to docs/evidence/f2/simulator_picco-home.png: %s" % ("yes" if same else "no (informational)"))
except OSError as e:
    print(f"reference image not compared: {e}")
EOF
echo "screenshot: $OUT/home.png"
