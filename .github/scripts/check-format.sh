#!/usr/bin/env bash
# Formatting and Python syntax check for this project (job "format" of
# .github/workflows/project-ci.yml). Run from anywhere in the repo:
#
#   .github/scripts/check-format.sh
#
# 1. clang-format 21 (the version upstream's .clang-format and bin/clang-format-fix
#    require) on the C/C++ files this project added or changed relative to the
#    pinned upstream commit (docs/BASE.md). Upstream files we did not touch are
#    not checked. Same invocation and exclusions as upstream's bin/clang-format-fix
#    (`clang-format -style=file`, generated fonts, hyphenation tables, uzlib and
#    miniz third-party code excluded). The check never edits the checkout: it
#    prints the diff clang-format would apply and fails if there is one.
#    Fix locally with:  <clang-format-21> -style=file -i <file>
# 2. Python: every *.py under tools/ and scripts/ must compile (syntax check;
#    byte code goes to a temporary directory, not into the checkout).
#
# Environment:
#   CLANG_FORMAT   the clang-format binary (default: clang-format-21, then clang-format)
#   PINNED_UPSTREAM  the commit to diff against (default: the pin in docs/BASE.md)
#
# Exit status: 0 clean, 1 a file needs formatting or does not compile, 2 setup problem.

set -euo pipefail

PINNED_UPSTREAM="${PINNED_UPSTREAM:-490813ff4541e74e654f54f66ece9efc908361a0}"
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

if [[ -n "${CLANG_FORMAT:-}" ]]; then
  CF="$CLANG_FORMAT"
elif command -v clang-format-21 >/dev/null 2>&1; then
  CF=clang-format-21
elif command -v clang-format >/dev/null 2>&1; then
  CF=clang-format
else
  echo "clang-format not found; install clang-format 21 (e.g. 'pip install clang-format==21.1.8') or set CLANG_FORMAT" >&2
  exit 2
fi
CF_VERSION="$("$CF" --version)"
CF_MAJOR="$(printf '%s\n' "$CF_VERSION" | grep -oE '[0-9]+' | head -n1)"
if [[ "$CF_MAJOR" != 21 ]]; then
  echo "need clang-format 21 (upstream's .clang-format targets it); $CF is: $CF_VERSION" >&2
  exit 2
fi
echo "clang-format: $CF_VERSION"

if ! git cat-file -e "${PINNED_UPSTREAM}^{commit}" 2>/dev/null; then
  echo "pinned upstream commit $PINNED_UPSTREAM is not in this clone; fetching it"
  git fetch --no-tags --depth=1 origin "$PINNED_UPSTREAM"
fi

# Added, modified, renamed or copied C/C++ files since the pin, minus upstream's exclusions.
mapfile -t FILES < <(
  git diff --name-only --diff-filter=AMRC "$PINNED_UPSTREAM" HEAD -- \
    | grep -E '\.(c|cpp|h|hpp)$' \
    | grep -v -E '^lib/EpdFont/builtinFonts/' \
    | grep -v -E '^lib/Epub/Epub/hyphenation/generated/' \
    | grep -v -E '^lib/uzlib/' \
    | grep -v -E '^lib/miniz/third_party/' \
    || true
)
echo "C/C++ files added or changed since ${PINNED_UPSTREAM:0:8}: ${#FILES[@]}"

status=0
bad=()
for f in "${FILES[@]}"; do
  [[ -f "$f" ]] || continue
  if ! "$CF" -style=file "$f" | diff -u --label "a/$f" --label "b/$f" "$f" -; then
    bad+=("$f")
  fi
done
if ((${#bad[@]})); then
  echo
  echo "::error::${#bad[@]} file(s) are not formatted with clang-format 21 (diff above):"
  printf '  %s\n' "${bad[@]}"
  echo "Fix with: $CF -style=file -i <file>   (clang-format 21; 'pip install clang-format==21.1.8' provides it)"
  status=1
else
  echo "clang-format: all ${#FILES[@]} files clean"
fi

echo
PYTHON="${PYTHON:-python3}"
PYC="$(mktemp -d)"
trap 'rm -rf "$PYC"' EXIT
echo "Python syntax check ($("$PYTHON" --version 2>&1)): tools/ scripts/"
if PYTHONPYCACHEPREFIX="$PYC" "$PYTHON" -m compileall -q tools scripts; then
  echo "python: all files compile"
else
  echo "::error::a Python file under tools/ or scripts/ does not compile (see above)"
  status=1
fi
exit "$status"
