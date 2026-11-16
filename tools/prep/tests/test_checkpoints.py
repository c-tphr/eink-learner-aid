"""Tests for tools/prep/checkpoints.py and the shared samples in fixtures/checkpoints."""

from __future__ import annotations

import copy
import json
import re
from pathlib import Path

import jsonschema
import pytest

from tools.prep import checkpoints as cp

REPO = Path(__file__).resolve().parents[3]
FIXTURES = REPO / "fixtures" / "checkpoints"
MANIFEST = json.loads((FIXTURES / "manifest.json").read_text(encoding="utf-8"))
SAMPLES = MANIFEST["samples"]
LAYERS = {"parse", "version", "schema", "cross-reference"}
VERDICTS = {"valid", "invalid", "unsupported-version"}
SHA = "1e874b76a5df7dff6e3ea423c297d1a8565e2ebf5c98c9e67ac248598f597fdd"


# --------------------------------------------------------------------------
# Manifest-driven sample tests


@pytest.mark.parametrize("sample", SAMPLES, ids=[s["file"] for s in SAMPLES])
def test_sample_matches_manifest(sample):
    issues = cp.check_file(FIXTURES / sample["file"])
    assert cp.verdict(issues) == sample["verdict"], [str(i) for i in issues]
    got = {(i.code, i.path) for i in issues}
    expected = {(i["code"], i["path"]) for i in sample["issues"]}
    assert got == expected, [str(i) for i in issues]
    assert {i.layer for i in issues} == ({sample["layer"]} if issues else set())


def test_manifest_entries_are_well_formed():
    for sample in SAMPLES:
        assert set(sample) == {"file", "verdict", "layer", "issues", "reason"}, sample["file"]
        assert sample["verdict"] in VERDICTS
        assert sample["reason"].strip()
        if sample["verdict"] == "valid":
            assert sample["file"].startswith("valid/")
            assert sample["layer"] is None and sample["issues"] == []
        else:
            assert sample["file"].startswith("invalid/")
            assert sample["layer"] in LAYERS
            assert sample["issues"], sample["file"]
            codes = {i["code"] for i in sample["issues"]}
            # One rule per invalid sample: one code, from the stated layer.
            assert len(codes) == 1, sample["file"]
            for issue in sample["issues"]:
                assert cp.LAYER_OF_CODE[issue["code"]] == sample["layer"]
            assert (sample["verdict"] == "unsupported-version") == (codes == {"unsupported-version"})


def test_manifest_lists_every_sample_file_and_nothing_else():
    listed = [s["file"] for s in SAMPLES]
    assert len(listed) == len(set(listed)), "duplicate manifest entries"
    on_disk = {
        p.relative_to(FIXTURES).as_posix()
        for folder in ("valid", "invalid")
        for p in (FIXTURES / folder).iterdir()
        if p.is_file()
    }
    assert set(listed) == on_disk


def test_every_issue_code_has_a_sample():
    covered = {i["code"] for s in SAMPLES for i in s["issues"]}
    assert covered == set(cp.LAYER_OF_CODE)


def test_plan_example_sample_matches_plan():
    """valid/plan-example.json is the PLAN.md section 5 example with a real hash."""
    plan = REPO / "docs" / "PLAN.md"
    if not plan.exists():
        pytest.skip("docs/PLAN.md not present")
    text = plan.read_text(encoding="utf-8")
    section = text.split("### Checkpoint file, version 1", 1)[1]
    block = re.search(r"```json\n(.*?)```", section, re.S).group(1)
    example = json.loads(block)
    example["book"]["source_sha256"] = SHA
    sample = json.loads((FIXTURES / "valid" / "plan-example.json").read_text(encoding="utf-8"))
    assert sample == example
    assert cp.validate_checkpoints(example) == []


# --------------------------------------------------------------------------
# Schema


def test_schema_is_valid_draft_2020_12():
    schema = cp.load_schema()
    assert schema["$schema"] == "https://json-schema.org/draft/2020-12/schema"
    jsonschema.Draft202012Validator.check_schema(schema)


def test_schema_limits_match_module_constants():
    prompt = cp.load_schema()["$defs"]["prompt"]["properties"]
    assert prompt["prompt"]["maxLength"] == cp.PROMPT_MAX_CHARS == 300
    assert prompt["answer"]["maxLength"] == cp.ANSWER_MAX_CHARS == 600


# --------------------------------------------------------------------------
# Unit tests on in-memory documents


def doc():
    return {
        "format": "rsvp-checkpoints",
        "version": 1,
        "book": {"title": "T", "language": "en", "source_sha256": SHA, "instructions_version": "1.0"},
        "sections": [
            {
                "id": sid,
                "chapter_href": "text/ch.xhtml",
                "start_anchor": f"rsvp-{sid}-start",
                "end_anchor": f"rsvp-{sid}-end",
                "label": "L",
                "word_count": 10,
            }
            for sid in ("s1", "s2")
        ],
        "checkpoints": [
            {"id": "c1", "section": "s1", "prompts": [{"id": "c1-p1", "prompt": "P?", "answer": "A."}]},
            {"id": "c2", "section": "s2", "prompts": [{"id": "c2-p1", "prompt": "P?", "answer": "A."}]},
        ],
    }


def codes(data):
    return [(i.code, i.path) for i in cp.validate_checkpoints(data)]


def test_base_document_is_valid():
    assert codes(doc()) == []


def test_duplicate_ids_are_reported_at_each_repeat():
    d = doc()
    d["sections"].append(copy.deepcopy(d["sections"][0]))
    d["sections"].append(copy.deepcopy(d["sections"][0]))
    assert codes(d) == [("duplicate-id", "/sections/2/id"), ("duplicate-id", "/sections/3/id")]


def test_id_namespaces_are_separate():
    d = doc()
    d["checkpoints"][0]["id"] = "s1"
    d["checkpoints"][0]["prompts"][0]["id"] = "s1"
    assert codes(d) == []


def test_prompt_ids_unique_across_checkpoints():
    d = doc()
    d["checkpoints"][1]["prompts"][0]["id"] = "c1-p1"
    assert codes(d) == [("duplicate-id", "/checkpoints/1/prompts/0/id")]


def test_unknown_section():
    d = doc()
    d["checkpoints"][0]["section"] = "s9"
    assert codes(d) == [("unknown-section", "/checkpoints/0/section")]


def test_section_ids_defined_after_checkpoint_order_do_not_matter():
    d = doc()
    d["checkpoints"].reverse()
    assert codes(d) == []


def test_multiple_checkpoints_for_section_reported_after_first():
    d = doc()
    d["checkpoints"][1]["section"] = "s1"
    d["checkpoints"].append({"id": "c3", "section": "s1", "prompts": [{"id": "c3-p1", "prompt": "P", "answer": "A"}]})
    assert codes(d) == [
        ("multiple-checkpoints-for-section", "/checkpoints/1/section"),
        ("multiple-checkpoints-for-section", "/checkpoints/2/section"),
    ]


@pytest.mark.parametrize(
    "field,value",
    [
        ("start_anchor", "rsvp-s2-start"),
        ("start_anchor", "rsvp-s1-end"),
        ("end_anchor", "rsvp-s1-start"),
        ("end_anchor", "RSVP-s1-end"),
        ("end_anchor", "rsvp-s1-end\n"),
    ],
)
def test_anchor_names(field, value):
    d = doc()
    d["sections"][0][field] = value
    assert codes(d) == [("anchor-name", f"/sections/0/{field}")]


@pytest.mark.parametrize(
    "text",
    [
        "a\nb",
        "a < b",
        "x<3",
        "a <= b",
        "Q&A",
        "&amp without semicolon",
        "&#;",
        "&#x;",
        " no-break inside",
        "  line separator is not Cc",
        "café \U0001F600",
    ],
)
def test_plain_text_accepts(text):
    assert cp.plain_text_problem(text) is None


@pytest.mark.parametrize(
    "text",
    [
        "<b>x</b>",
        "</p>",
        "<!-- c -->",
        "<?xml?>",
        "x<br/>",
        "&amp;",
        "&nbsp;",
        "&#160;",
        "&#xA0;",
        "&#XA0;",
        "a\tb",
        "a\rb",
        "a\u0000b",
        "a\u001fb",
        "a\u007fb",
        "a\u0080b",
        "a\u009fb",
        "a\u000bb",
        "a\u000cb",
    ],
)
def test_plain_text_rejects(text):
    assert cp.plain_text_problem(text) is not None


def test_not_plain_text_in_prompt_and_answer():
    d = doc()
    d["checkpoints"][0]["prompts"][0]["prompt"] = "<i>P</i>"
    d["checkpoints"][1]["prompts"][0]["answer"] = "A\tB"
    assert codes(d) == [
        ("not-plain-text", "/checkpoints/0/prompts/0/prompt"),
        ("not-plain-text", "/checkpoints/1/prompts/0/answer"),
    ]


def test_plain_text_rule_not_applied_to_labels_or_unknown_members():
    d = doc()
    d["sections"][0]["label"] = "<b>Label</b>"
    d["checkpoints"][0]["prompts"][0]["x_note"] = "<b>\t</b>"
    assert codes(d) == []


@pytest.mark.parametrize("text", [" ", "\n", " \n\n ", " ", "　    "])
def test_blank_text(text):
    assert cp.is_blank(text)
    d = doc()
    d["checkpoints"][0]["prompts"][0]["answer"] = text
    assert codes(d) == [("blank-text", "/checkpoints/0/prompts/0/answer")]


def test_zero_width_space_is_not_white_space():
    # U+200B has no White_Space property, so it is neither blank nor a control.
    assert not cp.is_blank("​")


@pytest.mark.parametrize("length,ok", [(300, True), (301, False)])
def test_prompt_limit_counts_code_points(length, ok):
    d = doc()
    d["checkpoints"][0]["prompts"][0]["prompt"] = "\U0001F600" * length
    assert (codes(d) == []) is ok


@pytest.mark.parametrize("length,ok", [(600, True), (601, False)])
def test_answer_limit_counts_code_points(length, ok):
    d = doc()
    d["checkpoints"][0]["prompts"][0]["answer"] = "é" * length
    assert (codes(d) == []) is ok


def test_combining_sequences_count_each_code_point():
    d = doc()
    d["checkpoints"][0]["prompts"][0]["prompt"] = "é" * 151  # 302 code points, 151 graphemes
    assert codes(d) == [("schema", "/checkpoints/0/prompts/0/prompt")]


def test_cross_reference_rules_skipped_when_schema_fails():
    d = doc()
    d["checkpoints"][0]["section"] = "s9"  # would be unknown-section
    del d["book"]["title"]
    assert codes(d) == [("schema", "/book")]


def test_unknown_members_are_ignored_everywhere():
    d = doc()
    d["future"] = 1
    d["book"]["future"] = [1]
    d["sections"][0]["future"] = {"a": None}
    d["checkpoints"][0]["future"] = "x"
    d["checkpoints"][0]["prompts"][0]["future"] = 2.5
    assert codes(d) == []


# --------------------------------------------------------------------------
# Version handling


@pytest.mark.parametrize("version", [2, 3, 10**20])
def test_higher_version_is_unsupported_not_schema(version):
    d = {"format": "rsvp-checkpoints", "version": version, "anything": "goes"}
    issues = cp.validate_checkpoints(d)
    assert [(i.code, i.path) for i in issues] == [("unsupported-version", "/version")]
    assert cp.verdict(issues) == "unsupported-version"


@pytest.mark.parametrize("version", [0, -1, 1.0, 2.0, "2", True, None])
def test_other_bad_versions_are_schema_errors(version):
    d = doc()
    d["version"] = version
    assert {c for c, _ in codes(d)} == {"schema"}


def test_higher_version_with_wrong_format_is_schema_error():
    d = doc()
    d["format"] = "something-else"
    d["version"] = 2
    assert {c for c, _ in codes(d)} == {"schema"}


# --------------------------------------------------------------------------
# Parse layer


def raw(data) -> bytes:
    return json.dumps(data).encode("utf-8")


def test_check_bytes_valid():
    assert cp.check_bytes(raw(doc())) == []


@pytest.mark.parametrize(
    "data",
    [
        b"",
        b"{",
        b'{"a": 1,}',
        b'{"a": NaN}',
        b'{"a": Infinity}',
        b'{"a": -Infinity}',
        b"{} {}",
        b"\xef\xbb\xbf{}",
        b'{"a": "\xff"}',
        b'{"a": "\\udc00"}',
        b'{"\\ud800": 1}',
    ],
)
def test_parse_errors(data):
    issues = cp.check_bytes(data)
    assert [i.code for i in issues] == ["parse"]
    assert cp.verdict(issues) == "invalid"


def test_valid_surrogate_pair_escape_is_accepted():
    value, issues = cp.parse_checkpoints(b'{"a": "\\ud83d\\ude00"}')
    assert issues == [] and value == {"a": "\U0001F600"}


def test_duplicate_keys_at_any_depth():
    issues = cp.check_bytes(b'{"a": {"b": 1, "b": 2}, "c": 1, "c": 2}')
    assert [i.code for i in issues] == ["duplicate-key", "duplicate-key"]


def test_raw_control_character_in_string_is_parse_error():
    assert [i.code for i in cp.check_bytes(b'{"a": "x\ty"}')] == ["parse"]


# --------------------------------------------------------------------------
# Helpers and API


def test_json_pointer_escaping():
    assert cp.json_pointer([]) == ""
    assert cp.json_pointer(["a/b", "c~d", 0]) == "/a~1b/c~0d/0"


def test_load_checkpoints_returns_document_or_raises(tmp_path):
    good = tmp_path / "good.json"
    good.write_bytes(raw(doc()))
    assert cp.load_checkpoints(good)["sections"][0]["id"] == "s1"

    bad = tmp_path / "bad.json"
    d = doc()
    d["checkpoints"][0]["section"] = "s9"
    bad.write_bytes(raw(d))
    with pytest.raises(cp.CheckpointFileError) as exc:
        cp.load_checkpoints(bad)
    assert [i.code for i in exc.value.issues] == ["unknown-section"]


def test_issue_str_and_layer():
    issue = cp.Issue("anchor-name", "/sections/0/end_anchor", "m")
    assert issue.layer == "cross-reference"
    assert str(issue) == "anchor-name at /sections/0/end_anchor: m"
