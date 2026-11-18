"""The ESP image parser, cross-checked against esptool's own (offline) parser."""

import hashlib

import esptool
import pytest
from esptool.bin_image import LoadFirmwareImage
from esptool.cmds import merge_bin

import synth
from piccoflash import PINNED_ESPTOOL_VERSION, espimage, guard, layout


def test_pinned_esptool_is_the_one_under_test():
    assert esptool.__version__ == PINNED_ESPTOOL_VERSION


@pytest.mark.parametrize("make", [synth.bootloader, lambda: synth.app(300_000)])
def test_synthetic_images_are_valid_for_esptool_and_for_us(make):
    data = make()
    img = LoadFirmwareImage("esp32s3", data)
    img.verify()  # esptool checks the checksum and the appended SHA-256
    assert img.chip_id == 9 and img.stored_digest == img.calc_digest
    info = espimage.parse_image(data)
    assert info.ok, info.errors
    assert info.image_length == len(data) == img.data_length + 32
    assert [length for _, length in info.segments] == [len(s.data) for s in img.segments]
    assert info.chip_id == 9 and info.hash_appended


def test_header_fields():
    info = espimage.parse_image(synth.bootloader())
    assert (info.flash_mode, info.flash_size, info.flash_freq) == ("DIO", "16MB", "80m")


def test_wrong_magic():
    data = bytearray(synth.bootloader())
    data[0] = 0xEA
    info = espimage.parse_image(bytes(data))
    assert not info.magic_ok and not info.ok


def test_truncated_image():
    data = synth.app(200_000)
    assert not espimage.parse_image(data[:-1]).ok
    assert not espimage.parse_image(data[:100]).ok
    assert not espimage.parse_image(data[:10]).ok


def test_corrupted_byte_fails_checksum_and_hash():
    data = bytearray(synth.app(200_000))
    data[5000] ^= 0x10
    info = espimage.parse_image(bytes(data))
    assert info.checksum_ok is False and info.sha256_ok is False


def test_trailing_data_is_detected_as_length_mismatch():
    data = synth.app(200_000) + b"\xff" * 16
    info = espimage.parse_image(data)
    assert info.ok and info.image_length == len(data) - 16


def test_merged_factory_image_made_by_esptool_is_not_one_image():
    # firmware.factory.bin is esptool merge-bin of the framework's images, with
    # boot_app0 at the framework default 0xE000 (spike S3, "Do not use firmware.factory.bin").
    boot, app = synth.bootloader(), synth.app(200_000)
    merged = merge_bin([(0x0, boot), (0x8000, layout.encode_table(layout.TWO_SLOT_TABLE)),
                        (0xE000, layout.boot_app0_bytes()), (0x10000, app)], "esp32s3")
    assert merged[:len(boot)] == boot and len(merged) == 0x10000 + len(app)
    info = espimage.parse_image(merged)
    assert info.image_length == len(boot) != len(merged)
    for role in ("bootloader", "app"):
        errs = guard.check_image_content(role, merged)
        assert any("not exactly one image" in e for e in errs) or any("application description" in e for e in errs)
    assert guard.check_extent("bootloader", 0, len(merged))


def test_signature_block_detection():
    boot = synth.bootloader()
    region = bytearray(b"\xff" * 0x8000)
    region[:len(boot)] = boot
    assert not espimage.signature_block_after(bytes(region), len(boot))
    sig_at = (len(boot) + 0xFFF) & ~0xFFF
    region[sig_at:sig_at + 2] = b"\xe7\x02"
    assert espimage.signature_block_after(bytes(region), len(boot))


def test_xor_all_matches_naive():
    data = hashlib.sha256(b"x").digest() * 1000
    naive = 0
    for b in data:
        naive ^= b
    assert espimage.xor_all(data) == naive
