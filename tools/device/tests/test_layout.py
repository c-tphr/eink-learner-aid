"""Layout facts against the repo's own files and spike S3's measurements."""

import hashlib
import os

import pytest

from piccoflash import layout

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def read(path):
    with open(os.path.join(REPO, path), encoding="utf-8") as fp:
        return fp.read()


def test_stock_table_equals_partitions_picco_csv():
    assert tuple(layout.parse_csv(read("partitions_picco.csv"))) == layout.STOCK_TABLE


def test_two_slot_table_equals_spike_csv():
    assert tuple(layout.parse_csv(read("docs/spikes/partitions_picco_2slot.proposed.csv"))) == layout.TWO_SLOT_TABLE


def test_two_slot_table_equals_d4_csv_when_present():
    # D4 moves the proposed CSV to the repo root. Until then the spike copy is the reference.
    path = os.path.join(REPO, "partitions_picco_2slot.csv")
    expected = layout.TWO_SLOT_TABLE
    actual = tuple(layout.parse_csv(read("partitions_picco_2slot.csv"))) if os.path.exists(path) else expected
    assert actual == expected


def test_two_slot_binary_matches_gen_esp32part_output_measured_in_s3():
    blob = layout.encode_table(layout.TWO_SLOT_TABLE)
    assert len(blob) == 0xC00
    assert hashlib.sha256(blob).hexdigest() == layout.TWO_SLOT_TABLE_SHA256


def test_boot_app0_matches_arduino_file_measured_in_s3():
    data = layout.boot_app0_bytes()
    assert len(data) == 8192
    assert hashlib.sha256(data).hexdigest() == layout.BOOT_APP0_SHA256
    recs = layout.parse_otadata(data)
    assert recs[0].seq == 1 and recs[0].crc_ok and recs[0].state_name == "UNDEFINED"
    # Spike S3 calls record 1 "invalid CRC"; in fact crc32_le(UINT32_MAX, 0) is 0xFFFFFFFF, so the
    # record is CRC-valid with sequence 0. It still loses to record 0 (higher sequence wins).
    assert recs[1].seq == 0 and recs[1].crc == 0xFFFFFFFF and recs[1].crc_ok
    assert layout.otadata_boot_slot(data, 2) == 0


@pytest.mark.parametrize("rows", [layout.STOCK_TABLE, layout.TWO_SLOT_TABLE])
@pytest.mark.parametrize("md5", [True, False])
def test_table_round_trip(rows, md5):
    parsed = layout.parse_table(layout.encode_table(rows, md5=md5))
    assert parsed.ok, parsed.errors
    assert tuple(parsed.rows) == rows
    assert parsed.md5_present == md5


def test_classify():
    assert layout.classify_table(layout.STOCK_TABLE) == "stock"
    assert layout.classify_table(layout.TWO_SLOT_TABLE) == "two-slot"
    assert layout.classify_table(layout.STOCK_TABLE[:-1]) == "other"


def test_md5_corruption_is_reported():
    blob = bytearray(layout.encode_table(layout.STOCK_TABLE))
    blob[0x24] ^= 1  # inside the second entry
    parsed = layout.parse_table(bytes(blob))
    assert not parsed.ok
    assert any("MD5" in e for e in parsed.errors)


def test_garbage_after_table_is_reported():
    blob = bytearray(layout.encode_table(layout.STOCK_TABLE))
    blob[0xBFF] = 0
    assert any("after the end" in e for e in layout.parse_table(bytes(blob)).errors)


def test_bad_entry_magic_and_empty_table():
    blob = bytearray(layout.encode_table(layout.STOCK_TABLE))
    blob[0] = 0x55
    assert not layout.parse_table(bytes(blob)).ok
    assert not layout.parse_table(b"\xff" * 0xC00).ok
    assert not layout.parse_table(b"").ok


def test_diff_rows_names_the_row():
    changed = list(layout.STOCK_TABLE)
    changed[0] = layout.Row("nvs", "data", "nvs", 0x9000, 0x5000)
    diffs = layout.diff_rows(changed, layout.STOCK_TABLE)
    assert len(diffs) == 1 and diffs[0].startswith("row 1")


def test_spike_numbers():
    slots = [r for r in layout.TWO_SLOT_TABLE if r.type == "app"]
    assert [r.size for r in slots] == [0x7E0000, 0x7E0000]
    assert 0x7E0000 * 9 // 10 == 7431782
    assert layout.TWO_SLOT_TABLE[0] == layout.STOCK_TABLE[0]  # nvs unchanged
    assert layout.TWO_SLOT_TABLE[1] == layout.STOCK_TABLE[1]  # otadata unchanged
