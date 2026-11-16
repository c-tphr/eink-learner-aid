"""Run a pinned EPUBCheck release on an EPUB file.

    python3 -m tools.prep.epubcheck path/to/book.epub

The release zip is downloaded once from github.com (w3c/epubcheck releases),
checked against the SHA-256 recorded below, and unpacked into a cache
directory outside the repository: ``$EPUBCHECK_CACHE_DIR`` if set, else
``$XDG_CACHE_HOME/eink-learner-aid`` or ``~/.cache/eink-learner-aid``.
Java is taken from ``$JAVA_HOME/bin/java`` or ``PATH``.

``run_epubcheck`` returns a ``Report`` whose ``errors`` (FATAL and ERROR)
and ``warnings`` come from EPUBCheck's JSON output. ``EpubcheckUnavailable``
is raised when Java is missing or the release cannot be downloaded or
verified; callers (tests) should skip with its message, never pass silently.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

EPUBCHECK_VERSION = "5.3.0"
EPUBCHECK_URL = (
    f"https://github.com/w3c/epubcheck/releases/download/v{EPUBCHECK_VERSION}/epubcheck-{EPUBCHECK_VERSION}.zip"
)
# SHA-256 of epubcheck-5.3.0.zip as downloaded from the URL above on 2026-11-16.
EPUBCHECK_SHA256 = "6c07e68584b2e2ce2f89fe06e1246dfead3eb36b46b340e7d93524f29dcff6c5"

ERROR_SEVERITIES = ("FATAL", "ERROR")
WARNING_SEVERITIES = ("WARNING",)


class EpubcheckUnavailable(RuntimeError):
    """EPUBCheck cannot be run here (no Java, download refused, bad checksum)."""


@dataclass(frozen=True)
class Message:
    severity: str
    id: str
    message: str
    locations: tuple[str, ...] = ()

    def __str__(self) -> str:
        where = f" at {', '.join(self.locations)}" if self.locations else ""
        return f"{self.severity} {self.id}{where}: {self.message}"


@dataclass
class Report:
    version: str
    returncode: int
    messages: list[Message] = field(default_factory=list)

    @property
    def errors(self) -> list[Message]:
        return [m for m in self.messages if m.severity in ERROR_SEVERITIES]

    @property
    def warnings(self) -> list[Message]:
        return [m for m in self.messages if m.severity in WARNING_SEVERITIES]


def cache_dir() -> Path:
    if os.environ.get("EPUBCHECK_CACHE_DIR"):
        return Path(os.environ["EPUBCHECK_CACHE_DIR"])
    base = os.environ.get("XDG_CACHE_HOME") or str(Path.home() / ".cache")
    return Path(base) / "eink-learner-aid"


def find_java() -> str:
    java_home = os.environ.get("JAVA_HOME")
    if java_home and (Path(java_home) / "bin" / "java").is_file():
        return str(Path(java_home) / "bin" / "java")
    java = shutil.which("java")
    if not java:
        raise EpubcheckUnavailable("Java not found: set JAVA_HOME or put 'java' on PATH")
    return java


def _download(url: str, target: Path) -> None:
    host = urllib.parse.urlsplit(url).hostname
    try:
        with urllib.request.urlopen(url, timeout=120) as response, target.open("wb") as out:
            shutil.copyfileobj(response, out)
    except urllib.error.HTTPError as exc:
        raise EpubcheckUnavailable(f"download of {url} refused by host {host}: HTTP {exc.code} {exc.reason}") from exc
    except (urllib.error.URLError, OSError) as exc:
        reason = getattr(exc, "reason", exc)
        raise EpubcheckUnavailable(f"download of {url} failed (host {host}): {reason}") from exc


def ensure_epubcheck(download: bool = True) -> Path:
    """Return the path of epubcheck.jar, downloading and verifying the release if needed."""
    root = cache_dir() / f"epubcheck-{EPUBCHECK_VERSION}"
    jar = root / f"epubcheck-{EPUBCHECK_VERSION}" / "epubcheck.jar"
    if jar.is_file():
        return jar
    if not download:
        raise EpubcheckUnavailable(f"EPUBCheck {EPUBCHECK_VERSION} is not in {root} and download is disabled")
    root.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=root.parent) as tmp:
        archive = Path(tmp) / "epubcheck.zip"
        _download(EPUBCHECK_URL, archive)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        if digest != EPUBCHECK_SHA256:
            raise EpubcheckUnavailable(
                f"checksum mismatch for {EPUBCHECK_URL}: got {digest}, expected {EPUBCHECK_SHA256}"
            )
        unpacked = Path(tmp) / "unpacked"
        with zipfile.ZipFile(archive) as zf:
            zf.extractall(unpacked)
        if not (unpacked / f"epubcheck-{EPUBCHECK_VERSION}" / "epubcheck.jar").is_file():
            raise EpubcheckUnavailable(f"{EPUBCHECK_URL} does not contain epubcheck.jar")
        try:
            unpacked.rename(root)
        except OSError:
            if not jar.is_file():  # lost a race with another process only if the jar is now there
                raise
    return jar


def parse_json_report(data: dict, returncode: int) -> Report:
    """Turn EPUBCheck's ``--json`` output into a Report."""
    version = data.get("checker", {}).get("checkerVersion", "")
    messages = []
    for m in data.get("messages", []):
        locations = []
        for loc in m.get("locations", []):
            text = loc.get("path", "")
            if loc.get("line", -1) not in (-1, None):
                text += f":{loc['line']}"
                if loc.get("column", -1) not in (-1, None):
                    text += f":{loc['column']}"
            locations.append(text)
        messages.append(Message(m.get("severity", ""), m.get("ID", ""), m.get("message", ""), tuple(locations)))
    return Report(version=version, returncode=returncode, messages=messages)


def run_epubcheck(epub: str | Path, download: bool = True) -> Report:
    """Run EPUBCheck on ``epub`` and return its messages."""
    java = find_java()
    jar = ensure_epubcheck(download=download)
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "report.json"
        proc = subprocess.run(
            [java, "-jar", str(jar), str(epub), "--json", str(out)],
            capture_output=True,
            text=True,
            timeout=600,
        )
        if not out.is_file():
            raise RuntimeError(f"EPUBCheck wrote no report (exit {proc.returncode}): {proc.stderr.strip()}")
        return parse_json_report(json.loads(out.read_text(encoding="utf-8")), proc.returncode)


def main(argv: list[str] | None = None) -> int:
    args = sys.argv[1:] if argv is None else argv
    if len(args) != 1:
        print("usage: python3 -m tools.prep.epubcheck FILE.epub", file=sys.stderr)
        return 2
    try:
        report = run_epubcheck(args[0])
    except EpubcheckUnavailable as exc:
        print(f"EPUBCheck unavailable: {exc}", file=sys.stderr)
        return 3
    for message in report.messages:
        if message.severity in ERROR_SEVERITIES + WARNING_SEVERITIES:
            print(message)
    print(f"EPUBCheck {report.version}: {len(report.errors)} errors, {len(report.warnings)} warnings")
    return 1 if report.errors else 0


if __name__ == "__main__":
    sys.exit(main())
