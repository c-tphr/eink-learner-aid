#!/usr/bin/env bash
# Self-check for scripts/sim.sh (the browser simulator). Used by developers
# and CI; the owner does not need it.
#
#   ./scripts/sim-selfcheck.sh [--screenshot FILE] [sim.sh options, e.g. --no-build]
#
# 1. starts `./scripts/sim.sh --no-wait` (with any extra options given);
# 2. checks that noVNC answers on SIM_PORT (default 6080): the landing page,
#    vnc.html, and a WebSocket that reaches the VNC server (RFB greeting);
# 3. checks that the simulator window is the Picco's portrait screen (480x800)
#    at the display's corner, as the browser page is told, then captures that
#    area of the virtual display to a PNG and checks that it is not blank;
# 4. presses Down through the X server and checks that the screen changed;
# 5. runs `./scripts/sim.sh --stop` and checks that no process it started
#    (Xvnc, websockify, supervisor, simulator) is left.
#
# SIM_PORT and SIM_DISPLAY are passed through to sim.sh, so the check can run
# beside another simulator. Exit status 0 when every check passes.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIM="$REPO_ROOT/scripts/sim.sh"
RUN_DIR="$REPO_ROOT/.sim/run"
OUT_DIR="$REPO_ROOT/.sim/selfcheck"
SIM_PORT="${SIM_PORT:-6080}"
SIM_DISPLAY="${SIM_DISPLAY:-1}"
SIM_DISPLAY="${SIM_DISPLAY#:}"
export SIM_PORT SIM_DISPLAY
SHOT="$OUT_DIR/screen.png"
SIM_ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --screenshot) SHOT="$2"; shift 2 ;;
    -h | --help) sed -n '2,/^$/s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) SIM_ARGS+=("$1"); shift ;;
  esac
done
[[ "$SHOT" == /* ]] || SHOT="$PWD/$SHOT"
mkdir -p "$OUT_DIR" "$(dirname "$SHOT")"

PASSED=0
FAILED=0
pass() { printf 'PASS  %s\n' "$*"; PASSED=$((PASSED + 1)); }
failed() { printf 'FAIL  %s\n' "$*"; FAILED=$((FAILED + 1)); }
info() { printf '      %s\n' "$*"; }

as_root() { if [[ $(id -u) -eq 0 ]]; then "$@"; else sudo -n "$@"; fi; }
if ! command -v xwd >/dev/null; then
  info "installing x11-apps (for xwd, the screen capture tool)"
  as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends x11-apps >/dev/null 2>&1 ||
    { as_root apt-get update -q >/dev/null 2>&1 &&
      as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends x11-apps >/dev/null 2>&1; }
fi

# Capture the simulator window's area of the X display (WxH at 0,0, the part
# the browser page shows) as PNG; print "WIDTHxHEIGHT DISTINCT_COLOURS DARK_PERCENT".
capture() {
  local png="$1" w="$2" h="$3" xwdfile="$OUT_DIR/capture.xwd"
  xwd -root -silent -display ":$SIM_DISPLAY" >"$xwdfile"
  python3 -I - "$xwdfile" "$png" "$w" "$h" <<'EOF'
import struct, sys, zlib
data = open(sys.argv[1], "rb").read()
h = struct.unpack(">25I", data[:100])
header_size, fmt, depth, width, height = h[0], h[2], h[3], h[4], h[5]
width, height = min(width, int(sys.argv[3])), min(height, int(sys.argv[4]))
byte_order, bpp, bpl = h[7], h[11], h[12]
masks = h[14:17]
ncolors = h[19]
if fmt != 2 or bpp not in (24, 32):
    sys.exit(f"unsupported xwd image: format {fmt}, {bpp} bits per pixel")
off = header_size + ncolors * 12
order = "little" if byte_order == 0 else "big"
step = bpp // 8
shifts = [(m & -m).bit_length() - 1 for m in masks]
rows, colours, dark = [], set(), 0
for y in range(height):
    line = data[off + y * bpl: off + y * bpl + width * step]
    row = bytearray(b"\0")
    for i in range(0, width * step, step):
        p = int.from_bytes(line[i:i + step], order)
        rgb = tuple((p & m) >> s for m, s in zip(masks, shifts))
        colours.add(rgb)
        if sum(rgb) < 3 * 128:
            dark += 1
        row += bytes(rgb)
    rows.append(bytes(row))
def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
open(sys.argv[2], "wb").write(png)
print(f"{width}x{height} {len(colours)} {100.0 * dark / (width * height):.1f}")
EOF
}

echo "== starting: ./scripts/sim.sh --no-wait ${SIM_ARGS[*]:-}"
if "$SIM" --no-wait "${SIM_ARGS[@]}" >"$OUT_DIR/sim-start.log" 2>&1; then
  pass "sim.sh --no-wait exited 0"
else
  failed "sim.sh --no-wait failed; output:"
  sed 's/^/      /' "$OUT_DIR/sim-start.log"
  "$SIM" --stop >/dev/null 2>&1 || true
  exit 1
fi
grep -E '^Open this address' "$OUT_DIR/sim-start.log" | sed 's/^/      /'
# Whatever happens below, do not leave the simulator running.
trap '"$SIM" --stop >/dev/null 2>&1 || true' EXIT

# Record what was started, to check after --stop.
PIDS=()
for f in "$RUN_DIR"/xvnc.pid "$RUN_DIR"/websockify.pid "$RUN_DIR"/supervisor.pid; do
  [[ -r "$f" ]] && PIDS+=("$(cut -d' ' -f1 "$f")")
done
GROUP_PIDS=()
for p in "${PIDS[@]}"; do
  while read -r q; do [[ -n "$q" ]] && GROUP_PIDS+=("$q"); done < <(pgrep -g "$p" 2>/dev/null || true)
done
info "started processes: ${GROUP_PIDS[*]}"

# 2. noVNC
if curl -sf -o /dev/null "http://localhost:$SIM_PORT/vnc.html"; then
  pass "noVNC answers: curl -sf http://localhost:$SIM_PORT/vnc.html"
else
  failed "noVNC did not answer on http://localhost:$SIM_PORT/vnc.html"
fi
if curl -sf "http://localhost:$SIM_PORT/" | grep -q 'BOOX Picco simulator'; then
  pass "landing page answers: http://localhost:$SIM_PORT/"
else
  failed "landing page missing at http://localhost:$SIM_PORT/"
fi
greeting="$(curl -s --http1.1 --max-time 3 -o - \
  -H 'Connection: Upgrade' -H 'Upgrade: websocket' -H 'Sec-WebSocket-Version: 13' \
  -H 'Sec-WebSocket-Key: c2ltLXNlbGZjaGVjay1rZXk=' -H 'Sec-WebSocket-Protocol: binary' \
  "http://localhost:$SIM_PORT/websockify" 2>/dev/null | tr -c '[:print:]' ' ' || true)"
if [[ "$greeting" == *"RFB 003.00"* ]]; then
  pass "WebSocket /websockify reaches the VNC server (greeting: $(grep -o 'RFB 003.00[0-9]' <<<"$greeting" | head -n 1))"
else
  failed "WebSocket /websockify did not return a VNC greeting"
fi

# 3. Screen capture: the window the browser page shows
sleep 2
display_size="$(DISPLAY=":$SIM_DISPLAY" xdotool getdisplaygeometry 2>/dev/null | tr ' ' x || true)"
win="$(DISPLAY=":$SIM_DISPLAY" xdotool search --onlyvisible --name '^Simulator - ' getwindowgeometry --shell 2>/dev/null | tr '\n' ' ' || true)"
info "virtual display's monitor area: $display_size (inside an 800x800 framebuffer); simulator window: $win"
if [[ "$win" == *"X=0 Y=0 WIDTH=480 HEIGHT=800"* ]]; then
  pass "simulator window is the Picco's portrait screen, 480x800, at the display's corner"
else
  failed "simulator window is not 480x800 at 0,0"
fi
page_size="$(curl -sf "http://localhost:$SIM_PORT/window.json" | tr -d ' \n' || true)"
if [[ "$page_size" == '{"w":480,"h":800,"x":0,"y":0}' ]]; then
  pass "the browser page is told to show 480x800 at 0,0 ($page_size)"
else
  failed "window.json says '$page_size', expected 480x800 at 0,0"
fi
read -r size colours dark < <(capture "$SHOT" 480 800)
info "screen: $size, $colours colours, $dark% dark pixels, saved to $SHOT"
if [[ "$size" == "480x800" ]]; then
  pass "captured the screen at 480x800"
else
  failed "captured $size, expected 480x800"
fi
if ((colours >= 2)) && awk -v d="$dark" 'BEGIN { exit !(d > 0.5 && d < 60) }'; then
  pass "screen is not blank ($colours colours, $dark% dark)"
else
  failed "screen looks blank ($colours colours, $dark% dark)"
fi

# 4. Key press through the X server
before="$(md5sum <"$SHOT" | cut -d' ' -f1)"
DISPLAY=":$SIM_DISPLAY" xdotool mousemove 240 400 key Down
sleep 2
read -r _ _ _ < <(capture "$OUT_DIR/after-key.png" 480 800)
after="$(md5sum <"$OUT_DIR/after-key.png" | cut -d' ' -f1)"
if [[ "$before" != "$after" ]]; then
  pass "pressing Down changed the screen (saved to $OUT_DIR/after-key.png)"
else
  failed "pressing Down did not change the screen"
fi

# 5. Stop
if "$SIM" --stop >"$OUT_DIR/sim-stop.log" 2>&1; then
  pass "sim.sh --stop exited 0"
else
  failed "sim.sh --stop failed: $(cat "$OUT_DIR/sim-stop.log")"
fi
# A process that has exited but not yet been reaped by its parent (a zombie,
# state Z) is not running; anything else still in /proc is.
alive() { [[ -d "/proc/$1" ]] && [[ "$(sed 's/^.*) //' "/proc/$1/stat" 2>/dev/null | cut -d' ' -f1)" != Z ]]; }
left=()
for p in "${GROUP_PIDS[@]}"; do
  alive "$p" && left+=("$p $(tr '\0' ' ' <"/proc/$p/cmdline" 2>/dev/null)")
done
for p in "${PIDS[@]}"; do
  while read -r q; do [[ -n "$q" ]] && left+=("$q (group $p)"); done < <(pgrep -g "$p" -r D,R,S,T,t,W,I 2>/dev/null || true)
done
if [[ ${#left[@]} -eq 0 ]]; then
  pass "nothing left running (${#GROUP_PIDS[@]} processes checked)"
else
  failed "still running after --stop: ${left[*]}"
fi
if [[ -e "/tmp/.X11-unix/X$SIM_DISPLAY" || -e "/tmp/.X$SIM_DISPLAY-lock" ]]; then
  failed "display :$SIM_DISPLAY socket or lock file left behind"
else
  pass "display :$SIM_DISPLAY released"
fi
if (exec 3<>"/dev/tcp/127.0.0.1/$SIM_PORT") 2>/dev/null; then
  failed "port $SIM_PORT still answers"
else
  pass "port $SIM_PORT released"
fi

echo
echo "$PASSED passed, $FAILED failed"
((FAILED == 0))
