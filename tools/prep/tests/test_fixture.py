"""Tests for the synthetic fixture book (tools/prep/fixture, fixtures/books)."""

from __future__ import annotations

import hashlib
import io
import json
import posixpath
import re
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator, validators

from tools.prep import checkpoints as cp
from tools.prep import epubcheck
from tools.prep.fixture import generate as gen

REPO = Path(__file__).resolve().parents[3]
BOOKS = REPO / "fixtures" / "books"
GENERATOR = REPO / "tools" / "prep" / "fixture" / "generate.py"
DRAFT_SCHEMA = REPO / "tools" / "prep" / "schema" / "draft.schema.json"

EPUB_BYTES = (BOOKS / "fixture.epub").read_bytes()
DRAFT = json.loads((BOOKS / "fixture.draft.json").read_text(encoding="utf-8"))
EXPECTED = json.loads((BOOKS / "fixture.expected.json").read_text(encoding="utf-8"))

XHTML = "{http://www.w3.org/1999/xhtml}"
EPUB_TYPE = "{http://www.idpf.org/2007/ops}type"
OPF = "{http://www.idpf.org/2007/opf}"
DC = "{http://purl.org/dc/elements/1.1/}"
BLOCK_TAGS = {"p", "h1", "h2", "figure", "table", "pre"}


def epub_zip() -> zipfile.ZipFile:
    return zipfile.ZipFile(io.BytesIO(EPUB_BYTES))


def opf_info():
    zf = epub_zip()
    container = ET.fromstring(zf.read("META-INF/container.xml"))
    rootfile = container.find(".//{urn:oasis:names:tc:opendocument:xmlns:container}rootfile")
    opf_path = rootfile.get("full-path")
    base = opf_path.rsplit("/", 1)[0] + "/" if "/" in opf_path else ""
    package = ET.fromstring(zf.read(opf_path))
    manifest = {i.get("id"): i for i in package.find(f"{OPF}manifest")}
    spine = [manifest[r.get("idref")].get("href") for r in package.find(f"{OPF}spine")]
    return zf, base, package, manifest, spine


def spine_documents() -> list[tuple[str, ET.Element]]:
    zf, base, _, _, spine = opf_info()
    return [(href, ET.fromstring(zf.read(base + href))) for href in spine]


def run_generator(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(GENERATOR), *args], capture_output=True, text=True, cwd=REPO)


# --------------------------------------------------------------------------
# Determinism and --check


def test_generator_is_deterministic(tmp_path):
    for name in ("a", "b"):
        result = run_generator("--out", str(tmp_path / name))
        assert result.returncode == 0, result.stderr
    in_process = gen.generate_files()
    for name in gen.OUTPUT_NAMES:
        first = (tmp_path / "a" / name).read_bytes()
        assert first == (tmp_path / "b" / name).read_bytes(), name
        assert first == in_process[name], name


def test_check_passes_on_committed_files():
    result = run_generator("--check")
    assert result.returncode == 0, result.stdout + result.stderr
    assert "generated files are current" in result.stdout


def test_check_names_stale_and_missing_files(tmp_path):
    assert run_generator("--out", str(tmp_path)).returncode == 0
    (tmp_path / "fixture.draft.json").write_text("{}\n", encoding="utf-8")
    (tmp_path / "README.md").unlink()
    result = run_generator("--check", "--out", str(tmp_path))
    assert result.returncode == 1
    assert f"differs: {tmp_path / 'fixture.draft.json'}" in result.stdout
    assert f"missing: {tmp_path / 'README.md'}" in result.stdout
    assert "fixture.epub" not in result.stdout


# --------------------------------------------------------------------------
# EPUB structure


def test_mimetype_is_first_and_stored():
    signature, _, _, method, _, _, _, csize, usize, name_len, extra_len = struct.unpack(
        "<IHHHHHIIIHH", EPUB_BYTES[:30]
    )
    assert signature == 0x04034B50
    assert EPUB_BYTES[30 : 30 + name_len] == b"mimetype"
    assert method == 0 and extra_len == 0 and csize == usize == 20
    assert EPUB_BYTES[30 + name_len : 30 + name_len + usize] == b"application/epub+zip"
    infos = epub_zip().infolist()
    assert infos[0].filename == "mimetype"
    assert all(i.date_time == (2026, 1, 1, 0, 0, 0) for i in infos)
    assert epub_zip().testzip() is None


def test_manifest_and_spine_are_consistent():
    zf, base, package, manifest, spine = opf_info()
    assert package.get("version") == "3.0"
    hrefs = {base + item.get("href") for item in manifest.values()}
    names = set(zf.namelist()) - {"mimetype", "META-INF/container.xml", base + "content.opf"}
    assert hrefs == names, "manifest and zip contents differ"
    for idref in package.find(f"{OPF}spine"):
        assert idref.get("idref") in manifest
    assert [i for i in manifest.values() if i.get("properties") == "nav"], "no nav document"
    toc = package.find(f"{OPF}spine").get("toc")
    assert manifest[toc].get("media-type") == "application/x-dtbncx+xml"
    meta = package.find(f"{OPF}metadata")
    identifier = meta.find(f"{DC}identifier").text
    assert identifier == gen.C.IDENTIFIER
    assert meta.find(f"{DC}language").text == "en"
    assert meta.find(f"{DC}title").text == gen.C.TITLE
    assert meta.find(f"{OPF}meta[@property='dcterms:modified']").text == gen.C.MODIFIED
    ncx = ET.fromstring(zf.read(base + manifest[toc].get("href")))
    uid = ncx.find(".//{http://www.daisy.org/z3986/2005/ncx/}meta[@name='dtb:uid']")
    assert uid.get("content") == identifier
    assert spine == [d["href"] for d in EXPECTED["documents"]]


def _body_blocks(root):
    return [el for el in root.find(f"{XHTML}body") if el.tag[len(XHTML) :] in BLOCK_TAGS]


def _mid_chapter(root, element):
    blocks = list(root.find(f"{XHTML}body"))
    i = blocks.index(element)
    return blocks[i - 1].tag == f"{XHTML}p" and blocks[i + 1].tag == f"{XHTML}p"


def test_every_content_type_is_present_mid_chapter():
    zf, base, _, manifest, _ = opf_info()
    found = {"figure": 0, "table": 0, "pre": 0, "noteref": 0}
    for href, root in spine_documents()[1:]:
        body = root.find(f"{XHTML}body")
        for figure in body.findall(f"{XHTML}figure"):
            img = figure.find(f"{XHTML}img")
            assert img is not None and img.get("alt")
            assert figure.find(f"{XHTML}figcaption").text
            png = zf.read(posixpath.normpath(posixpath.join(posixpath.dirname(base + href), img.get("src"))))
            assert png.startswith(b"\x89PNG\r\n\x1a\n") and len(png) < 8192
            assert _mid_chapter(root, figure)
            found["figure"] += 1
        for table in body.findall(f"{XHTML}table"):
            rows = table.findall(f".//{XHTML}tr")
            assert 3 <= len(rows) <= 4
            assert rows[0].findall(f"{XHTML}th") and table.find(f"{XHTML}thead") is not None
            assert _mid_chapter(root, table)
            found["table"] += 1
        for pre in body.findall(f"{XHTML}pre"):
            code = pre.find(f"{XHTML}code")
            assert code is not None and 4 <= len(code.text.split("\n")) <= 6
            assert _mid_chapter(root, pre)
            found["pre"] += 1
        notes = {a.get("id"): a for a in body.findall(f"{XHTML}aside") if a.get(EPUB_TYPE) == "footnote"}
        for sup in root.iter(f"{XHTML}sup"):
            a = sup.find(f"{XHTML}a")
            assert a.get(EPUB_TYPE) == "noteref"
            assert a.get("href").startswith("#") and a.get("href")[1:] in notes
            found["noteref"] += 1
        if notes:  # footnotes sit after the last body block
            assert list(body)[-len(notes) :] == list(notes.values())
    assert found["figure"] == found["table"] == found["pre"] == 1
    assert found["noteref"] >= 2


def test_block_ids_are_numbered_in_document_order():
    for href, root in spine_documents():
        ids = [el.get("id") for el in _body_blocks(root)]
        prefix = ids[0].split("-")[0]
        assert ids == [f"{prefix}-p{n:03d}" for n in range(1, len(ids) + 1)], href


# --------------------------------------------------------------------------
# Independent recount of the words from the XHTML


def _is_noteref(el) -> bool:
    return el.tag == f"{XHTML}sup" and any(a.get(EPUB_TYPE) == "noteref" for a in el.iter(f"{XHTML}a"))


def _visible(el, marks: list[int], prefix: str = "") -> str:
    """Visible text of ``el`` without note marks; appends to ``marks`` the word count before each mark."""
    text = el.text or ""
    for child in el:
        if _is_noteref(child):
            marks.append(len((prefix + text).split()))
        else:
            text += _visible(child, marks, prefix + text)
        text += child.tail or ""
    return text


def recount(root):
    words, blocks, marks = [], [], []
    for el in _body_blocks(root):
        tag = el.tag[len(XHTML) :]
        if tag in ("p", "h1", "h2"):
            local = []
            block_words = _visible(el, local).split()
            marks += [len(words) + n - 1 for n in local]
            blocks.append((el.get("id"), "heading" if tag != "p" else "paragraph", len(words)))
            words += block_words
        else:
            blocks.append((el.get("id"), tag, len(words) - 1))
    return words, blocks, marks


def test_expected_words_match_an_independent_recount():
    docs = {d["href"]: d for d in EXPECTED["documents"]}
    for href, root in spine_documents():
        expected = docs[href]
        words, blocks, marks = recount(root)
        assert words == expected["words"], href
        assert expected["word_count"] == len(words)
        got_blocks = []
        for b in expected["blocks"]:
            kind = {"image": "figure", "table": "table", "code": "pre"}.get(b["kind"], b["kind"])
            got_blocks.append((b["id"], kind, b.get("first_word", b.get("after_word"))))
        assert got_blocks == blocks, href
        assert [n["after_word"] for n in expected["noterefs"]] == marks, href


def test_expected_non_prose_text_and_footnotes_match_the_xhtml():
    docs = {d["href"]: d for d in EXPECTED["documents"]}
    for href, root in spine_documents():
        expected = docs[href]
        by_id = {el.get("id"): el for el in _body_blocks(root)}
        for b in expected["blocks"]:
            el = by_id[b["id"]]
            if b["kind"] == "image":
                assert el.find(f"{XHTML}figcaption").text == b["caption"]
                assert el.find(f"{XHTML}img").get("alt") == b["alt"]
                src = el.find(f"{XHTML}img").get("src")  # image_href is relative to the OPF, like hrefs
                assert posixpath.normpath(posixpath.join(posixpath.dirname(href), src)) == b["image_href"]
            elif b["kind"] == "table":
                rows = [[c.text for c in tr] for tr in el.iter(f"{XHTML}tr")]
                assert rows == [b["header"], *b["rows"]]
                assert el.find(f"{XHTML}caption").text == b["caption"]
            elif b["kind"] == "code":
                assert el.find(f"{XHTML}code").text.split("\n") == b["lines"]
        asides = [a for a in root.find(f"{XHTML}body") if a.tag == f"{XHTML}aside"]
        assert [a.get("id") for a in asides] == [f["id"] for f in expected["footnotes"]]
        for aside, note in zip(asides, expected["footnotes"]):
            got = [{"id": p.get("id"), "words": "".join(p.itertext()).split()} for p in aside]
            assert got == note["paragraphs"]
        for ref in expected["noterefs"]:
            sups = [s for s in root.iter(f"{XHTML}sup") if s.find(f"{XHTML}a").get("href") == f"#{ref['note']}"]
            assert len(sups) == 1 and sups[0].find(f"{XHTML}a").text == ref["mark"]


def test_inline_elements_do_not_split_words():
    docs = {d["href"]: d for d in EXPECTED["documents"]}
    seen = set()
    for href, root in spine_documents():
        for item in docs[href]["inline_elements"]:
            el = root.find(f".//{XHTML}{item['element']}")
            assert el is not None and el.text == item["text"]
            assert docs[href]["words"][item["word_index"]].startswith(item["text"])
            seen.add(item["element"])
    assert seen == {"em", "strong"}


def test_section_word_ranges_follow_the_blocks():
    docs = {d["href"]: d for d in EXPECTED["documents"]}
    for section in EXPECTED["sections"]:
        doc = docs[section["chapter_href"]]
        blocks = {b["id"]: b for b in doc["blocks"]}
        assert section["first_word"] == blocks[section["first_paragraph"]]["first_word"]
        assert section["last_word"] == blocks[section["last_paragraph"]]["last_word"]
        assert section["word_count"] == section["last_word"] - section["first_word"] + 1
        assert doc["words"][section["first_word"]] == section["first_word_text"]
        assert doc["words"][section["last_word"]] == section["last_word_text"]
    by_chapter = {}
    for section in EXPECTED["sections"]:
        by_chapter.setdefault(section["chapter_href"], []).append(section)
    for href, sections in by_chapter.items():  # sections tile each chapter's word stream
        assert sections[0]["first_word"] == 0
        assert sections[-1]["last_word"] == docs[href]["word_count"] - 1
        for a, b in zip(sections, sections[1:]):
            assert b["first_word"] == a["last_word"] + 1


def test_chapter_sizes_follow_the_guidance():
    counts = [d["word_count"] for d in EXPECTED["documents"] if d["role"] == "chapter"]
    assert 350 <= counts[0] <= 450 and 500 <= counts[1] <= 650 and 250 <= counts[2] <= 350, counts
    sections = [len([s for s in EXPECTED["sections"] if s["chapter_href"] == d["href"]])
                for d in EXPECTED["documents"] if d["role"] == "chapter"]
    assert sections == [2, 2, 1]


def test_tokenisation_cases_are_present():
    docs = {d["href"]: d for d in EXPECTED["documents"]}
    for case in EXPECTED["token_cases"]:
        assert docs[case["href"]]["words"][case["word_index"]] == case["word"], case["case"]
    words = [w for d in EXPECTED["documents"] if d["role"] == "chapter" for w in d["words"]]
    assert any(w.endswith(".") for w in words)
    assert any(w.endswith("?") for w in words)
    assert any(w.endswith("!") for w in words)
    assert any(w.endswith(",") for w in words) and any(w.endswith(";") for w in words)
    assert any(w.startswith('"') for w in words) and any(w.endswith('."') for w in words)
    assert any("—" in w for w in words)
    assert any(re.fullmatch(r"[A-Za-z]+-[A-Za-z]+[,.;]?", w) for w in words)
    assert "3.5" in words and "Dr." in words
    assert any(len(re.sub(r"[^A-Za-z]", "", w)) >= 14 for w in words)
    assert "café" in words
    non_ascii = [b for d in EXPECTED["documents"] for b in d["blocks"] if "first_word" in b
                 and any(ord(c) > 127 for w in docs[d["href"]]["words"][b["first_word"] : b["last_word"] + 1] for c in w)]
    assert len(non_ascii) == 1
    for href, root in spine_documents():
        for el in _body_blocks(root):
            if el.tag in (f"{XHTML}p", f"{XHTML}h1", f"{XHTML}h2"):
                text = _visible(el, [])
                assert "  " not in text and text == text.strip()


# --------------------------------------------------------------------------
# The draft


def _draft_validator():
    """Draft 2020-12 with the strict integer type of tools/prep/checkpoints.py (rejects 1.0)."""
    schema = json.loads(DRAFT_SCHEMA.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    strict = Draft202012Validator.TYPE_CHECKER.redefine("integer", lambda _c, v: cp.is_json_integer(v))
    return validators.extend(Draft202012Validator, type_checker=strict)(schema)


def test_draft_matches_its_schema():
    errors = list(_draft_validator().iter_errors(DRAFT))
    assert errors == [], [e.message for e in errors]


@pytest.mark.parametrize(
    "mutate",
    [
        lambda d: d["sections"][0].update(lable="typo"),
        lambda d: d["checkpoints"][0]["prompts"][0].pop("supporting_paragraphs"),
        lambda d: d["checkpoints"][0]["prompts"][0].update(prompt="x" * 301),
        lambda d: d.update(version=1.0),
        lambda d: d["sections"].clear(),
    ],
    ids=["unknown-member", "missing-support", "prompt-too-long", "float-version", "no-sections"],
)
def test_draft_schema_rejects_bad_drafts(mutate):
    bad = json.loads(json.dumps(DRAFT))
    mutate(bad)
    assert list(_draft_validator().iter_errors(bad))


def test_draft_x_members_are_allowed():
    ok = json.loads(json.dumps(DRAFT))
    ok["x_note"] = "anything"
    ok["sections"][0]["x_comment"] = 1
    assert list(_draft_validator().iter_errors(ok)) == []


def test_draft_ranges_cover_the_body_without_overlap():
    block_ids = {href: [el.get("id") for el in _body_blocks(root)] for href, root in spine_documents()}
    owner: dict[tuple[str, str], str] = {}
    for entry in DRAFT["excluded"] + DRAFT["sections"]:
        ids = block_ids[entry["chapter_href"]]
        first, last = ids.index(entry["first_paragraph"]), ids.index(entry["last_paragraph"])
        assert first <= last
        for block in ids[first : last + 1]:
            key = (entry["chapter_href"], block)
            assert key not in owner, f"{key} in two ranges"
            owner[key] = entry.get("id", "excluded")
    assert set(owner) == {(href, b) for href, ids in block_ids.items() for b in ids}
    assert [e["chapter_href"] for e in DRAFT["excluded"]] == ["text/title.xhtml"]
    assert all(e["reason"] for e in DRAFT["excluded"])
    spine = list(block_ids)
    positions = [(spine.index(s["chapter_href"]), block_ids[s["chapter_href"]].index(s["first_paragraph"]))
                 for s in DRAFT["sections"]]
    assert positions == sorted(positions)
    assert [s["id"] for s in DRAFT["sections"]] == [f"s{n:04d}" for n in range(1, len(DRAFT["sections"]) + 1)]
    assert DRAFT["book"]["source_sha256"] == EXPECTED["epub_sha256"]
    assert hashlib.sha256(EPUB_BYTES).hexdigest() == EXPECTED["epub_sha256"]


def test_draft_checkpoints_have_the_required_shape():
    sections = {s["id"]: s for s in EXPECTED["sections"]}
    checkpoints = DRAFT["checkpoints"]
    assert sorted(len(c["prompts"]) for c in checkpoints) == [1, 3]
    single = next(c for c in checkpoints if len(c["prompts"]) == 1)
    multi = next(c for c in checkpoints if len(c["prompts"]) == 3)
    assert not sections[single["section"]]["ends_chapter"], "single-prompt checkpoint must end mid-chapter"
    assert sections[multi["section"]]["ends_chapter"], "multi-prompt checkpoint must end at a chapter end"
    assert set(sections) - {c["section"] for c in checkpoints}, "need a section without a checkpoint"
    assert len({c["section"] for c in checkpoints}) == len(checkpoints)
    assert [c["id"] for c in checkpoints] == ["c0001", "c0002"]
    block_ids = {href: [el.get("id") for el in _body_blocks(root)] for href, root in spine_documents()}
    draft_sections = {s["id"]: s for s in DRAFT["sections"]}
    for c in checkpoints:
        s = draft_sections[c["section"]]
        ids = block_ids[s["chapter_href"]]
        inside = ids[ids.index(s["first_paragraph"]) : ids.index(s["last_paragraph"]) + 1]
        for n, prompt in enumerate(c["prompts"], start=1):
            assert prompt["id"] == f"{c['id']}-p{n}"
            assert prompt["supporting_paragraphs"] and set(prompt["supporting_paragraphs"]) <= set(inside)


def test_prompts_and_answers_are_plain_text_within_limits():
    for c in DRAFT["checkpoints"]:
        for prompt in c["prompts"]:
            for key, limit in (("prompt", cp.PROMPT_MAX_CHARS), ("answer", cp.ANSWER_MAX_CHARS)):
                text = prompt[key]
                assert 1 <= len(text) <= limit, prompt["id"]
                assert cp.plain_text_problem(text) is None, prompt["id"]
                assert not cp.is_blank(text), prompt["id"]


def test_draft_and_expected_make_a_valid_checkpoint_file():
    """What `prep build` will write: must pass the existing validator."""
    by_id = {s["id"]: s for s in EXPECTED["sections"]}
    data = {
        "format": "rsvp-checkpoints",
        "version": 1,
        "book": DRAFT["book"],
        "sections": [
            {
                "id": s["id"],
                "chapter_href": s["chapter_href"],
                "start_anchor": by_id[s["id"]]["start_anchor"],
                "end_anchor": by_id[s["id"]]["end_anchor"],
                "label": s["label"],
                "word_count": by_id[s["id"]]["word_count"],
            }
            for s in DRAFT["sections"]
        ],
        "checkpoints": [
            {"id": c["id"], "section": c["section"],
             "prompts": [{k: p[k] for k in ("id", "prompt", "answer")} for p in c["prompts"]]}
            for c in DRAFT["checkpoints"]
        ],
    }
    assert cp.validate_checkpoints(data) == []


# --------------------------------------------------------------------------
# EPUBCheck


def test_epubcheck_reports_no_errors():
    try:
        report = epubcheck.run_epubcheck(BOOKS / "fixture.epub")
    except epubcheck.EpubcheckUnavailable as exc:
        pytest.skip(f"EPUBCheck unavailable, fixture NOT checked: {exc}")
    assert report.version == epubcheck.EPUBCHECK_VERSION
    assert report.errors == [], [str(m) for m in report.errors]
    assert report.warnings == [], [str(m) for m in report.warnings]


def test_epubcheck_json_parsing():
    report = epubcheck.parse_json_report(
        {
            "checker": {"checkerVersion": "5.3.0"},
            "messages": [
                {"ID": "RSC-005", "severity": "ERROR", "message": "bad",
                 "locations": [{"path": "OEBPS/a.xhtml", "line": 3, "column": 7}]},
                {"ID": "OPF-085", "severity": "WARNING", "message": "meh", "locations": [{"path": "x", "line": -1}]},
                {"ID": "PKG-999", "severity": "FATAL", "message": "dead", "locations": []},
                {"ID": "ACC-001", "severity": "USAGE", "message": "hint", "locations": []},
            ],
        },
        1,
    )
    assert report.version == "5.3.0"
    assert [m.id for m in report.errors] == ["RSC-005", "PKG-999"]
    assert [m.id for m in report.warnings] == ["OPF-085"]
    assert report.errors[0].locations == ("OEBPS/a.xhtml:3:7",)
    assert report.warnings[0].locations == ("x",)


def test_epubcheck_unavailable_without_download(tmp_path, monkeypatch):
    monkeypatch.setenv("EPUBCHECK_CACHE_DIR", str(tmp_path))
    with pytest.raises(epubcheck.EpubcheckUnavailable, match="download is disabled"):
        epubcheck.ensure_epubcheck(download=False)
