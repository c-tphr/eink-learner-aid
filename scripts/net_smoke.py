"""Helpers for scripts/net-smoke.sh (Python standard library only).

  net_smoke.py proxy  --log FILE --port-file FILE
      A forward proxy for HTTPS (CONNECT only) on 127.0.0.1. It logs every
      CONNECT host and the answer it got, then relays the encrypted bytes
      unchanged: it never sees or touches TLS. When HTTPS_PROXY is set it
      chains to that proxy (the session's agent proxy), otherwise it connects
      directly.

  net_smoke.py report --work DIR --out FILE
      Writes docs/net-smoke-report.md from the files net-smoke.sh collected.
"""

import argparse
import json
import os
import re
import select
import socket
import sys
import threading
import time
from pathlib import Path
from urllib.parse import urlparse

# --------------------------------------------------------------------------
# Logging CONNECT proxy
# --------------------------------------------------------------------------

LOG_LOCK = threading.Lock()


def write_log(path, record):
    record["t"] = round(time.time(), 3)
    with LOG_LOCK, open(path, "a", encoding="utf-8") as f:
        f.write(json.dumps(record) + "\n")


def read_head(sock):
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            break
        data += chunk
        if len(data) > 65536:
            break
    head, _, rest = data.partition(b"\r\n\r\n")
    return head.decode("latin-1"), rest


def relay(a, b):
    sent = {a: 0, b: 0}
    socks = [a, b]
    try:
        while True:
            readable, _, _ = select.select(socks, [], [], 300)
            if not readable:
                break
            for s in readable:
                data = s.recv(65536)
                if not data:
                    return sent
                other = b if s is a else a
                other.sendall(data)
                sent[s] += len(data)
    except OSError:
        pass
    return sent


def handle(client, log_path, upstream):
    started = time.time()
    try:
        head, rest = read_head(client)
        first = head.split("\r\n", 1)[0]
        parts = first.split()
        if not parts:
            return  # a TCP reachability probe (pioarduino checks the proxy port this way)
        if len(parts) < 2 or parts[0].upper() != "CONNECT":
            write_log(log_path, {"event": "non_connect", "request": first})
            client.sendall(b"HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n")
            return
        target = parts[1]
        host, _, port = target.rpartition(":")
        if upstream:
            up = socket.create_connection((upstream.hostname, upstream.port or 80), timeout=90)
            up.sendall(f"CONNECT {target} HTTP/1.1\r\nHost: {target}\r\n\r\n".encode())
            resp, extra = read_head(up)
            status_line = resp.split("\r\n", 1)[0]
            code = status_line.split()[1] if len(status_line.split()) > 1 else "?"
            write_log(log_path, {"event": "connect", "host": host, "port": port, "code": code, "status": status_line})
            client.sendall(resp.encode("latin-1") + b"\r\n\r\n" + extra)
            if code != "200":
                return
        else:
            try:
                up = socket.create_connection((host, int(port)), timeout=90)
            except OSError as exc:
                write_log(log_path, {"event": "connect", "host": host, "port": port, "code": "ERR", "status": str(exc)})
                client.sendall(b"HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n")
                return
            write_log(log_path, {"event": "connect", "host": host, "port": port, "code": "200", "status": "direct"})
            client.sendall(b"HTTP/1.1 200 Connection Established\r\n\r\n")
        if rest:
            up.sendall(rest)
        up.settimeout(None)
        client.settimeout(None)
        sent = relay(client, up)
        write_log(
            log_path,
            {
                "event": "close",
                "host": host,
                "port": port,
                "bytes_out": sent.get(client, 0),
                "bytes_in": sent.get(up, 0),
                "seconds": round(time.time() - started, 1),
            },
        )
    except Exception as exc:  # noqa: BLE001 - log and drop this one connection
        write_log(log_path, {"event": "error", "error": f"{type(exc).__name__}: {exc}"})
    finally:
        client.close()


def run_proxy(log_path, port_file):
    up = os.environ.get("NET_SMOKE_UPSTREAM", "")
    upstream = urlparse(up if "://" in up else f"http://{up}") if up else None
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", 0))
    server.listen(64)
    Path(port_file).write_text(str(server.getsockname()[1]))
    while True:
        client, _ = server.accept()
        threading.Thread(target=handle, args=(client, log_path, upstream), daemon=True).start()


# --------------------------------------------------------------------------
# Report
# --------------------------------------------------------------------------

TLS_SECTION = """\
## TLS finding and how the setup handles it

**Symptom.** In a Claude Code cloud session, `pio run -e picco` from empty
caches fails with `CERTIFICATE_VERIFY_FAILED ... self-signed certificate in
certificate chain` while downloading
`https://github.com/espressif/arduino-esp32/releases/download/3.3.11/esp32-core-3.3.11.tar.xz`.
The host is allowed (`curl` gets 200).

**Cause.** The session's proxy re-terminates TLS; its CA is in
`/root/.ccr/ca-bundle.crt`, which the session exports as `SSL_CERT_FILE`,
`REQUESTS_CA_BUNDLE`, `CURL_CA_BUNDLE` and `GIT_SSL_CAINFO`. While PlatformIO
loads the pioarduino platform (55.03.311), `platform.py`
`configure_default_packages()` calls `setup_penv_minimal()`, which ends in
`builder/penv_setup.py` `_setup_certifi_env()`. That function overwrites those
four variables (and sets `CERTIFI_PATH`) in the running process with the
certifi bundle of the platform's private virtualenv (`<core_dir>/penv`), which
does not contain the proxy's CA. From then on, every download in that process
fails verification: PlatformIO's package manager, the registry API, git clones
of `lib_deps`, and subprocesses that inherit the variables.

Two related findings in the same code paths:

- `tool-esp_install/tools/idf_tools.py`, which the platform runs to download
  toolchains, builds its TLS contexts from `certifi.where()` (plus pinned
  DigiCert roots for `github.com`), then the system default, and as a last
  resort an **unverified** context (`get_ssl_fallback_contexts()`,
  `verify_mode = ssl.CERT_NONE`). With the CA variables overwritten, the
  system default fails too, so in a plain `pio run` the toolchain would be
  fetched without certificate verification. With the session's own variables
  the `system_default` context verifies against the session bundle and
  succeeds.
- After the overwrite, the platform's `uv pip install freertos-gdb` (PyPI, into
  the GDB package) fails with `invalid peer certificate: UnknownIssuer`. In
  `pio run` this is only a warning, repeated on every run.

**Handling** (`scripts/pio_prefetch.py`, run by `scripts/cloud-setup.sh`).
No process that has run the platform's package configuration downloads
anything:

1. A child process asks the platform which packages the environment needs
   (`configure_project_packages`, the same call `pio run` makes) and writes the
   list to a file. Tools that pioarduino installs through `idf_tools.py` are
   recorded there, not downloaded.
2. The parent process, whose CA variables are the session's own (it checks
   this before and after every step), installs those packages with
   PlatformIO's package manager, runs `idf_tools.py install` for each tool and
   fails if a download needed the unverified context, then calls the
   platform's own `install_tool()` so that its bookkeeping matches a normal
   install. It also installs `freertos-gdb`, PlatformIO's `tool-scons` and
   the project libraries (PlatformIO's own `lib_deps` installer).
3. The child step runs again as a check: if the platform would still download
   a package, the setup fails and names it.

After that, `pio run -e picco` finds everything installed and downloads
nothing (the build rows in the host table above show it). Nothing changes a CA
bundle or trust store, no platform file is patched, TLS verification is never
turned off and `HTTPS_PROXY` is left alone. Where there is no intercepting
proxy (GitHub Actions, Codespaces) the same steps run and simply succeed.

**Limits.** Anything that makes `pio run -e picco` download again runs into
the same failure: a new platform or framework version, `pio pkg update`, or
building an environment with `custom_sdkconfig` (upstream's `default`,
`sticky`, ...) in the same PlatformIO core directory, after which the
pioarduino Arduino builder reinstalls the framework for `picco`
(`builder/frameworks/arduino.py`, `check_reinstall_frwrk()`). Re-run
`scripts/cloud-setup.sh` after such a change."""


PURPOSE = {
    "github.com": "Release downloads (pioarduino platform, Arduino core, toolchain packages via idf_tools.py) and git clones (submodules, git lib_deps)",
    "objects.githubusercontent.com": "Storage behind github.com release downloads (redirect target)",
    "release-assets.githubusercontent.com": "Storage behind github.com release downloads (redirect target)",
    "codeload.github.com": "GitHub source archives",
    "raw.githubusercontent.com": "Raw files from GitHub",
    "api.registry.platformio.org": "PlatformIO registry API: native platform, registry libraries, tool-scons",
    "registry.platformio.org": "PlatformIO registry",
    "dl.registry.platformio.org": "PlatformIO registry package downloads",
    "dl.platformio.org": "PlatformIO downloads",
    "collector.platformio.org": "PlatformIO usage telemetry (not needed to build)",
    "pypi.org": "Python package index: pioarduino's private virtualenv (uv), PlatformIO itself",
    "files.pythonhosted.org": "Python package files for the same",
    "dl.espressif.com": "Espressif tool mirror",
    "archive.ubuntu.com": "Ubuntu packages (apt, plain HTTP)",
    "download.docker.com": "Docker apt repository preconfigured in the session image (not used by this project)",
    "security.ubuntu.com": "Ubuntu security updates (apt, plain HTTP)",
}


def purpose(host):
    if host in PURPOSE:
        return PURPOSE[host]
    if host.endswith(".registry.platformio.org") or host.endswith(".platformio.org"):
        return "PlatformIO registry mirror (package downloads)"
    if host.endswith(".githubusercontent.com"):
        return "GitHub content storage"
    return "(not classified)"


def load_jsonl(path):
    out = []
    if Path(path).is_file():
        for line in Path(path).read_text().splitlines():
            try:
                out.append(json.loads(line))
            except ValueError:
                pass
    return out


def phase_at(phases, t):
    current = "before setup"
    for name, start in phases:
        if t >= start:
            current = name
    return current


IOV = re.compile(r'iov_base="((?:\\x[0-9a-f]{2})+)"')
CONNECT_RE = re.compile(r'connect\(\d+, \{sa_family=AF_INET6?, sin6?_port=htons\((\d+)\), [^"]*"([^"]+)"')


def parse_dns_names(trace_path):
    """Names this process tree looked up itself (DNS queries seen by strace in
    sendmmsg/sendto buffers). Tools that go through the HTTPS proxy let the
    proxy resolve names, so these are the hosts reached directly."""
    names = {}
    if not Path(trace_path).is_file():
        return names
    for line in Path(trace_path).read_text(errors="replace").splitlines():
        if "send" not in line:
            continue
        for m in IOV.finditer(line):
            data = bytes(int(h, 16) for h in m.group(1).split("\\x")[1:])
            if len(data) < 17:
                continue
            labels, i = [], 12
            while i < len(data) and data[i] and data[i] < 64:
                labels.append(data[i + 1 : i + 1 + data[i]].decode("ascii", "replace"))
                i += 1 + data[i]
            name = ".".join(labels).lower()
            if "." in name:
                names[name] = names.get(name, 0) + 1
    return names


def parse_direct_connects(trace_path):
    """Direct TCP connections to ports 80/443 (not through the local proxy)."""
    found = {}
    if not Path(trace_path).is_file():
        return found
    for line in Path(trace_path).read_text(errors="replace").splitlines():
        m = CONNECT_RE.search(line)
        if not m:
            continue
        port, addr = m.group(1), m.group(2)
        if port in ("443", "80") and not addr.startswith("127.") and addr != "::1":
            key = f"{addr}:{port}"
            found[key] = found.get(key, 0) + 1
    return found


def addresses_of(names):
    out = {}
    for name in names:
        try:
            for info in socket.getaddrinfo(name, None):
                out.setdefault(info[4][0], set()).add(name)
        except OSError:
            pass
    return out


def fmt_bytes(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024
    return str(n)


def build_report(work, out):
    work = Path(work)
    meta = json.loads((work / "meta.json").read_text())
    phases = [(p["name"], p["start"]) for p in load_jsonl(work / "phases.jsonl")]
    events = load_jsonl(work / "proxy.jsonl")
    status = {}
    if (work / "agent-status.json").is_file():
        try:
            status = json.loads((work / "agent-status.json").read_text())
        except ValueError:
            status = {}
    builds = json.loads((work / "builds.json").read_text()) if (work / "builds.json").is_file() else {}
    setup_times = (work / "setup-times.txt").read_text().strip() if (work / "setup-times.txt").is_file() else ""

    hosts = {}
    refused = []
    for ev in events:
        if ev.get("event") == "connect":
            h = hosts.setdefault(ev["host"], {"ok": 0, "bad": 0, "phases": set(), "bytes": 0, "ports": set()})
            h["phases"].add(phase_at(phases, ev["t"]))
            h["ports"].add(ev.get("port"))
            if ev.get("code") == "200":
                h["ok"] += 1
            else:
                h["bad"] += 1
                refused.append((ev["host"], phase_at(phases, ev["t"]), f"CONNECT answered `{ev.get('status')}`"))
        elif ev.get("event") == "close":
            h = hosts.get(ev["host"])
            if h:
                h["bytes"] += ev.get("bytes_in", 0)
    since = meta["start_iso"]
    for f in status.get("recentRelayFailures", []):
        if f.get("ts", "") >= since:
            host = (f.get("host") or "").rsplit(":", 1)[0]
            refused.append((host, "agent proxy log", f"{f.get('kind')}: {f.get('detail')}"))

    dns = parse_dns_names(work / "strace.txt")
    direct = parse_direct_connects(work / "strace.txt")
    ip_names = addresses_of(dns)
    dns_phases = {}
    for trace in sorted(work.glob("strace-*.txt")):
        label = trace.stem.replace("strace-", "").replace("-", " ")
        for name in parse_dns_names(trace):
            dns_phases.setdefault(name, set()).add(label)
    apt_hosts = []
    if (work / "apt-hosts.txt").is_file():
        for line in (work / "apt-hosts.txt").read_text().splitlines():
            parts = line.split()
            if parts:
                apt_hosts.append(parts if len(parts) == 2 else ["http", parts[0]])

    lines = []
    w = lines.append
    w("# Network smoke test report")
    w("")
    w("Generated by `scripts/net-smoke.sh`. Do not edit by hand; run the script again.")
    w("")
    w("| Item | Value |")
    w("| --- | --- |")
    w(f"| Date (UTC) | {meta['start_iso']}Z to {meta['end_iso']}Z |")
    w(f"| Commit | `{meta['commit']}`{' (with uncommitted changes)' if meta.get('dirty') else ''} |")
    w(f"| PlatformIO | {meta.get('pio_version', '?')} |")
    w(f"| Network | {meta['network']} |")
    w(f"| PlatformIO core dir | empty temporary directory (`PLATFORMIO_CORE_DIR`) |")
    w(f"| Build, libdeps and build-cache dirs | empty temporary directories |")
    w(f"| Result | {meta['result']} |")
    w("")
    w("## How hosts were recorded")
    w("")
    w(
        "- Every HTTPS connection that honours `HTTPS_PROXY` went through a local CONNECT-logging proxy "
        "(`scripts/net_smoke.py proxy`) chained to the session's own proxy. It records the host and the "
        "answer to CONNECT and relays the encrypted bytes unchanged; it never terminates TLS."
    )
    w(
        "- Hosts in `NO_PROXY` (PyPI) are contacted directly. They were recorded by tracing DNS lookups of the "
        "whole run with `strace` (sendto/sendmmsg), which lists every name a process resolved itself."
        if meta.get("strace")
        else "- Hosts in `NO_PROXY` (PyPI) are contacted directly and are not seen by the logging proxy; "
        "`strace` was not available, so they are listed from the tools that ran."
    )
    w(
        "- `apt` uses plain HTTP (port 80) outside the HTTPS proxy. Its hosts come from an `apt-get update` "
        "run at the start of the test."
    )
    w(f"- The agent proxy's own failure log (`$HTTPS_PROXY/__agentproxy/status`, `recentRelayFailures`) was read at the end.")
    w("")
    w("## Hosts contacted")
    w("")
    w("| Host | Path | Phase | Connections | Bytes received | What for |")
    w("| --- | --- | --- | --- | --- | --- |")
    for host in sorted(hosts):
        h = hosts[host]
        conns = f"{h['ok']} ok" + (f", {h['bad']} refused" if h["bad"] else "")
        w(
            f"| `{host}` | HTTPS proxy | {', '.join(sorted(h['phases']))} | {conns} | {fmt_bytes(h['bytes'])} | {purpose(host)} |"
        )
    for name in sorted(dns):
        if name in hosts:
            continue
        w(f"| `{name}` | direct (DNS lookup seen, `NO_PROXY`) | {', '.join(sorted(dns_phases.get(name, []))) or '?'} | {dns[name]} lookups | n/a | {purpose(name)} |")
    for scheme, host in apt_hosts:
        path = "direct, plain HTTP" if scheme == "http" else f"apt over {scheme} (not through `HTTPS_PROXY`)"
        w(f"| `{host}` | {path} | apt index | n/a | n/a | {purpose(host)} |")
    w("")
    if meta.get("strace"):
        w("Direct TCP connections (not through the HTTPS proxy) seen by `strace`:")
        w("")
        w("| Address | Connections | Name it resolves from | Note |")
        w("| --- | --- | --- | --- |")
        for key in sorted(direct):
            addr = key.rsplit(":", 1)[0]
            names = ", ".join(sorted(ip_names.get(addr, []))) or "(no traced lookup)"
            note = ""
            if key == "185.199.110.153:80":
                note = "PlatformIO's internet check (`GET /` to a fixed GitHub Pages address); harmless if it fails"
            w(f"| `{key}` | {direct[key]} | {names} | {note} |")
        if not direct:
            w("| none | | | |")
        w("")
    w("## Hosts refused")
    w("")
    if refused:
        w("| Host | Where | Exact error |")
        w("| --- | --- | --- |")
        seen = set()
        for host, where, err in refused:
            key = (host, err)
            if key in seen:
                continue
            seen.add(key)
            w(f"| `{host}` | {where} | {err} |")
        w("")
        w(
            "`collector.platformio.org` is PlatformIO's usage telemetry. A refusal there does not affect "
            "any download or build; `scripts/cloud-setup.sh` turns telemetry off for its own `pio` calls. "
            "It need not be allowlisted."
        )
    else:
        w("None.")
    w("")
    w(TLS_SECTION)
    w("")
    w("## Setup timings (this run, traced)")
    w("")
    w("```")
    w(setup_times or "(not recorded)")
    w("```")
    w("")
    w("## Builds (clean, after setup)")
    w("")
    w("| Environment | Result | Time | Size |")
    w("| --- | --- | --- | --- |")
    for env, b in builds.items():
        w(f"| `{env}` | {b.get('result')} | {b.get('seconds', '?')} s | {b.get('size', '')} |")
    w("")
    for env, b in builds.items():
        if b.get("notes"):
            w(f"`{env}`: {b['notes']}")
            w("")
            if b.get("errors"):
                w("```")
                w(b["errors"].rstrip())
                w("```")
                w("")
    Path(out).write_text("\n".join(lines).rstrip() + "\n")


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("proxy")
    p.add_argument("--log", required=True)
    p.add_argument("--port-file", required=True)
    r = sub.add_parser("report")
    r.add_argument("--work", required=True)
    r.add_argument("--out", required=True)
    args = parser.parse_args()
    if args.cmd == "proxy":
        run_proxy(args.log, args.port_file)
    else:
        build_report(args.work, args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
