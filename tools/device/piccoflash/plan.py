"""flash-plan.json: format, verification, generation (make-plan) and printing.

The plan says which files to write; the guard (guard.py) decides whether they
may be written. A plan that disagrees with the guard's hard-coded policy is
refused, never obeyed. The JSON Schema is tools/device/schema/flash-plan.schema.json;
``validate_document`` below enforces the same rules without third-party
packages, and the tests check that the two agree.
"""

from __future__ import annotations

import datetime
import json
import os
import platform
import re
from dataclasses import dataclass
from typing import List, Optional, Sequence, Tuple

from . import PINNED_ESPTOOL_VERSION, __version__, guard, layout

PLAN_FORMAT = "picco-flash-plan"
PLAN_VERSION = 1
PLAN_FILE_NAME = "flash-plan.json"

_HEX_RE = re.compile(r"^0x[0-9A-F]+$")
_SHA_RE = re.compile(r"^[0-9a-f]{64}$")
_TEXT_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]{0,63}$")


class PlanError(Exception):
    def __init__(self, reasons: Sequence[str]):
        self.reasons = list(reasons)
        super().__init__("; ".join(self.reasons))


@dataclass
class Plan:
    path: str  # the flash-plan.json file
    doc: dict
    images: List[guard.PlannedImage]

    @property
    def directory(self) -> str:
        return os.path.dirname(os.path.abspath(self.path))

    def file_path(self, image: guard.PlannedImage) -> str:
        return os.path.join(self.directory, image.file_name)

    def image(self, role: str) -> guard.PlannedImage:
        return next(i for i in self.images if i.role == role)


def resolve_plan_path(path: str) -> str:
    if os.path.isdir(path):
        return os.path.join(path, PLAN_FILE_NAME)
    return path


def validate_document(doc: object) -> List[str]:
    """Structural checks equivalent to schema/flash-plan.schema.json."""
    errs: List[str] = []
    if not isinstance(doc, dict):
        return ["the plan must be a JSON object"]
    allowed = {"format", "version", "firmware_version", "build_id", "created_utc", "device", "images",
               "partition_table", "tools"}
    for key in sorted(set(doc) - allowed):
        errs.append("unexpected key %r" % key)
    for key in sorted(allowed - set(doc)):
        errs.append("missing key %r" % key)
    if doc.get("format") != PLAN_FORMAT:
        errs.append("format must be %r" % PLAN_FORMAT)
    if doc.get("version") != PLAN_VERSION or isinstance(doc.get("version"), bool):
        errs.append("version must be %d" % PLAN_VERSION)
    for key in ("firmware_version", "build_id"):
        if not isinstance(doc.get(key), str) or not _TEXT_RE.match(doc.get(key, "")):
            errs.append("%s must be 1-64 characters of letters, digits, '.', '_', '+', '-'" % key)
    if not isinstance(doc.get("created_utc"), str):
        errs.append("created_utc must be a string")
    if not isinstance(doc.get("device"), str):
        errs.append("device must be a string")
    images = doc.get("images")
    if not isinstance(images, list) or len(images) != 4:
        errs.append("images must be a list of exactly 4 entries")
        images = images if isinstance(images, list) else []
    for n, img in enumerate(images):
        where = "images[%d]" % n
        if not isinstance(img, dict):
            errs.append("%s must be an object" % where)
            continue
        if set(img) != {"role", "file", "offset", "size", "sha256"}:
            errs.append("%s must have exactly the keys role, file, offset, size, sha256" % where)
        if img.get("role") not in guard.ROLES:
            errs.append("%s.role must be one of %s" % (where, ", ".join(guard.ROLES)))
        f = img.get("file")
        if not isinstance(f, str) or f in ("", ".", "..") or "/" in f or "\\" in f or ":" in f:
            errs.append("%s.file must be a plain file name" % where)
        if not isinstance(img.get("offset"), str) or not _HEX_RE.match(img.get("offset", "")):
            errs.append("%s.offset must be an upper-case hex string like 0x20000" % where)
        size = img.get("size")
        if not isinstance(size, int) or isinstance(size, bool) or size < 1:
            errs.append("%s.size must be a positive integer" % where)
        if not isinstance(img.get("sha256"), str) or not _SHA_RE.match(img.get("sha256", "")):
            errs.append("%s.sha256 must be 64 lower-case hex digits" % where)
    rows = doc.get("partition_table")
    if not isinstance(rows, list) or not rows:
        errs.append("partition_table must be a non-empty list")
        rows = []
    for n, row in enumerate(rows):
        where = "partition_table[%d]" % n
        if not isinstance(row, dict) or set(row) != {"name", "type", "subtype", "offset", "size", "flags"}:
            errs.append("%s must have exactly the keys name, type, subtype, offset, size, flags" % where)
            continue
        for key in ("offset", "size"):
            if not isinstance(row[key], str) or not _HEX_RE.match(row[key]):
                errs.append("%s.%s must be an upper-case hex string" % (where, key))
        if not isinstance(row["flags"], int) or isinstance(row["flags"], bool):
            errs.append("%s.flags must be an integer" % where)
    tools = doc.get("tools")
    if not isinstance(tools, dict) or not all(isinstance(tools.get(k), str) for k in ("piccoflash", "esptool", "python")):
        errs.append("tools must be an object with string piccoflash, esptool and python versions")
    return errs


def _images_from_doc(doc: dict) -> List[guard.PlannedImage]:
    return [guard.PlannedImage(i["role"], i["file"], int(i["offset"], 16), i["size"], i["sha256"]) for i in doc["images"]]


def load(path: str) -> Plan:
    """Read and structurally validate a plan. Does not look at the image files."""
    path = resolve_plan_path(path)
    try:
        with open(path, "r", encoding="utf-8") as fp:
            doc = json.load(fp)
    except FileNotFoundError:
        raise PlanError(["no flash plan at %s" % path])
    except (OSError, ValueError) as exc:
        raise PlanError(["cannot read the flash plan %s: %s" % (path, exc)])
    errs = validate_document(doc)
    if errs:
        raise PlanError(errs)
    return Plan(path, doc, _images_from_doc(doc))


def verify(plan: Plan) -> List[str]:
    """Everything the guard can check offline: policy, table, and every file's bytes."""
    errs = guard.check_plan_set(plan.images)
    try:
        rows = [layout.Row.from_dict(r) for r in plan.doc["partition_table"]]
        if tuple(rows) != layout.TWO_SLOT_TABLE:
            errs.append("the plan's partition_table is not the two-slot table the guard allows")
            errs += layout.diff_rows(rows, layout.TWO_SLOT_TABLE)
    except (KeyError, ValueError) as exc:
        errs.append("partition_table cannot be read: %s" % exc)
    for img in plan.images:
        p = plan.file_path(img)
        try:
            with open(p, "rb") as fp:
                data = fp.read()
        except OSError as exc:
            errs.append("%s: cannot read %s: %s" % (img.role, img.file_name, exc))
            continue
        try:
            guard.approve_flash_write(img, data)
        except guard.GuardError as exc:
            errs += exc.reasons
    return guard._dedupe(errs)


def load_and_verify(path: str) -> Plan:
    plan = load(path)
    errs = verify(plan)
    if errs:
        raise PlanError(errs)
    return plan


def read_image_bytes(plan: Plan, image: guard.PlannedImage) -> bytes:
    with open(plan.file_path(image), "rb") as fp:
        return fp.read()


def format_table(plan: Plan) -> List[str]:
    """The plan as a table, in address order."""
    lines = ["%-16s %-10s %-10s %10s  %-15s %s" % ("Region", "Offset", "Erase end", "Size", "File", "SHA-256 (first 16)")]
    for img in sorted(plan.images, key=lambda i: i.offset):
        start, end = guard.touched_span(img.offset, img.size)
        lines.append("%-16s 0x%08X 0x%08X %10d  %-15s %s" % (
            img.role, img.offset, end - 1, img.size, img.file_name, img.sha256[:16]))
    app = next((i for i in plan.images if i.role == "app"), None)
    if app is not None:
        pct = 100.0 * app.size / guard.REGION_BY_ROLE["app"].max_len
        lines.append("app uses %.1f%% of its 0x7E0000-byte slot%s" % (
            pct, "; OVER the CI budget of %d bytes" % guard.APP_BUDGET if app.size > guard.APP_BUDGET else ""))
    return lines


def format_plan_header(plan: Plan) -> List[str]:
    d = plan.doc
    return [
        "Flash plan: %s" % plan.path,
        "Firmware version: %s   build: %s   created: %s" % (d["firmware_version"], d["build_id"], d["created_utc"]),
        "Made with: piccoflash %s, esptool %s, Python %s" % (d["tools"]["piccoflash"], d["tools"]["esptool"],
                                                            d["tools"]["python"]),
    ]


# --- make-plan ---------------------------------------------------------------

def make_plan(sources: dict, out_dir: str, firmware_version: str, build_id: str,
              expect_offsets: Optional[dict] = None, now: Optional[datetime.datetime] = None) -> Plan:
    """Copy the four images into ``out_dir`` and write a verified flash-plan.json.

    ``sources`` maps role -> source path. ``expect_offsets`` maps role -> the
    offset the caller believes (for example from the build's
    FLASH_EXTRA_IMAGES); any disagreement with the guard is refused. On any
    failure nothing is left behind in ``out_dir``.
    """
    errs: List[str] = []
    for role in sorted(set(sources) - set(guard.ROLES)):
        errs.append("unknown role %r" % role)
    for role in guard.ROLES:
        if role not in sources:
            errs.append("missing --%s" % role.replace("_", "-"))
    for role, off in sorted((expect_offsets or {}).items()):
        region = guard.REGION_BY_ROLE.get(role)
        if region is None:
            errs.append("--expect-offset names unknown role %r" % role)
        elif off != region.offset:
            errs.append("--expect-offset %s=0x%X disagrees with the only allowed offset 0x%X" % (role, off, region.offset))
            if role in sources and os.path.isfile(sources[role]):
                errs += [e for e in guard.check_extent(role, off, os.path.getsize(sources[role]))
                         if "allowed offset" not in e]
    for role, src in sorted(sources.items()):
        if guard._BAD_NAME_RE.search(os.path.basename(src)):
            errs.append("%s: source %r looks like a merged image; use the separate file" % (role, src))
    if not re.match(_TEXT_RE, firmware_version or ""):
        errs.append("--version must be 1-64 characters of letters, digits, '.', '_', '+', '-'")
    if not re.match(_TEXT_RE, build_id or ""):
        errs.append("--build-id must be 1-64 characters of letters, digits, '.', '_', '+', '-'")
    if os.path.exists(out_dir) and (not os.path.isdir(out_dir) or os.listdir(out_dir)):
        errs.append("output folder %s must not exist or be empty" % out_dir)
    if errs:
        raise PlanError(errs)

    created_dir = not os.path.exists(out_dir)
    os.makedirs(out_dir, exist_ok=True)
    written: List[str] = []
    try:
        images = []
        for role in guard.ROLES:
            region = guard.REGION_BY_ROLE[role]
            src = sources[role]
            with open(src, "rb") as fp:
                data = fp.read()
            dest = os.path.join(out_dir, region.file_name)
            with open(dest, "xb") as fp:
                written.append(dest)
                fp.write(data)
            with open(dest, "rb") as fp:
                copied = fp.read()
            with open(src, "rb") as fp:
                again = fp.read()
            digest = guard.sha256(data)
            if guard.sha256(copied) != digest or guard.sha256(again) != digest:
                raise PlanError(["%s: %s changed while it was being copied" % (role, src)])
            images.append({"role": role, "file": region.file_name, "offset": "0x%X" % region.offset,
                           "size": len(data), "sha256": digest})
        now = now or datetime.datetime.now(datetime.timezone.utc)
        doc = {
            "format": PLAN_FORMAT,
            "version": PLAN_VERSION,
            "firmware_version": firmware_version,
            "build_id": build_id,
            "created_utc": now.strftime("%Y-%m-%dT%H:%M:%SZ"),
            "device": "BOOX Picco (ESP32-S3, 16 MB flash), two-slot layout",
            "images": images,
            "partition_table": [r.as_dict() for r in layout.TWO_SLOT_TABLE],
            "tools": {"piccoflash": __version__, "esptool": PINNED_ESPTOOL_VERSION,
                      "python": platform.python_version()},
        }
        plan_path = os.path.join(out_dir, PLAN_FILE_NAME)
        with open(plan_path, "x", encoding="utf-8") as fp:
            written.append(plan_path)
            json.dump(doc, fp, indent=2)
            fp.write("\n")
        return load_and_verify(plan_path)
    except BaseException:
        for p in reversed(written):
            try:
                os.remove(p)
            except OSError:
                pass
        if created_dir:
            try:
                os.rmdir(out_dir)
            except OSError:
                pass
        raise


def parse_expect_offset(text: str) -> Tuple[str, int]:
    role, _, value = text.partition("=")
    role = role.strip().replace("-", "_")
    try:
        return role, int(value.strip(), 16)
    except ValueError:
        raise PlanError(["--expect-offset must look like ROLE=0xHEX, got %r" % text])
