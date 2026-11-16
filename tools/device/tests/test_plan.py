"""make-plan, plan verification, the JSON Schema."""

import copy
import json
import os

import jsonschema
import pytest

import synth
from helpers import make_kit, run_cli
from piccoflash import common, layout, plan as planmod

SCHEMA = os.path.join(os.path.dirname(__file__), "..", "schema", "flash-plan.schema.json")


@pytest.fixture
def kit(tmp_path):
    return make_kit(str(tmp_path))


def load_doc(kit):
    with open(os.path.join(kit, "flash-plan.json")) as fp:
        return json.load(fp)


def save_doc(kit, doc):
    with open(os.path.join(kit, "flash-plan.json"), "w") as fp:
        json.dump(doc, fp)


def test_make_plan_writes_a_verified_plan(kit):
    assert sorted(os.listdir(kit)) == ["boot_app0.bin", "bootloader.bin", "firmware.bin", "flash-plan.json",
                                       "partitions.bin"]
    doc = load_doc(kit)
    assert [(i["role"], i["offset"]) for i in doc["images"]] == [
        ("bootloader", "0x0"), ("partition_table", "0x8000"), ("boot_app0", "0x15000"), ("app", "0x20000")]
    assert doc["partition_table"] == [r.as_dict() for r in layout.TWO_SLOT_TABLE]
    planmod.load_and_verify(kit)


def test_plan_command_prints_the_table(kit):
    code, out = run_cli(["plan", "--plan", kit])
    assert code == common.OK, out
    for needle in ("0x00000000", "0x00008000", "0x00015000", "0x00020000", "bootloader.bin", "partitions.bin",
                   "boot_app0.bin", "firmware.bin", "Plan verified", "0d3fad517e642121", "f94c5d786a7a8fab"):
        assert needle in out


def test_generated_plan_matches_schema(kit):
    with open(SCHEMA) as fp:
        schema = json.load(fp)
    jsonschema.Draft202012Validator.check_schema(schema)
    jsonschema.validate(load_doc(kit), schema)


def _mutations(doc):
    def m(fn):
        d = copy.deepcopy(doc)
        fn(d)
        return d

    yield "extra key", m(lambda d: d.update(force=True))
    yield "wrong format", m(lambda d: d.update(format="other"))
    yield "version true", m(lambda d: d.update(version=True))
    yield "three images", m(lambda d: d["images"].pop())
    yield "five images", m(lambda d: d["images"].append(dict(d["images"][0])))
    yield "path in file", m(lambda d: d["images"][0].update(file="../bootloader.bin"))
    yield "lower-case hex", m(lambda d: d["images"][0].update(offset="0x0e000"))
    yield "decimal offset", m(lambda d: d["images"][0].update(offset=57344))
    yield "short sha", m(lambda d: d["images"][0].update(sha256="abc"))
    yield "unknown role", m(lambda d: d["images"][0].update(role="spiffs"))
    yield "extra image key", m(lambda d: d["images"][0].update(force=True))
    yield "bad version label", m(lambda d: d.update(firmware_version="1 0"))
    yield "no tools", m(lambda d: d.pop("tools"))


def test_schema_and_builtin_validator_agree(kit):
    with open(SCHEMA) as fp:
        schema = json.load(fp)
    doc = load_doc(kit)
    assert planmod.validate_document(doc) == []
    for name, bad in _mutations(doc):
        schema_ok = jsonschema.Draft202012Validator(schema).is_valid(bad)
        ours_ok = planmod.validate_document(bad) == []
        assert not schema_ok and not ours_ok, name


def test_altered_offset_in_plan_is_refused(kit):
    doc = load_doc(kit)
    doc["images"][2]["offset"] = "0xE000"
    save_doc(kit, doc)
    code, out = run_cli(["plan", "--plan", kit])
    assert code == common.STOP
    assert "0xE000" in out and "NVS" in out


def test_altered_hash_in_plan_is_refused(kit):
    doc = load_doc(kit)
    doc["images"][3]["sha256"] = "0" * 64
    save_doc(kit, doc)
    code, out = run_cli(["plan", "--plan", kit])
    assert code == common.STOP and "changed after the plan" in out


def test_altered_file_after_make_plan_is_refused(kit):
    p = os.path.join(kit, "firmware.bin")
    data = bytearray(open(p, "rb").read())
    data[-1] ^= 1
    open(p, "wb").write(bytes(data))
    code, out = run_cli(["plan", "--plan", kit])
    assert code == common.STOP and "app" in out


def test_altered_partition_rows_in_plan_are_refused(kit):
    doc = load_doc(kit)
    doc["partition_table"][2]["size"] = "0x7F0000"
    save_doc(kit, doc)
    code, out = run_cli(["plan", "--plan", kit])
    assert code == common.STOP and "not the two-slot table" in out


def test_missing_plan(tmp_path):
    code, out = run_cli(["plan", "--plan", str(tmp_path)])
    assert code == common.STOP and "no flash plan" in out


def _make(tmp_path, sources, *extra):
    out = str(tmp_path / "kit")
    code, text = run_cli(["make-plan", "--bootloader", sources["bootloader"], "--partitions",
                          sources["partition_table"], "--boot-app0", sources["boot_app0"], "--app", sources["app"],
                          "--version", "0.1.0", "-o", out, *extra])
    return code, text, out


def test_make_plan_refuses_wrong_expected_offset(tmp_path):
    src = synth.kit_sources(str(tmp_path / "build"))
    code, text, out = _make(tmp_path, src, "--expect-offset", "boot_app0=0xE000")
    assert code == common.STOP
    assert "disagrees with the only allowed offset 0x15000" in text and "NVS" in text
    assert not os.path.exists(out)


def test_make_plan_accepts_matching_expected_offsets(tmp_path):
    src = synth.kit_sources(str(tmp_path / "build"))
    code, text, out = _make(tmp_path, src, "--expect-offset", "boot_app0=0x15000", "--expect-offset", "app=0x20000")
    assert code == common.OK, text


@pytest.mark.parametrize("role,mutate", [
    ("boot_app0", lambda b: b[:100] + bytes([b[100] ^ 1]) + b[101:]),
    ("partition_table", lambda b: layout.encode_table(layout.STOCK_TABLE)),
    ("app", lambda b: b[:-1]),
    ("bootloader", lambda b: synth.app(30_000)),
])
def test_make_plan_refuses_altered_or_wrong_files(tmp_path, role, mutate):
    src = synth.kit_sources(str(tmp_path / "build"))
    with open(src[role], "rb") as fp:
        data = fp.read()
    with open(src[role], "wb") as fp:
        fp.write(mutate(data))
    code, text, out = _make(tmp_path, src)
    assert code == common.STOP, text
    assert "REFUSED: no plan was written" in text
    assert not os.path.exists(out)


def test_make_plan_refuses_factory_named_source(tmp_path):
    src = synth.kit_sources(str(tmp_path / "build"))
    merged = str(tmp_path / "build" / "firmware.factory.bin")
    os.rename(src["app"], merged)
    src["app"] = merged
    code, text, out = _make(tmp_path, src)
    assert code == common.STOP and "merged image" in text


def test_make_plan_refuses_nonempty_output(tmp_path):
    src = synth.kit_sources(str(tmp_path / "build"))
    (tmp_path / "kit").mkdir()
    (tmp_path / "kit" / "old.txt").write_text("x")
    code, text, out = _make(tmp_path, src)
    assert code == common.STOP and "must not exist or be empty" in text
    assert os.listdir(out) == ["old.txt"]
