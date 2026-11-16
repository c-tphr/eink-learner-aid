"""
PlatformIO pre: script: apply this project's patches to the simulator library.

crosspoint-simulator replaces the firmware's lib/hal with its own shims, so
every HAL method the firmware gains needs a matching stub in the simulator
(see the simulator's FORKING.md). The patches in `scripts/simulator_patches/`
carry those stubs; their headers say which firmware change each hunk answers.

Like scripts/patch_sdfat.py, this patches only the copy the current
environment builds:
  * the target is `<libdeps_dir>/<env>/simulator`, a git checkout fetched
    by PlatformIO from `lib_deps`;
  * its HEAD must be the commit pinned in platformio.project.ini
    ([simulator_pin] commit); any other commit fails the build, because the
    patches were reviewed against that commit only.

"Already applied" is decided by content, not by marker files: for every
prefix of the patch list (none, the first, the first two, ...) the script
builds the expected files in a temporary directory, starting from the pinned
commit's bytes (`git show HEAD:<path>`) and applying the patches with
`git apply`. If the files in the library equal one of those states, the
patches after that prefix are pending; the full set must apply on top of
the pristine files before anything is written. Files that already hold the
final bytes are not rewritten, so an incremental build recompiles nothing.
Content that matches no state (a hand edit, a patch that no longer applies)
fails the build; nothing is skipped silently.
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile


PATCH_DIR_NAME = "simulator_patches"
LIBRARY_NAME = "simulator"


class PatchError(RuntimeError):
    pass


def git_env(ceiling):
    # Keep git inside the library checkout (or the temp dir): never let it
    # discover the enclosing project repository or read user configuration.
    env = {key: os.environ[key] for key in ("HOME", "PATH", "TMPDIR", "LANG") if key in os.environ}
    env.update(GIT_CEILING_DIRECTORIES=str(ceiling), GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
    return env


def git(args, cwd, ceiling, binary=False):
    result = subprocess.run(["git"] + args, cwd=cwd, env=git_env(ceiling), capture_output=True,
                            text=not binary)
    return result


def patch_files(project_dir):
    patch_dir = Path(project_dir) / "scripts" / PATCH_DIR_NAME
    patches = sorted(patch_dir.glob("*.patch"))
    if not patches:
        raise PatchError("no simulator patches found in " + str(patch_dir))
    return patches


def touched_paths(patches, library):
    paths = []
    for patch in patches:
        result = git(["apply", "--numstat", str(patch)], library, library.parent)
        if result.returncode:
            raise PatchError("unreadable simulator patch " + patch.name + ":\n" + result.stderr)
        for line in result.stdout.splitlines():
            path = line.split("\t", 2)[2]
            if path not in paths:
                paths.append(path)
    return paths


def pristine(library, path):
    """Bytes of `path` at the library's HEAD, or None if HEAD lacks it."""
    result = git(["show", "HEAD:" + path], library, library.parent, binary=True)
    return result.stdout if result.returncode == 0 else None


def read(base, path):
    file = base / path
    return file.read_bytes() if file.is_file() else None


def expected_states(library, patches, paths):
    """[(applied count, {path: bytes or None})] for 0..len(patches)."""
    states = []
    with tempfile.TemporaryDirectory(prefix="simulator-patch-") as tmp:
        work = Path(tmp)
        for path in paths:
            data = pristine(library, path)
            if data is not None:
                (work / path).parent.mkdir(parents=True, exist_ok=True)
                (work / path).write_bytes(data)
        states.append((0, {p: read(work, p) for p in paths}))
        for count, patch in enumerate(patches, start=1):
            result = git(["apply", str(patch)], work, work)
            if result.returncode:
                raise PatchError(
                    "simulator patch " + patch.name + " does not apply to the pinned commit "
                    "(after the patches before it):\n" + result.stderr)
            states.append((count, {p: read(work, p) for p in paths}))
    return states


def apply_patches(project_dir, library, pinned):
    library = Path(library).resolve()
    if not (library / ".git").exists():
        raise PatchError(
            "the simulator library at " + str(library) + " is not a git checkout; "
            "it must be fetched from the git URL in platformio.project.ini")
    top = git(["rev-parse", "--show-toplevel"], library, library.parent)
    if top.returncode or Path(top.stdout.strip()).resolve() != library:
        raise PatchError("could not read the simulator library's own git checkout at " + str(library))
    head = git(["rev-parse", "HEAD"], library, library.parent).stdout.strip()
    if head != pinned:
        raise PatchError(
            "the simulator library is at " + (head or "an unknown commit") + ", but the patches in "
            "scripts/simulator_patches/ are for the pinned commit " + pinned + ". Delete "
            + str(library) + " so PlatformIO fetches the pinned commit again, or review the "
            "patches against the new commit and update [simulator_pin] in platformio.project.ini")

    patches = patch_files(project_dir)
    paths = touched_paths(patches, library)
    states = expected_states(library, patches, paths)
    current = {p: read(library, p) for p in paths}
    matched = [count for count, files in states if files == current]
    if not matched:
        raise PatchError(
            "the simulator library's files match neither the pinned commit nor the pinned commit "
            "plus a prefix of scripts/simulator_patches/ (edited by hand?). Delete " + str(library)
            + " so PlatformIO fetches it again")

    # Content outside the patched files must be the pinned commit's.
    others = git(["diff", "--name-only", "HEAD", "--"], library, library.parent).stdout.split()
    unexpected = sorted(set(others) - set(paths))
    if unexpected:
        raise PatchError("unreviewed changes in the simulator library: " + ", ".join(unexpected))

    applied = max(matched)
    final = states[-1][1]
    if applied == len(patches):
        return []
    for path in paths:
        if current[path] == final[path]:
            continue
        target = library / path
        if final[path] is None:
            target.unlink()
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            tmp = target.with_name(target.name + ".patching")
            tmp.write_bytes(final[path])
            if target.exists():
                shutil.copymode(target, tmp)
            os.replace(tmp, target)
    names = [patch.name for patch in patches[applied:]]
    for name in names:
        print("Applied simulator patch: " + name)
    return names


def patch_selected_environment(env):
    from SCons.Script import COMMAND_LINE_TARGETS  # noqa: PLC0415 -- only inside SCons

    if COMMAND_LINE_TARGETS and set(COMMAND_LINE_TARGETS) <= {"clean", "cleanall", "fullclean"}:
        return
    pinned = env.GetProjectConfig().get("simulator_pin", "commit").strip()
    library = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env["PIOENV"] / LIBRARY_NAME
    if not library.is_dir():
        raise PatchError(
            "the simulator library is missing at " + str(library)
            + "; PlatformIO installs it from lib_deps before the build")
    apply_patches(env["PROJECT_DIR"], library, pinned)


if "Import" in globals():
    Import("env")  # noqa: F821 -- supplied by SCons
    try:
        patch_selected_environment(env)  # noqa: F821
    except PatchError as error:
        import sys

        sys.stderr.write("\nERROR [patch_simulator.py]: " + str(error) + "\n\n")
        env.Exit(1)  # noqa: F821
