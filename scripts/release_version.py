# Pre script of [env:picco_2slot] (platformio.project.ini; task A0, docs/RELEASING.md).
#
# The project's release version has one source: the environment variable EINK_RELEASE_VERSION,
# which only the release workflow (.github/workflows/release-kit.yml) sets, from its `version` input.
#
#   unset or empty  development build: CROSSPOINT_VERSION stays as platformio.project.ini sets it,
#                   "<[crosspoint] version>-dev-picco2" (for example 2.0.0-dev-picco2).
#   MAJOR.MINOR.PATCH (e.g. 2.0.1)
#                   release build: CROSSPOINT_VERSION becomes "2.0.1-picco2", whose numbers the Wi-Fi
#                   updater compares with the release tag v2.0.1 (lib/UpdatePolicy/ReleaseNaming.h).
#   anything else   the build stops.
#
# It replaces the define in BUILD_FLAGS before PlatformIO turns BUILD_FLAGS into CPPDEFINES, so
# `pio run -e picco_2slot -t envdump` shows the same string the firmware is compiled with.
#
# Run directly (`python3 scripts/release_version.py [VERSION]`) it prints the string a build
# would use, without building.

import os
import re
import sys

ENV_VAR = "EINK_RELEASE_VERSION"
SUFFIX = "-picco2"
DEFINE = "-DCROSSPOINT_VERSION="
# Each part 1 to 9 digits (the updater's limit), no leading zeros.
VERSION_RE = re.compile(r"^(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$")


def release_string(version):
    """The CROSSPOINT_VERSION of a release build, or raises ValueError for a bad version."""
    if not VERSION_RE.match(version):
        raise ValueError(
            "%s=%r is not MAJOR.MINOR.PATCH (digits only, no leading zeros, e.g. 2.0.1)" % (ENV_VAR, version))
    return version + SUFFIX


def replace_define(flags, value):
    """flags with the one CROSSPOINT_VERSION define set to value; raises ValueError unless there is one."""
    hits = [i for i, f in enumerate(flags) if f.replace(" ", "").startswith(DEFINE)]
    if len(hits) != 1:
        raise ValueError("expected one %s flag in build_flags, found %d" % (DEFINE, len(hits)))
    out = list(flags)
    out[hits[0]] = '%s\\"%s\\"' % (DEFINE, value)
    return out


def _apply(env):
    version = os.environ.get(ENV_VAR, "").strip()
    if not version:
        print("eink-learner-aid version: development build (%s not set)" % ENV_VAR)
        return
    try:
        value = release_string(version)
        env.Replace(BUILD_FLAGS=replace_define(env.get("BUILD_FLAGS", []), value))
    except ValueError as e:
        sys.stderr.write("release_version.py: %s\n" % e)
        env.Exit(1)
        return
    print("eink-learner-aid version: release build, CROSSPOINT_VERSION=%s" % value)


try:
    Import("env")  # noqa: F821  (SCons)
except NameError:
    if __name__ == "__main__":
        arg = sys.argv[1] if len(sys.argv) > 1 else os.environ.get(ENV_VAR, "")
        if not arg:
            print("development build: CROSSPOINT_VERSION as set in platformio.project.ini")
            sys.exit(0)
        try:
            print(release_string(arg))
        except ValueError as err:
            print(err, file=sys.stderr)
            sys.exit(1)
else:
    _apply(env)  # noqa: F821
