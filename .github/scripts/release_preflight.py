"""Refuse a release version that is already used or not new (job steps of .github/workflows/release-kit.yml).

    GH_TOKEN=... GITHUB_REPOSITORY=owner/repo python3 .github/scripts/release_preflight.py 2.0.1

Reads every tag and every release of the repository with `gh api` and fails (exit 1) when a tag or a
release already carries this version (as `v2.0.1` or `2.0.1`), or when one carries a HIGHER version,
so release versions only ever go up. Versions are read like the Wi-Fi updater reads them
(lib/UpdatePolicy/ReleaseNaming.h: optional `v`, MAJOR.MINOR.PATCH at the start). Draft releases are
listed only to a token that can write, so the check is complete only in the release job; the
preflight job runs it with a read-only token to stop early. Changes nothing.
"""

import json
import os
import re
import subprocess
import sys

VERSION_RE = re.compile(r"^v?([0-9]{1,9})\.([0-9]{1,9})\.([0-9]{1,9})(?![0-9.])")


def parse(text):
    m = VERSION_RE.match(text or "")
    return tuple(int(x) for x in m.groups()) if m else None


def api_list(path):
    out = subprocess.run(["gh", "api", "--paginate", "--slurp", path], capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit("gh api %s failed: %s" % (path, out.stderr.strip()))
    return [item for page in json.loads(out.stdout) for item in page]


def main(argv):
    if len(argv) != 2 or parse(argv[1]) is None:
        print(__doc__)
        return 2
    version = parse(argv[1])
    repo = os.environ["GITHUB_REPOSITORY"]
    seen = [("tag", t["name"]) for t in api_list("repos/%s/tags?per_page=100" % repo)]
    for r in api_list("repos/%s/releases?per_page=100" % repo):
        state = "draft release" if r.get("draft") else "pre-release" if r.get("prerelease") else "release"
        seen.append((state, r.get("tag_name") or ""))
    print("%d tags and releases in %s" % (len(seen), repo))
    problems = []
    for kind, name in seen:
        other = parse(name)
        if other is None:
            continue
        if other == version:
            problems.append("%s %s already uses version %s" % (kind, name, argv[1]))
        elif other > version:
            problems.append("%s %s has a higher version than %s" % (kind, name, argv[1]))
    for p in problems:
        print("::error::%s" % p)
    if problems:
        print("Choose a version above every existing tag and release, or delete an abandoned draft first "
              "(docs/RELEASING.md). Nothing was changed.")
        return 1
    print("version %s is new and above every existing tag and release" % argv[1])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
