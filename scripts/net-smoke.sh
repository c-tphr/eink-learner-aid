#!/usr/bin/env bash
# Network smoke test: from empty PlatformIO caches, run scripts/cloud-setup.sh
# and a clean build of `picco` and `simulator_picco`, record every host
# contacted and every refusal, and write docs/net-smoke-report.md.
#
#   ./scripts/net-smoke.sh
#
# Builds the committed HEAD in a temporary git worktree (fresh submodules,
# .pio and build cache) with a temporary PLATFORMIO_CORE_DIR, so the normal
# ~/.platformio and this checkout's .pio are not touched. Uncommitted changes
# are not tested. Set NET_SMOKE_KEEP=1 to keep the temporary directory.
#
# Exit status:
#   0  setup succeeded, nothing needed was refused, both builds succeeded
#   1  setup failed or a needed host was refused (see the report)
#   3  network clean, but a build failed for a reason other than the network

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPORT="$REPO_ROOT/docs/net-smoke-report.md"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/net-smoke.XXXXXX")"
ORIG_PROXY="${HTTPS_PROXY:-${https_proxy:-}}"
START_ISO="$(date -u +%Y-%m-%dT%H:%M:%S)"
PROXY_PID=""
SRC="$WORK/src"

say() { printf '[net-smoke] %s\n' "$*"; }

cleanup() {
  [[ -n "$PROXY_PID" ]] && kill "$PROXY_PID" 2>/dev/null
  if [[ -d "$SRC" && "${NET_SMOKE_KEEP:-0}" != 1 ]]; then
    git -C "$REPO_ROOT" worktree remove --force "$SRC" >/dev/null 2>&1
  fi
  if [[ "${NET_SMOKE_KEEP:-0}" == 1 ]]; then
    say "kept $WORK"
  else
    rm -rf "$WORK"
  fi
}
trap cleanup EXIT

# Elapsed time from a clock that stops while the machine is paused (cloud
# session VMs are snapshotted and resumed; wall-clock time jumps over that).
clock() { if [[ -r /proc/uptime ]]; then cut -d' ' -f1 /proc/uptime; else date +%s.%N; fi; }
since() { awk -v a="$1" -v b="$(clock)" 'BEGIN { printf "%.0f", b - a }'; }
mark() { printf '{"name": "%s", "start": %s}\n' "$1" "$(date +%s.%N)" >>"$WORK/phases.jsonl"; }

export PLATFORMIO_CORE_DIR="$WORK/core"
mkdir -p "$PLATFORMIO_CORE_DIR"
# upstream's scripts/patch_sdfat.py requires libdeps inside the project's
# .pio, so a clean build needs a clean checkout rather than PLATFORMIO_*_DIR.
if [[ -n "$(git -C "$REPO_ROOT" status --porcelain --untracked-files=no)" ]]; then
  say "note: uncommitted changes are not part of this test; it builds HEAD"
fi
git -C "$REPO_ROOT" worktree add -q --detach "$SRC" HEAD || { say "git worktree add failed" >&2; exit 1; }

# --- local CONNECT-logging proxy, chained to the session's proxy ------------
NET_SMOKE_UPSTREAM="$ORIG_PROXY" python3 "$REPO_ROOT/scripts/net_smoke.py" proxy \
  --log "$WORK/proxy.jsonl" --port-file "$WORK/port" &
PROXY_PID=$!
for _ in $(seq 50); do [[ -s "$WORK/port" ]] && break; sleep 0.1; done
if [[ ! -s "$WORK/port" ]]; then
  say "logging proxy did not start" >&2
  exit 1
fi
LOCAL_PROXY="http://127.0.0.1:$(cat "$WORK/port")"
say "logging proxy $LOCAL_PROXY -> ${ORIG_PROXY:-direct}"

NETWORK="direct (no HTTPS proxy)"
if [[ -n "$ORIG_PROXY" ]]; then
  if curl -fsS --max-time 5 --noproxy '*' "${ORIG_PROXY%/}/__agentproxy/status" >/dev/null 2>&1; then
    NETWORK="Claude Code agent proxy (allowlisting, TLS re-terminated)"
  else
    NETWORK="HTTPS proxy"
  fi
fi

TRACE=()
if command -v strace >/dev/null && strace -f -qq -e trace=connect -o /dev/null true 2>/dev/null; then
  TRACE=(strace -f -qq --seccomp-bpf -x -s 512 -e trace=connect,sendto,sendmmsg)
  say "tracing DNS lookups with strace (for hosts outside the HTTPS proxy)"
fi
traced() {
  local out="$1"
  shift
  if [[ ${#TRACE[@]} -gt 0 ]]; then
    "${TRACE[@]}" -o "$out" "$@"
  else
    "$@"
  fi
}

# --- apt index (plain HTTP, outside the HTTPS proxy) ------------------------
mark "apt index"
if [[ $(id -u) -eq 0 ]] && command -v apt-get >/dev/null; then
  apt-get update -q 2>&1 | tee "$WORK/apt.log" | tail -1
  grep -E '^(Get|Hit|Ign):' "$WORK/apt.log" | awk '{print $2}' |
    sed -E 's#^([a-z]+)://([^/]+)/.*#\1 \2#' | sort -u >"$WORK/apt-hosts.txt" || true
fi

# --- setup from empty caches ------------------------------------------------
mark "setup"
say "running cloud-setup.sh with empty caches"
SETUP_START="$(clock)"
HTTPS_PROXY="$LOCAL_PROXY" https_proxy="$LOCAL_PROXY" \
  traced "$WORK/strace-setup.txt" "$SRC/scripts/cloud-setup.sh" 2>&1 | tee "$WORK/setup.log" |
  grep -E '^\[(setup|prefetch)\]'
SETUP_RC=${PIPESTATUS[0]}
say "setup exit $SETUP_RC after $(since "$SETUP_START")s"
sed -n '/phase times:/,/total/p' "$WORK/setup.log" | sed 's/^\[setup\] *//' >"$WORK/setup-times.txt"

# --- clean builds -------------------------------------------------------------
declare -A BUILD_RC
build() {
  local env="$1" t0 rc
  mark "build $env"
  say "pio run -e $env (clean)"
  t0="$(clock)"
  (cd "$SRC" && HTTPS_PROXY="$LOCAL_PROXY" https_proxy="$LOCAL_PROXY" \
    traced "$WORK/strace-build-$env.txt" pio run -e "$env") >"$WORK/build-$env.log" 2>&1
  rc=$?
  BUILD_RC[$env]=$rc
  # Sizes now: building the next environment can clear .pio/build.
  local f
  for f in firmware.bin program; do
    [[ -f "$SRC/.pio/build/$env/$f" ]] && echo "$f $(stat -c %s "$SRC/.pio/build/$env/$f")" >>"$WORK/build-$env.size"
  done
  since "$t0" >"$WORK/build-$env.seconds"
  say "  exit $rc after $(cat "$WORK/build-$env.seconds")s"
}
if [[ $SETUP_RC -eq 0 ]]; then
  build picco
  build simulator_picco
fi
mark "end"

curl -fsS --max-time 5 --noproxy '*' "${ORIG_PROXY%/}/__agentproxy/status" >"$WORK/agent-status.json" 2>/dev/null ||
  rm -f "$WORK/agent-status.json"
cat "$WORK"/strace-*.txt >"$WORK/strace.txt" 2>/dev/null || true

# --- results ----------------------------------------------------------------
python3 - "$WORK" <<'EOF'
import json, re, sys
from pathlib import Path
work = Path(sys.argv[1])
builds = {}
for env in ("picco", "simulator_picco"):
    log = work / f"build-{env}.log"
    if not log.is_file():
        builds[env] = {"result": "not run (setup failed)"}
        continue
    text = log.read_text(errors="replace")
    ok = "[SUCCESS]" in text
    b = {"result": "SUCCESS" if ok else "FAILED", "seconds": (work / f"build-{env}.seconds").read_text().strip()}
    sizes = [l.strip() for l in text.splitlines() if l.startswith(("RAM:", "Flash:"))]
    size_file = work / f"build-{env}.size"
    if size_file.is_file():
        for line in size_file.read_text().splitlines():
            name, n = line.split()
            sizes.append(f"{name} {int(n):,} B")
    b["size"] = "<br>".join(sizes)
    if not ok:
        errs = sorted({l.strip() for l in text.splitlines() if "error:" in l})
        b["errors"] = "\n".join(errs[:20])
        net = [l for l in text.splitlines() if re.search(r"CERTIFICATE_VERIFY_FAILED|HTTPClientError|Could not resolve|403 Forbidden|Connection refused", l)]
        b["notes"] = ("download failure during the build: " + net[0].strip()) if net else \
            "compile errors, not a network failure (no download was attempted). The build stops at the " \
            "first failing file; docs/BASE.md, section Simulator, lists every error found with `SCONSFLAGS=-k`"
    builds[env] = b
(work / "builds.json").write_text(json.dumps(builds, indent=2))
EOF

NEEDED_REFUSED=$(python3 - "$WORK" "$START_ISO" <<'EOF'
import json, sys
from pathlib import Path
work, since = Path(sys.argv[1]), sys.argv[2]
bad = set()
for line in (work / "proxy.jsonl").read_text().splitlines() if (work / "proxy.jsonl").is_file() else []:
    ev = json.loads(line)
    if ev.get("event") == "connect" and ev.get("code") != "200":
        bad.add(ev["host"])
st = work / "agent-status.json"
if st.is_file():
    for f in json.loads(st.read_text()).get("recentRelayFailures", []):
        if f.get("ts", "") >= since:
            bad.add((f.get("host") or "").rsplit(":", 1)[0])
bad.discard("collector.platformio.org")  # telemetry only
print(" ".join(sorted(bad)))
EOF
)

if [[ $SETUP_RC -ne 0 || -n "$NEEDED_REFUSED" ]]; then
  RC=1
  RESULT="FAILED: setup exit $SETUP_RC; refused hosts needed: ${NEEDED_REFUSED:-none}"
elif [[ ${BUILD_RC[picco]:-1} -ne 0 || ${BUILD_RC[simulator_picco]:-1} -ne 0 ]]; then
  RC=3
  RESULT="network clean; build failed (picco exit ${BUILD_RC[picco]:-?}, simulator_picco exit ${BUILD_RC[simulator_picco]:-?})"
else
  RC=0
  RESULT="PASSED: network clean, both builds succeeded"
fi

python3 - "$WORK" <<EOF
import json, subprocess
from pathlib import Path
work = Path("$WORK")
def git(*a):
    return subprocess.run(["git", "-C", "$REPO_ROOT", *a], capture_output=True, text=True).stdout.strip()
pio = subprocess.run(["pio", "--version"], capture_output=True, text=True).stdout.strip()
meta = {
    "start_iso": "$START_ISO",
    "end_iso": subprocess.run(["date", "-u", "+%Y-%m-%dT%H:%M:%S"], capture_output=True, text=True).stdout.strip(),
    "commit": git("rev-parse", "HEAD"),
    "dirty": bool(git("status", "--porcelain", "--untracked-files=no")),
    "pio_version": pio,
    "network": "$NETWORK",
    "strace": ${#TRACE[@]} > 0,
    "result": "$RESULT",
}
(work / "meta.json").write_text(json.dumps(meta, indent=2))
EOF

python3 "$REPO_ROOT/scripts/net_smoke.py" report --work "$WORK" --out "$REPORT"
say "$RESULT"
say "report: $REPORT"
exit $RC
