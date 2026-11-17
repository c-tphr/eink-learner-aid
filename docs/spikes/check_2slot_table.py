#!/usr/bin/env python3
"""Checks for the proposed Picco two-slot partition table (spike S3).

ESP-IDF's gen_esp32part.py already checks alignment, overlaps and the flash
size. This script adds the project-specific rules it cannot know about:

- nvs and otadata are byte-for-byte the stock rows from partitions_picco.csv;
- exactly two app partitions, ota_0 and ota_1, of equal size, no factory app;
- app offsets aligned to 0x10000, otadata 0x2000 long and 0x1000 aligned;
- nothing below 0x9000 (bootloader 0x0-0x7FFF, partition table 0x8000-0x8FFF);
- no overlaps and everything ends at or below 16 MB.

It prints the slot size and the "slot minus 10%" image budget.

Usage (from the repo root; the table in use is partitions_picco_2slot.csv, task D4; the spike's
docs/spikes/partitions_picco_2slot.proposed.csv has the same rows and is kept as the record):
    python3 docs/spikes/check_2slot_table.py partitions_picco_2slot.csv partitions_picco.csv
"""

import sys

FLASH_SIZE = 16 * 1024 * 1024
TABLE_END = 0x9000  # partition table sector 0x8000-0x8FFF
APP_ALIGN = 0x10000


def parse(path):
    rows = {}
    with open(path, encoding="utf-8") as fp:
        for line in fp:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            name, ptype, subtype, offset, size = (t.strip() for t in line.split(",")[:5])
            rows[name] = {
                "type": ptype,
                "subtype": subtype,
                "offset": int(offset, 0),
                "size": int(size, 0),
            }
    return rows


def main(proposed_path, stock_path):
    new = parse(proposed_path)
    stock = parse(stock_path)
    errors = []

    for name in ("nvs", "otadata"):
        if new.get(name) != stock.get(name):
            errors.append(f"{name} differs from stock: {new.get(name)} vs {stock.get(name)}")

    apps = {n: r for n, r in new.items() if r["type"] == "app"}
    subtypes = sorted(r["subtype"] for r in apps.values())
    if subtypes != ["ota_0", "ota_1"]:
        errors.append(f"app subtypes must be exactly ota_0 and ota_1, got {subtypes}")
    sizes = {r["size"] for r in apps.values()}
    if len(sizes) != 1:
        errors.append(f"app slots differ in size: {sorted(sizes)}")
    for n, r in apps.items():
        if r["offset"] % APP_ALIGN:
            errors.append(f"{n} offset {r['offset']:#x} not aligned to {APP_ALIGN:#x}")

    ota = new.get("otadata", {})
    if ota.get("size") != 0x2000 or ota.get("offset", 1) % 0x1000:
        errors.append(f"otadata must be 0x2000 long and sector aligned: {ota}")

    spans = sorted((r["offset"], r["offset"] + r["size"], n) for n, r in new.items())
    if spans and spans[0][0] < TABLE_END:
        errors.append(f"{spans[0][2]} starts below {TABLE_END:#x}")
    for (s0, e0, n0), (s1, e1, n1) in zip(spans, spans[1:]):
        if s1 < e0:
            errors.append(f"{n1} ({s1:#x}) overlaps {n0} (ends {e0:#x})")
    if spans and spans[-1][1] > FLASH_SIZE:
        errors.append(f"{spans[-1][2]} ends at {spans[-1][1]:#x}, past 16 MB")

    print("Offset     End        Size       Name")
    for s, e, n in spans:
        print(f"{s:#010x} {e:#010x} {e - s:#010x} {n}")

    if sizes and len(sizes) == 1:
        slot = sizes.pop()
        budget = slot * 9 // 10
        print(f"slot size: {slot} bytes ({slot:#x}, {slot / 1048576:.3f} MiB)")
        print(f"budget (slot minus 10%, rounded down): {budget} bytes ({budget:#x})")

    for e in errors:
        print("ERROR:", e)
    print("OK" if not errors else f"{len(errors)} error(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
