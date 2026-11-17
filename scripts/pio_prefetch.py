"""Pre-fetch every package a PlatformIO environment needs, with verified TLS.

Run it with the Python interpreter that runs PlatformIO (scripts/cloud-setup.sh
finds it), from the project root:

    python scripts/pio_prefetch.py picco simulator_picco

Why this exists
---------------
The pioarduino platform (platform-espressif32 55.03.311) sets up its private
virtualenv while it configures packages, and its penv_setup._setup_certifi_env()
then overwrites SSL_CERT_FILE, REQUESTS_CA_BUNDLE, CURL_CA_BUNDLE and
GIT_SSL_CAINFO in the running process with the certifi bundle of that
virtualenv. Behind a TLS-intercepting proxy (Claude Code cloud sessions) that
bundle lacks the proxy's CA, so every later download in the same process fails
verification. Its idf_tools.py also loads certifi explicitly and, when every
verified attempt fails, retries without verification.

This script never lets a process that has run the platform's package
configuration download anything:

1. "plan" runs in a child process. It asks the platform which packages the
   environment needs (configure_project_packages, exactly what `pio run` does)
   and writes the list to a file. Tools that the platform installs through
   idf_tools.py are recorded, not downloaded. The only downloads in the child
   are the platform's own virtualenv setup from PyPI, which the platform runs
   before it overwrites the CA variables.
2. This process, whose CA variables are the session's own (checked after
   every step), installs the missing packages with PlatformIO's package
   manager, runs idf_tools.py for each recorded tool (refusing a download that
   needed idf_tools.py's unverified fallback), and lets the platform finish its
   own bookkeeping (install_tool). It also installs freertos-gdb, which the
   platform would fetch from PyPI after the overwrite.
3. The project libraries and PlatformIO's SCons package are installed here too.
4. "plan" runs again as a check: if the platform would still download a
   package, the script fails and names it.

Nothing here changes a CA bundle, a trust store or a platform file.
Exit status: 0 when everything is in place, 1 on any failure.
"""

import argparse
import json
import logging
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from urllib.parse import urlparse

CA_VARS = ("SSL_CERT_FILE", "REQUESTS_CA_BUNDLE", "CURL_CA_BUNDLE", "GIT_SSL_CAINFO")
SESSION_CA = {k: os.environ.get(k) for k in CA_VARS}


def log(msg):
    print(f"[prefetch] {msg}", flush=True)


class Failure(Exception):
    """A download or install step failed; message names the package and host."""


def host_of(url):
    if not url:
        return "?"
    if "://" not in url:
        return "registry.platformio.org (PlatformIO registry)"
    return urlparse(url).hostname or url


# --------------------------------------------------------------------------
# Child process: ask the platform what it needs.
# --------------------------------------------------------------------------


def run_plan(env_name, out_path, targets):
    errors = []

    class Recorder(logging.Handler):
        def emit(self, record):
            if record.levelno >= logging.ERROR:
                errors.append(record.getMessage())

    logging.getLogger().addHandler(Recorder())

    from platformio.platform.factory import PlatformFactory
    from platformio.project.config import ProjectConfig

    spec = ProjectConfig.get_instance().get(f"env:{env_name}", "platform")
    platform = PlatformFactory.new(spec)

    # pioarduino installs some tools in two steps: PlatformIO fetches a small
    # package holding a tools.json, then install_tool() runs idf_tools.py to
    # download the tool itself. In this process the CA variables may already
    # be overwritten, so record such tools instead of letting idf_tools.py
    # download them here; the parent process installs them.
    tools = []
    if hasattr(platform, "install_tool"):
        original_install_tool = platform.install_tool

        def recording_install_tool(name):
            tools.append(name)
            path = platform.get_package_dir(name)
            if path and Path(path, "tools.json").is_file():
                platform.packages[name]["optional"] = False
                return True
            return original_install_tool(name)

        platform.install_tool = recording_install_tool

    platform.configure_project_packages(env_name, targets)

    packages = []
    for name, opts in platform.packages.items():
        if opts.get("optional"):
            continue
        pkg = platform.get_package(name)
        packages.append(
            {
                "name": name,
                "version": opts.get("version"),
                "installed": bool(pkg),
                "path": pkg.path if pkg else None,
                "tools_json": bool(pkg and Path(pkg.path, "tools.json").is_file()),
            }
        )
    changed = {k: os.environ.get(k) for k in CA_VARS if os.environ.get(k) != SESSION_CA[k]}
    Path(out_path).write_text(
        json.dumps(
            {
                "platform": platform.name,
                "platform_version": platform.version,
                "packages": packages,
                "tools": tools,
                "ca_vars_overwritten": changed,
                "errors": errors,
            },
            indent=2,
        )
    )
    return 0


def plan(env_name, targets):
    """Run the platform's package configuration in a child process."""
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp, "plan.json")
        cmd = [sys.executable, __file__, "--plan", env_name, "--plan-out", str(out)]
        for t in targets:
            cmd += ["--target", t]
        proc = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if proc.stdout.strip():
            for line in proc.stdout.strip().splitlines():
                log(f"  (platform) {line}")
        if proc.returncode != 0 or not out.is_file():
            raise Failure(f"package configuration for env:{env_name} failed (exit {proc.returncode})")
        result = json.loads(out.read_text())
    if result["errors"]:
        raise Failure(
            f"platform reported errors while configuring env:{env_name}: " + "; ".join(result["errors"])
        )
    return result


# --------------------------------------------------------------------------
# This process: download with the session's CA settings.
# --------------------------------------------------------------------------


def assert_clean_ca_env():
    for k in CA_VARS:
        if os.environ.get(k) != SESSION_CA[k]:
            raise Failure(
                f"{k} changed in the downloading process ({SESSION_CA[k]!r} -> {os.environ.get(k)!r}); "
                "refusing to download with a CA setting this script did not start with"
            )


def load_platform(env_name):
    from platformio.package.manager.platform import PlatformPackageManager
    from platformio.platform.factory import PlatformFactory
    from platformio.project.config import ProjectConfig

    spec = ProjectConfig.get_instance().get(f"env:{env_name}", "platform")
    pm = PlatformPackageManager()
    if not pm.get_package(spec):
        log(f"installing platform {spec}")
    try:
        # skip_dependencies and no project_env: installs the platform package
        # only, without running its package configuration.
        pkg = pm.install(spec, skip_dependencies=True)
    except Exception as exc:  # noqa: BLE001 - report any failure with its host
        raise Failure(f"platform {spec} from {host_of(spec)}: {exc}") from exc
    assert_clean_ca_env()
    return PlatformFactory.new(pkg)


def penv_python(core_dir):
    exe = Path(core_dir, "penv", "bin", "python")
    return str(exe) if exe.is_file() else None


def install_package(platform, name, version):
    from platformio.package.meta import PackageSpec

    spec = PackageSpec(owner=platform.packages[name].get("owner"), name=name, requirements=version)
    url = spec.uri or version
    log(f"installing {name} from {host_of(url)}")
    try:
        platform.pm.install(spec)
    except Exception as exc:  # noqa: BLE001
        raise Failure(f"package {name} from {url} (host {host_of(url)}): {exc}") from exc
    assert_clean_ca_env()


def run_idf_tools(platform, name, core_dir):
    """Download a tools.json package's tool with idf_tools.py, verified TLS only."""
    tool_dir = Path(platform.get_package_dir(name))
    idf_tools = Path(platform.get_package_dir("tool-esp_install") or "", "tools", "idf_tools.py")
    if not idf_tools.is_file():
        raise Failure(f"{name} needs idf_tools.py, but tool-esp_install is not installed")
    python = penv_python(core_dir) or sys.executable
    env = dict(os.environ)
    env["IDF_TOOLS_PATH"] = core_dir
    cmd = [python, str(idf_tools), "--non-interactive", "--tools-json", str(tool_dir / "tools.json"), "install"]
    log(f"idf_tools.py install for {name}")
    proc = subprocess.run(cmd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = proc.stdout or ""
    urls = re.findall(r"Downloading (\S+)", out)
    used = re.findall(r"Successfully downloaded using SSL configuration: (\S+)", out)
    for url in urls:
        log(f"  {name}: downloaded {url}")
    if "unverified" in used:
        raise Failure(
            f"idf_tools.py downloaded {name} ({', '.join(urls)}) only after turning TLS verification off; "
            "the files are not trusted. Check the CA settings of this shell."
        )
    if proc.returncode != 0:
        tail = "\n".join(out.strip().splitlines()[-15:])
        hosts = ", ".join(sorted({host_of(u) for u in urls})) or "unknown host"
        raise Failure(f"idf_tools.py install for {name} failed (host {hosts}):\n{tail}")
    for cfg in sorted(set(used)):
        log(f"  {name}: TLS verified with idf_tools configuration '{cfg}'")


def finish_tools(platform, core_dir, tools):
    """Run idf_tools.py, then the platform's own install_tool, for each tool
    the platform installs through idf_tools.py that still carries a tools.json."""
    for name in tools:
        path = platform.get_package_dir(name)
        if not path or not Path(path, "tools.json").is_file():
            continue
        run_idf_tools(platform, name, core_dir)
        py = penv_python(core_dir)
        if py:
            platform._penv_python = py  # what configure_default_packages would set
        # The tool is installed now, so this only copies metadata and
        # re-registers the package from IDF_TOOLS_PATH; it downloads nothing.
        if not platform.install_tool(name):
            raise Failure(f"platform install_tool({name}) failed after idf_tools.py succeeded")
        assert_clean_ca_env()
        if Path(platform.get_package_dir(name) or "", "tools.json").is_file():
            raise Failure(f"{name} still has a tools.json after install_tool")


def install_gdb_python_helpers(platform, core_dir):
    """pioarduino installs the freertos-gdb PyPI package into each GDB tool
    after it has overwritten the CA variables, so in `pio run` that step fails
    behind an intercepting proxy (a warning, retried on every run). Run the
    platform's own function here, with the session's CA settings."""
    penv_setup = sys.modules.get("penv_setup")
    py = penv_python(core_dir)
    if not hasattr(platform, "install_tool") or not hasattr(penv_setup, "install_freertos_gdb") or not py:
        return
    gdb_packages = [g for g in getattr(penv_setup, "GDB_TOOL_PACKAGES", {}).values() if g in platform.packages]
    if not any(platform.get_package_dir(g) for g in gdb_packages):
        return
    uv = Path(core_dir, "penv", "bin", "uv")
    log("freertos-gdb in the GDB packages (PyPI if missing)")
    penv_setup.install_freertos_gdb(platform, str(uv), py, str(Path(core_dir, ".cache", "uv")))
    assert_clean_ca_env()
    for gdb in gdb_packages:
        path = platform.get_package_dir(gdb)
        if path and not Path(path, "share", "gdb", "python", "freertos_gdb").is_dir():
            raise Failure(f"freertos-gdb (from pypi.org / files.pythonhosted.org) is missing in {gdb}")


def install_libraries(env_name):
    from platformio.package.commands.install import _install_project_env_libraries
    from platformio.project.config import ProjectConfig

    specs = [s for s in ProjectConfig.get_instance().get(f"env:{env_name}", "lib_deps") if "symlink://" not in s]
    log(f"lib_deps for env:{env_name} ({len(specs)} from the network, the rest are symlinks)")
    try:
        # The function `pio run` and `pio pkg install -e` call for lib_deps.
        _install_project_env_libraries(env_name, {})
    except Exception as exc:  # noqa: BLE001
        hosts = sorted({host_of(s.split("=", 1)[-1].split("#")[0]) if "://" in s else host_of(None) for s in specs})
        raise Failure(f"library install for env:{env_name} failed (hosts tried: {', '.join(hosts)}): {exc}") from exc
    assert_clean_ca_env()


def install_core_scons():
    from platformio.package.manager.core import get_core_package_dir

    try:
        get_core_package_dir("tool-scons")
    except Exception as exc:  # noqa: BLE001
        raise Failure(f"PlatformIO core package tool-scons from registry.platformio.org: {exc}") from exc
    assert_clean_ca_env()


def prefetch_env(env_name, core_dir, targets):
    started = time.monotonic()
    log(f"== env:{env_name}")
    platform = load_platform(env_name)
    log(f"platform {platform.name} {platform.version}")

    # Packages the platform marks as always required (tool-esp_install for
    # pioarduino). Installing them here keeps the plan step from downloading.
    for name, opts in platform.packages.items():
        if not opts.get("optional") and not platform.get_package(name):
            install_package(platform, name, opts.get("version"))

    first = plan(env_name, targets)
    if first["ca_vars_overwritten"]:
        log(
            "platform overwrites "
            + ", ".join(sorted(first["ca_vars_overwritten"]))
            + " while configuring; downloads are done outside that process"
        )
    platform = load_platform(env_name)  # fresh instance, nothing configured
    for item in first["packages"]:
        if not item["installed"]:
            install_package(platform, item["name"], item["version"])
    finish_tools(platform, core_dir, first["tools"])
    for name in first["tools"]:
        # Every tool is installed now: this only checks versions and points
        # the platform at the installed directories, as `pio run` does.
        if not platform.install_tool(name):
            raise Failure(f"platform install_tool({name}) failed")
        assert_clean_ca_env()
    install_gdb_python_helpers(platform, core_dir)

    install_core_scons()
    install_libraries(env_name)

    check = plan(env_name, targets)
    missing = [
        p["name"]
        for p in check["packages"]
        if not p["installed"] or (p["tools_json"] and p["name"] in check["tools"])
    ]
    if missing:
        raise Failure(f"env:{env_name}: the platform would still download: {', '.join(missing)}")
    names = ", ".join(sorted(p["name"] for p in check["packages"]))
    log(f"env:{env_name} complete in {time.monotonic() - started:.0f}s; platform packages: {names or 'none'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("envs", nargs="*", help="PlatformIO environments to prepare")
    parser.add_argument("--project-dir", default=".")
    parser.add_argument("--target", action="append", default=[], help="build targets to plan for")
    parser.add_argument("--plan", help=argparse.SUPPRESS)
    parser.add_argument("--plan-out", help=argparse.SUPPRESS)
    args = parser.parse_args()

    os.chdir(args.project_dir)
    if args.plan:
        return run_plan(args.plan, args.plan_out, args.target)

    from platformio.project.config import ProjectConfig

    core_dir = ProjectConfig.get_instance().get("platformio", "core_dir")
    try:
        for env_name in args.envs:
            prefetch_env(env_name, core_dir, args.target)
    except Failure as exc:
        print(f"[prefetch] FAILED: {exc}", file=sys.stderr, flush=True)
        return 1
    except Exception as exc:  # noqa: BLE001 - say which step broke, then the traceback
        import traceback

        traceback.print_exc()
        print(f"[prefetch] FAILED: unexpected {type(exc).__name__}: {exc}", file=sys.stderr, flush=True)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
