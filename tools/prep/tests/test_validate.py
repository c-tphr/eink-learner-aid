"""Tests for ``prep validate`` (tools/prep/validate.py): every check passes on the prepared fixture and fails on
a file broken for it; the negative fixtures fail exactly the checks their manifest names."""

from __future__ import annotations

import json
import os

import pytest

from tools.prep import validate as V
from tools.prep.build import build_bytes
from tools.prep.epub import load_book_bytes
from tools.prep.tests.bookmaker import make_epub, xhtml
from tools.prep.tests.prepkit import (CH1, CH2, CH3, CP, DRAFT, DRAFT_MANIFEST, MANIFEST, NEGATIVE, OPF, ORIGINAL,
                                      ORIGINAL_BYTES, PREPARED, PREPARED_BYTES, TS, edit, edit_checkpoints,
                                      error_epubcheck, failing, ok_epubcheck, real_epubcheck, replace_once, run_prep,
                                      span, status, unavailable_epubcheck, without_member)

DRAFT_BYTES = DRAFT.read_bytes()


def run(prepared: bytes = PREPARED_BYTES, original: bytes | None = ORIGINAL_BYTES, draft: bytes | None = None,
        epubcheck=ok_epubcheck, **kw) -> V.Outcome:
    return V.validate_bytes(original, "fixture.epub", prepared, "test.prepared.epub", draft,
                            "draft.json" if draft is not None else None, epubcheck, TS, **kw)


def only_failure(prepared: bytes, check: str, codes: set[str], **kw) -> V.Outcome:
    out = run(prepared, **kw)
    assert failing(out.result) == {check: codes}, failing(out.result)
    assert not out.passed
    return out


# --------------------------------------------------------------------------
# The prepared fixture passes everything


def test_prepared_fixture_passes_every_check_with_real_epubcheck():
    out = V.validate_files(ORIGINAL, PREPARED, epubcheck=real_epubcheck, created=TS)
    assert out.passed, failing(out.result)
    assert set(status(out.result).values()) == {"pass"}
    assert [c["name"] for c in out.result["checks"]] == [name for name, _ in V.CHECKS]
    assert out.result["tool"]["epubcheck_version"] == "5.3.0"
    assert out.result["coverage_mode"] == "implicit"
    assert V.validate_result_schema(out.result) == []


def test_prepared_fixture_passes_with_the_draft_exclusions():
    out = run(draft=DRAFT_BYTES)
    assert out.passed, failing(out.result)
    assert out.result["coverage_mode"] == "draft"
    assert out.result["draft"] == {"file": "draft.json", "sha256": V.sha256(DRAFT_BYTES)}
    details = next(c for c in out.result["checks"] if c["name"] == "coverage")["details"]
    assert "text/title.xhtml: words 0-19 excluded by the draft: Front matter: title page." in details


def test_result_records_hashes_versions_and_timestamp():
    out = run()
    r = out.result
    assert r["format"] == "rsvp-validation" and r["version"] == 1 and r["created"] == TS
    assert r["original"]["sha256"] == V.sha256(ORIGINAL_BYTES)
    assert r["prepared"]["sha256"] == V.sha256(PREPARED_BYTES)
    assert r["tool"]["version"] == V.TOOL_VERSION
    assert V.validate_result_schema(r) == []


# --------------------------------------------------------------------------
# One failing case per check (and per code where cheap)


def test_open_fails_on_unreadable_files():
    out = run(b"not a zip")
    assert failing(out.result) == {"open": {"unreadable-prepared"}}
    assert status(out.result)["epubcheck"] == "not-run"
    assert status(out.result)["anchors"] == "not-run"
    out = run(original=b"not a zip")
    assert "unreadable-original" in failing(out.result)["open"]
    assert V.validate_result_schema(out.result) == []


def test_source_hash_mismatch_and_changed_original():
    only_failure((NEGATIVE / "wrong-source-hash.prepared.epub").read_bytes(), "source-hash", {"source-mismatch"})
    out = run(original_after=lambda: ORIGINAL_BYTES + b"x")
    assert failing(out.result) == {"source-hash": {"original-changed"}}


def test_validate_never_writes_the_original(tmp_path):
    original = tmp_path / "fixture.epub"
    original.write_bytes(ORIGINAL_BYTES)
    os.chmod(original, 0o444)
    before = os.stat(original)
    out = V.validate_files(original, PREPARED, epubcheck=ok_epubcheck, created=TS)
    assert out.passed
    after = os.stat(original)
    assert (before.st_mtime_ns, before.st_size) == (after.st_mtime_ns, after.st_size)
    assert original.read_bytes() == ORIGINAL_BYTES


ITEM = '<item id="rsvp-checkpoints" href="rsvp/checkpoints.json" media-type="application/json"/>'


@pytest.mark.parametrize("new_item, codes, opf_changed", [
    ("", {"checkpoint-item-missing"}, False),
    ('<item id="rsvp-checkpoints" href="rsvp/checkpoints.json" media-type="text/plain"/>',
     {"checkpoint-item-media-type"}, False),
    ('<item id="rsvp-checkpoints" href="checkpoints.json" media-type="application/json"/>', {"checkpoint-item-href"},
     False),
    (ITEM + '\n<item id="rsvp-checkpoints2" href="rsvp/checkpoints.json" media-type="application/json"/>',
     {"checkpoint-item-repeated"}, True),
])
def test_checkpoint_item_rules(new_item, codes, opf_changed):
    old = ITEM + "\n" if new_item == "" else ITEM  # a missing item: the line is gone, as in the original
    out = run(edit(PREPARED_BYTES, {OPF: replace_once(old, new_item)}))
    got = failing(out.result)
    assert got.pop("checkpoint-item") == codes
    # Removing the one rsvp-checkpoints item restores the original package document unless a second item was added.
    assert got == ({"archive-unchanged": {"opf-changed"}} if opf_changed else {})


def test_checkpoint_file_missing():
    out = run(without_member(PREPARED_BYTES, CP))
    assert failing(out.result)["checkpoint-item"] == {"checkpoint-file-missing"}
    assert status(out.result)["checkpoint-file"] == "not-run"
    assert status(out.result)["anchors"] == "not-run"


def test_checkpoint_file_cross_reference_and_schema():
    only_failure((NEGATIVE / "unknown-section.prepared.epub").read_bytes(), "checkpoint-file", {"unknown-section"})
    out = run(edit_checkpoints(lambda d: d["checkpoints"][0]["prompts"].append(dict(d["checkpoints"][1]["prompts"][0]))))
    assert failing(out.result) == {"checkpoint-file": {"duplicate-id"}}
    out = run(edit_checkpoints(lambda d: d["sections"][0].__setitem__("word_count", "233")))
    assert failing(out.result) == {"checkpoint-file": {"schema"}}
    assert status(out.result)["anchors"] == "not-run"  # structure checks need a schema-valid file
    out = run(edit(PREPARED_BYTES, {CP: lambda raw: raw[:-3]}))
    assert failing(out.result)["checkpoint-file"] == {"parse"}


def test_firmware_limits():
    out = run(edit_checkpoints(lambda d: d["sections"][0].__setitem__("label", "L" * 201)))
    got = failing(out.result)
    assert got["firmware-limits"] == {"limit"}
    assert got["checkpoint-file"] == {"schema"}  # the schema carries the same limit
    failure = next(c for c in out.result["checks"] if c["name"] == "firmware-limits")["failures"][0]
    assert failure["message"].startswith("MAX_LABEL_CHARS:") and failure["location"].endswith("#/sections/0/label")
    # A limit the schema cannot express: the string pool, through long ids is impractical; use total prompts.
    def many_prompts(d):
        for i in range(2, 34):
            d["checkpoints"].append({"id": f"c9{i:03d}", "section": "s0002", "prompts": [
                {"id": f"c9{i:03d}-p{k}", "prompt": "Q?", "answer": "A."} for k in range(32)]})
    out = run(edit_checkpoints(many_prompts))
    assert any(f["message"].startswith("MAX_PROMPTS:")
               for f in next(c for c in out.result["checks"] if c["name"] == "firmware-limits")["failures"])


def test_anchor_element_repeated_and_not_in_body():
    out = run(edit(PREPARED_BYTES, {CH1: replace_once(span("rsvp-s0001-start"),
                                                         '<span id="rsvp-s0001-start" class="x"></span>')}))
    # Removing the (empty) anchor elements still restores the original bytes; the anchors check flags the class.
    assert failing(out.result) == {"anchors": {"anchor-element"}}
    out = run(edit(PREPARED_BYTES, {CH1: replace_once(span("rsvp-s0001-start"), '<b id="rsvp-s0001-start">x</b>')}))
    assert "anchor-element" in failing(out.result)["anchors"]
    out = run(edit(PREPARED_BYTES, {CH1: replace_once(span("rsvp-s0002-end"),
                                                         span("rsvp-s0002-end") + span("rsvp-s0002-end"))}))
    assert failing(out.result)["anchors"] == {"anchor-repeated"}
    moved = edit(PREPARED_BYTES, {CH1: lambda d: replace_once("<head>", "<head>" + span("rsvp-s0001-start"))(
        replace_once(span("rsvp-s0001-start"), "")(d))})
    assert failing(run(moved).result)["anchors"] == {"anchor-not-in-body"}


def test_section_overlap_order_unknown_document_and_unaccounted():
    # s0002's start anchor moved up beside s0001's: the two sections overlap.
    overlap = edit(PREPARED_BYTES, {CH1: lambda d: replace_once(span("rsvp-s0001-start"), span("rsvp-s0001-start") +
                                                                span("rsvp-s0002-start"))(
        replace_once(span("rsvp-s0002-start"), "")(d))})
    assert "section-overlap" in failing(run(overlap).result)["anchors"]
    # Sections listed out of reading order.
    out = run(edit_checkpoints(lambda d: d["sections"].reverse()))
    assert failing(out.result) == {"anchors": {"section-order"}}
    out = run(edit_checkpoints(lambda d: d["sections"][4].__setitem__("chapter_href", "text/missing.xhtml")))
    assert "unknown-document" in failing(out.result)["anchors"]
    extra = edit(PREPARED_BYTES, {CH3: replace_once(span("rsvp-s0005-start"), span("rsvp-s0005-start") +
                                                    span("rsvp-extra"))})
    assert failing(run(extra).result) == {"anchors": {"unaccounted-anchor"}}


@pytest.mark.parametrize("entry", [e for e in MANIFEST["prepared"] if e["failing_checks"] == ["anchors"]],
                         ids=lambda e: e["file"])
def test_anchor_negative_fixtures(entry):
    out = run((NEGATIVE / entry["file"]).read_bytes())
    assert failing(out.result) == {"anchors": set(entry["codes"])}
    assert status(out.result)["word-counts"] == "incomplete"  # a broken section is not counted, nor passed


def test_broken_link_names_the_checkpoint():
    out = run((NEGATIVE / "broken-link.prepared.epub").read_bytes())
    failure = next(c for c in out.result["checks"] if c["name"] == "anchors")["failures"][0]
    assert failure["code"] == "anchor-missing"
    assert failure["location"] == "OEBPS/text/chapter-2.xhtml#rsvp-s0004-end"
    assert "checkpoint c0002 links to this section" in failure["message"]


def test_word_boundaries():
    # An anchor strictly inside a word: the word pass flags it.
    inside = edit(PREPARED_BYTES, {CH3: lambda d: replace_once("seconds, exactly as it had", "sec" + span(
        "rsvp-s0005-end") + "onds, exactly as it had")(replace_once(span("rsvp-s0005-end"), "")(d))})
    assert "anchor-inside-word" in failing(run(inside).result)["word-boundaries"]
    # An anchor in a table cell and one in a code block.
    table = edit(PREPARED_BYTES, {CH2: lambda d: replace_once("<td>Gannet Rock</td>",
                                                              "<td>" + span("rsvp-s0003-end") + "Gannet Rock</td>")(
        replace_once(span("rsvp-s0003-end"), "")(d))})
    assert "anchor-in-table" in failing(run(table).result)["word-boundaries"]
    code = edit(PREPARED_BYTES, {CH3: lambda d: replace_once("<code>fog_hours", "<code>" + span("rsvp-s0005-end") +
                                                             "fog_hours")(replace_once(span("rsvp-s0005-end"), "")(d))})
    assert "anchor-in-code" in failing(run(code).result)["word-boundaries"]


def _drop_section(d):
    d["sections"].pop(1)  # s0002, which has no checkpoint


def test_coverage_uncovered_words_implicit_and_draft():
    dropped = edit(edit_checkpoints(_drop_section), {CH1: lambda d: replace_once(span("rsvp-s0002-end"), "")(
        replace_once(span("rsvp-s0002-start"), "")(d))})
    out = run(dropped)
    assert failing(out.result) == {"coverage": {"uncovered-words"}}
    failure = next(c for c in out.result["checks"] if c["name"] == "coverage")["failures"][0]
    assert failure["location"] == "OEBPS/text/chapter-1.xhtml words 233-426"
    # With a draft: the title page must be listed as excluded.
    draft = json.loads(DRAFT_BYTES)
    draft["excluded"] = []
    out = run(draft=json.dumps(draft).encode())
    assert failing(out.result) == {"coverage": {"uncovered-words"}}
    assert next(c for c in out.result["checks"] if c["name"] == "coverage")["failures"][0]["location"] == \
        "OEBPS/text/title.xhtml words 0-19"


def test_coverage_draft_problems():
    draft = json.loads(DRAFT_BYTES)
    draft["excluded"].append({"chapter_href": "text/chapter-1.xhtml", "first_paragraph": "d02-p001",
                              "last_paragraph": "d02-p001", "reason": "Front matter: epigraph."})
    assert failing(run(draft=json.dumps(draft).encode()).result) == {"coverage": {"excluded-in-section"}}
    draft = json.loads(DRAFT_BYTES)
    draft["sections"][0]["label"] = "Another label"
    assert failing(run(draft=json.dumps(draft).encode()).result) == {"coverage": {"draft-mismatch"}}
    draft = json.loads(DRAFT_BYTES)
    draft["excluded"][0]["last_paragraph"] = "d01-p009"
    # The title page is then neither excluded nor in a section.
    assert failing(run(draft=json.dumps(draft).encode()).result) == {"coverage": {"draft-exclusion",
                                                                                   "uncovered-words"}}
    out = run(draft=b"{}")
    assert failing(out.result) == {"coverage": {"draft-unreadable"}}


# --------------------------------------------------------------------------
# exclusions


def exclusions(out: V.Outcome) -> dict:
    return next(c for c in out.result["checks"] if c["name"] == "exclusions")


@pytest.mark.parametrize("reason, category", [
    ("Front matter: title page.", "Front matter"),
    ("front matter of this essay: its title heading", "Front matter"),
    ("Back matter: the publisher's colophon.", "Back matter"),
    ("Editorial matter: the editor's introduction.", "Editorial matter"),
    ("Editorial matter", "Editorial matter"),
    ("Title page.", None),
    ("Frontmatter: title page.", None),
    ("Not prepared: only Common Sense is prepared in this edition.", None),
    ("Back mattering", None),
])
def test_exclusion_category(reason, category):
    assert V.exclusion_category(reason) == category


def test_document_roles_from_structural_markup_only():
    from tools.prep import matter
    from tools.prep.epub import load_book
    books = ORIGINAL.parent
    paine = matter.document_roles(load_book(books / "paine-essays.epub"))
    roles = {k.rsplit("/", 1)[1]: v.kind for k, v in paine.items()}
    assert [name for name, kind in roles.items() if kind == matter.BODY] == [
        "common-sense.xhtml", "the-republican-proclamation.xhtml", "anti-monarchal-essay.xhtml",
        "declaration-of-rights.xhtml", "dissertation-on-first-principles-of-government.xhtml",
        "the-decline-and-fall-of-the-english-system-of-finance.xhtml", "agrarian-justice.xhtml"]
    assert {roles[n] for n in ("titlepage.xhtml", "imprint.xhtml", "endnotes.xhtml", "colophon.xhtml",
                               "uncopyright.xhtml")} == {matter.FRONT_BACK}
    fixture = {k: (v.kind, v.evidence) for k, v in matter.document_roles(load_book(ORIGINAL)).items()}
    assert fixture == {
        "OEBPS/text/title.xhtml": (matter.FRONT_BACK, "landmarks type titlepage"),
        "OEBPS/text/chapter-1.xhtml": (matter.BODY, "landmarks type bodymatter"),
        "OEBPS/text/chapter-2.xhtml": (matter.BODY, "inside the landmarks' body matter"),
        "OEBPS/text/chapter-3.xhtml": (matter.BODY, "inside the landmarks' body matter"),
        "OEBPS/text/endnotes.xhtml": (matter.FRONT_BACK, "epub:type endnotes"),
    }


def test_real_book_passes_with_its_committed_draft():
    books = ORIGINAL.parent
    out = V.validate_files(books / "paine-essays.epub", books / "paine-essays.prepared.epub",
                           books / "paine-essays.draft.json", epubcheck=ok_epubcheck, created=TS)
    assert out.passed, failing(out.result)
    assert ("excluded words that count against the limit: 648 of the book's 50507 words outside footnotes "
            "(limit 10%)") in exclusions(out)["details"]


def test_exclusions_pass_on_the_fixture_with_and_without_its_draft():
    for draft in (None, DRAFT_BYTES):
        c = exclusions(run(draft=draft))
        assert c["status"] == "pass", c
        assert "text/title.xhtml: 20 words excluded; the EPUB marks it as front-back (landmarks type titlepage)" in \
            c["details"]


def test_exclusion_category_is_required_in_draft_mode():
    draft = json.loads(DRAFT_BYTES)
    draft["excluded"][0]["reason"] = "Title page."
    out = run(draft=json.dumps(draft).encode())
    assert failing(out.result) == {"exclusions": {"exclusion-category"}}
    assert exclusions(out)["failures"][0]["location"] == "draft.json#/excluded/0/reason"


def test_excluded_body_chapter_fails_with_and_without_the_draft():
    entry = DRAFT_MANIFEST["drafts"][0]
    prepared = (NEGATIVE / entry["file"]).read_bytes()
    out = run(prepared, draft=(NEGATIVE / entry["draft"]).read_bytes())
    assert failing(out.result) == {"exclusions": {"exclusion-category", "excluded-body-document",
                                                  "excluded-body-text"}}
    out = run(prepared)  # implicit mode: chapter 2 has no section, so it counts as excluded
    assert failing(out.result) == {"exclusions": {"excluded-body-document", "excluded-body-text"}}
    text = next(f for f in exclusions(out)["failures"] if f["code"] == "excluded-body-text")
    assert "608 excluded words (44.2% of the book's 1376 words)" in text["message"]


def built(draft: dict, original: bytes = ORIGINAL_BYTES) -> tuple[bytes, bytes]:
    raw = json.dumps(draft).encode()
    return build_bytes(load_book_bytes(original, "book.epub"), raw).data, raw


@pytest.mark.parametrize("last, words, codes", [
    ("d02-p002", 61, set()),  # 4.4% of the book's 1376 words
    ("d02-p005", 184, {"excluded-body-text"}),  # 13.4%
])
def test_editorial_matter_inside_a_body_document_is_allowed_up_to_the_limit(last, words, codes):
    """The opening of chapter 1 (as if an editor had written it) excluded as editorial matter."""
    draft = json.loads(DRAFT_BYTES)
    nxt = f"d02-p{int(last[-3:]) + 1:03d}"
    draft["sections"][0]["first_paragraph"] = nxt
    draft["checkpoints"][0]["prompts"][0]["supporting_paragraphs"] = ["d02-p006"]
    draft["excluded"].append({"chapter_href": "text/chapter-1.xhtml", "first_paragraph": "d02-p001",
                              "last_paragraph": last, "reason": "Editorial matter: the editor's headnote."})
    prepared, raw = built(draft)
    out = run(prepared, draft=raw)
    assert failing(out.result) == ({"exclusions": codes} if codes else {})
    assert f"text/chapter-1.xhtml: {words} words excluded; the EPUB marks it as body (landmarks type bodymatter)" \
        in exclusions(out)["details"]


def test_unmarked_book_counts_only_text_between_sections(tmp_path):
    """Without any structural markup, a document after the last section may be excluded; one between may not."""
    para = " ".join(f"word{n}" for n in range(50))
    chapters = [(f"ch{n}.xhtml", xhtml(f"<p>{para}</p>")) for n in range(1, 5)]
    original = make_epub(tmp_path / "book.epub", chapters).read_bytes()

    def draft(excluded: int) -> dict:
        sections = [n for n in range(1, 5) if n != excluded]
        return {"format": "rsvp-draft", "version": 1,
                "book": {"title": "Test Book", "language": "en", "source_sha256": V.sha256(original),
                         "instructions_version": "1.1"},
                "excluded": [{"chapter_href": f"ch{excluded}.xhtml", "first_paragraph": f"d0{excluded}-p001",
                              "last_paragraph": f"d0{excluded}-p001", "reason": "Back matter: something."}],
                "sections": [{"id": f"s000{n}", "chapter_href": f"ch{n}.xhtml", "first_paragraph": f"d0{n}-p001",
                              "last_paragraph": f"d0{n}-p001", "label": f"Chapter {n}"} for n in sections],
                "checkpoints": []}

    prepared, raw = built(draft(4), original)
    out = V.validate_bytes(original, "book.epub", prepared, "p.epub", raw, "draft.json", ok_epubcheck, TS)
    assert exclusions(out)["status"] == "pass", exclusions(out)
    assert "ch4.xhtml: 50 words excluded; the EPUB marks it as unmarked (no structural markup)" in \
        exclusions(out)["details"]
    prepared, raw = built(draft(2), original)
    out = V.validate_bytes(original, "book.epub", prepared, "p.epub", raw, "draft.json", ok_epubcheck, TS)
    assert failing(out.result) == {"exclusions": {"excluded-body-document", "excluded-body-text"}}


def test_whole_body_document_fails_even_under_the_limit(monkeypatch):
    entry = DRAFT_MANIFEST["drafts"][0]
    prepared = (NEGATIVE / entry["file"]).read_bytes()
    draft = json.loads((NEGATIVE / entry["draft"]).read_bytes())
    draft["excluded"][1]["reason"] = "Back matter: misfiled."
    raw = json.dumps(draft).encode()
    monkeypatch.setattr(V, "MAX_EXCLUDED_BODY_PERCENT", 50)
    out = run(prepared, draft=raw)
    assert failing(out.result) == {"exclusions": {"excluded-body-document"}}  # 44.2% is under 50%


def test_draft_negative_fixtures_fail_exactly_their_checks():
    assert DRAFT_MANIFEST["format"] == "rsvp-negative-drafts" and DRAFT_MANIFEST["drafts"]
    for entry in DRAFT_MANIFEST["drafts"]:
        out = V.validate_files(ORIGINAL, NEGATIVE / entry["file"], NEGATIVE / entry["draft"],
                               epubcheck=real_epubcheck, created=TS)
        got = failing(out.result)
        assert set(got) == set(entry["failing_checks"]), got
        assert set().union(*got.values()) == set(entry["codes"]), got
        assert not out.passed


def test_word_counts():
    only_failure((NEGATIVE / "word-count-off-by-one.prepared.epub").read_bytes(), "word-counts", {"word-count"})


def test_text_unchanged_words_and_flags():
    out = run((NEGATIVE / "changed-word.prepared.epub").read_bytes())
    assert failing(out.result) == {"text-unchanged": {"words-changed"}, "archive-unchanged": {"document-changed"}}
    # A heading turned into a paragraph: same words, different flags (HEADING lost).
    flags = edit(PREPARED_BYTES, {CH3: lambda d: replace_once('<h1 id="c3-p001">', '<p id="c3-p001">')(
        replace_once("Log</h1>", "Log</p>")(d))})
    out = run(flags)
    assert failing(out.result) == {"text-unchanged": {"flags-changed"}, "archive-unchanged": {"document-changed"}}
    # A table cell turned into a paragraph: markers change (and flags).
    markers = edit(PREPARED_BYTES, {CH3: replace_once("<pre id=\"c3-p004\"><code>", "<div id=\"c3-p004\"><code>")})
    markers = edit(markers, {CH3: replace_once("</code></pre>", "</code></div>")})
    assert "markers-changed" in failing(run(markers).result)["text-unchanged"]


def test_archive_unchanged():
    only_failure((NEGATIVE / "extra-member.prepared.epub").read_bytes(), "archive-unchanged", {"member-added"})
    out = run(without_member(PREPARED_BYTES, "OEBPS/style.css"))
    assert failing(out.result)["archive-unchanged"] == {"member-removed"}
    out = run(edit(PREPARED_BYTES, {"OEBPS/style.css": lambda d: d + b"\n/* x */\n"}))
    assert failing(out.result) == {"archive-unchanged": {"member-changed"}}
    out = run(edit(PREPARED_BYTES, {OPF: replace_once("<dc:language>en</dc:language>",
                                                      "<dc:language>en-GB</dc:language>")}))
    assert failing(out.result) == {"archive-unchanged": {"opf-changed"}}
    out = run(edit(PREPARED_BYTES, {CH1: replace_once("</body>", "<!-- prepared --></body>")}))
    assert failing(out.result) == {"archive-unchanged": {"document-changed"}}


def test_epubcheck_errors_fail_and_unavailable_is_not_a_pass():
    out = run(epubcheck=error_epubcheck)
    assert failing(out.result) == {"epubcheck": {"epubcheck-error"}}
    out = run(epubcheck=unavailable_epubcheck)
    assert status(out.result)["epubcheck"] == "not-run"
    assert not out.passed
    assert out.result["tool"]["epubcheck_version"] is None
    check = next(c for c in out.result["checks"] if c["name"] == "epubcheck")
    assert check["failures"][0]["code"] == "epubcheck-unavailable"


# --------------------------------------------------------------------------
# The negative fixtures, with the real EPUBCheck


def test_negative_manifest_lists_every_file():
    listed = ({e["file"] for e in MANIFEST["prepared"]} | {e["file"] for e in MANIFEST["checks"]} | {"manifest.json"}
              | {e[k] for e in MANIFEST["checker"] for k in ("file", "draft")}
              | {"draft-manifest.json"} | {e[k] for e in DRAFT_MANIFEST["drafts"] for k in ("file", "draft")})
    assert listed == {p.name for p in NEGATIVE.iterdir()}
    assert "broken-link.prepared.epub" in listed
    for entry in MANIFEST["prepared"]:
        assert entry["failing_checks"] and entry["codes"] and entry["reason"].strip()
        assert set(entry["failing_checks"]) <= {name for name, _ in V.CHECKS}


@pytest.mark.parametrize("entry", MANIFEST["prepared"], ids=lambda e: e["file"])
def test_negative_fixture_fails_exactly_its_checks(entry):
    out = V.validate_files(ORIGINAL, NEGATIVE / entry["file"], epubcheck=real_epubcheck, created=TS)
    got = failing(out.result)
    assert set(got) == set(entry["failing_checks"]), got
    assert set().union(*got.values()) == set(entry["codes"]), got
    assert not out.passed
    assert all(s in ("pass", "incomplete") for name, s in status(out.result).items() if name not in got)


@pytest.mark.parametrize("entry", MANIFEST["checker"], ids=lambda e: e["file"])
def test_checker_fixture_passes_the_validator(entry):
    """Structurally valid, so only the AI checker can catch its planted fault."""
    out = V.validate_files(ORIGINAL, NEGATIVE / entry["file"], NEGATIVE / entry["draft"], epubcheck=real_epubcheck,
                           created=TS)
    assert out.passed, failing(out.result)
    assert out.result["coverage_mode"] == "draft"


# --------------------------------------------------------------------------
# Command line


def test_cli_passes_on_the_fixture_and_writes_json(tmp_path):
    result = run_prep("validate", ORIGINAL, PREPARED, "--json", tmp_path / "v.json", "--timestamp", TS)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASSED: all 13 checks passed." in result.stdout
    for name, _ in V.CHECKS:
        assert name in result.stdout
    data = json.loads((tmp_path / "v.json").read_text(encoding="utf-8"))
    assert data["passed"] and data["created"] == TS and V.validate_result_schema(data) == []
    again = run_prep("validate", ORIGINAL, PREPARED, "--json", tmp_path / "w.json", "--timestamp", TS)
    assert again.returncode == 0
    assert (tmp_path / "v.json").read_bytes() == (tmp_path / "w.json").read_bytes()


def test_cli_rejects_the_broken_link(tmp_path):
    result = run_prep("validate", ORIGINAL, NEGATIVE / "broken-link.prepared.epub", "--json", tmp_path / "v.json")
    assert result.returncode == 1
    assert "anchor-missing at OEBPS/text/chapter-2.xhtml#rsvp-s0004-end" in result.stderr
    assert "FAILED" in result.stdout
    assert json.loads((tmp_path / "v.json").read_text(encoding="utf-8"))["passed"] is False


def test_cli_work_dir_supplies_the_draft(tmp_path):
    (tmp_path / "draft.json").write_bytes(DRAFT_BYTES)
    result = run_prep("validate", ORIGINAL, PREPARED, "--work-dir", tmp_path, "--json", tmp_path / "v.json")
    assert result.returncode == 0, result.stderr
    assert json.loads((tmp_path / "v.json").read_text(encoding="utf-8"))["coverage_mode"] == "draft"


def test_cli_timestamp_from_source_date_epoch(tmp_path):
    env = dict(os.environ, SOURCE_DATE_EPOCH="1767225600")
    result = run_prep("validate", ORIGINAL, PREPARED, "--json", tmp_path / "v.json", env=env)
    assert result.returncode == 0
    assert json.loads((tmp_path / "v.json").read_text(encoding="utf-8"))["created"] == "2026-01-01T00:00:00Z"


def test_cli_usage_errors_exit_2(tmp_path):
    for args in (["validate"], ["validate", ORIGINAL], ["validate", ORIGINAL, PREPARED, "--draft", DRAFT,
                                                       "--work-dir", tmp_path],
                 ["validate", ORIGINAL, PREPARED, "--timestamp", "yesterday"],
                 ["validate", ORIGINAL, PREPARED, "--work-dir", tmp_path / "missing"]):
        assert run_prep(*args).returncode == 2, args


def test_cli_missing_input_exits_1(tmp_path):
    result = run_prep("validate", tmp_path / "missing.epub", PREPARED)
    assert result.returncode == 1
    assert "unreadable-original" in result.stderr
