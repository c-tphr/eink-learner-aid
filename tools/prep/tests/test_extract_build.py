"""Tests for ``prep extract`` and ``prep build`` (tools/prep/extract.py, build.py)."""

from __future__ import annotations

import hashlib
import json
import os
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

import pytest

from tools.prep import build as build_module
from tools.prep import checkpoints as cp
from tools.prep import epub as epub_module
from tools.prep import epubcheck, words
from tools.prep.build import build_file
from tools.prep.epub import load_book, load_book_bytes, all_text
from tools.prep.epubzip import Archive
from tools.prep.fixture import generate as gen
from tools.prep.tests.bookmaker import make_draft, make_epub, xhtml

REPO = Path(__file__).resolve().parents[3]
BOOKS = REPO / "fixtures" / "books"
FIXTURE = BOOKS / "fixture.epub"
DRAFT = BOOKS / "fixture.draft.json"
EXPECTED = json.loads((BOOKS / "fixture.expected.json").read_text(encoding="utf-8"))
EXPECTED_DOCS = {d["href"]: d for d in EXPECTED["documents"]}


def run_prep(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, "-m", "tools.prep", *map(str, args)], capture_output=True, text=True,
                          cwd=REPO)


@pytest.fixture(scope="module")
def prepared(tmp_path_factory) -> Path:
    out = tmp_path_factory.mktemp("build") / "fixture.prepared.epub"
    result = run_prep("build", FIXTURE, DRAFT, "-o", out)
    assert result.returncode == 0, result.stderr
    return out


@pytest.fixture(scope="module")
def workdir(tmp_path_factory) -> Path:
    out = tmp_path_factory.mktemp("extract") / "work"
    result = run_prep("extract", FIXTURE, out)
    assert result.returncode == 0, result.stderr
    return out


# --------------------------------------------------------------------------
# Shared word definition


def test_one_word_pass():
    """extract, build and the generator's self-check all use tools/prep/words.py (the rules: test_words.py)."""
    assert epub_module.word_stream is words.word_stream
    assert build_module.word_stream is words.word_stream
    assert gen.word_pass is words
    assert words.tokenize("a\u00a0b  c\td\u2014e\r\nf") == ["a", "b", "c", "d\u2014", "e", "f"]


# --------------------------------------------------------------------------
# extract


def test_extract_writes_the_work_folder(workdir):
    names = sorted(str(p.relative_to(workdir)) for p in workdir.rglob("*") if p.is_file())
    assert names == ["source.json", "structure.json", "text/d01-title.txt", "text/d02-chapter-1.txt",
                     "text/d03-chapter-2.txt", "text/d04-chapter-3.txt"]
    source = json.loads((workdir / "source.json").read_text(encoding="utf-8"))
    assert source["source_file"] == "fixture.epub"
    assert source["source_sha256"] == hashlib.sha256(FIXTURE.read_bytes()).hexdigest() == EXPECTED["epub_sha256"]
    assert (source["title"], source["language"], source["opf_path"]) == (gen.C.TITLE, "en", "OEBPS/content.opf")
    assert [s["href"] for s in source["spine"]] == [d["href"] for d in EXPECTED["documents"]]
    assert all(s["linear"] and s["media_type"] == "application/xhtml+xml" for s in source["spine"])
    assert [s["text_file"] for s in source["spine"]] == names[2:]
    assert [(t["label"], t["href"], t["depth"], t["paragraph"]) for t in source["toc"]] == [
        ("The Lamp Room", "text/chapter-1.xhtml", 1, "d02-p001"),
        ("The Fog Season", "text/chapter-2.xhtml", 1, "d03-p001"),
        ("The Keeper's Log", "text/chapter-3.xhtml", 1, "d04-p001"),
    ]


def test_extract_is_deterministic(workdir, tmp_path):
    again = tmp_path / "again"
    assert run_prep("extract", FIXTURE, again).returncode == 0
    for path in workdir.rglob("*"):
        if path.is_file():
            assert (again / path.relative_to(workdir)).read_bytes() == path.read_bytes(), path
    # Re-extracting into the same folder replaces the files and keeps others (a draft, say).
    (again / "draft.json").write_text("{}", encoding="utf-8")
    assert run_prep("extract", FIXTURE, again).returncode == 0
    assert (again / "draft.json").is_file()


def test_extract_structure_matches_expected(workdir):
    structure = json.loads((workdir / "structure.json").read_text(encoding="utf-8"))
    assert (structure["version"], structure["source_sha256"]) == (2, EXPECTED["epub_sha256"])
    for doc in structure["documents"]:
        exp = EXPECTED_DOCS[doc["href"]]
        assert doc["word_count"] == exp["word_count"]
        got = [(b["ref"], b["kind"], b["source_id"], b["first_word"], b["word_count"])
               for b in doc["blocks"] + doc["footnotes"]]
        want = [(b["paragraph"], b["kind"], b["id"], b["first_word"], b["word_count"]) for b in exp["blocks"]]
        assert got == want, doc["href"]
        assert [n["note_id"] for n in doc["footnotes"]] == [b["note"] for b in exp["blocks"] if b["kind"] == "footnote"]
        for b in doc["blocks"]:
            assert b["path"].startswith("/html[1]/body[1]/") and b["line"] > 0
            assert b["anchor_placement"] == ("inside" if b["kind"] in ("heading", "paragraph") else "around")


def test_extract_text_files_number_every_block(workdir):
    for doc in EXPECTED["documents"]:
        prefix = doc["blocks"][0]["paragraph"].split("-")[0]
        text = next((workdir / "text").glob(f"{prefix}-*.txt")).read_text(encoding="utf-8")
        entries = [chunk.strip("\n").split("\n") for chunk in text.split("\n\n") if chunk.startswith("[")]
        assert all(len(e) == 2 for e in entries)
        main = [e for e in entries if "-p" in e[0].split("]")[0]]
        blocks = [b for b in doc["blocks"] if b["kind"] != "footnote"]
        assert [e[0].split("]")[0][1:] for e in main] == [b["paragraph"] for b in blocks]
        for (header, line), block in zip(main, blocks):
            assert block["kind"] in header and f"id {block['id']}" in header
            assert f"{block['word_count']} words" in header
            if block["kind"] in ("heading", "paragraph"):
                expected_words = doc["words"][block["first_word"] : block["first_word"] + block["word_count"]]
                shown = [w.split("[^")[0] for w in line.split(" ")]
                assert shown == expected_words, block["paragraph"]
            else:
                assert line.startswith(f"[{ {'image': 'Image', 'table': 'Table', 'code': 'Code'}[block['kind']] }: ")
        notes = [e for e in entries if "-n" in e[0].split("]")[0]]
        assert [n[1].split(" ") for n in notes] == [doc["words"][b["first_word"] : b["first_word"] + b["word_count"]]
                                                    for b in doc["blocks"] if b["kind"] == "footnote"]
    sample = (workdir / "text" / "d02-chapter-1.txt").read_text(encoding="utf-8")
    assert "[d02-p004] image, 10 words, id c1-p004\n[Image: <figure>; alt " in sample
    assert "never stood beside one.[^1]\n" in sample
    chapter_2 = (workdir / "text" / "d03-chapter-2.txt").read_text(encoding="utf-8")
    assert " disturbed.[^2]\n" in chapter_2 and " engine.[^3] Then " in chapter_2


def test_extract_refuses_a_foreign_folder(tmp_path):
    (tmp_path / "notes.txt").write_text("mine", encoding="utf-8")
    result = run_prep("extract", FIXTURE, tmp_path)
    assert result.returncode == 1
    assert "not empty" in result.stderr
    assert (tmp_path / "notes.txt").read_text(encoding="utf-8") == "mine"


def test_fixture_draft_cites_extract_references(workdir):
    structure = json.loads((workdir / "structure.json").read_text(encoding="utf-8"))
    known = {b["ref"]: d["href"] for d in structure["documents"] for b in d["blocks"]}
    draft = json.loads(DRAFT.read_text(encoding="utf-8"))
    for entry in draft["excluded"] + draft["sections"]:
        assert known[entry["first_paragraph"]] == known[entry["last_paragraph"]] == entry["chapter_href"]
    for c in draft["checkpoints"]:
        for p in c["prompts"]:
            assert all(r in known for r in p["supporting_paragraphs"])


# --------------------------------------------------------------------------
# build: the fixture


def _anchor_spans(data: bytes) -> list[bytes]:
    out = []
    pos = data.find(b'<span id="rsvp-')
    while pos >= 0:
        end = data.index(b"</span>", pos) + len(b"</span>")
        out.append(data[pos:end])
        pos = data.find(b'<span id="rsvp-', end)
    return out


def test_build_adds_every_anchor_once_in_order(prepared):
    original, after = load_book(FIXTURE), load_book(prepared)
    for section in EXPECTED["sections"]:
        sd = after.doc_by_href(section["chapter_href"])
        for other in after.spine:
            data = other.doc.data
            for anchor in (section["start_anchor"], section["end_anchor"]):
                count = data.count(f'id="{anchor}"'.encode())
                assert count == (1 if other is sd else 0), (anchor, other.href)
    for sd in after.spine:
        ids = [s.split(b'"')[1].decode() for s in _anchor_spans(sd.doc.data)]
        want = [a for s in EXPECTED["sections"] if s["chapter_href"] == sd.href
                for a in (s["start_anchor"], s["end_anchor"])]
        assert ids == want, sd.href
        assert original.doc_by_href(sd.href).doc.ids.keys() <= sd.doc.ids.keys()


def test_anchors_land_on_the_expected_word_positions(prepared):
    """The firmware's rule: an anchor's word index is the number of words started before it (section 5)."""
    book = load_book(prepared)
    for section in EXPECTED["sections"]:
        sd = book.doc_by_href(section["chapter_href"])
        assert sd.words == EXPECTED_DOCS[sd.href]["words"]
        for anchor_id, index in ((section["start_anchor"], section["start"]), (section["end_anchor"], section["end"])):
            element = sd.doc.ids[anchor_id]
            assert element.local == "span" and element.children == [] and not element.attrib.keys() - {"id"}
            anchor = sd.stream.anchor(anchor_id)
            assert (anchor.index, anchor.inside_word) == (index, False), anchor_id
        assert section["word_count"] == section["end"] - section["start"]
        assert sd.doc.ids[section["start_anchor"]].order < sd.doc.ids[section["end_anchor"]].order


def test_word_sequences_are_unchanged(prepared):
    before, after = load_book(FIXTURE), load_book(prepared)
    for a, b in zip(before.spine, after.spine, strict=True):
        assert a.words == b.words == EXPECTED_DOCS[a.href]["words"]
        assert a.stream.words == b.stream.words and a.stream.markers == b.stream.markers
        assert words.tokenize(all_text(a.doc.root)) == words.tokenize(all_text(b.doc.root))
        assert [(x.ref, x.kind, x.words) for x in a.blocks + a.notes] == [(x.ref, x.kind, x.words)
                                                                           for x in b.blocks + b.notes]


def _local_record(archive: Archive, name: str) -> bytes:
    e = archive.by_name[name]
    return archive.data[e.local_offset : e.end_offset]


def _central_without_offset(archive: Archive, name: str) -> bytes:
    rec = archive.by_name[name].central_record
    return rec[:42] + rec[46:]


def test_only_touched_members_differ(prepared):
    a, b = Archive(FIXTURE.read_bytes()), Archive(prepared.read_bytes())
    touched = {"OEBPS/content.opf", "OEBPS/text/chapter-1.xhtml", "OEBPS/text/chapter-2.xhtml",
               "OEBPS/text/chapter-3.xhtml"}
    assert b.names() == a.names() + ["OEBPS/rsvp/checkpoints.json"]
    for name in a.names():
        if name in touched:
            assert a.read(name) != b.read(name), name
            ea, eb = a.by_name[name], b.by_name[name]
            assert (ea.method, ea.dos_time, ea.dos_date) == (eb.method, eb.dos_time, eb.dos_date)
        else:
            assert _local_record(a, name) == _local_record(b, name), name
            assert _central_without_offset(a, name) == _central_without_offset(b, name), name
    assert b.data[:30 + 8 + 20] == a.data[:30 + 8 + 20]  # mimetype first and stored
    assert b.comment == a.comment
    assert zipfile.ZipFile(prepared).testzip() is None


def test_touched_files_differ_only_by_the_insertions(prepared):
    a, b = Archive(FIXTURE.read_bytes()), Archive(prepared.read_bytes())
    for name in ("OEBPS/text/chapter-1.xhtml", "OEBPS/text/chapter-2.xhtml", "OEBPS/text/chapter-3.xhtml"):
        data = b.read(name)
        for span in _anchor_spans(data):
            assert span == b'<span id="' + span.split(b'"')[1] + b'"></span>'
            data = data.replace(span, b"", 1)
        assert data == a.read(name), name
    line = b'<item id="rsvp-checkpoints" href="rsvp/checkpoints.json" media-type="application/json"/>\n'
    opf = b.read("OEBPS/content.opf")
    assert opf.count(line) == 1
    assert opf.replace(line, b"") == a.read("OEBPS/content.opf")
    assert opf.index(line) + len(line) == opf.index(b"</manifest>")


def test_checkpoint_file_is_valid_and_matches_the_draft(prepared):
    archive = Archive(prepared.read_bytes())
    raw = archive.read("OEBPS/rsvp/checkpoints.json")
    assert cp.check_bytes(raw) == []
    data = json.loads(raw)
    draft = json.loads(DRAFT.read_text(encoding="utf-8"))
    assert data["book"] == draft["book"]
    assert [(s["id"], s["chapter_href"], s["start_anchor"], s["end_anchor"], s["label"], s["word_count"])
            for s in data["sections"]] == [
        (s["id"], s["chapter_href"], s["start_anchor"], s["end_anchor"], s["label"], s["word_count"])
        for s in EXPECTED["sections"]]
    assert [s["label"] for s in data["sections"]] == [s["label"] for s in draft["sections"]]
    assert data["checkpoints"] == [
        {"id": c["id"], "section": c["section"],
         "prompts": [{"id": p["id"], "prompt": p["prompt"], "answer": p["answer"]} for p in c["prompts"]]}
        for c in draft["checkpoints"]]
    assert raw.endswith(b"}\n") and "supporting_paragraphs" not in raw.decode()


def test_build_is_deterministic(prepared, tmp_path):
    again = tmp_path / "again.epub"
    assert run_prep("build", FIXTURE, DRAFT, "-o", again).returncode == 0
    assert again.read_bytes() == prepared.read_bytes()


def test_build_never_writes_the_input(tmp_path):
    copy = tmp_path / "book.epub"
    copy.write_bytes(FIXTURE.read_bytes())
    (tmp_path / "sub").mkdir()
    os.link(copy, tmp_path / "hardlink.epub")
    for target in (copy, tmp_path / "sub" / ".." / "book.epub", tmp_path / "hardlink.epub"):
        result = run_prep("build", copy, DRAFT, "-o", target)
        assert result.returncode == 1
        assert "is the input EPUB" in result.stderr
    assert copy.read_bytes() == FIXTURE.read_bytes()


def test_prepared_book_cannot_be_prepared_again(prepared, tmp_path):
    result = run_prep("build", prepared, DRAFT, "-o", tmp_path / "twice.epub")
    assert result.returncode == 1
    assert "already-prepared" in result.stderr and "source-mismatch" in result.stderr


def _epubcheck(path: Path):
    try:
        return epubcheck.run_epubcheck(path)
    except epubcheck.EpubcheckUnavailable as exc:
        pytest.skip(f"EPUBCheck unavailable, prepared EPUB NOT checked: {exc}")


def test_prepared_fixture_passes_epubcheck(prepared):
    report = _epubcheck(prepared)
    assert report.errors == [], [str(m) for m in report.errors]
    assert report.warnings == [], [str(m) for m in report.warnings]


def test_usage_errors_exit_2(tmp_path):
    for args in ([], ["frobnicate"], ["build", str(FIXTURE), str(DRAFT)], ["extract", str(FIXTURE)]):
        assert run_prep(*args).returncode == 2, args


def test_missing_input_exits_1(tmp_path):
    result = run_prep("extract", tmp_path / "nope.epub", tmp_path / "w")
    assert result.returncode == 1 and "cannot read" in result.stderr
    (tmp_path / "bad.epub").write_bytes(b"not a zip")
    result = run_prep("extract", tmp_path / "bad.epub", tmp_path / "w")
    assert result.returncode == 1 and "not a zip archive" in result.stderr


# --------------------------------------------------------------------------
# build: invalid drafts


def _mutated(tmp_path: Path, mutate) -> Path:
    data = json.loads(DRAFT.read_text(encoding="utf-8"))
    mutate(data)
    path = tmp_path / "draft.json"
    path.write_text(json.dumps(data), encoding="utf-8")
    return path


def _sections(d):
    return d["sections"]


@pytest.mark.parametrize(
    "mutate, code, text",
    [
        (lambda d: _sections(d)[0].update(last_paragraph="d02-p099"), "unknown-paragraph",
         "paragraph 'd02-p099' does not exist (text/chapter-1.xhtml has d02-p001 to d02-p010)"),
        (lambda d: _sections(d)[1].update(first_paragraph="d02-p005"), "overlapping-ranges",
         "d02-p005, d02-p006 also belong to section s0001"),
        (lambda d: _sections(d)[1].update(last_paragraph="d03-p002"), "range-across-documents",
         "d02-p007 is in text/chapter-1.xhtml but d03-p002 is in text/chapter-2.xhtml"),
        (lambda d: d["book"].update(source_sha256="0" * 64), "source-mismatch",
         "re-run prep extract on this file"),
        (lambda d: d["checkpoints"][0]["prompts"][0].update(answer="x" * 601), "schema",
         "answer has 601 characters; the limit is 600"),
        (lambda d: _sections(d)[0].update(first_paragraph="c1-p001"), "schema",
         "'c1-p001' is not a paragraph reference such as 'd02-p006'"),
        (lambda d: _sections(d)[1].update(first_paragraph="d02-p009"), "uncovered-paragraphs",
         "text/chapter-1.xhtml: d02-p007, d02-p008 belong to no section"),
        (lambda d: _sections(d)[1].update(first_paragraph="d02-p010", last_paragraph="d02-p007"), "reversed-range",
         "last_paragraph d02-p007 comes before first_paragraph d02-p010"),
        (lambda d: _sections(d)[0].update(chapter_href="text/chapter-2.xhtml"), "wrong-document",
         "are in text/chapter-1.xhtml, not text/chapter-2.xhtml"),
        (lambda d: _sections(d)[0].update(chapter_href="text/missing.xhtml"), "unknown-document",
         "'text/missing.xhtml' is not an XHTML spine document"),
        (lambda d: _sections(d).reverse(), "section-order", "list sections in reading order"),
        (lambda d: d["checkpoints"][0]["prompts"][0].update(supporting_paragraphs=["d02-p007"]),
         "support-outside-section", "d02-p007 is not inside section s0001"),
        (lambda d: d["checkpoints"][0]["prompts"][0].update(prompt="Why <b>this</b>?"), "not-plain-text",
         "prompt contains markup"),
        (lambda d: d["checkpoints"][1].update(section="s0001"), "multiple-checkpoints-for-section",
         "section 's0001' already has a checkpoint"),
        (lambda d: d["checkpoints"][1].update(section="s0099"), "unknown-section", "'s0099', which does not exist"),
        (lambda d: _sections(d)[1].update(id="s0001"), "duplicate-id", "section id 's0001' is used more than once"),
    ],
    ids=["unknown-paragraph", "overlap", "across-documents", "sha-mismatch", "answer-too-long", "source-id-cited",
         "uncovered", "reversed", "wrong-document", "unknown-document", "order", "support-outside", "markup",
         "two-checkpoints", "unknown-section", "duplicate-section-id"],
)
def test_invalid_drafts_are_refused(tmp_path, mutate, code, text):
    out = tmp_path / "out.epub"
    result = run_prep("build", FIXTURE, _mutated(tmp_path, mutate), "-o", out)
    assert result.returncode == 1, result.stderr
    assert "the draft cannot be built" in result.stderr
    assert f"{code} at " in result.stderr
    assert text in result.stderr, result.stderr
    assert not out.exists()


def test_every_problem_is_listed(tmp_path):
    def mutate(d):
        _sections(d)[0].update(last_paragraph="d02-p099")
        d["checkpoints"][0]["prompts"][0].update(answer=" ")
        d["checkpoints"][1]["prompts"][1].update(supporting_paragraphs=["d03-p001"])
    result = run_prep("build", FIXTURE, _mutated(tmp_path, mutate), "-o", tmp_path / "out.epub")
    assert result.returncode == 1
    for code in ("unknown-paragraph", "uncovered-paragraphs", "blank-text", "support-outside-section"):
        assert f"{code} at " in result.stderr, code


def test_unparsable_draft_is_refused(tmp_path):
    path = tmp_path / "draft.json"
    path.write_text('{"format": "rsvp-draft", "format": "x"}', encoding="utf-8")
    result = run_prep("build", FIXTURE, path, "-o", tmp_path / "out.epub")
    assert result.returncode == 1 and "duplicate-key" in result.stderr


# --------------------------------------------------------------------------
# build: books made in the test


PLAIN_CHAPTER = xhtml(
    "<h1>One</h1>\n<p>First paragraph has five words.</p>\n<p>Second one, <em>also</em> short.</p>\n"
    "<p>Third and last.</p>"
)


def _build(tmp_path: Path, epub: Path, sections, excluded=(), checkpoints=None) -> tuple[Path, subprocess.CompletedProcess]:
    draft = make_draft(tmp_path / "draft.json", epub, sections, excluded, checkpoints)
    out = tmp_path / "out.prepared.epub"
    return out, run_prep("build", epub, draft, "-o", out)


@pytest.mark.parametrize("opf_dir", ["", "OEBPS", "a/b"])
def test_checkpoint_file_sits_beside_the_opf(tmp_path, opf_dir):
    epub = make_epub(tmp_path / "book.epub", [("ch1.xhtml", PLAIN_CHAPTER)], opf_dir=opf_dir)
    out, result = _build(tmp_path, epub, [("s0001", "ch1.xhtml", "d01-p001", "d01-p004")])
    assert result.returncode == 0, result.stderr
    archive = Archive(out.read_bytes())
    prefix = f"{opf_dir}/" if opf_dir else ""
    assert archive.names()[-1] == f"{prefix}rsvp/checkpoints.json"
    assert cp.check_bytes(archive.read(f"{prefix}rsvp/checkpoints.json")) == []
    book = load_book(out)
    item = next(m for m in book.manifest if m.id == "rsvp-checkpoints")
    assert (item.href, item.media_type, item.zip_path) == ("rsvp/checkpoints.json", "application/json",
                                                           f"{prefix}rsvp/checkpoints.json")
    data = json.loads(archive.read(f"{prefix}rsvp/checkpoints.json"))
    assert data["sections"][0]["chapter_href"] == "ch1.xhtml" and data["sections"][0]["word_count"] == 13


def test_blocks_without_ids_and_deflated_entries(tmp_path):
    epub = make_epub(tmp_path / "book.epub", [("text/ch1.xhtml", PLAIN_CHAPTER), ("text/ch2.xhtml", PLAIN_CHAPTER)],
                     compression=zipfile.ZIP_DEFLATED)
    work = tmp_path / "work"
    assert run_prep("extract", epub, work).returncode == 0
    structure = json.loads((work / "structure.json").read_text(encoding="utf-8"))
    blocks = structure["documents"][0]["blocks"]
    assert [(b["ref"], b["kind"], b["source_id"], b["word_count"]) for b in blocks] == [
        ("d01-p001", "heading", None, 1), ("d01-p002", "paragraph", None, 5), ("d01-p003", "paragraph", None, 4),
        ("d01-p004", "paragraph", None, 3)]
    out, result = _build(tmp_path, epub, [("s0001", "text/ch1.xhtml", "d01-p001", "d01-p002"),
                                          ("s0002", "text/ch1.xhtml", "d01-p003", "d01-p004"),
                                          ("s0003", "text/ch2.xhtml", "d02-p001", "d02-p004")])
    assert result.returncode == 0, result.stderr
    a, b = Archive(epub.read_bytes()), Archive(out.read_bytes())
    assert [e.method for e in a.entries] == [0] + [8] * (len(a.entries) - 1)
    for e in b.entries:
        if e.name == "OEBPS/rsvp/checkpoints.json":
            assert e.method == 8  # like the package document
        else:
            assert e.method == a.by_name[e.name].method
            assert (e.dos_time, e.dos_date) == (a.by_name[e.name].dos_time, a.by_name[e.name].dos_date)
    for name in ("mimetype", "META-INF/container.xml", "OEBPS/nav.xhtml"):
        assert _local_record(a, name) == _local_record(b, name)
    assert b"rsvp-s0003-start" in b.read("OEBPS/text/ch2.xhtml")
    assert zipfile.ZipFile(out).testzip() is None


class _NoSeek:
    """A write-only stream: zipfile then writes data descriptors after each entry."""

    def __init__(self):
        self.buf = bytearray()

    def write(self, b):
        self.buf += b
        return len(b)

    def flush(self):
        pass

    def tell(self):
        return len(self.buf)

    def seekable(self):
        return False


def test_data_descriptor_entries_are_copied_verbatim(tmp_path):
    src = make_epub(tmp_path / "src.epub", [("ch1.xhtml", PLAIN_CHAPTER)], compression=zipfile.ZIP_DEFLATED)
    stream = _NoSeek()
    with zipfile.ZipFile(src) as zin, zipfile.ZipFile(stream, "w") as zout:
        for info in zin.infolist():
            zout.writestr(info, zin.read(info.filename))
    epub = tmp_path / "book.epub"
    epub.write_bytes(bytes(stream.buf))
    a = Archive(epub.read_bytes())
    assert all(e.flags & 0x08 for e in a.entries[1:])
    out, result = _build(tmp_path, epub, [("s0001", "ch1.xhtml", "d01-p001", "d01-p004")])
    assert result.returncode == 0, result.stderr
    b = Archive(out.read_bytes())
    assert _local_record(a, "OEBPS/nav.xhtml") == _local_record(b, "OEBPS/nav.xhtml")
    assert not b.by_name["OEBPS/ch1.xhtml"].flags & 0x08
    assert zipfile.ZipFile(out).testzip() is None


def test_manifest_line_follows_indentation_and_crlf(tmp_path):
    epub = make_epub(tmp_path / "book.epub", [("ch1.xhtml", PLAIN_CHAPTER)], newline="\r\n", indent="  ")
    out, result = _build(tmp_path, epub, [("s0001", "ch1.xhtml", "d01-p001", "d01-p004")])
    assert result.returncode == 0, result.stderr
    before = Archive(epub.read_bytes()).read("OEBPS/content.opf")
    after = Archive(out.read_bytes()).read("OEBPS/content.opf")
    line = b'    <item id="rsvp-checkpoints" href="rsvp/checkpoints.json" media-type="application/json"/>\r\n'
    assert after.replace(line, b"") == before
    assert after.index(line) + len(line) == after.index(b"  </manifest>")


EDGE_CHAPTER = xhtml(
    '<section epub:type="chapter">\n'
    "<h1>Edge cases</h1>\n"
    '<figure><img src="../images/pic.png" alt="A lighthouse"/><figcaption>A figure first.</figcaption></figure>\n'
    "<p>Words after&#160;the figure, with a line<br/>break inside.</p>\n"
    "<table><tr><td>cell one</td><td>cell two</td></tr></table>"
    "<pre><code>code line</code></pre>\n"
    "<ul>\n<li>List item one</li>\n<li>List <b>item</b> two</li>\n</ul>\n"
    "<blockquote><p>A quoted paragraph.</p></blockquote>\n"
    "<div class=\"wrap\"><div>Text in nested divs.</div></div>\n"
    "<p> </p>\n"
    "<p hidden=\"hidden\">Hidden text.</p>\n"
    '<p>Last words.<sup><a epub:type="noteref" href="#n1">1</a></sup></p>\n'
    '<figure><img src="../images/pic.png" alt="Again"/></figure>\n'
    "</section>\n"
    '<aside epub:type="footnote" id="n1"><p>The note.</p></aside>',
    "Edge",
)


def test_segmentation_of_varied_markup(tmp_path):
    epub = make_epub(tmp_path / "book.epub", [("text/edge.xhtml", EDGE_CHAPTER)], with_image=True)
    book = load_book(epub)
    sd = book.spine[0]
    got = [(b.ref, b.kind, b.element.local, b.word_count, b.placement) for b in sd.blocks]
    assert got == [
        ("d01-p001", "heading", "h1", 2, "inside"),
        ("d01-p002", "image", "figure", 3, "around"),
        ("d01-p003", "paragraph", "p", 9, "inside"),
        ("d01-p004", "table", "table", 4, "around"),
        ("d01-p005", "code", "pre", 2, "around"),
        ("d01-p006", "list-item", "li", 3, "inside"),
        ("d01-p007", "list-item", "li", 3, "inside"),
        ("d01-p008", "blockquote", "p", 3, "inside"),
        ("d01-p009", "paragraph", "div", 4, "inside"),
        ("d01-p010", "paragraph", "p", 2, "inside"),
        ("d01-p011", "image", "figure", 0, "around"),
    ]
    # &#160; (U+00A0) and <br/> separate words; captions, cells, code and the note body are words.
    assert sd.blocks[1].words == ["A", "figure", "first."]
    assert sd.blocks[2].words == ["Words", "after", "the", "figure,", "with", "a", "line", "break", "inside."]
    assert sd.blocks[3].words == ["cell", "one", "cell", "two"] and sd.blocks[4].words == ["code", "line"]
    assert sd.blocks[9].words == ["Last", "words."]
    assert [(n.ref, n.kind, n.note_id, n.words) for n in sd.notes] == [("d01-n001", "footnote", "n1", ["The", "note."])]
    assert sd.words == [w for b in sd.blocks + sd.notes for w in b.words]
    assert [(m.kind, m.start, m.end) for m in sd.stream.markers] == [
        ("IMAGE", 2, 2), ("TABLE", 14, 18), ("CODE", 18, 20), ("IMAGE", 35, 35), ("NOTE", 35, 37)]
    assert sd.blocks[1].placeholder == '[Image: <figure>; alt "A lighthouse"; caption "A figure first."]'
    assert sd.blocks[3].placeholder == "[Table: <table>; 1 rows x 2 columns]"
    assert sd.blocks[4].placeholder == "[Code: <pre>; 1 lines]"


def test_footnote_marks_and_text_in_no_word(tmp_path):
    chapter = xhtml(
        '<p>One.<sup><a href="#n1">1</a></sup> Two.<a href="#n2"><sup>2</sup></a> Three<sup>rd</sup> '
        'web<sup><a href="https://example.org/#x">4</a></sup> five<span epub:type="pagebreak" id="p7">7</span> '
        "<ruby>six<rt>6</rt></ruby> end.</p>\n"
        '<aside epub:type="footnote" id="n1"><p>Note one.</p></aside>\n'
        '<aside epub:type="footnote" id="n2"><p>Note two.</p></aside>'
    )
    epub = make_epub(tmp_path / "book.epub", [("ch1.xhtml", chapter)])
    sd = load_book(epub).spine[0]
    assert sd.blocks[0].words == ["One.", "Two.", "Threerd", "web4", "five", "six", "end."]
    assert [n.words for n in sd.notes] == [["Note", "one."], ["Note", "two."]]
    assert sd.words == sd.blocks[0].words + ["Note", "one.", "Note", "two."]
    assert [(m.kind, m.start, m.end) for m in sd.stream.markers] == [("NOTE", 7, 9), ("NOTE", 9, 11)]
    assert [(r.mark, r.word) for r in sd.stream.noterefs] == [("1", 0), ("2", 1)]
    work = tmp_path / "work"
    assert run_prep("extract", epub, work).returncode == 0
    text = (work / "text" / "d01-ch1.txt").read_text(encoding="utf-8")
    assert "\nOne.[^1] Two.[^2] Threerd web4 five six end.\n" in text


def test_anchors_around_non_prose_blocks_pass_epubcheck(tmp_path):
    epub = make_epub(tmp_path / "book.epub", [("text/edge.xhtml", EDGE_CHAPTER), ("text/plain.xhtml", PLAIN_CHAPTER)],
                     with_image=True, compression=zipfile.ZIP_DEFLATED)
    # s0001 starts with a heading and ends with a table; s0002 starts with the pre right after it
    # (end and start anchors at the same offset) and ends with a figure; s0003 is one list item.
    out, result = _build(tmp_path, epub, [
        ("s0001", "text/edge.xhtml", "d01-p001", "d01-p004"),
        ("s0002", "text/edge.xhtml", "d01-p005", "d01-p011"),
        ("s0003", "text/plain.xhtml", "d02-p001", "d02-p004"),
    ])
    assert result.returncode == 0, result.stderr
    data = Archive(out.read_bytes()).read("OEBPS/text/edge.xhtml").decode()
    assert '</table><span id="rsvp-s0001-end"></span><span id="rsvp-s0002-start"></span><pre>' in data
    assert '<h1><span id="rsvp-s0001-start"></span>Edge cases</h1>' in data
    assert "\n<span id=\"rsvp-s0002-end\"></span>" not in data
    assert '<figure><img src="../images/pic.png" alt="Again"/></figure><span id="rsvp-s0002-end"></span>' in data
    before, after = load_book(epub), load_book(out)
    for a, b in zip(before.spine, after.spine):
        assert a.words == b.words
        assert words.tokenize(all_text(a.doc.root)) == words.tokenize(all_text(b.doc.root))
    sections = json.loads(Archive(out.read_bytes()).read("OEBPS/rsvp/checkpoints.json"))["sections"]
    assert [s["word_count"] for s in sections] == [18, 17, 13]
    report = _epubcheck(out)
    assert report.errors == [], [str(m) for m in report.errors]
    assert report.warnings == [], [str(m) for m in report.warnings]


def test_section_of_only_non_prose_is_refused(tmp_path):
    epub = make_epub(tmp_path / "book.epub", [("text/edge.xhtml", EDGE_CHAPTER)], with_image=True)
    # The second figure has no caption: no words between its anchors. (The first one's caption is three words.)
    out, result = _build(tmp_path, epub, [
        ("s0001", "text/edge.xhtml", "d01-p001", "d01-p001"),
        ("s0002", "text/edge.xhtml", "d01-p002", "d01-p010"),
        ("s0003", "text/edge.xhtml", "d01-p011", "d01-p011"),
    ])
    assert result.returncode == 1
    assert "empty-section at /sections/2: section s0003 has no words" in result.stderr


def test_existing_anchor_id_is_refused(tmp_path):
    chapter = xhtml('<p id="rsvp-s0001-end">Taken id here.</p>')
    epub = make_epub(tmp_path / "book.epub", [("ch1.xhtml", chapter)])
    out, result = _build(tmp_path, epub, [("s0001", "ch1.xhtml", "d01-p001", "d01-p001")])
    assert result.returncode == 1
    assert "anchor-id-conflict" in result.stderr and not out.exists()


def test_anchor_without_a_valid_place_is_refused(tmp_path):
    chapter = xhtml("<p>Intro words.</p>\n<ul><table><tr><td>cell</td></tr></table></ul>")
    epub = make_epub(tmp_path / "book.epub", [("ch1.xhtml", chapter)])
    assert [b.placement for b in load_book(epub).spine[0].blocks] == ["inside", "none"]
    out, result = _build(tmp_path, epub, [("s0001", "ch1.xhtml", "d01-p001", "d01-p002")])
    assert result.returncode == 1
    assert "anchor-placement at /sections/0: no valid place for the end anchor at d01-p002" in result.stderr


def test_prefixed_xhtml_gets_prefixed_spans(tmp_path):
    chapter = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<h:html xmlns:h="http://www.w3.org/1999/xhtml"><h:head><h:title>T</h:title></h:head>'
        "<h:body><h:p>Prefixed words here.</h:p></h:body></h:html>\n"
    )
    epub = make_epub(tmp_path / "book.epub", [("ch1.xhtml", chapter)])
    out, result = _build(tmp_path, epub, [("s0001", "ch1.xhtml", "d01-p001", "d01-p001")])
    assert result.returncode == 0, result.stderr
    assert (b'<h:p><h:span id="rsvp-s0001-start"></h:span>Prefixed words here.<h:span id="rsvp-s0001-end">'
            b"</h:span></h:p>") in Archive(out.read_bytes()).read("OEBPS/ch1.xhtml")


def test_named_entities_from_an_external_dtd(tmp_path):
    chapter = (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">\n'
        '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>T</title></head>\n'
        "<body><p>caf&eacute;&nbsp;au lait &amp; more</p></body></html>\n"
    )
    epub = make_epub(tmp_path / "book.epub", [("ch1.xhtml", chapter)])
    book = load_book(epub)
    assert book.spine[0].words == ["caf\u00e9", "au", "lait", "&", "more"]
    out, result = _build(tmp_path, epub, [("s0001", "ch1.xhtml", "d01-p001", "d01-p001")])
    assert result.returncode == 0, result.stderr
    data = Archive(out.read_bytes()).read("OEBPS/ch1.xhtml")
    assert b'<p><span id="rsvp-s0001-start"></span>caf&eacute;&nbsp;au lait &amp; more<span id="rsvp-s0001-end">' \
        in data


def test_load_book_bytes_matches_load_book():
    a = load_book(FIXTURE)
    b = load_book_bytes(FIXTURE.read_bytes(), "fixture.epub")
    assert a.sha256 == b.sha256 and [d.words for d in a.spine] == [d.words for d in b.spine]


def test_build_file_api(tmp_path):
    result = build_file(FIXTURE, DRAFT, tmp_path / "x.epub")
    assert result.touched == ["OEBPS/content.opf", "OEBPS/text/chapter-1.xhtml", "OEBPS/text/chapter-2.xhtml",
                              "OEBPS/text/chapter-3.xhtml"]
    assert result.added == "OEBPS/rsvp/checkpoints.json"
    assert (tmp_path / "x.epub").read_bytes() == result.data


def test_central_directory_offsets_point_at_local_headers(prepared):
    data = prepared.read_bytes()
    archive = Archive(data)
    for e in archive.entries:
        sig, *_rest, name_len, _extra = struct.unpack_from("<IHHHHHIIIHH", data, e.local_offset)
        assert sig == 0x04034B50 and data[e.local_offset + 30 : e.local_offset + 30 + name_len] == e.name.encode()


def test_anchor_inside_a_word_is_refused(tmp_path):
    """Two canvas blocks with no boundary between them make one word, so no anchor fits between them."""
    chapter = xhtml("<p>Intro words.</p>\n<div><canvas>A</canvas><canvas>B</canvas></div>\n<p>More words.</p>")
    epub = make_epub(tmp_path / "book.epub", [("ch1.xhtml", chapter)])
    sd = load_book(epub).spine[0]
    assert sd.words == ["Intro", "words.", "AB", "More", "words."]
    assert [(b.ref, b.kind, b.placement) for b in sd.blocks] == [
        ("d01-p001", "paragraph", "inside"), ("d01-p002", "media", "around"), ("d01-p003", "media", "around"),
        ("d01-p004", "paragraph", "inside")]
    out, result = _build(tmp_path, epub, [("s0001", "ch1.xhtml", "d01-p001", "d01-p002"),
                                          ("s0002", "ch1.xhtml", "d01-p003", "d01-p004")])
    assert result.returncode == 1 and not out.exists()
    assert "anchor-placement at /sections/0: the end anchor of section s0001 would fall inside the word" \
        in result.stderr
    assert "anchor-placement at /sections/1: the start anchor of section s0002" in result.stderr
