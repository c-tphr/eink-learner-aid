"""The work folder: identify records, backups and logs.

Records are plain JSON files written atomically (temporary file, then
rename). Later commands find what they need by reading them; there is no
other state. File names carry a UTC timestamp, so sorting by name sorts by
time.
"""

from __future__ import annotations

import datetime
import glob
import hashlib
import json
import os
from typing import List, Optional, Tuple

BACKUP_PREFIX = "picco-backup-"
IDENTIFY_PREFIX = "identify-"


_last_stamp = [""]


def timestamp(now: Optional[datetime.datetime] = None) -> str:
    """UTC time to the millisecond, e.g. 20261118T120754.123Z; never repeats within a run."""
    now = now or datetime.datetime.now(datetime.timezone.utc)
    stamp = now.strftime("%Y%m%dT%H%M%S") + ".%03dZ" % (now.microsecond // 1000)
    while stamp <= _last_stamp[0]:
        now += datetime.timedelta(milliseconds=1)
        stamp = now.strftime("%Y%m%dT%H%M%S") + ".%03dZ" % (now.microsecond // 1000)
    _last_stamp[0] = stamp
    return stamp


def mac_tag(mac: str) -> str:
    return mac.replace(":", "").lower()


def ensure(path: str) -> str:
    os.makedirs(path, exist_ok=True)
    return path


def write_json_atomic(path: str, obj: dict) -> None:
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fp:
        json.dump(obj, fp, indent=2, sort_keys=False)
        fp.write("\n")
        fp.flush()
        os.fsync(fp.fileno())
    os.replace(tmp, path)


def write_bytes(path: str, data: bytes) -> None:
    with open(path, "xb") as fp:
        fp.write(data)
        fp.flush()
        os.fsync(fp.fileno())


def read_json(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as fp:
        return json.load(fp)


def file_sha256(path: str) -> Tuple[int, str]:
    h = hashlib.sha256()
    n = 0
    with open(path, "rb") as fp:
        while True:
            chunk = fp.read(1 << 20)
            if not chunk:
                break
            n += len(chunk)
            h.update(chunk)
    return n, h.hexdigest()


def identify_records(work: str) -> List[str]:
    return sorted(glob.glob(os.path.join(work, IDENTIFY_PREFIX + "*.json")))


def latest_identify(work: str, mac: Optional[str] = None) -> Optional[Tuple[str, dict]]:
    """The newest identify record (optionally for one MAC)."""
    for path in reversed(identify_records(work)):
        try:
            rec = read_json(path)
        except (OSError, ValueError):
            continue
        if mac is None or rec.get("chip", {}).get("mac") == mac:
            return path, rec
    return None


def backup_sidecars(work: str, mac: Optional[str] = None) -> List[str]:
    pattern = BACKUP_PREFIX + (mac_tag(mac) if mac else "*") + "-*.json"
    return sorted(p for p in glob.glob(os.path.join(work, pattern)) if not p.endswith("-MISMATCH.json"))


def sidecar_for(backup_path: str) -> str:
    base = backup_path[:-4] if backup_path.lower().endswith(".bin") else backup_path
    return base + ".json"


def latest_verified_backup(work: str, mac: str) -> Optional[Tuple[str, dict]]:
    for path in reversed(backup_sidecars(work, mac)):
        try:
            rec = read_json(path)
        except (OSError, ValueError):
            continue
        if rec.get("verified") is True and rec.get("mac") == mac:
            return path, rec
    return None


def free_bytes(path: str) -> int:
    import shutil

    return shutil.disk_usage(path).free
