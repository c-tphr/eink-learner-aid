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
from tools.prep import epubcheck, refs, words
from tools.prep.build import build_file
from tools.prep.epub import load_book
from tools.prep.fixture import generate as gen

REPO = Path(__file__).resolve().parents[3]
BOOKS = REPO / "fixtures" / "books"
GENERATOR = REPO / "tools" / "prep" / "fixture" / "generate.py"
DRAFT_SCHEMA = REPO / "tools" / "prep" / "schema" / "draft.schema.json"

EPUB_BYTES = (BOOKS / "fixture.epub").read_bytes()
DRAFT = json.loads((BOOKS / "fixture.draft.json").read_text(encoding="utf-8"))
EXPECTED = json.loads((BOOKS / "fixture.expected.json").read_text(encoding="utf-8"))
EXPECTED_DOCS = {d["href"]: d for d in EXPECTED["documents"]}

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


def paragraph_refs() -> dict[str, list[str]]:
    """Per spine href, the paragraph reference of each body block, recounted from spine position and order."""
    docs = spine_documents()
    out = {}
    for spine_index, (href, root) in enumerate(docs, start=1):
        blocks = _body_blocks(root)
        out[href] = [refs.paragraph_ref(spine_index, len(docs), n, len(blocks)) for n in range(1, len(blocks) + 1)]
    return out


def _mid_chapter(root, element):
    blocks = list(root.find(f"{XHTML}body"))
    i = blocks.index(element)
    return blocks[i - 1].tag == f"{XHTML}p" and blocks[i + 1].tag == f"{XHTML}p"


def test_every_content_type_is_present_mid_chapter():
    zf, base, _, manifest, _ = opf_info()
    found = {"figure": 0, "table": 0, "pre": 0, "br": 0, "pagebreak": 0}
    forms = []
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
        found["br"] += len(list(body.iter(f"{XHTML}br")))
        for span in body.iter(f"{XHTML}span"):
            if span.get(EPUB_TYPE) == "pagebreak" and span.get("role") == "doc-pagebreak" and span.text:
                found["pagebreak"] += 1
        notes = {a.get("id"): a for a in body.findall(f"{XHTML}aside") if a.get(EPUB_TYPE) == "footnote"}
        for a in body.iter(f"{XHTML}a"):
            assert a.get("href").startswith("#") and a.get("href")[1:] in notes
            inner_sup = a.find(f"{XHTML}sup")
            parent_sup = any(a in list(s) for s in body.iter(f"{XHTML}sup"))
            if a.get(EPUB_TYPE) == "noteref":
                forms.append("noteref")
            elif parent_sup:
                forms.append("sup-link")
            elif inner_sup is not None:
                forms.append("link-sup")
        if notes:  # footnotes sit after the last body block
            assert list(body)[-len(notes) :] == list(notes.values())
    assert found == {"figure": 1, "table": 1, "pre": 1, "br": 2, "pagebreak": 1}
    assert forms == ["noteref", "sup-link", "link-sup"]


def test_block_ids_are_numbered_in_document_order():
    for href, root in spine_documents():
        ids = [el.get("id") for el in _body_blocks(root)]
        prefix = ids[0].split("-")[0]
        assert ids == [f"{prefix}-p{n:03d}" for n in range(1, len(ids) + 1)], href


# --------------------------------------------------------------------------
# Two routes to the same answer: the expected file comes from the generator's content model; here it is compared
# with the preparation tools' word pass (tools/prep/words.py) and block segmentation (tools/prep/epub.py) run on
# the XHTML in fixture.epub.


def test_expected_matches_the_word_pass_on_the_epub():
    book = load_book(BOOKS / "fixture.epub")
    for sd in book.spine:
        expected = EXPECTED_DOCS[sd.href]
        assert sd.words == expected["words"], sd.href
        assert expected["word_count"] == len(sd.words)
        assert sd.stream.flag_ranges() == expected["flags"], sd.href
        assert [{"kind": m.kind, "start": m.start, "end": m.end} for m in sd.stream.markers] == expected["markers"]
        assert [(r.mark, r.word) for r in sd.stream.noterefs] == [(n["mark"], n["word"]) for n in expected["noterefs"]]
        assert sd.stream.anchors == []  # the original has no anchors; the prepared copy's are checked below


def test_expected_blocks_match_prep_extract_segmentation():
    book = load_book(BOOKS / "fixture.epub")
    for sd in book.spine:
        expected = EXPECTED_DOCS[sd.href]["blocks"]
        got = [(b.ref, b.source_id, b.kind, b.element.local, b.start, b.word_count) for b in sd.blocks + sd.notes]
        assert got == [(b["paragraph"], b["id"], b["kind"], b["element"], b["first_word"], b["word_count"])
                       for b in expected], sd.href
        # Every word belongs to exactly one block.
        assert [w for b in sd.blocks + sd.notes for w in b.words] == sd.words


def test_expected_sections_match_prep_build(tmp_path):
    out = tmp_path / "fixture.prepared.epub"
    build_file(BOOKS / "fixture.epub", BOOKS / "fixture.draft.json", out)
    prepared = load_book(out)
    data = json.loads(prepared.archive.read("OEBPS/rsvp/checkpoints.json"))
    counts = {s["id"]: s["word_count"] for s in data["sections"]}
    for section in EXPECTED["sections"]:
        sd = prepared.doc_by_href(section["chapter_href"])
        start, end = sd.stream.anchor(section["start_anchor"]), sd.stream.anchor(section["end_anchor"])
        assert (start.index, end.index) == (section["start"], section["end"]), section["id"]
        assert not start.inside_word and not end.inside_word
        assert counts[section["id"]] == section["word_count"] == section["end"] - section["start"]


def test_generator_cross_check_catches_a_disagreement(monkeypatch):
    """A wrong rule on either route makes the generator refuse to write anything."""
    docs = gen.build_model()
    xhtml = {d["href"]: gen.render_document(d).encode("utf-8") for d in docs}
    assert gen.cross_check(docs, xhtml) == []
    # Route 1 wrong: the content model forgets that an em dash ends a word.
    monkeypatch.setattr(gen, "_DASH_PIECE", re.compile(".+"))
    problems = gen.cross_check(gen.build_model(), xhtml)
    assert "text/chapter-2.xhtml: words differ" in problems
    monkeypatch.undo()
    # Route 2 wrong: the word pass treats U+00A0 as part of a word.
    monkeypatch.setattr(words, "SEPARATORS", words.SEPARATORS - {" "})
    assert "text/chapter-1.xhtml: words differ" in gen.cross_check(docs, xhtml)
    with pytest.raises(ValueError, match="the content model and the word pass disagree"):
        gen.generate_files()


def test_expected_paragraph_refs_follow_spine_position_and_block_order():
    by_href = paragraph_refs()
    for doc in EXPECTED["documents"]:
        main = [b for b in doc["blocks"] if b["kind"] != "footnote"]
        assert [b["paragraph"] for b in main] == by_href[doc["href"]]
        notes = [b for b in doc["blocks"] if b["kind"] == "footnote"]
        prefix = by_href[doc["href"]][0].split("-")[0]
        assert [b["paragraph"] for b in notes] == [f"{prefix}-n{n:03d}" for n in range(1, len(notes) + 1)]
        assert [p for f in doc["footnotes"] for p in f["paragraphs"]] == [b["paragraph"] for b in notes]
    assert by_href["text/chapter-1.xhtml"][5] == "d02-p006"


def test_expected_non_prose_text_and_footnotes_match_the_xhtml():
    for href, root in spine_documents():
        expected = EXPECTED_DOCS[href]
        by_id = {el.get("id"): el for el in root.iter() if el.get("id")}
        for b in expected["blocks"]:
            el = by_id[b["id"]]
            block_words = expected["words"][b["first_word"] : b["first_word"] + b["word_count"]]
            if b["kind"] == "image":
                assert el.find(f"{XHTML}figcaption").text == b["caption"]
                assert el.find(f"{XHTML}img").get("alt") == b["alt"]
                src = el.find(f"{XHTML}img").get("src")  # image_href is relative to the OPF, like hrefs
                assert posixpath.normpath(posixpath.join(posixpath.dirname(href), src)) == b["image_href"]
                assert block_words == b["caption"].split()  # the caption is words, the alt text is not
                assert {"kind": "IMAGE", "start": b["first_word"], "end": b["first_word"]} in expected["markers"]
            elif b["kind"] == "table":
                rows = [[c.text for c in tr] for tr in el.iter(f"{XHTML}tr")]
                assert rows == [b["header"], *b["rows"]]
                assert el.find(f"{XHTML}caption").text == b["caption"]
                cells = [b["caption"], *(c for row in rows for c in row)]
                assert block_words == " ".join(cells).split()
                assert {"kind": "TABLE", "start": b["first_word"], "end": b["first_word"] + b["word_count"]} \
                    in expected["markers"]
            elif b["kind"] == "code":
                assert el.find(f"{XHTML}code").text.split("\n") == b["lines"]
                assert block_words == "\n".join(b["lines"]).split()
                assert {"kind": "CODE", "start": b["first_word"], "end": b["first_word"] + b["word_count"]} \
                    in expected["markers"]
            elif b["kind"] == "footnote":
                assert block_words == el.text.split()
        asides = [a for a in root.find(f"{XHTML}body") if a.tag == f"{XHTML}aside"]
        assert [a.get("id") for a in asides] == [f["id"] for f in expected["footnotes"]]
        for note in expected["footnotes"]:
            assert {"kind": "NOTE", "start": note["start"], "end": note["end"]} in expected["markers"]
        for ref in expected["noterefs"]:
            links = [a for a in root.iter(f"{XHTML}a") if a.get("href") == f"#{ref['note']}"]
            assert len(links) == 1 and "".join(links[0].itertext()) == ref["mark"]
            assert expected["words"][ref["word"]] != ref["mark"]  # the mark is not a word


def test_inline_elements_do_not_split_words():
    seen = set()
    for href, root in spine_documents():
        for item in EXPECTED_DOCS[href]["inline_elements"]:
            el = root.find(f".//{XHTML}{item['element']}")
            assert el is not None and el.text == item["text"]
            assert EXPECTED_DOCS[href]["words"][item["word"]].startswith(item["text"])
            seen.add(item["element"])
    assert seen == {"em", "strong"}


def test_section_word_ranges_follow_the_blocks():
    for section in EXPECTED["sections"]:
        doc = EXPECTED_DOCS[section["chapter_href"]]
        blocks = {b["paragraph"]: b for b in doc["blocks"]}
        first, last = blocks[section["first_paragraph"]], blocks[section["last_paragraph"]]
        assert section["start"] == first["first_word"]
        assert section["end"] == last["first_word"] + last["word_count"]
        assert first["id"] == section["first_block_id"] and last["id"] == section["last_block_id"]
        assert section["word_count"] == section["end"] - section["start"]
        assert doc["words"][section["start"]] == section["first_word_text"]
        assert doc["words"][section["end"] - 1] == section["last_word_text"]
    by_chapter = {}
    for section in EXPECTED["sections"]:
        by_chapter.setdefault(section["chapter_href"], []).append(section)
    for href, sections in by_chapter.items():  # sections tile each chapter up to its footnote bodies
        notes = [m["start"] for m in EXPECTED_DOCS[href]["markers"] if m["kind"] == "NOTE"]
        assert sections[0]["start"] == 0
        assert sections[-1]["end"] == min(notes, default=EXPECTED_DOCS[href]["word_count"])
        for a, b in zip(sections, sections[1:]):
            assert b["start"] == a["end"]


def _prose_word_count(doc) -> int:
    skipped = {i for m in doc["markers"] if m["kind"] != "IMAGE" for i in range(m["start"], m["end"])}
    return doc["word_count"] - len(skipped)


def test_chapter_sizes_follow_the_guidance():
    """Word counts outside the TABLE, CODE and NOTE regions keep the sizes the fixture was designed with."""
    counts = [_prose_word_count(d) for d in EXPECTED["documents"] if d["role"] == "chapter"]
    assert 350 <= counts[0] <= 450 and 500 <= counts[1] <= 650 and 250 <= counts[2] <= 350, counts
    sections = [len([s for s in EXPECTED["sections"] if s["chapter_href"] == d["href"]])
                for d in EXPECTED["documents"] if d["role"] == "chapter"]
    assert sections == [2, 2, 1]


def test_tokenisation_cases_are_present():
    flags_of = {}
    for doc in EXPECTED["documents"]:
        for name, runs in doc["flags"].items():
            for start, end in runs:
                for i in range(start, end):
                    flags_of.setdefault((doc["href"], i), set()).add(name)
    for case in EXPECTED["token_cases"]:
        assert EXPECTED_DOCS[case["href"]]["words"][case["word_index"]] == case["word"], case["case"]
        assert set(case["flags"]) == flags_of.get((case["href"], case["word_index"]), set()), case["case"]
    words = [w for d in EXPECTED["documents"] if d["role"] == "chapter" for w in d["words"]]
    assert any(w.endswith(".") for w in words)
    assert any(w.endswith("?") for w in words)
    assert any(w.endswith("!") for w in words)
    assert any(w.endswith(",") for w in words) and any(w.endswith(";") for w in words)
    assert any(w.startswith('"') for w in words) and any(w.endswith('."') for w in words)
    assert any(w.endswith("—") for w in words) and any(w.endswith("–") for w in words)
    assert not any(c in w for w in words for c in " ­")
    assert any(re.fullmatch(r"[A-Za-z]+-[A-Za-z]+[,.;]?", w) for w in words)
    assert "3.5" in words and "Dr." in words and "café" in words and "lighthouse" in words
    assert any(len(re.sub(r"[^A-Za-z]", "", w)) >= 14 for w in words)
    names = {name for d in EXPECTED["documents"] for name, runs in d["flags"].items() if runs}
    assert names == {"PARA_END", "LINE_END", "NONPROSE", "NOTEREF", "HEADING"}  # BYTE_SPLIT: test_words.py only
    kinds = {m["kind"] for d in EXPECTED["documents"] for m in d["markers"]}
    assert kinds == {"IMAGE", "TABLE", "CODE", "NOTE"}
    for href, root in spine_documents():
        for el in _body_blocks(root):
            if el.tag in (f"{XHTML}p", f"{XHTML}h1", f"{XHTML}h2"):
                text = el.text or ""
                assert "  " not in text and text == text.lstrip()


def test_expected_rules_version():
    assert (EXPECTED["version"], EXPECTED["rules_version"]) == (2, gen.RULES_VERSION) == (2, 1)
    assert "S1-word-stream.md section 5" in EXPECTED["rules"]["source"]
    assert "SENT_END" not in json.dumps(EXPECTED)  # sentence ends are the firmware's alone


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
    block_ids = paragraph_refs()
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
    block_ids = paragraph_refs()
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
