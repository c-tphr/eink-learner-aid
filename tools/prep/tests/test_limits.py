"""Tests for tools/prep/limits.py: the mirror of lib/CheckpointCore/CheckpointLimits.h, and each limit."""

from __future__ import annotations

import copy
import json
import re

import pytest

from tools.prep import checkpoints as cp
from tools.prep import limits as L
from tools.prep.tests.prepkit import REPO, PREPARED_BYTES
from tools.prep.epubzip import Archive

HEADER = REPO / "lib" / "CheckpointCore" / "CheckpointLimits.h"
CONSTANT_RE = re.compile(r"inline\s+constexpr\s+(?:uint\d+_t|size_t)\s+([A-Z_]+)\s*=\s*([^;]+);")
# Buffer sizes derived from the limits (not limits themselves); the test evaluates them too.
DERIVED = {"ID_BUFFER_BYTES", "PROMPT_BUFFER_BYTES", "ANSWER_BUFFER_BYTES", "LABEL_BUFFER_BYTES",
           "TITLE_BUFFER_BYTES", "PATH_BUFFER_BYTES"}
SAMPLE = json.loads(Archive(PREPARED_BYTES).read("OEBPS/rsvp/checkpoints.json"))


def header_constants() -> dict[str, str]:
    text = re.sub(r"//[^\n]*", "", HEADER.read_text(encoding="utf-8"))
    return dict(CONSTANT_RE.findall(text))


def evaluate(expr: str, known: dict[str, int]) -> int:
    expr = re.sub(r"(\d+)u\b", r"\1", expr)
    expr = re.sub(r"textBufferBytes\(([A-Z_]+)\)", r"(\1 * 4 + 1)", expr)
    expr = re.sub(r"static_cast<size_t>\(([^)]*)\)", r"(\1)", expr)
    assert re.fullmatch(r"[A-Z_0-9\s*+()]+", expr), expr
    return int(eval(expr, {"__builtins__": {}}, dict(known)))  # noqa: S307 - digits, names, * and + only


def test_python_limits_mirror_the_firmware_header():
    constants = header_constants()
    assert len(constants) >= 20, "the header parser found too few constants"
    values: dict[str, int] = {}
    for name, expr in constants.items():
        values[name] = evaluate(expr, values)
    limits = {name: value for name, value in values.items() if name not in DERIVED}
    assert limits == L.LIMITS, "tools/prep/limits.py differs from lib/CheckpointCore/CheckpointLimits.h"
    assert set(constants) - set(limits) == DERIVED  # a new limit in the header must be mirrored or listed here


def test_schema_carries_the_limits_a_schema_can_express():
    schema = cp.load_schema()
    d = schema["$defs"]
    assert schema["properties"]["sections"]["maxItems"] == L.MAX_SECTIONS
    assert schema["properties"]["checkpoints"]["maxItems"] == L.MAX_CHECKPOINTS
    assert d["checkpoint"]["properties"]["prompts"]["maxItems"] == L.MAX_PROMPTS_PER_CHECKPOINT
    assert d["book"]["properties"]["title"]["maxLength"] == L.MAX_TITLE_CHARS
    assert d["book"]["properties"]["language"]["maxLength"] == L.MAX_LANGUAGE_CHARS
    assert d["book"]["properties"]["instructions_version"]["maxLength"] == L.MAX_INSTRUCTIONS_VERSION_CHARS
    assert d["section"]["properties"]["chapter_href"]["maxLength"] == L.MAX_HREF_CHARS
    assert d["section"]["properties"]["label"]["maxLength"] == L.MAX_LABEL_CHARS
    assert d["id"]["maxLength"] == L.MAX_ID_CHARS
    assert d["prompt"]["properties"]["prompt"]["maxLength"] == L.MAX_PROMPT_CHARS
    assert d["prompt"]["properties"]["answer"]["maxLength"] == L.MAX_ANSWER_CHARS


def raw(data) -> bytes:
    return json.dumps(data).encode("utf-8")


def names(data, raw_bytes: bytes | None = None) -> set[str]:
    return {lim.name for lim in L.check_limits(raw_bytes if raw_bytes is not None else raw(data), data)}


def test_the_prepared_fixture_is_within_every_limit():
    assert L.check_limits(raw(SAMPLE), SAMPLE) == []


def _with(mutate):
    data = copy.deepcopy(SAMPLE)
    mutate(data)
    return data


def _section(i: int, href: str = "text/chapter-1.xhtml", sid: str | None = None) -> dict:
    sid = sid or f"s{i:05d}"
    return {"id": sid, "chapter_href": href, "start_anchor": f"rsvp-{sid}-start", "end_anchor": f"rsvp-{sid}-end",
            "label": "L", "word_count": 1}


def _checkpoint(i: int, prompts: int = 1) -> dict:
    return {"id": f"c{i:05d}", "section": "s0001",
            "prompts": [{"id": f"c{i:05d}-p{k}", "prompt": "Q?", "answer": "A."} for k in range(prompts)]}


@pytest.mark.parametrize("name, mutate", [
    ("MAX_TITLE_CHARS", lambda d: d["book"].__setitem__("title", "t" * 301)),
    ("MAX_LANGUAGE_CHARS", lambda d: d["book"].__setitem__("language", "x" * 36)),
    ("MAX_INSTRUCTIONS_VERSION_CHARS", lambda d: d["book"].__setitem__("instructions_version", "1" * 33)),
    ("MAX_LABEL_CHARS", lambda d: d["sections"][0].__setitem__("label", "é" * 201)),
    ("MAX_HREF_CHARS", lambda d: d["sections"][0].__setitem__("chapter_href", "h" * 256)),
    ("MAX_ID_CHARS", lambda d: d["checkpoints"][0].__setitem__("id", "c" * 65)),
    ("MAX_PROMPT_CHARS", lambda d: d["checkpoints"][0]["prompts"][0].__setitem__("prompt", "q" * 301)),
    ("MAX_ANSWER_CHARS", lambda d: d["checkpoints"][0]["prompts"][0].__setitem__("answer", "a" * 601)),
    ("MAX_SECTIONS", lambda d: d.__setitem__("sections", [_section(i) for i in range(513)])),
    ("MAX_CHECKPOINTS", lambda d: d.__setitem__("checkpoints", [_checkpoint(i) for i in range(513)])),
    ("MAX_PROMPTS_PER_CHECKPOINT", lambda d: d.__setitem__("checkpoints", [_checkpoint(1, 33)])),
    ("MAX_PROMPTS", lambda d: d.__setitem__("checkpoints", [_checkpoint(i, 32) for i in range(33)])),
    ("MAX_CHAPTERS", lambda d: d.__setitem__("sections", [_section(i, f"c{i}.xhtml") for i in range(257)])),
    ("MAX_DEPTH", lambda d: d.__setitem__("x_deep", json.loads("[" * 32 + "]" * 32))),
    ("MAX_OPEN_MEMBERS", lambda d: d.__setitem__("x_wide", {f"k{i}": 0 for i in range(130)})),
])
def test_each_limit(name, mutate):
    assert name in names(_with(mutate))


def test_limits_at_the_boundary_pass():
    at = _with(lambda d: (d["book"].__setitem__("title", "t" * 300),
                          d.__setitem__("x_deep", json.loads("[" * 31 + "]" * 31)),
                          d.__setitem__("checkpoints", [_checkpoint(i, 32) for i in range(32)])))
    assert names(at) == set()


def test_string_pool():
    # 250 sections with 63-character ids, each in its own 60-character chapter: (64 + 61) * 250 > 16384.
    data = _with(lambda d: d.__setitem__("sections", [
        _section(i, f"{'h' * 54}{i:06d}", f"s{i:062d}") for i in range(250)]))
    assert names(data) == {"STRING_POOL_BYTES"}
    small = _with(lambda d: d.__setitem__("sections", [_section(i, "c.xhtml", f"s{i:062d}") for i in range(250)]))
    assert names(small) == set()  # 250 * 64 + 8 = 16008 bytes


def test_file_size_and_number_length():
    big = _with(lambda d: d.__setitem__("x_padding", "p" * L.MAX_FILE_BYTES))
    assert "MAX_FILE_BYTES" in names(big)
    long_number = raw(SAMPLE)[:-1] + b', "x_n": ' + b"1" * 65 + b"}"
    assert "MAX_NUMBER_CHARS" in names(json.loads(long_number), long_number)
