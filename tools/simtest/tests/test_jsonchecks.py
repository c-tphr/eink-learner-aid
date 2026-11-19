"""JSON pointer and schema checks."""

import pytest

from tools.simtest import jsonchecks
from tools.simtest.jsonchecks import JsonCheckError, resolve_pointer, validate

DOC = {"a": {"b/c": [10, {"d": True}], "m~n": 1}, "e": None}


def test_pointer():
    assert resolve_pointer(DOC, "") is DOC
    assert resolve_pointer(DOC, "/a/b~1c/0") == 10
    assert resolve_pointer(DOC, "/a/b~1c/1/d") is True
    assert resolve_pointer(DOC, "/a/m~0n") == 1
    assert resolve_pointer(DOC, "/e") is None


@pytest.mark.parametrize("pointer,msg", [
    ("a", "must be empty or start with '/'"),
    ("/x", "no member 'x' (members: a, e)"),
    ("/a/b~1c/5", "index 5 out of range (length 2)"),
    ("/a/b~1c/01", "is not an array index"),
    ("/e/f", "cannot descend into null"),
])
def test_pointer_errors(pointer, msg):
    with pytest.raises(JsonCheckError, match=msg.replace("(", r"\(").replace(")", r"\)")):
        resolve_pointer(DOC, pointer)


def test_json_equal_follows_json_types():
    assert jsonchecks.json_equal(1, 1.0)
    assert not jsonchecks.json_equal(True, 1)
    assert jsonchecks.json_equal({"a": [1, "x"]}, {"a": [1.0, "x"]})
    assert not jsonchecks.json_equal({"a": 1}, {"a": 1, "b": 2})


SCHEMA = {
    "$schema": "https://json-schema.org/draft/2020-12/schema",
    "type": "object",
    "required": ["version", "prompts"],
    "additionalProperties": False,
    "properties": {
        "version": {"const": 1},
        "mode": {"enum": ["rsvp", "page"]},
        "prompts": {"type": "array", "minItems": 1, "items": {"$ref": "#/$defs/prompt"}},
    },
    "$defs": {
        "prompt": {
            "type": "object",
            "required": ["id", "grade"],
            "properties": {
                "id": {"type": "string", "pattern": "^p[0-9]+$"},
                "grade": {"anyOf": [{"type": "null"}, {"enum": ["correct", "incorrect"]}]},
                "tries": {"type": "integer", "minimum": 0, "maximum": 9},
            },
        }
    },
}


def test_schema_accepts_valid_document():
    doc = {"version": 1, "mode": "rsvp", "prompts": [{"id": "p1", "grade": None, "tries": 2.0},
                                                    {"id": "p2", "grade": "incorrect"}]}
    assert validate(doc, SCHEMA) == []


def test_schema_reports_every_problem_with_its_path():
    doc = {"version": 2, "mode": "x", "prompts": [{"id": "q", "grade": "maybe", "tries": -1}], "extra": 1}
    errors = validate(doc, SCHEMA)
    joined = "\n".join(errors)
    assert "/version: expected 1, got 2" in joined
    assert "/mode: 'x' is not one of ['rsvp', 'page']" in joined
    assert "/prompts/0/id: 'q' does not match /^p[0-9]+$/" in joined
    assert "/prompts/0/grade: matches none of anyOf" in joined
    assert "/prompts/0/tries: -1 is below minimum 0" in joined
    assert "/: unexpected member 'extra'" in joined


def test_schema_type_errors():
    assert validate("x", {"type": "object"}) == ["/: expected type object, got string"]
    assert validate(True, {"type": "integer"}) == ["/: expected type integer, got boolean"]
    assert validate(3, {"type": "number"}) == []
    assert validate([1, 1], {"uniqueItems": True}) == ["/: item 1 is a duplicate"]
    assert validate({}, {"required": ["a"]}) == ["/: missing required member 'a'"]


def test_unsupported_keyword_is_refused():
    with pytest.raises(JsonCheckError, match="unsupported keyword.*if"):
        validate({}, {"properties": {"a": {"if": {}}}})


def test_remote_ref_is_refused():
    with pytest.raises(JsonCheckError, match="only local"):
        validate({}, {"$ref": "http://example.com/s.json"})
