#!/usr/bin/env bash
# Prepare a machine to build `picco` (firmware) and `simulator_picco` offline.
#
#   ./scripts/cloud-setup.sh
#
# Installs system packages, PlatformIO, the git submodules, and every
# toolchain, framework package and library both environments need, so that a
# later `pio run -e picco` / `pio run -e simulator_picco` downloads nothing.
# Idempotent and non-interactive; a second run only checks.
#
# Works in Claude Code cloud sessions (TLS-intercepting proxy), GitHub Actions
# and Codespaces. See docs/net-smoke-report.md for why PlatformIO's packages
# are fetched by scripts/pio_prefetch.py instead of by `pio run`.
#
# Environment:
#   PLATFORMIO_CORE_DIR  PlatformIO's package directory (default ~/.platformio)
#   SETUP_SKIP_APT=1     skip the system package phase
#   PIO_VENV             where to install PlatformIO if it is missing
#
# Exit status: 0 when everything is in place, non-zero on the first failure,
# with a message naming the package and host that failed.

set -euo pipefail

PIO_VERSION="6.2.0"
ENVS=(picco simulator_picco)
APT_PACKAGES=(
  build-essential cmake git curl ca-certificates xz-utils unzip
  python3 python3-venv pkg-config
  libsdl2-dev libssl-dev xvfb xauth
)

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Our own pio calls only: no usage reports (collector.platformio.org is not
# on the cloud allowlist, and the refusal is noise in a setup log).
export PLATFORMIO_SETTING_ENABLE_TELEMETRY=No

# Elapsed time from a clock that stops while the machine is paused (cloud
# session VMs are snapshotted and resumed; wall-clock time jumps over that).
clock() { if [[ -r /proc/uptime ]]; then cut -d' ' -f1 /proc/uptime; else date +%s.%N; fi; }
since() { awk -v a="$1" -v b="$(clock)" 'BEGIN { printf "%.0f", b - a }'; }
START_CLOCK="$(clock)"
START_ISO="$(date -u +%Y-%m-%dT%H:%M:%S)"
declare -a PHASE_LINES=()
CURRENT_PHASE=""

say() { printf '[setup] %s\n' "$*"; }

proxy_url() { printf '%s' "${HTTPS_PROXY:-${https_proxy:-}}"; }

# True when HTTPS goes through Claude Code's agent proxy (detected, not assumed).
agent_proxy_status() {
  local p
  p="$(proxy_url)"
  [[ -n "$p" ]] || return 1
  curl -fsS --max-time 5 --noproxy '*' "${p%/}/__agentproxy/status" 2>/dev/null
}

print_refusals() {
  local status
  status="$(agent_proxy_status)" || return 0
  STATUS_JSON="$status" python3 - "$START_ISO" <<'EOF'
import json, os, sys
since = sys.argv[1]
failures = [f for f in json.loads(os.environ["STATUS_JSON"]).get("recentRelayFailures", [])
            if f.get("ts", "") >= since]
if failures:
    print("[setup] the session proxy refused or cut these connections during this run:")
    for f in failures:
        print(f"[setup]   {f.get('host')}  {f.get('kind')}: {f.get('detail')}")
    print("[setup] a host listed here must be added to the environment's network allowlist")
EOF
}

on_exit() {
  local rc=$?
  if [[ $rc -ne 0 ]]; then
    echo >&2
    say "FAILED in phase '${CURRENT_PHASE:-?}' (exit $rc) after $(since "$START_CLOCK")s" >&2
    print_refusals >&2 || true
  fi
}
trap on_exit EXIT

run_phase() {
  local name="$1"
  shift
  CURRENT_PHASE="$name"
  local t0 t1
  t0="$(clock)"
  say "== $name"
  "$@"
  t1="$(clock)"
  PHASE_LINES+=("$(awk -v n="$name" -v a="$t0" -v b="$t1" 'BEGIN { printf "%-22s %7.1fs", n, b - a }')")
}

as_root() {
  if [[ $(id -u) -eq 0 ]]; then
    "$@"
  elif command -v sudo >/dev/null && sudo -n true 2>/dev/null; then
    sudo "$@"
  else
    say "need root (or passwordless sudo) for: $*" >&2
    return 1
  fi
}

phase_apt() {
  if [[ "${SETUP_SKIP_APT:-0}" == 1 ]]; then
    say "skipped (SETUP_SKIP_APT=1)"
    return
  fi
  local missing=() p
  for p in "${APT_PACKAGES[@]}"; do
    dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q 'install ok installed' || missing+=("$p")
  done
  if [[ ${#missing[@]} -eq 0 ]]; then
    say "all ${#APT_PACKAGES[@]} system packages present"
    return
  fi
  say "installing: ${missing[*]}"
  local log
  log="$(mktemp)"
  if ! as_root env DEBIAN_FRONTEND=noninteractive apt-get update -q >"$log" 2>&1 ||
    ! as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends "${missing[@]}" >>"$log" 2>&1; then
    tail -20 "$log" >&2
    grep -E '^(Err|E|W):' "$log" | sed 's/^/[setup] apt: /' >&2 || true
    say "apt failed; the hosts are in the 'Err:'/'Failed to fetch' lines above" >&2
    return 1
  fi
  grep -E '^(Get|Hit):' "$log" | awk '{print $2}' | sed -E 's#^[a-z]+://([^/]+)/.*#\1#' | sort -u |
    sed 's/^/[setup] apt used /' || true
}

pio_python() {
  # The interpreter that runs PlatformIO (pio_prefetch.py imports platformio).
  pio system info --json-output | python3 -c 'import json,sys; print(json.load(sys.stdin)["python_exe"]["value"])'
}

phase_platformio() {
  if command -v pio >/dev/null && [[ "$(pio --version 2>/dev/null)" == *"version $PIO_VERSION" ]]; then
    say "PlatformIO $PIO_VERSION already installed: $(command -v pio)"
    return
  fi
  local venv="${PIO_VENV:-$HOME/.local/share/platformio-$PIO_VERSION}"
  say "installing PlatformIO $PIO_VERSION into $venv (from PyPI)"
  if [[ ! -x "$venv/bin/python" ]]; then
    python3 -m venv "$venv"
  fi
  "$venv/bin/python" -m pip install -q --disable-pip-version-check "platformio==$PIO_VERSION"
  mkdir -p "$HOME/.local/bin"
  ln -sf "$venv/bin/pio" "$HOME/.local/bin/pio"
  ln -sf "$venv/bin/platformio" "$HOME/.local/bin/platformio"
  if [[ $(id -u) -eq 0 ]] && { [[ ! -e /usr/local/bin/pio ]] || [[ -L /usr/local/bin/pio ]]; }; then
    ln -sf "$venv/bin/pio" /usr/local/bin/pio
    ln -sf "$venv/bin/platformio" /usr/local/bin/platformio
  fi
  export PATH="$HOME/.local/bin:$PATH"
  local got
  got="$(pio --version)"
  if [[ "$got" != *"version $PIO_VERSION" ]]; then
    say "PATH finds '$got' at $(command -v pio); put $HOME/.local/bin first in PATH" >&2
    return 1
  fi
}

phase_submodules() {
  git -C "$REPO_ROOT" submodule update --init --recursive
  git -C "$REPO_ROOT" submodule status --recursive | sed 's/^/[setup] /'
  if git -C "$REPO_ROOT" submodule status --recursive | grep -q '^[-+U]'; then
    say "a submodule is missing or not at the recorded commit" >&2
    return 1
  fi
}

phase_prefetch() {
  local py
  py="$(pio_python)"
  "$py" "$REPO_ROOT/scripts/pio_prefetch.py" --project-dir "$REPO_ROOT" "${ENVS[@]}"
}

say "repo: $REPO_ROOT"
say "PlatformIO core dir: ${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
if agent_proxy_status >/dev/null; then
  say "intercepting proxy: detected (Claude Code agent proxy at $(proxy_url)); using the pre-fetch path, because 'pio run' alone fails TLS verification here"
elif [[ -n "$(proxy_url)" ]]; then
  say "intercepting proxy: not detected (plain HTTPS proxy $(proxy_url)); running the same pre-fetch, which 'pio run' would not need"
else
  say "intercepting proxy: not detected (direct network, e.g. GitHub Actions or Codespaces); running the same pre-fetch, which 'pio run' would not need"
fi

run_phase "system packages" phase_apt
run_phase "platformio" phase_platformio
run_phase "git submodules" phase_submodules
run_phase "platformio packages" phase_prefetch
CURRENT_PHASE="done"

TOTAL="$(since "$START_CLOCK")"
say "phase times:"
for line in "${PHASE_LINES[@]}"; do say "  $line"; done
say "  total                  ${TOTAL}s"
if (( TOTAL > 300 )); then
  say "note: over 300s; cloud environments cache a setup script only if it finishes in about five minutes"
fi
say "ready: pio run -e picco && pio run -e simulator_picco"
