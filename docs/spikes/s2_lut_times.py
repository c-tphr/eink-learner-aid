#!/usr/bin/env python3
"""S2 spike helper: decode SSD1677 waveform tables and print their drive times.

Reads the LUT byte arrays straight out of the pinned FreeInk SDK headers, so the
numbers in docs/spikes/S2-waveforms.md can be reproduced:

    python3 docs/spikes/s2_lut_times.py            # SDK tables + proposed trials
    python3 docs/spikes/s2_lut_times.py --c        # also print the trial tables as C

Standard library only. Run from the repository root (or pass --root).

SSD1677 110-byte table layout (as documented in
freeink-sdk/libs/display/FreeInkDisplay/src/driver/PaperMonoDriver.cpp:108-115):
  [0..49]    5 LUT entries x 10 groups; each byte = 4 phases (A..D) x 2 bits,
             phase A in the most significant pair. Selector 00=VSS(0 V),
             01=VSH1, 10=VSL, 11=VSH2 (PaperMonoDriver.cpp:39-41).
  [50..99]   10 groups x {TP_A, TP_B, TP_C, TP_D, RP}; TP in frames, a group
             runs RP+1 times (PaperMonoDriver.cpp:112-113).
  [100..104] frame-rate codes, one nibble per group, two groups per byte.
  [105..109] VGH, VSH1, VSH2, VSL, VCOM (Ssd1677Driver.cpp:1259-1268).
Stock Picco 105-byte "image/gray" tables carry no voltage tail.

Frame-rate codes. Known from the repo:
  0x2 -> 50 Hz  (vendor comment, freeink-sdk/docs/metalio-waveform-source.md:26)
  0x4 -> 100 Hz (same file, line 27; shows the HIGH nibble is the even group)
  0x8 -> 5000 us/frame, "the fastest the controller offers"
         (PaperMonoDriver.cpp:43-45)
These fit f = 25 Hz x code. Codes 1, 3, 5-7 are extrapolated from that fit
and flagged "assumed"; codes 9-F are reported as unknown.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

LUT_DIR = Path("freeink-sdk/libs/display/FreeInkDisplay/src/lut")
HEADERS = ["Ssd1677Luts.h", "X4ProTextAaLuts.h"]
SSD1677_TABLES = [
    "lut_grayscale",
    "lut_grayscale_sticky",
    "lut_grayscale_metalio",
    "lut_factory_quality",
    "lut_factory_quality_metalio",
    "lut_fast_metalio",
    "lut_picco_fast",
    "lut_picco_depg_full",
    "lut_picco_depg_fast_slot1",
    "lut_picco_depg_custom_fast",
    "lut_picco_depg_fast",
    "lut_picco_yrd_opm_gray",
    "lut_picco_opm_image",
    "lut_picco_depg_image",
    "lut_picco_depg_gray",
    "kX4ProTextSsd1677",
]
KNOWN_FR = {2: 50.0, 4: 100.0, 8: 200.0}
SEL = {0: "0", 1: "VSH1", 2: "VSL", 3: "VSH2"}


def frame_ms(code: int) -> tuple[float | None, str]:
    if code in KNOWN_FR:
        return 1000.0 / KNOWN_FR[code], "known"
    if 1 <= code <= 8:
        return 1000.0 / (25.0 * code), "assumed"
    return None, "unknown"


def parse_tables(root: Path) -> dict[str, list[int]]:
    tables: dict[str, list[int]] = {}
    decl = re.compile(
        r"inline\s+(?:constexpr\s+)?(?:const\s+)?(?:unsigned\s+char|uint8_t)\s+(\w+)\s*\[[^\]]*\]\s*(?:PROGMEM)?\s*=\s*\{(.*?)\};",
        re.S,
    )
    for name in HEADERS:
        text = (root / LUT_DIR / name).read_text()
        text = re.sub(r"//[^\n]*", "", text)  # drop line comments
        for m in decl.finditer(text):
            tables[m.group(1)] = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", m.group(2))]
    return tables


def decode(name: str, b: list[int]) -> dict:
    groups = []
    total_frames = 0
    total_ms = 0.0
    status = "known"
    for g in range(10):
        tp = b[50 + 5 * g: 50 + 5 * g + 4]
        rp = b[50 + 5 * g + 4]
        frames = sum(tp) * (rp + 1)
        if frames == 0:
            continue
        fr_byte = b[100 + g // 2]
        code = (fr_byte >> 4) if g % 2 == 0 else (fr_byte & 0x0F)
        ms, how = frame_ms(code)
        if how == "unknown":
            status = "unknown"
        elif how == "assumed" and status == "known":
            status = "assumed"
        groups.append((g, tp, rp, frames, code, ms))
        total_frames += frames
        if ms is not None and total_ms is not None:
            total_ms += frames * ms
        else:
            total_ms = None
    # Per LUT entry: frames at each rail (for DC accounting).
    entries = []
    for e in range(5):
        rails = {"VSH1": 0, "VSL": 0, "VSH2": 0}
        for g, tp, rp, _frames, _code, _ms in groups:
            byte = b[e * 10 + g]
            for p in range(4):
                sel = (byte >> (6 - 2 * p)) & 3
                if sel:
                    rails[SEL[sel]] += tp[p] * (rp + 1)
        entries.append(rails)
    tail = b[105:110] if len(b) >= 110 else None
    return dict(name=name, length=len(b), groups=groups, frames=total_frames, ms=total_ms, status=status,
                entries=entries, tail=tail)


def fmt(d: dict) -> str:
    lines = [f"{d['name']}  ({d['length']} bytes)"]
    for g, tp, rp, frames, code, ms in d["groups"]:
        ms_txt = f"{ms:.1f} ms/frame" if ms is not None else "frame time unknown"
        lines.append(
            f"  group {g}: TP A..D = {tp[0]}+{tp[1]}+{tp[2]}+{tp[3]} = {sum(tp)}, RP={rp} -> x{rp + 1} = {frames} frames;"
            f" FR code {code:X} ({ms_txt})"
        )
    if d["ms"] is not None:
        lines.append(f"  total: {d['frames']} frames = {d['ms']:.0f} ms  [frame time {d['status']}]")
    else:
        lines.append(f"  total: {d['frames']} frames; duration unknown (FR code outside 1..8)")
    # Transition names hold for B/W differential refreshes (RED = old, BW = new,
    # 0 = black); grayscale tables use the same entries as gray-level selectors.
    names = ["L0 (old B -> new B)", "L1 (old B -> new W)", "L2 (old W -> new B)", "L3 (old W -> new W)", "L4 (VCOM)"]
    for n, rails in zip(names, d["entries"]):
        if any(rails.values()):
            net = rails["VSH1"] - rails["VSL"]
            lines.append(
                f"  {n}: VSH1 {rails['VSH1']} f, VSL {rails['VSL']} f, VSH2 {rails['VSH2']} f; net strong-rail frames {net:+d}"
            )
    if d["tail"]:
        t = d["tail"]
        lines.append(f"  voltage tail VGH/VSH1/VSH2/VSL/VCOM = {' '.join(f'{v:02X}' for v in t)}")
    return "\n".join(lines)


def trial(base: list[int], tp: tuple[int, int, int, int], tail: list[int]) -> list[int]:
    """Trial table A/B/C: the base table's VS rows L1/L2, top-up (L0) removed,
    group 0 timing replaced, FR 0x22, the panel's own stock voltage tail."""
    t = [0] * 110
    t[10] = base[10]  # L1 (black -> white): VSL in phases A and C
    t[20] = base[20]  # L2 (white -> black): VSH1 in phases A and C
    t[50:54] = list(tp)
    t[54] = 0  # RP: run once
    t[100:105] = [0x22] * 5
    t[105:110] = tail
    return t


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=".", help="repository root (default: current directory)")
    ap.add_argument("--c", action="store_true", help="print the proposed trial tables as C initializers")
    args = ap.parse_args()
    root = Path(args.root)
    tables = parse_tables(root)
    missing = [n for n in SSD1677_TABLES if n not in tables]
    if missing:
        print(f"missing tables: {missing}", file=sys.stderr)
        return 1

    print("== SSD1677 tables in the pinned SDK ==\n")
    for n in SSD1677_TABLES:
        print(fmt(decode(n, tables[n])))
        print()

    depg = tables["lut_picco_depg_custom_fast"]
    yrd = tables["lut_picco_fast"]
    # Sanity: both stock fast tables drive L1 with VSL and L2 with VSH1.
    assert depg[10] == 0x88 and depg[20] == 0x44, "DEPG custom fast VS rows changed"
    assert yrd[10] == 0x80 and yrd[20] == 0x40, "YRD/OPM fast VS rows changed"
    trials = {
        "A": (7, 1, 7, 0),
        "B": (5, 1, 5, 0),
        "C": (4, 0, 4, 0),
    }
    tails = {"DEPG0397": depg[105:110], "YRD0426_OPM040B3": yrd[105:110]}
    print("== Proposed trial tables (derived from lut_picco_depg_custom_fast) ==\n")
    out = {}
    for panel, tail in tails.items():
        for tid, tp in trials.items():
            name = f"lut_rsvp_trial_{tid.lower()}_{panel.lower()}"
            t = trial(depg, tp, tail)
            out[name] = t
            print(fmt(decode(name, t)))
            print()

    # Delivered-pace estimate: one word per update, weights averaging 1.0.
    print("== Pace ceiling, one word per update, 60 ms dwell (no target cap; a 240 WPM target caps at 240) ==")
    print("   (waveform from the tables; SPI = 2 planes x 48000 B at 20 MHz = 38.4 ms, lower bound;")
    print("    controller power-up, LUT upload and rendering are not included)\n")
    spi = 2 * 48000 * 8 / 20e6 * 1000
    rows = [
        ("Stock DEPG0397 (lut_picco_depg_custom_fast)", decode("x", depg)["ms"]),
        ("Trial A", decode("x", out["lut_rsvp_trial_a_depg0397"])["ms"]),
        ("Trial B", decode("x", out["lut_rsvp_trial_b_depg0397"])["ms"]),
        ("Trial C", decode("x", out["lut_rsvp_trial_c_depg0397"])["ms"]),
        ("Trial D DEPG0397 (lut_picco_depg_fast)", decode("x", tables["lut_picco_depg_fast"])["ms"]),
        ("Trial D YRD/OPM (lut_picco_fast, FR code 1 assumed)", decode("x", yrd)["ms"]),
    ]
    for label, wave in rows:
        no_spi = 60000 / (wave + 60)
        with_spi = 60000 / (wave + spi + 60)
        print(f"  {label:55s} waveform {wave:5.0f} ms -> {no_spi:5.0f} WPM (plan's arithmetic), "
              f"{with_spi:5.0f} WPM incl. SPI")
    print()

    if args.c:
        print("== C initializers ==\n")
        for name, t in out.items():
            print(f"inline const unsigned char {name}[110] PROGMEM = {{")
            for i in range(0, 110, 10):
                print("    " + ", ".join(f"0x{v:02X}" for v in t[i:i + 10]) + ",")
            print("};")
    return 0


if __name__ == "__main__":
    sys.exit(main())
