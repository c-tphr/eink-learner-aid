#!/usr/bin/env bash
# Run the BOOX Picco simulator in a browser tab (GitHub Codespaces or any
# Ubuntu machine). One command, from anywhere:
#
#   ./scripts/sim.sh              install what is missing, build, start, show the address
#   ./scripts/sim.sh --reset      the same, but first wipe the simulated settings and
#                                 reading state (books stay), like a new device
#   ./scripts/sim.sh --no-build   restart quickly with the simulator already built
#   ./scripts/sim.sh --no-wait    return once everything is up (for scripts and checks)
#   ./scripts/sim.sh --prepare    install and build only; start nothing (dev container)
#   ./scripts/sim.sh --status     say what is running and where
#   ./scripts/sim.sh --stop       stop everything this script started
#
# What it starts, all on this machine:
#   Xvnc (TigerVNC)  a virtual display with a VNC server, listening on localhost only
#   websockify       serves the noVNC web client and relays it to Xvnc; the only
#                    port meant to be opened from a browser (6080)
#   a supervisor     runs the simulator in that display, restarts it whenever it
#                    exits, and tells the page where the window is
#   index.html       the page to open: the simulator's screen only, zoomed by
#                    whole pixels, following portrait/landscape rotation
#
# Settings (environment variables):
#   SIM_PORT          browser port (default 6080)
#   SIM_DISPLAY       X display number (default 1; its VNC port is 5900 + this)
#   SIM_VNC_PORT      VNC port on localhost (default 5900 + SIM_DISPLAY)
#   SIM_HTTP_PORT     the simulator's file-transfer web server (default 8080; uses the next port too)
#   CROSSPOINT_SIM_SD the simulated SD card folder (default <repo>/fs_)
#
# Files: logs in <repo>/.sim/logs/, process records in <repo>/.sim/run/.
# See docs/SIMULATOR.md. Exit status 0 on success, 1 on failure (with one
# sentence saying what failed and the log to read).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SELF="$REPO_ROOT/scripts/sim.sh"
STATE_DIR="$REPO_ROOT/.sim"
LOG_DIR="$STATE_DIR/logs"
RUN_DIR="$STATE_DIR/run"
WEB_DIR="$STATE_DIR/web"
EVENTS_LOG="$LOG_DIR/events.log"
SIM_LOG="$LOG_DIR/simulator.log"

ENV_NAME="simulator_picco"
PROGRAM="$REPO_ROOT/.pio/build/$ENV_NAME/program"
SIM_PORT="${SIM_PORT:-6080}"
SIM_DISPLAY="${SIM_DISPLAY:-1}"
SIM_DISPLAY="${SIM_DISPLAY#:}"
SIM_VNC_PORT="${SIM_VNC_PORT:-$((5900 + SIM_DISPLAY))}"
SIM_HTTP_PORT="${SIM_HTTP_PORT:-8080}"
SD_DIR="${CROSSPOINT_SIM_SD:-$REPO_ROOT/fs_}"
[[ "$SD_DIR" == /* ]] || SD_DIR="$PWD/$SD_DIR"
WINDOW_NAME='^Simulator - '
# The simulator's window is the Picco's screen: 480x800 in portrait, 800x480
# when the firmware turns it to landscape. The virtual display is a square
# that holds either, with the window at its top-left corner (see
# start_display); the browser page shows only the window's area
# (WEB_DIR/window.json says where it is). A display sized exactly to the
# window does not work: when the window grows past the display's edge on
# rotation, SDL draws that first frame garbled.
DISPLAY_SIZE=800
# System packages for the display. The compiler, SDL2 and OpenSSL headers,
# Python and PlatformIO come from scripts/cloud-setup.sh (step 2).
DISPLAY_PACKAGES=(tigervnc-standalone-server novnc websockify xdotool x11-xserver-utils curl)
NOVNC_DIR=/usr/share/novnc

export PATH="$HOME/.local/bin:$PATH"
export PLATFORMIO_SETTING_ENABLE_TELEMETRY=No

# ---------------------------------------------------------------- output

say() { printf '%s\n' "$*"; }
step() { printf '\n== Step %s of 6: %s\n' "$1" "$2"; }
detail() { printf '   %s\n' "$*"; }
fail() {
  # $1: one plain sentence (what failed and what to do); $2: log file
  printf '\nFAILED: %s\n' "$1" >&2
  if [[ -n "${2:-}" ]]; then
    printf 'Details are in this log file: %s\n' "$2" >&2
    if [[ -s "$2" ]]; then
      printf -- '--- last lines of the log ---\n' >&2
      tail -n 15 "$2" >&2
      printf -- '-----------------------------\n' >&2
    fi
  fi
  exit 1
}
event() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*" >>"$EVENTS_LOG"; }

clock() { cut -d' ' -f1 /proc/uptime; }
elapsed() { awk -v a="$1" -v b="$(clock)" 'BEGIN { printf "%.0f", b - a }'; }
declare -a TIMINGS=()
record_time() { TIMINGS+=("$(printf 'step %s %5ss  %s' "$1" "$(elapsed "$2")" "$3")"); }

as_root() {
  if [[ $(id -u) -eq 0 ]]; then
    "$@"
  elif command -v sudo >/dev/null && sudo -n true 2>/dev/null; then
    sudo "$@"
  else
    return 1
  fi
}

usage() { sed -n '2,/^$/s/^# \{0,1\}//p' "$SELF"; }

# Runs a command with its output appended to log file $1, printing a line
# every 30 seconds so a long step does not look stuck. Ctrl+C stops it too.
run_logged() {
  local log="$1" pid t0 next=30 rc=0
  shift
  "$@" >>"$log" 2>&1 &
  pid=$!
  t0="$(clock)"
  trap 'kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; exit 130' INT TERM
  while kill -0 "$pid" 2>/dev/null; do
    sleep 1
    if (($(elapsed "$t0") >= next)); then
      detail "... still working ($((next / 60)) min $((next % 60)) s)"
      next=$((next + 30))
    fi
  done
  wait "$pid" || rc=$?
  trap - INT TERM
  return "$rc"
}

# ---------------------------------------------------------------- processes

# A process we started is recorded as "<pid> <start time>" so that --stop
# never signals an unrelated process that reused the number.
# Fields 3 (state) and 22 (start time) of /proc/<pid>/stat, counted after the
# command name, which is in parentheses and may contain spaces.
proc_field() { sed 's/^.*) //' "/proc/$1/stat" 2>/dev/null | awk -v f="$(($2 - 2))" '{ print $f }'; }
proc_start() { proc_field "$1" 22; }
record_pid() { printf '%s %s\n' "$2" "$(proc_start "$2")" >"$RUN_DIR/$1.pid"; }
# Prints the pid when the recorded process is still the one we started (and
# has not exited: a zombie waiting to be reaped does not count).
our_pid() {
  local file="$RUN_DIR/$1.pid" pid start
  [[ -r "$file" ]] || return 1
  read -r pid start <"$file" || return 1
  [[ -n "$pid" && -d "/proc/$pid" && "$(proc_start "$pid")" == "$start" ]] || return 1
  [[ "$(proc_field "$pid" 3)" != Z ]] || return 1
  printf '%s' "$pid"
}
is_running() { our_pid "$1" >/dev/null; }
# True while process group $1 has a process that has not exited.
live_in_group() { pgrep -g "$1" -r D,R,S,T,t,W,I >/dev/null 2>&1; }

# Each component is started with setsid, so it leads its own process group;
# stopping it signals the whole group (the simulator and helper loops too).
stop_component() {
  local name="$1" file="$RUN_DIR/$1.pid" pid="" start=""
  [[ -r "$file" ]] && read -r pid start <"$file"
  # Our group is still there while its leader (even a zombie) is the process
  # we started, or while anything is left in it: Linux does not hand out a
  # pid that is still in use as a process group id.
  if [[ -n "$pid" ]] && { [[ "$(proc_start "$pid")" == "$start" ]] || pgrep -g "$pid" >/dev/null 2>&1; }; then
    kill -TERM -- "-$pid" 2>/dev/null || true
    for _ in $(seq 1 50); do
      live_in_group "$pid" || break
      sleep 0.1
    done
    # Anything left in the group (a process that ignored TERM).
    kill -KILL -- "-$pid" 2>/dev/null || true
  fi
  rm -f "$file"
}

stop_all() {
  stop_component supervisor
  stop_component websockify
  stop_component xvnc
  rm -f "$RUN_DIR/state"
}

port_busy() { (exec 3<>"/dev/tcp/127.0.0.1/$1") 2>/dev/null; }

display_busy() {
  local lock="/tmp/.X$SIM_DISPLAY-lock" pid
  if [[ -e "/tmp/.X11-unix/X$SIM_DISPLAY" || -e "$lock" ]]; then
    pid="$(tr -d ' ' <"$lock" 2>/dev/null || true)"
    # A lock left by a process that no longer exists is stale; Xvnc replaces it.
    [[ -n "$pid" && -d "/proc/$pid" ]] && return 0
    [[ -z "$pid" && -e "/tmp/.X11-unix/X$SIM_DISPLAY" ]] && return 0
  fi
  return 1
}

address() {
  if [[ "${CODESPACES:-}" == true && -n "${CODESPACE_NAME:-}" ]]; then
    printf 'https://%s-%s.%s/' "$CODESPACE_NAME" "$SIM_PORT" "${GITHUB_CODESPACES_PORT_FORWARDING_DOMAIN:-app.github.dev}"
  else
    printf 'http://localhost:%s/' "$SIM_PORT"
  fi
}

print_keys() {
  cat <<EOF

Open this address in Chrome:   $(address)
(noVNC's own page, showing the whole 800x800 virtual display, is $(address)vnc.html?autoconnect=1&resize=scale)

Click the screen once, then:
  Mouse click / drag   tap / swipe on the touch screen
  Up / Down arrows     the side keys (page back / forward)
  P                    power button: hold it half a second to sleep; any key wakes
  T                    slide switch
  Return               the Picco's Confirm input
Books: put .epub files in $SD_DIR/books/ (they show up in the file browser).
Stop:  press Ctrl+C here, or run ./scripts/sim.sh --stop
Reset: ./scripts/sim.sh --reset (wipes settings and reading state; books stay)
EOF
}

# ---------------------------------------------------------------- step 1

step_packages() {
  local missing=() p log="$LOG_DIR/packages.log"
  for p in "${DISPLAY_PACKAGES[@]}"; do
    dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q 'install ok installed' || missing+=("$p")
  done
  if [[ ${#missing[@]} -eq 0 ]]; then
    detail "already installed"
    return
  fi
  command -v apt-get >/dev/null ||
    fail "This machine has no apt-get; the simulator script supports Ubuntu and Debian only (a GitHub Codespace from this repo is Ubuntu)." ""
  detail "installing: ${missing[*]} (about a minute)"
  : >"$log"
  if ! as_root true; then
    fail "Installing system packages needs administrator rights (root or sudo without a password), which this account does not have." ""
  fi
  if ! run_logged "$log" as_root env DEBIAN_FRONTEND=noninteractive apt-get -o DPkg::Lock::Timeout=300 update -q ||
    ! run_logged "$log" as_root env DEBIAN_FRONTEND=noninteractive apt-get -o DPkg::Lock::Timeout=300 install -y -q \
      --no-install-recommends "${missing[@]}"; then
    fail "Installing the display packages failed; check the internet connection and run ./scripts/sim.sh again." "$log"
  fi
}

# ---------------------------------------------------------------- step 2

setup_fingerprint() {
  {
    cat "$REPO_ROOT/scripts/cloud-setup.sh" "$REPO_ROOT/scripts/pio_prefetch.py" \
      "$REPO_ROOT/platformio.ini" "$REPO_ROOT/platformio.project.ini"
    git -C "$REPO_ROOT" submodule status --recursive 2>/dev/null || true
    printf '%s\n' "${PLATFORMIO_CORE_DIR:-$HOME/.platformio}" "${PLATFORMIO_LIBDEPS_DIR:-}"
  } | sha256sum | cut -d' ' -f1
}

step_toolchain() {
  local log="$LOG_DIR/setup.log" stamp="$STATE_DIR/setup.done" fp
  fp="$(setup_fingerprint)"
  if [[ -r "$stamp" && "$(cat "$stamp")" == "$fp" ]] && command -v pio >/dev/null &&
    [[ -d "$REPO_ROOT/.pio/libdeps/$ENV_NAME/simulator" ]]; then
    detail "already installed ($(pio --version 2>/dev/null))"
    return
  fi
  detail "compiler, SDL2 and OpenSSL headers, PlatformIO and the simulator's libraries"
  detail "(about 2 minutes the first time; the ESP32 toolchain is not needed and not fetched)"
  : >"$log"
  local attempt
  for attempt in 1 2; do
    if run_logged "$log" "$REPO_ROOT/scripts/cloud-setup.sh" --simulator-only; then
      # Fingerprint again: the setup has just initialised the submodules.
      setup_fingerprint >"$stamp"
      return
    fi
    [[ $attempt -eq 1 ]] && detail "a download failed; trying once more" && echo "=== retry ===" >>"$log"
  done
  fail "Installing the build tools failed (usually a download problem); run ./scripts/sim.sh again in a minute." "$log"
}

# ---------------------------------------------------------------- step 3

# Runs in run_logged's background subshell, so the cd stays there.
pio_build() { cd "$REPO_ROOT" && pio run -e "$ENV_NAME"; }

step_build() {
  local log="$LOG_DIR/build.log"
  if [[ "$NO_BUILD" == 1 ]]; then
    [[ -x "$PROGRAM" ]] || fail "--no-build was given but the simulator has not been built yet; run ./scripts/sim.sh without --no-build." ""
    detail "skipped (--no-build); using $PROGRAM"
    return
  fi
  # PlatformIO decides what to recompile; with nothing changed it compiles
  # nothing and takes about 15 seconds (--no-build skips even that).
  if [[ -x "$PROGRAM" ]]; then
    detail "checking for changes (under a minute when nothing changed)"
  else
    detail "compiling the firmware for this computer (about 3 minutes on 4 cores, 6 on 2)"
  fi
  : >"$log"
  if ! run_logged "$log" pio_build; then
    fail "Building the simulator failed; this is a problem in the code, not on your side: send the log file below to the developers." "$log"
  fi
  [[ -x "$PROGRAM" ]] || fail "The build finished but $PROGRAM is missing." "$log"
  if grep -qE '^(Compiling|Linking) ' "$log"; then
    detail "built"
  else
    detail "up to date (nothing to compile)"
  fi
}

# ---------------------------------------------------------------- step 4

# Files the firmware can open as books; --reset keeps these in books/.
is_book() { [[ "${1,,}" =~ \.(epub|xtc|xtch|txt|md)$ ]]; }

reset_card() {
  [[ -d "$SD_DIR" ]] || return 0
  say "Resetting the simulated device: removing settings and reading state from $SD_DIR (books stay)."
  local f
  # Everything outside books/ is device state (.crosspoint/, NVS, caches...).
  find "$SD_DIR" -mindepth 1 -maxdepth 1 ! -name books -exec rm -rf {} +
  # Inside books/, keep the books themselves; anything else there is state.
  if [[ -d "$SD_DIR/books" ]]; then
    while IFS= read -r -d '' f; do
      is_book "$(basename "$f")" || rm -f "$f"
    done < <(find "$SD_DIR/books" -type f -print0)
    find "$SD_DIR/books" -mindepth 1 -type d -empty -delete
  fi
}

step_books() {
  mkdir -p "$SD_DIR/books"
  local src name dest copied=0 kept=0 n=0
  shopt -s nullglob
  for src in "$REPO_ROOT"/fixtures/books/*.epub; do
    n=$((n + 1))
    name="$(basename "$src")"
    dest="$SD_DIR/books/$name"
    if [[ ! -e "$dest" || "$RESET" == 1 ]]; then
      cp "$src" "$dest"
      copied=$((copied + 1))
    elif ! cmp -s "$src" "$dest"; then
      detail "kept your copy of $name (it differs from fixtures/books; --reset replaces it)"
      kept=$((kept + 1))
    fi
  done
  shopt -u nullglob
  if [[ $n -eq 0 ]]; then
    detail "no books in fixtures/books (nothing to copy)"
  else
    detail "$n test book(s) in fixtures/books: $copied copied, $((n - copied)) already there"
  fi
  detail "the simulated SD card is $SD_DIR ($(find "$SD_DIR/books" -type f | wc -l) file(s) in books/)"
}

# ---------------------------------------------------------------- step 5

write_web_page() {
  mkdir -p "$WEB_DIR"
  local entry
  for entry in app core vendor vnc.html vnc_lite.html; do
    [[ -e "$NOVNC_DIR/$entry" ]] && ln -sfn "$NOVNC_DIR/$entry" "$WEB_DIR/$entry"
  done
  # The landing page: noVNC's client showing only the simulator window's
  # area of the display, zoomed by a whole number of device pixels (sharp)
  # and centred; a browser window too small for that falls back to fit.
  cat >"$WEB_DIR/index.html" <<'HTML'
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>BOOX Picco simulator</title>
<link rel="icon" href="data:,">
<style>
  html, body { margin: 0; height: 100%; overflow: hidden; background: #3b3b3b; color: #eee;
    font: 13px system-ui, sans-serif; }
  #bar { position: fixed; top: 0; left: 0; right: 0; height: 28px; display: flex; align-items: center;
    gap: 14px; padding: 0 10px; background: #222; white-space: nowrap; overflow: hidden; }
  #bar .hint { color: #aaa; overflow: hidden; text-overflow: ellipsis; }
  #bar .grow { flex: 1; }
  #bar button { font: inherit; color: #eee; background: #444; border: 1px solid #666; border-radius: 3px;
    padding: 1px 8px; cursor: pointer; }
  #crop { position: fixed; overflow: hidden; background: #fff; }
  #screen { position: absolute; left: 0; top: 0; }
  #crop.sharp canvas { image-rendering: pixelated; }
  #msg { position: fixed; top: 40%; left: 0; right: 0; text-align: center; font-size: 16px; }
</style>
</head>
<body>
<div id="bar">
  <b>BOOX Picco simulator</b>
  <span id="status">Connecting...</span>
  <span class="hint">Click the screen, then: Up/Down = side keys, hold P = sleep (any key wakes), T = slide switch, mouse = touch</span>
  <span class="grow"></span>
  <span id="scale"></span>
  <button id="mode" title="Sharp: whole-pixel zoom. Fit: fill the window (slightly soft).">Fit to window</button>
</div>
<div id="crop"><div id="screen"></div></div>
<div id="msg"></div>
<script type="module">
import RFB from './core/rfb.js';

const crop = document.getElementById('crop');
const screen = document.getElementById('screen');
const statusEl = document.getElementById('status');
const scaleEl = document.getElementById('scale');
const modeBtn = document.getElementById('mode');
const msg = document.getElementById('msg');
const BAR = 28;
let mode = 'sharp';
try { mode = localStorage.getItem('picco-sim-mode') || 'sharp'; } catch (e) {}
let rfb = null;
let fb = { w: 800, h: 800 };     // the virtual display
let win = { w: 480, h: 800, x: 0, y: 0 };  // the simulator window on it

function layout() {
  const canvas = screen.querySelector('canvas');
  if (canvas && canvas.width > 0 && canvas.height > 0) fb = { w: canvas.width, h: canvas.height };
  const x = Math.max(0, Math.min(win.x, fb.w - 1)), y = Math.max(0, Math.min(win.y, fb.h - 1));
  const w = Math.min(win.w, fb.w - x), h = Math.min(win.h, fb.h - y);
  const dpr = window.devicePixelRatio || 1;
  const availW = window.innerWidth - 8, availH = window.innerHeight - BAR - 8;
  const k = Math.floor(Math.min(availW * dpr / w, availH * dpr / h));
  const sharp = mode === 'sharp' && k >= 1;
  const s = sharp ? k / dpr : Math.min(availW / w, availH / h);  // CSS px per screen pixel
  // Put the top-left corner on a device pixel so whole-pixel zoom stays crisp.
  const left = Math.round(((window.innerWidth - w * s) / 2) * dpr) / dpr;
  const top = Math.round((BAR + (window.innerHeight - BAR - h * s) / 2) * dpr) / dpr;
  Object.assign(crop.style, { left: left + 'px', top: top + 'px', width: w * s + 'px', height: h * s + 'px' });
  // noVNC scales the whole display to fit this box, that is by exactly s.
  Object.assign(screen.style, { left: -x * s + 'px', top: -y * s + 'px', width: fb.w * s + 'px', height: fb.h * s + 'px' });
  crop.classList.toggle('sharp', sharp);
  scaleEl.textContent = w + 'x' + h + ' at ' + (sharp ? k + 'x' : (s * dpr).toFixed(2) + 'x');
  modeBtn.textContent = mode === 'sharp' ? 'Fit to window' : 'Sharp pixels';
}

async function pollWindow() {
  try {
    const r = await fetch('window.json?' + Date.now(), { cache: 'no-store' });
    if (r.ok) {
      const j = await r.json();
      const next = { w: j.w, h: j.h, x: j.x || 0, y: j.y || 0 };
      if (next.w > 0 && next.h > 0 && JSON.stringify(next) !== JSON.stringify(win)) { win = next; layout(); }
    }
  } catch (e) {}
}

function connect() {
  const url = (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/websockify';
  rfb = new RFB(screen, url, { shared: true });
  rfb.scaleViewport = true;
  rfb.resizeSession = false;
  rfb.background = '#ffffff';
  rfb.addEventListener('connect', () => {
    statusEl.textContent = 'Connected';
    msg.textContent = '';
    layout();
    rfb.focus();
  });
  rfb.addEventListener('disconnect', () => {
    statusEl.textContent = 'Not connected; retrying...';
    msg.textContent = 'The simulator is not reachable. If you stopped it, run ./scripts/sim.sh again.';
    rfb = null;
    setTimeout(connect, 2000);
  });
}

modeBtn.addEventListener('click', () => {
  mode = mode === 'sharp' ? 'fit' : 'sharp';
  try { localStorage.setItem('picco-sim-mode', mode); } catch (e) {}
  layout();
  if (rfb) rfb.focus();
});
window.addEventListener('resize', layout);
// The window turns to 800x480 when the firmware rotates the screen.
setInterval(pollWindow, 700);
setInterval(() => {
  const c = screen.querySelector('canvas');
  if (c && (c.width !== fb.w || c.height !== fb.h)) layout();
}, 500);
pollWindow();
layout();
connect();
</script>
</body>
</html>
HTML
  # Written by the supervisor once the window exists; until then the page
  # assumes portrait at 0,0.
  rm -f "$WEB_DIR/window.json"
}

# Tells the page which part of the display the simulator window covers.
write_window_size() {
  printf '{"w": %s, "h": %s, "x": %s, "y": %s}\n' "$1" "$2" "${3:-0}" "${4:-0}" >"$WEB_DIR/window.json.tmp"
  mv -f "$WEB_DIR/window.json.tmp" "$WEB_DIR/window.json"
}

start_display() {
  local log="$LOG_DIR/display.log"
  : >"$log"
  setsid Xvnc ":$SIM_DISPLAY" -geometry "${DISPLAY_SIZE}x${DISPLAY_SIZE}" -depth 24 \
    -localhost -interface 127.0.0.1 -rfbport "$SIM_VNC_PORT" -nolisten tcp \
    -SecurityTypes None -AlwaysShared -AcceptSetDesktopSize=0 \
    -desktop "BOOX Picco simulator" </dev/null >>"$log" 2>&1 &
  record_pid xvnc $!
  local ready=0 out
  for _ in $(seq 1 100); do
    if DISPLAY=":$SIM_DISPLAY" xdotool getdisplaygeometry >/dev/null 2>&1 && port_busy "$SIM_VNC_PORT"; then
      ready=1
      break
    fi
    is_running xvnc || break
    sleep 0.1
  done
  [[ $ready == 1 ]] || fail "The virtual display (Xvnc on display :$SIM_DISPLAY) did not start." "$log"
  # SDL centres a new window on the monitor, and nothing may move it later:
  # SDL keeps its own idea of the position, and a window moved behind its back
  # draws garbage when it next grows (portrait to landscape). So present a
  # 480x800 monitor at the top-left of the 800x800 display: the window opens
  # at 0,0 and has room to turn to 800x480 in place.
  out="$(DISPLAY=":$SIM_DISPLAY" xrandr 2>>"$log" | awk '/ connected/ { print $1; exit }')"
  if ! {
    [[ -n "$out" ]] &&
      DISPLAY=":$SIM_DISPLAY" xrandr --newmode picco 0 480 0 0 0 800 0 0 0 >>"$log" 2>&1 &&
      DISPLAY=":$SIM_DISPLAY" xrandr --addmode "$out" picco >>"$log" 2>&1 &&
      DISPLAY=":$SIM_DISPLAY" xrandr --fb "${DISPLAY_SIZE}x${DISPLAY_SIZE}" --output "$out" --mode picco >>"$log" 2>&1
  }; then
    fail "Setting up the virtual display's screen layout failed." "$log"
  fi
}

start_novnc() {
  local log="$LOG_DIR/novnc.log" bind=127.0.0.1
  # In a Codespace the port is reached through GitHub's port forwarding, which
  # connects from inside the container; listen on all interfaces there so it
  # is found whichever way the forwarder connects. Elsewhere, localhost only.
  [[ "${CODESPACES:-}" == true ]] && bind=0.0.0.0
  : >"$log"
  setsid websockify --web "$WEB_DIR" "$bind:$SIM_PORT" "127.0.0.1:$SIM_VNC_PORT" </dev/null >>"$log" 2>&1 &
  record_pid websockify $!
  for _ in $(seq 1 100); do
    curl -sf -o /dev/null "http://127.0.0.1:$SIM_PORT/" && return 0
    is_running websockify || break
    sleep 0.1
  done
  fail "The noVNC web server (websockify on port $SIM_PORT) did not start." "$log"
}

start_supervisor() {
  : >"$SIM_LOG"
  setsid "$SELF" --supervise </dev/null >>"$LOG_DIR/supervisor.log" 2>&1 &
  record_pid supervisor $!
  # Ready once the window is up and the page has been told where it is.
  for _ in $(seq 1 300); do
    if DISPLAY=":$SIM_DISPLAY" xdotool search --onlyvisible --name "$WINDOW_NAME" >/dev/null 2>&1 &&
      [[ -s "$WEB_DIR/window.json" ]]; then
      return 0
    fi
    is_running supervisor || break
    sleep 0.1
  done
  fail "The simulator window did not appear within 30 seconds." "$SIM_LOG"
}

write_state() {
  {
    printf 'port=%s\ndisplay=%s\nvnc_port=%s\nprogram=%s\n' "$SIM_PORT" "$SIM_DISPLAY" "$SIM_VNC_PORT" "$PROGRAM"
    printf 'program_mtime=%s\nsd=%s\n' "$(stat -c %Y "$PROGRAM")" "$SD_DIR"
  } >"$RUN_DIR/state"
}

state_value() { sed -n "s/^$1=//p" "$RUN_DIR/state" 2>/dev/null; }

all_running() { is_running xvnc && is_running websockify && is_running supervisor; }

step_start() {
  if all_running && [[ "$(state_value port)" == "$SIM_PORT" && "$(state_value display)" == "$SIM_DISPLAY" &&
    "$(state_value program_mtime)" == "$(stat -c %Y "$PROGRAM")" && "$RESET" != 1 ]]; then
    detail "already running (display :$SIM_DISPLAY, port $SIM_PORT); nothing to restart"
    return
  fi
  if is_running xvnc || is_running websockify || is_running supervisor; then
    detail "stopping the previous run first"
    stop_all
  fi
  if port_busy "$SIM_PORT"; then
    fail "Port $SIM_PORT is already used by another program; stop that program, or choose another port, for example: SIM_PORT=6081 ./scripts/sim.sh" ""
  fi
  if display_busy; then
    fail "Display :$SIM_DISPLAY is already used by another program; choose another one, for example: SIM_DISPLAY=$((SIM_DISPLAY + 10)) ./scripts/sim.sh" ""
  fi
  if port_busy "$SIM_VNC_PORT"; then
    fail "Port $SIM_VNC_PORT (VNC, localhost only) is already used by another program; choose another display, for example: SIM_DISPLAY=$((SIM_DISPLAY + 10)) ./scripts/sim.sh" ""
  fi
  if port_busy "$SIM_HTTP_PORT"; then
    detail "note: port $SIM_HTTP_PORT is busy, so the simulator's File Transfer screen will not work (set SIM_HTTP_PORT to change it)"
  fi
  : >"$EVENTS_LOG"
  write_web_page
  detail "virtual display :$SIM_DISPLAY (VNC on localhost:$SIM_VNC_PORT only)"
  start_display
  detail "noVNC web client on port $SIM_PORT"
  start_novnc
  detail "simulator window"
  write_state
  start_supervisor
}

# ---------------------------------------------------------------- supervisor

# Runs in its own process group (setsid); short messages go to events.log
# (shown by the foreground sim.sh), its own output to supervisor.log.
# Keeps the simulator running: when it exits (window closed, a firmware
# restart, a crash) it is started again. Sleep and wake need nothing from
# here: the simulator imitates a wake by exec'ing itself, so the process
# (and its pid) stays the same and this loop just keeps waiting.
supervise() {
  export DISPLAY=":$SIM_DISPLAY"
  export CROSSPOINT_SIM_SD="$SD_DIR"
  export CROSSPOINT_SIM_HTTP_PORT="$SIM_HTTP_PORT"
  local child="" rc started quick=0
  trap '[[ -n "$child" ]] && kill -TERM "$child" 2>/dev/null; exit 0' TERM INT HUP
  watch_window &
  cd "$REPO_ROOT"
  while true; do
    started="$(clock)"
    printf '\n===== simulator started %s =====\n' "$(date '+%F %T')" >>"$SIM_LOG"
    "$PROGRAM" >>"$SIM_LOG" 2>&1 </dev/null &
    child=$!
    event "simulator running (pid $child)"
    rc=0
    wait "$child" || rc=$?
    child=""
    if (($(elapsed "$started") < 10)); then quick=$((quick + 1)); else quick=0; fi
    if ((quick >= 5)); then
      event "the simulator keeps closing right after it starts (exit status $rc); waiting 30 s before trying again. Log: $SIM_LOG"
      sleep 30
      quick=0
    else
      event "the simulator closed (exit status $rc); starting it again"
      sleep 1
    fi
  done
}

# Tells the browser page where the simulator window is: 480x800 in portrait,
# 800x480 in landscape, normally at 0,0. (It never moves the window: see
# start_display.)
watch_window() {
  local id geom x y w h last=""
  while true; do
    sleep 0.3
    id="$(xdotool search --onlyvisible --name "$WINDOW_NAME" 2>/dev/null | tail -n 1)" || continue
    [[ -n "$id" ]] || continue
    geom="$(xdotool getwindowgeometry --shell "$id" 2>/dev/null)" || continue
    x="$(sed -n 's/^X=//p' <<<"$geom")"
    y="$(sed -n 's/^Y=//p' <<<"$geom")"
    w="$(sed -n 's/^WIDTH=//p' <<<"$geom")"
    h="$(sed -n 's/^HEIGHT=//p' <<<"$geom")"
    [[ -n "$x" && -n "$y" && -n "$w" && -n "$h" ]] || continue
    if [[ "$x $y $w $h" != "$last" ]]; then
      write_window_size "$w" "$h" "$x" "$y"
      [[ -n "$last" ]] && event "screen is now ${w}x${h}"
      last="$x $y $w $h"
    fi
  done
}

# ---------------------------------------------------------------- commands

cmd_status() {
  local name pid any=0
  for name in xvnc websockify supervisor; do
    if pid="$(our_pid "$name")"; then
      say "  $name: running (pid $pid)"
      any=1
    else
      say "  $name: not running"
    fi
  done
  if pid="$(pgrep -f -x "$PROGRAM" 2>/dev/null | head -n 1)" && [[ -n "$pid" ]] && is_running supervisor; then
    say "  simulator: running (pid $pid)"
  fi
  if all_running; then
    SIM_PORT="$(state_value port)"
    say "The simulator is running: open $(address)"
  elif [[ $any -eq 1 ]]; then
    say "Partly running; ./scripts/sim.sh starts it again cleanly, ./scripts/sim.sh --stop stops it."
  else
    say "The simulator is not running. Start it with ./scripts/sim.sh"
  fi
  say "Logs: $LOG_DIR"
}

cmd_stop() {
  if is_running xvnc || is_running websockify || is_running supervisor; then
    say "Stopping the simulator, noVNC and the virtual display..."
    stop_all
    say "Stopped."
  else
    stop_all
    say "Nothing to stop: the simulator is not running."
  fi
}

follow() {
  say ""
  say "Running. Messages from the simulator appear below; press Ctrl+C to stop it."
  trap 'say ""; say "Stopping..."; stop_all; say "Stopped. Run ./scripts/sim.sh to start again."; exit 0' INT TERM
  trap 'exit 0' HUP
  tail -n +1 -F "$EVENTS_LOG" 2>/dev/null &
  local tail_pid=$! warned=0
  while true; do
    sleep 3
    if ! all_running; then
      if [[ $warned -eq 0 ]]; then
        say "Something stopped (see ./scripts/sim.sh --status); run ./scripts/sim.sh again to restart everything."
        warned=1
      fi
      if ! is_running xvnc && ! is_running websockify && ! is_running supervisor; then
        kill "$tail_pid" 2>/dev/null || true
        exit 1
      fi
    fi
  done
}

main() {
  RESET=0 NO_BUILD=0 NO_WAIT=0 PREPARE=0
  local action=run arg
  for arg in "$@"; do
    case "$arg" in
      --reset) RESET=1 ;;
      --no-build) NO_BUILD=1 ;;
      --no-wait) NO_WAIT=1 ;;
      --prepare) PREPARE=1 ;;
      --stop) action=stop ;;
      --status) action=status ;;
      --supervise) action=supervise ;;
      -h | --help) usage; exit 0 ;;
      *) say "Unknown option: $arg"; usage; exit 2 ;;
    esac
  done
  mkdir -p "$LOG_DIR" "$RUN_DIR"
  # Keep .sim/ out of git without touching upstream's .gitignore.
  [[ -e "$STATE_DIR/.gitignore" ]] || printf '*\n' >"$STATE_DIR/.gitignore"
  case "$action" in
    stop) cmd_stop; return ;;
    status) cmd_status; return ;;
    supervise) supervise; return ;;
  esac

  local t0 total
  total="$(clock)"
  say "BOOX Picco simulator: $REPO_ROOT"
  if [[ "$RESET" == 1 ]]; then
    if is_running supervisor; then stop_component supervisor; fi
    reset_card
  fi

  step 1 "installing system packages (display, VNC server, noVNC)"
  t0="$(clock)"; step_packages; record_time 1 "$t0" "system packages"
  step 2 "installing build tools and the simulator's libraries"
  t0="$(clock)"; step_toolchain; record_time 2 "$t0" "build tools"
  step 3 "building the simulator"
  t0="$(clock)"; step_build; record_time 3 "$t0" "build"
  step 4 "copying books to the simulated SD card"
  t0="$(clock)"; step_books; record_time 4 "$t0" "books"
  if [[ "$PREPARE" == 1 ]]; then
    say ""
    say "Prepared (steps 1 to 4); nothing started. Start the simulator with ./scripts/sim.sh"
    print_times "$total"
    return
  fi
  step 5 "starting the virtual display, noVNC and the simulator"
  t0="$(clock)"; step_start; record_time 5 "$t0" "start"
  step 6 "ready"
  print_keys
  print_times "$total"
  [[ "$NO_WAIT" == 1 ]] && return
  follow
}

print_times() {
  local line
  say ""
  say "Time taken:"
  for line in "${TIMINGS[@]}"; do say "  $line"; done
  say "  total  $(elapsed "$1")s. Logs: $LOG_DIR"
}

main "$@"
