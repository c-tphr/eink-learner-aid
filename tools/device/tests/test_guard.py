"""The offset guard: accepts exactly the canonical plan, refuses everything else."""

import hashlib
import random

import pytest

import synth
from fakedevice import FakeChip
from piccoflash import guard, layout
from piccoflash.guard import GuardError, PlannedImage


def sha(b):
    return hashlib.sha256(b).hexdigest()


@pytest.fixture(scope="module")
def images():
    return {
        "bootloader": synth.bootloader(),
        "partition_table": layout.encode_table(layout.TWO_SLOT_TABLE),
        "boot_app0": layout.boot_app0_bytes(),
        "app": synth.app(500_000),
    }


def planned(images, **override):
    out = []
    for r in guard.FLASH_REGIONS:
        data = images[r.role]
        out.append(PlannedImage(r.role, r.file_name, r.offset, len(data), sha(data)))
    return out


def reasons(fn, *a):
    with pytest.raises(GuardError) as ei:
        fn(*a)
    return " | ".join(ei.value.reasons)


# --- accepts the canonical plan ---------------------------------------------

def test_canonical_plan_is_accepted(images):
    plan = planned(images)
    assert guard.check_plan_set(plan) == []
    for img in plan:
        approved = guard.approve_flash_write(img, images[img.role])
        assert approved.offset == img.offset and approved.data == images[img.role]
        guard.recheck(approved)


def test_policy_table_is_what_the_issue_says():
    r = guard.REGION_BY_ROLE
    assert (r["bootloader"].offset, r["bootloader"].max_len) == (0x0, 0x7FFF)
    assert (r["partition_table"].offset, r["partition_table"].end) == (0x8000, 0x9000)
    assert (r["boot_app0"].offset, r["boot_app0"].min_len, r["boot_app0"].max_len) == (0x15000, 0x2000, 0x2000)
    assert (r["app"].offset, r["app"].max_len, r["app"].end) == (0x20000, 0x7E0000, 0x800000)
    assert guard.FLASH_ORDER == ("app", "boot_app0", "partition_table", "bootloader")


# --- placement ----------------------------------------------------------------

def test_image_at_0xE000_is_refused(images):
    plan = planned(images)
    plan[2] = PlannedImage("boot_app0", "boot_app0.bin", 0xE000, 0x2000, plan[2].sha256)
    msg = " | ".join(guard.check_plan_set(plan))
    assert "0xE000" in msg and "NVS" in msg
    assert "0xE000" in reasons(guard.approve_flash_write, plan[2], images["boot_app0"])


@pytest.mark.parametrize("role,offset,length", [
    ("partition_table", 0x8000, 0x1001),  # runs one byte into 0x9000
    ("bootloader", 0x0, 0x9001),
    ("boot_app0", 0x14000, 0x2000),  # starts inside NVS
    ("boot_app0", 0x9000, 0x2000),
    ("app", 0x10000, 0x2000),  # 0x10000-0x11FFF is NVS
])
def test_anything_touching_nvs_is_refused(role, offset, length):
    errs = guard.check_extent(role, offset, length)
    assert errs
    assert any("NVS" in e for e in errs)


def test_fifth_image_is_refused(images):
    plan = planned(images) + [PlannedImage("spiffs", "spiffs.bin", 0xF50000, 0x1000, "0" * 64)]
    msg = " | ".join(guard.check_plan_set(plan))
    assert "exactly 4 images" in msg and "unknown role" in msg


def test_extra_copy_of_a_known_role_is_refused(images):
    plan = planned(images)
    plan.append(PlannedImage("app", "firmware.bin", 0x800000, 1000, "0" * 64))
    msg = " | ".join(guard.check_plan_set(plan))
    assert "duplicate image role: app" in msg


@pytest.mark.parametrize("missing", guard.ROLES)
def test_missing_image_is_refused(images, missing):
    plan = [p for p in planned(images) if p.role != missing]
    assert "missing image: %s" % missing in " | ".join(guard.check_plan_set(plan))


def test_duplicate_offsets_are_refused(images):
    plan = planned(images)
    plan[1] = PlannedImage("partition_table", "partitions.bin", 0x0, 3072, plan[1].sha256)
    msg = " | ".join(guard.check_plan_set(plan))
    assert "duplicate offset 0x0" in msg and "overlap" in msg


def test_offset_not_aligned_or_negative():
    assert guard.check_extent("app", 0x20001, 100)
    assert guard.check_extent("app", -1, 100)
    assert guard.check_extent("nonsense", 0x20000, 100)


# --- lengths --------------------------------------------------------------------

def test_app_length_limits():
    assert guard.check_extent("app", 0x20000, 0x7E0000) == []
    assert "longer than the maximum" in " ".join(guard.check_extent("app", 0x20000, 0x7E0001))


def test_bootloader_length_limits():
    assert guard.check_extent("bootloader", 0x0, 0x7FFF) == []
    assert guard.check_extent("bootloader", 0x0, 0x8000)
    assert guard.check_extent("bootloader", 0x0, 0x8001)


@pytest.mark.parametrize("n", [0x1FFF, 0x2001, 0x1000, 0])
def test_boot_data_wrong_size(n):
    assert guard.check_extent("boot_app0", 0x15000, n)
    assert guard.check_image_content("boot_app0", b"\x01" * n)


def test_partition_table_size_limit():
    assert guard.check_extent("partition_table", 0x8000, 0xC00) == []
    assert guard.check_extent("partition_table", 0x8000, 0x1001)


# --- content ----------------------------------------------------------------------

def test_file_changed_after_planning_is_refused(images):
    img = planned(images)[3]
    changed = bytearray(images["app"])
    changed[-40] ^= 1  # inside the appended SHA-256: still parses, hash of the file differs
    msg = reasons(guard.approve_flash_write, img, bytes(changed))
    assert "changed after the plan" in msg


def test_same_length_valid_but_different_file_is_refused(images):
    img = planned(images)[0]
    other = synth.bootloader(seed=99)
    assert len(other) == img.size
    assert "changed after the plan" in reasons(guard.approve_flash_write, img, other)


def test_merged_factory_image_at_0x0_is_refused(images):
    merged = images["bootloader"] + b"\xff" * (0x8000 - len(images["bootloader"])) + images["partition_table"] \
        + b"\xff" * 0x1000 + images["app"]
    img = PlannedImage("bootloader", "firmware.factory.bin", 0x0, len(merged), sha(merged))
    msg = reasons(guard.approve_flash_write, img, merged)
    assert "longer than the maximum" in msg and "not exactly one image" in msg
    plan = planned(images)
    plan[0] = img
    assert "merged image" in " | ".join(guard.check_plan_set(plan))


@pytest.mark.parametrize("table", [
    layout.encode_table(layout.STOCK_TABLE),
    layout.encode_table(layout.STOCK_TABLE, md5=False),
    layout.encode_table(layout.TWO_SLOT_TABLE[:-1]),
    layout.encode_table(layout.TWO_SLOT_TABLE, md5=False),
    layout.encode_table(list(layout.TWO_SLOT_TABLE[:2]) + [layout.Row("app0", "app", "ota_0", 0x20000, 0x7D0000)]
                        + list(layout.TWO_SLOT_TABLE[3:])),
    b"\xff" * 0xC00,
])
def test_any_table_other_than_ours_is_refused(table):
    errs = guard.check_image_content("partition_table", table)
    assert errs
    if table == layout.encode_table(layout.STOCK_TABLE):
        assert "STOCK" in errs[0]


def test_wrong_magic_bytes_are_refused(images):
    for role in ("bootloader", "app"):
        bad = bytearray(images[role])
        bad[0] = 0x00
        assert any("magic" in e for e in guard.check_image_content(role, bytes(bad)))


def test_wrong_chip_and_swapped_files_are_refused(images):
    c3 = synth.esp_image([(synth.DRAM, b"\x50" + b"\x00" * 95)], chip_id=5)
    assert any("not ESP32-S3" in e for e in guard.check_image_content("bootloader", c3))
    assert any("application description" in e for e in guard.check_image_content("app", images["bootloader"]))
    assert any("bootloader description" in e for e in guard.check_image_content("bootloader",
                                                                                 synth.app(20_000)))


def test_bootloader_header_must_be_dio_16mb_80m():
    qio = synth.esp_image([(synth.DRAM, b"\x50" + b"\x00" * 95)], mode=0)
    assert any("DIO/16MB/80m" in e for e in guard.check_image_content("bootloader", qio))
    small = synth.esp_image([(synth.DRAM, b"\x50" + b"\x00" * 95)], size_freq=0x2F)
    assert any("DIO/16MB/80m" in e for e in guard.check_image_content("bootloader", small))


def test_boot_app0_must_be_the_known_file():
    good = layout.boot_app0_bytes()
    assert guard.check_image_content("boot_app0", good) == []
    other = bytearray(good)
    other[0] = 2  # sequence 2 with a now-invalid CRC
    assert guard.check_image_content("boot_app0", bytes(other))
    assert guard.check_image_content("boot_app0", b"\xff" * 0x2000)


# --- restore ----------------------------------------------------------------------

def test_restore_accepts_exact_backup():
    data = bytes(synth.stock_flash())
    w = guard.approve_restore_write(data, len(data), sha(data))
    assert (w.offset, w.length) == (0, 16 * 1024 * 1024)
    guard.recheck(w)


def test_restore_wrong_size_or_hash_is_refused():
    data = bytes(synth.stock_flash())
    assert "16 MiB" in reasons(guard.approve_restore_write, data[:-1], len(data), sha(data[:-1]))
    assert "16 MiB" in reasons(guard.approve_restore_write, data + b"\xff", len(data), sha(data + b"\xff"))
    assert "does not match" in reasons(guard.approve_restore_write, data, len(data), sha(b"other"))
    assert "record says" in reasons(guard.approve_restore_write, data, 4 * 1024 * 1024, sha(data))
    assert "no valid SHA-256" in reasons(guard.approve_restore_write, data, len(data), "")


# --- the approval object ------------------------------------------------------------

def test_approved_write_cannot_be_forged_or_altered(images):
    with pytest.raises(GuardError):
        guard.ApprovedWrite(object(), "flash", "app", 0x20000, b"x", sha(b"x"))
    with pytest.raises(GuardError):
        guard.recheck(object())
    w = guard.approve_flash_write(planned(images)[2], images["boot_app0"])
    with pytest.raises(AttributeError):
        w.offset = 0xE000
    object.__setattr__(w, "offset", 0xE000)  # deliberate tampering
    with pytest.raises(GuardError):
        guard.recheck(w)
    w2 = guard.approve_flash_write(planned(images)[2], images["boot_app0"])
    object.__setattr__(w2, "data", b"\x00" * 0x2000)
    with pytest.raises(GuardError):
        guard.recheck(w2)


def test_fake_device_write_refuses_unapproved_bytes():
    chip = FakeChip(synth.stock_flash())
    dev = chip.factory("COM7")
    dev.connect()
    with pytest.raises(GuardError):
        dev.write(("flash", 0x0, b"\x00" * 16))
    assert chip.writes == []


# --- property: never approve a byte outside the four regions --------------------------

ALLOWED = [(0x0, 0x8000), (0x8000, 0x9000), (0x15000, 0x17000), (0x20000, 0x800000)]  # written independently
NVS = (0x9000, 0x15000)


def touched_sectors(offset, size):
    first = offset // 0x1000
    last = (offset + size - 1) // 0x1000 if size else first
    return range(first, last + 1)


def inside_allowed(offset, size):
    for s in touched_sectors(offset, size):
        a, b = s * 0x1000, s * 0x1000 + 0x1000
        if not any(lo <= a and b <= hi for lo, hi in ALLOWED):
            return False
        if a < NVS[1] and NVS[0] < b:
            return False
    return offset >= 0


def random_write(rng):
    interesting = [0x0, 0x8000, 0x9000, 0xE000, 0x10000, 0x15000, 0x17000, 0x20000, 0xE0000, 0x800000, 0xFF0000]
    mode = rng.random()
    if mode < 0.5:
        off = rng.choice(interesting) + rng.choice([0, 0, 0, 1, -1, 0x1000, -0x1000])
    elif mode < 0.8:
        off = rng.choice([0x0, 0x8000, 0x15000, 0x20000])
    else:
        off = rng.randrange(0, 0x1000000)
    size = rng.choice([
        rng.randrange(0, 0x9000), rng.randrange(0, 0x800000), rng.choice([0x7FFF, 0x8000, 0xC00, 0x1000, 0x1001,
                                                                          0x2000, 0x1FFF, 0x2001, 0x7E0000,
                                                                          0x7E0001, 0x7DFFFF, 1]),
    ])
    return off, size


def test_property_guard_never_approves_bytes_outside_the_regions():
    rng = random.Random(20261118)
    approved = 0
    for _ in range(4000):
        writes = [random_write(rng) for _ in range(rng.randint(1, 6))]
        for (off, size), role in zip(writes, guard.check_extents(writes)):
            if role is not None:
                approved += 1
                assert off >= 0 and size > 0
                assert inside_allowed(off, size), (hex(off), hex(size), role)
    assert approved > 1000  # the property is not vacuous


def test_property_random_plans_never_pass_with_a_stray_byte(images):
    rng = random.Random(7)
    passed = 0
    for _ in range(3000):
        plan = []
        for r in guard.FLASH_REGIONS:
            off, size = (r.offset, len(images[r.role])) if rng.random() < 0.8 else random_write(rng)
            plan.append(PlannedImage(r.role, r.file_name, off, size, "a" * 64))
        if rng.random() < 0.1:
            plan.append(PlannedImage(rng.choice(guard.ROLES), "firmware.bin", *random_write(rng), "a" * 64))
        if guard.check_plan_set(plan) == []:
            passed += 1
            assert len(plan) == 4
            for p in plan:
                assert inside_allowed(p.offset, p.size)
                assert p.offset == guard.REGION_BY_ROLE[p.role].offset
    assert passed > 100
