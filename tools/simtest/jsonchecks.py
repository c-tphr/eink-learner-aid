"""JSON assertions: RFC 6901 pointers and a small JSON Schema validator.

The validator covers the draft 2020-12 keywords listed in SUPPORTED. A schema
that uses any other keyword is rejected, so a check never passes because a
keyword was silently ignored. Only the standard library is used.
"""

from __future__ import annotations

import math
import re
from typing import Any

_MISSING = object()


class JsonCheckError(Exception):
    """A pointer, schema or value problem, with a message for the report."""


def resolve_pointer(document: Any, pointer: str) -> Any:
    """Return the value at an RFC 6901 JSON pointer ("" is the whole document)."""
    if pointer == "":
        return document
    if not pointer.startswith("/"):
        raise JsonCheckError(f"JSON pointer must be empty or start with '/': {pointer!r}")
    value = document
    walked = ""
    for raw in pointer[1:].split("/"):
        token = raw.replace("~1", "/").replace("~0", "~")
        walked += "/" + raw
        if isinstance(value, dict):
            if token not in value:
                keys = ", ".join(sorted(value)) or "none"
                raise JsonCheckError(f"{walked}: no member {token!r} (members: {keys})")
            value = value[token]
        elif isinstance(value, list):
            if not re.fullmatch(r"0|[1-9][0-9]*", token):
                raise JsonCheckError(f"{walked}: {token!r} is not an array index")
            index = int(token)
            if index >= len(value):
                raise JsonCheckError(f"{walked}: index {index} out of range (length {len(value)})")
            value = value[index]
        else:
            raise JsonCheckError(f"{walked}: cannot descend into {type_name(value)}")
    return value


def type_name(value: Any) -> str:
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "boolean"
    if isinstance(value, int):
        return "integer"
    if isinstance(value, float):
        return "number"
    if isinstance(value, str):
        return "string"
    if isinstance(value, list):
        return "array"
    if isinstance(value, dict):
        return "object"
    return type(value).__name__


def json_equal(a: Any, b: Any) -> bool:
    """Equality as JSON sees it: 1 == 1.0, but true != 1."""
    if isinstance(a, bool) or isinstance(b, bool):
        return isinstance(a, bool) and isinstance(b, bool) and a == b
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return a == b
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(json_equal(x, y) for x, y in zip(a, b))
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(json_equal(a[k], b[k]) for k in a)
    return type(a) is type(b) and a == b


SUPPORTED = {
    "$schema", "$id", "$comment", "$defs", "definitions", "$ref", "title", "description",
    "default", "examples", "type", "enum", "const", "properties", "required",
    "additionalProperties", "patternProperties", "items", "prefixItems", "minItems",
    "maxItems", "uniqueItems", "minimum", "maximum", "exclusiveMinimum", "exclusiveMaximum",
    "multipleOf", "minLength", "maxLength", "pattern", "minProperties", "maxProperties",
    "allOf", "anyOf", "oneOf", "not",
}


def _is_type(value: Any, name: str) -> bool:
    if name == "integer":
        return (isinstance(value, int) and not isinstance(value, bool)) or (
            isinstance(value, float) and value.is_integer())
    if name == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    return type_name(value) == name


class SchemaValidator:
    def __init__(self, schema: Any):
        self.root = schema
        self._check_keywords(schema, "#")

    def _check_keywords(self, schema: Any, where: str) -> None:
        if isinstance(schema, bool):
            return
        if not isinstance(schema, dict):
            raise JsonCheckError(f"schema at {where} must be an object or boolean")
        unknown = sorted(set(schema) - SUPPORTED)
        if unknown:
            raise JsonCheckError(
                f"schema at {where} uses unsupported keyword(s) {', '.join(unknown)}; "
                f"tools/simtest/jsonchecks.py supports: {', '.join(sorted(SUPPORTED))}")
        for key in ("properties", "patternProperties", "$defs", "definitions"):
            for name, sub in schema.get(key, {}).items():
                self._check_keywords(sub, f"{where}/{key}/{name}")
        for key in ("additionalProperties", "items", "not"):
            if key in schema and not isinstance(schema[key], list):
                self._check_keywords(schema[key], f"{where}/{key}")
        for key in ("allOf", "anyOf", "oneOf", "prefixItems"):
            for i, sub in enumerate(schema.get(key, [])):
                self._check_keywords(sub, f"{where}/{key}/{i}")

    def _ref(self, ref: str) -> Any:
        if not ref.startswith("#"):
            raise JsonCheckError(f"only local $ref values ('#/...') are supported, not {ref!r}")
        return resolve_pointer(self.root, ref[1:])

    def errors(self, value: Any, schema: Any = _MISSING, path: str = "") -> list[str]:
        if schema is _MISSING:
            schema = self.root
        here = path or "/"
        if schema is True:
            return []
        if schema is False:
            return [f"{here}: no value is allowed here"]
        out: list[str] = []
        if "$ref" in schema:
            out += self.errors(value, self._ref(schema["$ref"]), path)
        if "type" in schema:
            types = schema["type"] if isinstance(schema["type"], list) else [schema["type"]]
            if not any(_is_type(value, t) for t in types):
                out.append(f"{here}: expected type {'/'.join(types)}, got {type_name(value)}")
                return out
        if "enum" in schema and not any(json_equal(value, e) for e in schema["enum"]):
            out.append(f"{here}: {value!r} is not one of {schema['enum']!r}")
        if "const" in schema and not json_equal(value, schema["const"]):
            out.append(f"{here}: expected {schema['const']!r}, got {value!r}")
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            if "minimum" in schema and value < schema["minimum"]:
                out.append(f"{here}: {value} is below minimum {schema['minimum']}")
            if "maximum" in schema and value > schema["maximum"]:
                out.append(f"{here}: {value} is above maximum {schema['maximum']}")
            if "exclusiveMinimum" in schema and value <= schema["exclusiveMinimum"]:
                out.append(f"{here}: {value} is not above {schema['exclusiveMinimum']}")
            if "exclusiveMaximum" in schema and value >= schema["exclusiveMaximum"]:
                out.append(f"{here}: {value} is not below {schema['exclusiveMaximum']}")
            if "multipleOf" in schema:
                q = value / schema["multipleOf"]
                if not math.isclose(q, round(q)):
                    out.append(f"{here}: {value} is not a multiple of {schema['multipleOf']}")
        if isinstance(value, str):
            if "minLength" in schema and len(value) < schema["minLength"]:
                out.append(f"{here}: string shorter than {schema['minLength']}")
            if "maxLength" in schema and len(value) > schema["maxLength"]:
                out.append(f"{here}: string longer than {schema['maxLength']}")
            if "pattern" in schema and not re.search(schema["pattern"], value):
                out.append(f"{here}: {value!r} does not match /{schema['pattern']}/")
        if isinstance(value, list):
            if "minItems" in schema and len(value) < schema["minItems"]:
                out.append(f"{here}: fewer than {schema['minItems']} items")
            if "maxItems" in schema and len(value) > schema["maxItems"]:
                out.append(f"{here}: more than {schema['maxItems']} items")
            if schema.get("uniqueItems"):
                for i in range(len(value)):
                    if any(json_equal(value[i], value[j]) for j in range(i)):
                        out.append(f"{here}: item {i} is a duplicate")
            prefix = schema.get("prefixItems", [])
            for i, sub in enumerate(prefix[: len(value)]):
                out += self.errors(value[i], sub, f"{path}/{i}")
            items = schema.get("items")
            if isinstance(items, list):  # draft 4-7 tuple form
                for i, sub in enumerate(items[: len(value)]):
                    out += self.errors(value[i], sub, f"{path}/{i}")
            elif items is not None:
                for i in range(len(prefix), len(value)):
                    out += self.errors(value[i], items, f"{path}/{i}")
        if isinstance(value, dict):
            for name in schema.get("required", []):
                if name not in value:
                    out.append(f"{here}: missing required member {name!r}")
            if "minProperties" in schema and len(value) < schema["minProperties"]:
                out.append(f"{here}: fewer than {schema['minProperties']} members")
            if "maxProperties" in schema and len(value) > schema["maxProperties"]:
                out.append(f"{here}: more than {schema['maxProperties']} members")
            props = schema.get("properties", {})
            patterns = schema.get("patternProperties", {})
            for name, member in value.items():
                child = f"{path}/{name}"
                matched = False
                if name in props:
                    matched = True
                    out += self.errors(member, props[name], child)
                for pattern, sub in patterns.items():
                    if re.search(pattern, name):
                        matched = True
                        out += self.errors(member, sub, child)
                if not matched and "additionalProperties" in schema:
                    extra = schema["additionalProperties"]
                    if extra is False:
                        out.append(f"{here}: unexpected member {name!r}")
                    else:
                        out += self.errors(member, extra, child)
        for sub in schema.get("allOf", []):
            out += self.errors(value, sub, path)
        if "anyOf" in schema and not any(not self.errors(value, s, path) for s in schema["anyOf"]):
            out.append(f"{here}: matches none of anyOf")
        if "oneOf" in schema:
            n = sum(1 for s in schema["oneOf"] if not self.errors(value, s, path))
            if n != 1:
                out.append(f"{here}: matches {n} of oneOf, expected exactly 1")
        if "not" in schema and not self.errors(value, schema["not"], path):
            out.append(f"{here}: must not match the 'not' schema")
        return out


def validate(value: Any, schema: Any) -> list[str]:
    """Return a list of messages; empty means the value matches the schema."""
    return SchemaValidator(schema).errors(value)
