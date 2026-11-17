"""The timings-file validator and header generator (scripts/display_timings.py).

Checks that a missing file means defaults, that the committed example is valid but refused under
the real name, that every kind of malformed file is refused as a whole, and that a generated header
compiles and resolves to the file's numbers through lib/DisplayProfileCore (same precedence rules
as the firmware). Run by ctest (test/display_profile/CMakeLists.txt) or directly.
"""

from __future__ import annotations

import contextlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import display_timings as dt  # noqa: E402

EXAMPLE = ROOT / "docs/device/picco-timings.example.json"


def valid_doc() -> dict:
    return {
        "format": "crosspoint-display-timings",
        "version": 1,
        "panel": "DEPG0397",
        "controller": "SSD1677",
        "source": "rsvp-bench/results-stock.json and serial.log, device session",
        "date": "2026-12-01",
        "profiles": {
            "stock": {"wordUpdateMs": {"median": 421, "p95": 433, "max": 470}, "clearMs": {"median": 1002}},
            "trial-c": {"wordUpdateMs": {"median": 207, "p95": 215, "max": 240}, "clearMs": {"median": 998}},
        },
    }


class TempDir(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="display-timings-"))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def write(self, text: str | bytes, name: str = "picco-timings.json") -> Path:
        path = self.tmp / name
        if isinstance(text, bytes):
            path.write_bytes(text)
        else:
            path.write_text(text, encoding="utf-8")
        return path


class ExampleAndValidFiles(TempDir):
    def test_example_is_valid_but_only_as_an_example(self) -> None:
        doc = dt.load(EXAMPLE, allow_example=True)
        self.assertEqual(doc["profiles"]["stock"]["wordUpdateMs"]["median"], 542)
        self.assertEqual(doc["profiles"]["stock"]["clearMs"]["median"], 1958)
        with self.assertRaisesRegex(dt.TimingsError, "example file"):
            dt.load(EXAMPLE)
        # Copied to the real name unchanged, the build refuses it.
        project = self.tmp / "project"
        (project / "docs/device").mkdir(parents=True)
        shutil.copy(EXAMPLE, project / dt.TIMINGS_PATH)
        with self.assertRaises(dt.TimingsError):
            dt.generate(project, self.tmp / "out")

    def test_valid_file_loads(self) -> None:
        path = self.write(json.dumps(valid_doc()))
        doc = dt.load(path)
        self.assertEqual(list(doc["profiles"]), ["stock", "trial-c"])

    def test_null_p95_and_max_mean_not_measured(self) -> None:
        doc = valid_doc()
        doc["profiles"]["stock"]["wordUpdateMs"].update(p95=None, max=None)
        dt.load(self.write(json.dumps(doc)))

    def test_cli_check(self) -> None:
        good = self.write(json.dumps(valid_doc()))
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertEqual(dt.main(["check", str(good)]), 0)
            self.assertEqual(dt.main(["check", "--example", str(EXAMPLE)]), 0)
            self.assertEqual(dt.main(["check", str(EXAMPLE)]), 1)
            self.assertEqual(dt.main([]), 2)
        self.assertIn("example file", err.getvalue())


class MalformedFilesAreRefused(TempDir):
    def refused(self, text: str | bytes, pattern: str) -> None:
        path = self.write(text)
        with self.assertRaisesRegex(dt.TimingsError, pattern):
            dt.load(path)
        project = self.tmp / "project"
        (project / "docs/device").mkdir(parents=True, exist_ok=True)
        shutil.copy(path, project / dt.TIMINGS_PATH)
        out = self.tmp / "out"
        with self.assertRaises(dt.TimingsError):
            dt.generate(project, out)
        self.assertFalse((out / dt.HEADER_NAME).exists(), "a refused file must not produce a header")

    def mutated(self, change) -> str:
        doc = valid_doc()
        change(doc)
        return json.dumps(doc)

    def test_bad_json(self) -> None:
        self.refused("{", "not valid JSON")
        self.refused('{"format": "crosspoint-display-timings",}', "not valid JSON")
        self.refused("", "not valid JSON")
        self.refused(json.dumps(valid_doc()) + " x", "not valid JSON")

    def test_not_utf8(self) -> None:
        self.refused(b'{"source": "\xff"}', "not UTF-8")

    def test_duplicate_member(self) -> None:
        text = json.dumps(valid_doc()).replace('"stock": {', '"stock": {"clearMs": {"median": 5}, ', 1)
        self.refused(text, "duplicate member 'clearMs'")

    def test_fractions_nan_and_infinity(self) -> None:
        self.refused(json.dumps(valid_doc()).replace("421", "421.5"), "not a whole number")
        self.refused(json.dumps(valid_doc()).replace("421", "4.21e2"), "not a whole number")
        self.refused(json.dumps(valid_doc()).replace("421", "NaN"), "NaN is not a valid time")
        self.refused(json.dumps(valid_doc()).replace("421", "Infinity"), "Infinity is not a valid time")

    def test_negative_zero_and_absurd_values(self) -> None:
        def set_update(v):
            return lambda d: d["profiles"]["stock"]["wordUpdateMs"].update(median=v)

        self.refused(self.mutated(set_update(-5)), "below minimum")
        self.refused(self.mutated(set_update(0)), "below minimum")
        self.refused(self.mutated(set_update(5001)), "above maximum")
        self.refused(self.mutated(set_update("421")), "expected type integer")
        self.refused(self.mutated(set_update(True)), "expected type integer")
        self.refused(self.mutated(lambda d: d["profiles"]["stock"]["clearMs"].update(median=10001)), "above maximum")
        self.refused(self.mutated(lambda d: d["profiles"]["stock"]["clearMs"].update(median=-1)), "below minimum")

    def test_unknown_profile_ids_are_rejected(self) -> None:
        self.refused(self.mutated(lambda d: d["profiles"].update({"trial_a": d["profiles"]["stock"]})),
                     "unexpected member 'trial_a'")
        self.refused(self.mutated(lambda d: d["profiles"].update({"trial-e": d["profiles"]["stock"]})),
                     "unexpected member 'trial-e'")

    def test_stock_is_required(self) -> None:
        self.refused(self.mutated(lambda d: d["profiles"].pop("stock")), "missing required member 'stock'")

    def test_inconsistent_percentiles(self) -> None:
        self.refused(self.mutated(lambda d: d["profiles"]["stock"]["wordUpdateMs"].update(p95=400)),
                     "p95 400 is below the median 421")
        self.refused(self.mutated(lambda d: d["profiles"]["stock"]["wordUpdateMs"].update(max=430)),
                     "max 430 is below p95 433")

    def test_structure(self) -> None:
        self.refused(self.mutated(lambda d: d.update(format="other")), "/format")
        self.refused(self.mutated(lambda d: d.update(version=2)), "/version")
        self.refused(self.mutated(lambda d: d.update(panel="DEPG")), "/panel")
        self.refused(self.mutated(lambda d: d.update(panel="XteinkX4")), "XteinkX4 is allowed only in the example")
        self.refused(self.mutated(lambda d: d.update(date="1 Dec 2026")), "/date")
        self.refused(self.mutated(lambda d: d.update(extra=1)), "unexpected member 'extra'")
        self.refused(self.mutated(lambda d: d.update(example=True)), "example file")
        self.refused(self.mutated(lambda d: d["profiles"]["stock"].pop("clearMs")), "missing required member 'clearMs'")
        self.refused(self.mutated(lambda d: d["profiles"]["stock"]["wordUpdateMs"].pop("p95")),
                     "missing required member 'p95'")
        self.refused("[]", "expected type object")


class GeneratedHeader(TempDir):
    def test_missing_file_means_defaults(self) -> None:
        header, summary = dt.generate(self.tmp, self.tmp / "out")
        self.assertIn("absent: built-in defaults", summary)
        text = header.read_text(encoding="utf-8")
        self.assertIn("TimingFile TIMINGS_FILE{TIMINGS_PROFILES, 0,", text)

    def test_rewritten_only_when_changed(self) -> None:
        header, _ = dt.generate(self.tmp, self.tmp / "out")
        os.utime(header, (1_000_000, 1_000_000))
        dt.generate(self.tmp, self.tmp / "out")
        self.assertEqual(header.stat().st_mtime, 1_000_000)
        (self.tmp / "docs/device").mkdir(parents=True)
        (self.tmp / dt.TIMINGS_PATH).write_text(json.dumps(valid_doc()), encoding="utf-8")
        dt.generate(self.tmp, self.tmp / "out")
        self.assertNotEqual(header.stat().st_mtime, 1_000_000)

    def test_strings_are_escaped(self) -> None:
        doc = valid_doc()
        doc["source"] = 'quote " backslash \\ and é'
        text = dt.render_header(doc)
        self.assertIn(r'"quote \" backslash \\ and \303\251"', text)

    def test_header_compiles_and_resolves_to_the_file(self) -> None:
        compiler = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++")
        if not compiler:
            self.skipTest("no C++ compiler")
        (self.tmp / "docs/device").mkdir(parents=True)
        (self.tmp / dt.TIMINGS_PATH).write_text(json.dumps(valid_doc()), encoding="utf-8")
        dt.generate(self.tmp, self.tmp / "gen")
        program = self.tmp / "resolve.cpp"
        program.write_text(
            r"""
#include <cstdio>
#include "DisplayTimingsFile.generated.h"
using namespace displayprofile;
int main() {
  if (checkTimingFile(generated::TIMINGS_FILE) != FileError::NONE) return 3;
  TimingModel model;
  if (!model.resolve(generated::TIMINGS_FILE, TimingOverrides{})) return 4;
  for (const ProfileInfo& info : PROFILES) {
    const ResolvedTiming& r = model.forProfile(info.id);
    std::printf("%s %u %u %s %s\n", info.key, r.timing.updateMs, r.timing.clearMs,
                timingSourceName(r.updateSource), timingSourceName(r.clearSource));
  }
  std::printf("%s|%s|%s\n", generated::TIMINGS_FILE.panel, generated::TIMINGS_FILE.date, generated::TIMINGS_PATH);
}
""",
            encoding="utf-8")
        exe = self.tmp / "resolve"
        lib = ROOT / "lib/DisplayProfileCore"
        subprocess.run(
            [compiler, "-std=c++20", "-Wall", "-Werror", "-I", str(self.tmp / "gen"), "-I", str(lib), "-I",
             str(ROOT / "lib/RsvpCore"), str(program), str(lib / "DisplayTiming.cpp"),
             str(lib / "DisplayProfiles.cpp"), "-o", str(exe)],
            check=True)
        out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout.splitlines()
        self.assertEqual(out, [
            "stock 421 1002 file file",
            "trial-a 338 1002 defaults file",  # not in the file: default update, the file's stock clear
            "trial-b 258 1002 defaults file",
            "trial-c 207 998 file file",
            "trial-d 198 1002 defaults file",
            "DEPG0397|2026-12-01|docs/device/picco-timings.json",
        ])


class AgreesWithTheFirmware(unittest.TestCase):
    def test_profile_ids_match_the_profile_table(self) -> None:
        text = (ROOT / "lib/DisplayProfileCore/DisplayProfiles.h").read_text(encoding="utf-8")
        table = re.findall(r'\{ProfileId::(\w+), "([a-z-]+)"', text)
        self.assertEqual([(enum, key) for key, enum in dt.PROFILE_IDS.items()], table)
        schema = json.loads((ROOT / dt.SCHEMA_PATH).read_text(encoding="utf-8"))
        self.assertEqual(sorted(schema["properties"]["profiles"]["properties"]), sorted(dt.PROFILE_IDS))

    def test_schema_limits_match_the_firmware_check(self) -> None:
        text = (ROOT / "lib/DisplayProfileCore/DisplayTiming.h").read_text(encoding="utf-8")
        limits = dict(re.findall(r"constexpr uint32_t (MAX_FILE_\w+_MS) = (\d+);", text))
        schema = json.loads((ROOT / dt.SCHEMA_PATH).read_text(encoding="utf-8"))
        profile = schema["$defs"]["profile"]["properties"]
        self.assertEqual(profile["wordUpdateMs"]["properties"]["median"]["maximum"], int(limits["MAX_FILE_UPDATE_MS"]))
        self.assertEqual(profile["clearMs"]["properties"]["median"]["maximum"], int(limits["MAX_FILE_CLEAR_MS"]))
        self.assertEqual(profile["wordUpdateMs"]["properties"]["median"]["minimum"], 1)
        self.assertEqual(profile["clearMs"]["properties"]["median"]["minimum"], 1)

    def test_no_real_timings_file_with_invented_numbers(self) -> None:
        # Committed only after the device session; until then the simulator runs on its defaults.
        path = ROOT / dt.TIMINGS_PATH
        if path.exists():
            doc = dt.load(path)
            self.assertNotIn(doc["panel"], ("XteinkX4",))


if __name__ == "__main__":
    unittest.main(verbosity=2)
