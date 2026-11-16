"""Loader and validator for ``rsvp/checkpoints.json`` (docs/PLAN.md section 5).

Validation runs in four layers, in this order. The first layer that reports
anything stops validation, so every issue list comes from exactly one layer.

1. ``parse``: the bytes are UTF-8 without a byte order mark, form one JSON
   value, have no duplicate member names, no NaN/Infinity and no string that
   decodes to an unpaired surrogate.
2. ``version``: a file whose ``format`` is ``rsvp-checkpoints`` and whose
   ``version`` is an integer above 1 is reported as ``unsupported-version``
   and nothing else is checked (the firmware opens it as an ordinary EPUB).
3. ``schema``: ``schema/checkpoints.schema.json`` (JSON Schema 2020-12), with
   a strict ``integer`` type that rejects ``1.0``.
4. ``cross-reference``: rules the schema cannot express (see
   ``validate_cross_references``).

The rules and issue codes are documented in ``tools/prep/README.md``.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

import jsonschema
from jsonschema import Draft202012Validator

FORMAT = "rsvp-checkpoints"
SUPPORTED_VERSION = 1
PROMPT_MAX_CHARS = 300
ANSWER_MAX_CHARS = 600

SCHEMA_PATH = Path(__file__).resolve().parent / "schema" / "checkpoints.schema.json"

# Issue codes. Each maps to the layer that reports it.
PARSE = "parse"
DUPLICATE_KEY = "duplicate-key"
UNSUPPORTED_VERSION = "unsupported-version"
SCHEMA = "schema"
DUPLICATE_ID = "duplicate-id"
UNKNOWN_SECTION = "unknown-section"
MULTIPLE_CHECKPOINTS_FOR_SECTION = "multiple-checkpoints-for-section"
ANCHOR_NAME = "anchor-name"
NOT_PLAIN_TEXT = "not-plain-text"
BLANK_TEXT = "blank-text"

LAYER_OF_CODE = {
    PARSE: "parse",
    DUPLICATE_KEY: "parse",
    UNSUPPORTED_VERSION: "version",
    SCHEMA: "schema",
    DUPLICATE_ID: "cross-reference",
    UNKNOWN_SECTION: "cross-reference",
    MULTIPLE_CHECKPOINTS_FOR_SECTION: "cross-reference",
    ANCHOR_NAME: "cross-reference",
    NOT_PLAIN_TEXT: "cross-reference",
    BLANK_TEXT: "cross-reference",
}

VALID = "valid"
INVALID = "invalid"


@dataclass(frozen=True)
class Issue:
    """One validation finding.

    ``path`` is an RFC 6901 JSON Pointer into the document ("" is the root).
    For a missing member it points at the object that lacks it.
    """

    code: str
    path: str
    message: str

    @property
    def layer(self) -> str:
        return LAYER_OF_CODE[self.code]

    def __str__(self) -> str:
        return f"{self.code} at {self.path or '/'}: {self.message}"


class CheckpointFileError(ValueError):
    """Raised by ``load_checkpoints`` when a file is not a valid version 1 file."""

    def __init__(self, issues: list[Issue]):
        self.issues = issues
        super().__init__("; ".join(str(i) for i in issues))


# --------------------------------------------------------------------------
# Helpers


def json_pointer(parts: Iterable[Any]) -> str:
    """Build an RFC 6901 pointer from path segments."""
    out = []
    for part in parts:
        out.append("/" + str(part).replace("~", "~0").replace("/", "~1"))
    return "".join(out)


def is_json_integer(value: Any) -> bool:
    """True for a JSON number written without fraction or exponent."""
    return isinstance(value, int) and not isinstance(value, bool)


# Unicode general category Cc except U+000A LINE FEED.
_CONTROL_RE = re.compile("[\u0000-\u0009\u000b-\u001f\u007f-\u009f]")
# '<' followed by a letter, '/', '!' or '?' starts a tag, comment,
# declaration or processing instruction.
_TAG_RE = re.compile(r"<[A-Za-z/!?]")
# A complete named, decimal or hexadecimal character reference.
_ENTITY_RE = re.compile(r"&(?:[A-Za-z][A-Za-z0-9]*|#[0-9]+|#[xX][0-9A-Fa-f]+);")
# Code points with the Unicode White_Space property (Unicode 15).
WHITE_SPACE = frozenset(
    "\u0009\u000a\u000b\u000c\u000d \u0085  "
    "           "
    "    　"
)


def plain_text_problem(text: str) -> str | None:
    """Return why ``text`` is not plain text, or None if it is.

    Plain text: no code point of general category Cc other than U+000A
    (so no NUL, tab, carriage return, DEL or C1 controls), no markup tag
    start (``<`` followed by an ASCII letter, ``/``, ``!`` or ``?``), and no
    complete character reference (``&name;``, ``&#123;``, ``&#x1F;``).
    """
    m = _CONTROL_RE.search(text)
    if m:
        return f"contains control character U+{ord(m.group()):04X}"
    m = _TAG_RE.search(text)
    if m:
        return f"contains markup starting {m.group()!r}"
    m = _ENTITY_RE.search(text)
    if m:
        return f"contains character reference {m.group()!r}"
    return None


def is_blank(text: str) -> bool:
    """True when every code point has the Unicode White_Space property."""
    return all(ch in WHITE_SPACE for ch in text)


# --------------------------------------------------------------------------
# Layer 1: parse


def _reject_constant(name: str) -> Any:
    raise ValueError(f"{name} is not valid JSON")


def _find_surrogates(value: Any, parts: list[Any]) -> Iterable[Issue]:
    def bad(s: str) -> bool:
        return any("\ud800" <= ch <= "\udfff" for ch in s)

    if isinstance(value, str):
        if bad(value):
            yield Issue(PARSE, json_pointer(parts), "string contains an unpaired surrogate escape")
    elif isinstance(value, dict):
        for key, item in value.items():
            if bad(key):
                yield Issue(PARSE, json_pointer(parts), "member name contains an unpaired surrogate escape")
            yield from _find_surrogates(item, parts + [key])
    elif isinstance(value, list):
        for index, item in enumerate(value):
            yield from _find_surrogates(item, parts + [index])


def parse_checkpoints(raw: bytes) -> tuple[Any, list[Issue]]:
    """Parse bytes into a JSON value. Returns (value, parse-layer issues)."""
    if raw.startswith(b"\xef\xbb\xbf"):
        return None, [Issue(PARSE, "", "file starts with a UTF-8 byte order mark")]
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        return None, [Issue(PARSE, "", f"not valid UTF-8: {exc}")]

    duplicates: list[str] = []

    def pairs_hook(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        obj: dict[str, Any] = {}
        for key, item in pairs:
            if key in obj:
                duplicates.append(key)
            obj[key] = item
        return obj

    try:
        value = json.loads(text, object_pairs_hook=pairs_hook, parse_constant=_reject_constant)
    except ValueError as exc:  # json.JSONDecodeError is a ValueError
        return None, [Issue(PARSE, "", f"not valid JSON: {exc}")]
    if duplicates:
        return None, [
            Issue(DUPLICATE_KEY, "", f"member name {key!r} appears more than once in one object")
            for key in duplicates
        ]
    surrogates = list(_find_surrogates(value, []))
    if surrogates:
        return None, surrogates
    return value, []


# --------------------------------------------------------------------------
# Layer 3: schema


def _strict_integer(checker: Any, instance: Any) -> bool:
    return is_json_integer(instance)


_STRICT_TYPES = Draft202012Validator.TYPE_CHECKER.redefine("integer", _strict_integer)
StrictValidator = jsonschema.validators.extend(Draft202012Validator, type_checker=_STRICT_TYPES)

_validator: Any = None


def load_schema() -> dict[str, Any]:
    with SCHEMA_PATH.open(encoding="utf-8") as f:
        return json.load(f)


def _get_validator() -> Any:
    global _validator
    if _validator is None:
        schema = load_schema()
        Draft202012Validator.check_schema(schema)
        _validator = StrictValidator(schema)
    return _validator


def validate_schema(data: Any) -> list[Issue]:
    errors = sorted(
        _get_validator().iter_errors(data),
        key=lambda e: (json_pointer(e.absolute_path), e.message),
    )
    return [Issue(SCHEMA, json_pointer(e.absolute_path), _shorten(e.message)) for e in errors]


def _shorten(message: str, limit: int = 200) -> str:
    return message if len(message) <= limit else message[: limit - 3] + "..."


# --------------------------------------------------------------------------
# Layer 4: cross-reference


def validate_cross_references(data: dict[str, Any]) -> list[Issue]:
    """Rules the schema cannot express. Expects a schema-valid document.

    - ``duplicate-id``: section ids are unique among sections, checkpoint ids
      among checkpoints, prompt ids among all prompts in the file. Reported
      at each repeat, not at the first occurrence.
    - ``anchor-name``: ``start_anchor`` is ``rsvp-<id>-start`` and
      ``end_anchor`` is ``rsvp-<id>-end`` for the section's own id.
    - ``unknown-section``: a checkpoint's ``section`` names a section that
      exists.
    - ``multiple-checkpoints-for-section``: at most one checkpoint per
      section. Reported at each checkpoint after the first.
    - ``not-plain-text`` / ``blank-text``: prompts and answers are plain text
      (``plain_text_problem``) and not made only of white space.
    """
    issues: list[Issue] = []

    section_ids: set[str] = set()
    for i, section in enumerate(data["sections"]):
        sid = section["id"]
        if sid in section_ids:
            issues.append(Issue(DUPLICATE_ID, f"/sections/{i}/id", f"section id {sid!r} is used more than once"))
        section_ids.add(sid)
        for field, suffix in (("start_anchor", "start"), ("end_anchor", "end")):
            expected = f"rsvp-{sid}-{suffix}"
            if section[field] != expected:
                issues.append(
                    Issue(
                        ANCHOR_NAME,
                        f"/sections/{i}/{field}",
                        f"{field} is {section[field]!r}, expected {expected!r}",
                    )
                )

    checkpoint_ids: set[str] = set()
    prompt_ids: set[str] = set()
    sections_with_checkpoint: set[str] = set()
    for i, checkpoint in enumerate(data["checkpoints"]):
        cid = checkpoint["id"]
        if cid in checkpoint_ids:
            issues.append(
                Issue(DUPLICATE_ID, f"/checkpoints/{i}/id", f"checkpoint id {cid!r} is used more than once")
            )
        checkpoint_ids.add(cid)

        target = checkpoint["section"]
        if target not in section_ids:
            issues.append(
                Issue(UNKNOWN_SECTION, f"/checkpoints/{i}/section", f"checkpoint names section {target!r}, which does not exist")
            )
        elif target in sections_with_checkpoint:
            issues.append(
                Issue(
                    MULTIPLE_CHECKPOINTS_FOR_SECTION,
                    f"/checkpoints/{i}/section",
                    f"section {target!r} already has a checkpoint",
                )
            )
        sections_with_checkpoint.add(target)

        for j, prompt in enumerate(checkpoint["prompts"]):
            base = f"/checkpoints/{i}/prompts/{j}"
            pid = prompt["id"]
            if pid in prompt_ids:
                issues.append(Issue(DUPLICATE_ID, f"{base}/id", f"prompt id {pid!r} is used more than once"))
            prompt_ids.add(pid)
            for field in ("prompt", "answer"):
                text = prompt[field]
                problem = plain_text_problem(text)
                if problem:
                    issues.append(Issue(NOT_PLAIN_TEXT, f"{base}/{field}", f"{field} {problem}"))
                if is_blank(text):
                    issues.append(Issue(BLANK_TEXT, f"{base}/{field}", f"{field} contains only white space"))

    return issues


# --------------------------------------------------------------------------
# Entry points


def validate_checkpoints(data: Any) -> list[Issue]:
    """Validate an already-parsed document (layers 2 to 4)."""
    if (
        isinstance(data, dict)
        and data.get("format") == FORMAT
        and is_json_integer(data.get("version"))
        and data["version"] > SUPPORTED_VERSION
    ):
        return [
            Issue(
                UNSUPPORTED_VERSION,
                "/version",
                f"version {data['version']} is not supported (this reader supports {SUPPORTED_VERSION})",
            )
        ]
    issues = validate_schema(data)
    if issues:
        return issues
    return validate_cross_references(data)


def check_bytes(raw: bytes) -> list[Issue]:
    """Validate the raw bytes of a checkpoint file (all four layers)."""
    data, issues = parse_checkpoints(raw)
    if issues:
        return issues
    return validate_checkpoints(data)


def check_file(path: str | Path) -> list[Issue]:
    return check_bytes(Path(path).read_bytes())


def verdict(issues: list[Issue]) -> str:
    """'valid', 'unsupported-version' or 'invalid'."""
    if not issues:
        return VALID
    if any(i.code == UNSUPPORTED_VERSION for i in issues):
        return UNSUPPORTED_VERSION
    return INVALID


def load_checkpoints(path: str | Path) -> dict[str, Any]:
    """Read and validate a checkpoint file; return the document.

    Raises ``CheckpointFileError`` (with ``.issues``) unless the file is a
    valid version 1 file.
    """
    data, issues = parse_checkpoints(Path(path).read_bytes())
    if not issues:
        issues = validate_checkpoints(data)
    if issues:
        raise CheckpointFileError(issues)
    return data
