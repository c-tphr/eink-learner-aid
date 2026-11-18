"""The word pass (tools/prep/words.py) against docs/spikes/S1-word-stream.md section 5, rule by rule.

Each test names the rule it covers. The last group feeds the C++ prototype's worked example
(test/spike_word_stream/fixture_s1.xhtml) through the Python pass and compares with what the
prototype's own test expects.
"""

from __future__ import annotations

import re
from pathlib import Path

import pytest

from tools.prep import entities, words
from tools.prep.words import (
    BYTE_SPLIT,
    HEADING,
    LINE_END,
    NONPROSE,
    NOTEREF,
    PARA_END,
    tokenize,
    word_stream,
)
from tools.prep.xmltree import XmlError

REPO = Path(__file__).resolve().parents[3]
SPIKE = REPO / "test" / "spike_word_stream"

HEAD = ('<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE html>\n'
        '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">'
        "<head><title>T</title></head>")
DTD_HEAD = ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">\n'
            '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">'
            "<head><title>T</title></head>")


def stream(body: str, head: str = HEAD):
    return word_stream(f"{head}<body>{body}</body></html>".encode("utf-8"))


def texts(body: str, head: str = HEAD) -> list[str]:
    return stream(body, head).texts


def flags(body: str) -> list[tuple[str, int]]:
    return [(w.text, w.flags) for w in stream(body).words]


# --------------------------------------------------------------------------
# Input text


def test_input_only_body_text_counts():
    data = (HEAD.replace("<title>T</title>", "<title>Title words</title><style>p {}</style>")
            + "<body><p>Body words</p></body></html>").encode()
    assert word_stream(data).texts == ["Body", "words"]


@pytest.mark.parametrize("element", ["style", "script", "title", "head", "rp"])
def test_input_non_visible_elements_inside_body_hold_no_words(element):
    # Not a boundary either: the words on both sides join.
    assert texts(f"<p>a<{element}>x y</{element}>b</p>") == ["ab"]


def test_input_xml_entities_and_numeric_references():
    assert texts("<p>a&amp;b &lt;c&gt; caf&#233; x&#x2014;y &#65;</p>") == ["a&b", "<c>", "café", "x—", "y", "A"]


def test_input_named_html_entities_expand_through_the_firmware_table():
    got = texts("<p>Mr.&nbsp;Darcy &hellip; &lang;x&rang; &check; &foo; &mdash;</p>", DTD_HEAD)
    # &lang;/&rang; are U+3008/U+3009 in the firmware's (HTML 4) table, not HTML 5's U+27E8/U+27E9;
    # &check; exists only in HTML 5, so like any unknown name it stays literal.
    assert got == ["Mr.", "Darcy", "…", "〈x〉", "&check;", "&foo;", "—"]


def test_input_undeclared_entity_under_the_html5_doctype_is_an_error():
    with pytest.raises(XmlError, match="undefined entity"):
        stream("<p>a&nbsp;b</p>")


def test_input_entity_table_is_the_firmwares():
    source = (REPO / "lib" / "Epub" / "Epub" / "htmlEntities.cpp").read_text(encoding="utf-8")
    pairs = re.findall(r'\{"&([A-Za-z0-9]+);",\s*"((?:[^"\\]|\\.)*)"\}', source)

    def unescape(value: str) -> str:
        """A C string literal's UTF-8 bytes (\\xHH, \\uXXXX and \\" escapes), decoded."""
        out = bytearray()
        for m in re.finditer(r'\\x([0-9A-Fa-f]{2})|\\u([0-9A-Fa-f]{4})|\\(.)|(.)', value, re.S):
            byte, unit, escaped, plain = m.groups()
            if byte:
                out.append(int(byte, 16))
            else:
                out += (chr(int(unit, 16)) if unit else escaped or plain).encode("utf-8")
        return out.decode("utf-8")

    table = {name: unescape(value) for name, value in pairs}
    assert len(table) == len(pairs) == 253
    assert entities.HTML_ENTITIES == table


# --------------------------------------------------------------------------
# Text that is in the text but in no word


def test_no_word_hidden_attribute():
    assert texts('<p>one <span hidden="hidden">two</span> three</p>') == ["one", "three"]


@pytest.mark.parametrize("attrs", ['epub:type="pagebreak"', 'role="doc-pagebreak"',
                                   'epub:type="pagebreak" role="doc-pagebreak" id="p12" aria-label="12"'])
def test_no_word_page_break_markers(attrs):
    assert texts(f"<p>one <span {attrs}>12</span> two</p>") == ["one", "two"]


def test_no_word_tags_inside_hidden_content_are_ignored():
    """Hidden, page-break and rt content "never splits a word" (section 5; the lead's decision on PR #56, as in the
    prototype): the element's own tags and every tag inside it are ignored entirely: no boundary, no flag, no
    IMAGE/TABLE/CODE/NOTE marker. Anchors inside are still recorded."""
    s = stream('<div>c<div hidden="">y<p>z</p><br/></div>d<span hidden=""><img src="i.png" alt=""/>'
               '<table><tr><td>t</td></tr></table><aside epub:type="footnote"><p>n</p></aside></span>e</div>'
               '<p>f<span epub:type="pagebreak"><h1>12</h1></span>g <ruby>k<rt><br/>K</rt>j</ruby></p>'
               '<div>h<span hidden=""><span id="rsvp-x"></span></span>i</div>')
    assert s.texts == ["cde", "fg", "kj", "hi"]
    assert [(w.text, w.flags) for w in s.words] == [("cde", PARA_END), ("fg", 0), ("kj", PARA_END), ("hi", PARA_END)]
    assert s.markers == []
    assert [(a.id, a.index, a.inside_word) for a in s.anchors] == [("rsvp-x", 4, True)]


def test_no_word_page_break_values_are_exact():
    # "as the renderer tests them": a token list that merely contains pagebreak is ordinary text.
    assert texts('<p>a <span epub:type="pagebreak other">12</span> b</p>') == ["a", "12", "b"]
    assert texts('<p>a <span role="doc-pagebreak x">12</span> b</p>') == ["a", "12", "b"]


def test_no_word_rt():
    assert texts("<p><ruby>kan<rp>(</rp><rt>KAN</rt><rp>)</rp></ruby> ji</p>") == ["kan", "ji"]


def test_no_word_text_never_splits_a_word():
    assert texts('<p>light<span epub:type="pagebreak">12</span>house and in<span hidden="">x y</span>side</p>') == [
        "lighthouse", "and", "inside"]


@pytest.mark.parametrize("mark", [
    '<a epub:type="noteref" href="#n">1</a>',
    '<a epub:type="other noteref" href="notes.xhtml#n">1</a>',
    '<a role="doc-noteref" href="#n">1</a>',
    '<a epub:type="noteref">1</a>',  # explicit: no href needed
    '<sup><a href="#n">1</a></sup>',
    '<a href="#n"><sup>1</sup></a>',
    '<sup><a href="notes.xhtml#n">1</a></sup>',
])
def test_no_word_footnote_reference_marks(mark):
    s = stream(f"<p>recall.{mark} Next</p>")
    assert [(w.text, w.flags) for w in s.words] == [("recall.", NOTEREF), ("Next", PARA_END)]
    assert [(r.mark, r.word) for r in s.noterefs] == [("1", 0)]


@pytest.mark.parametrize("href", ["https://example.org/#n", "http://example.org/#n", "mailto:a@b#n",
                                  "ftp://example.org/#n", "tel:123#n", "javascript:go()#n", "notes.xhtml"])
def test_no_word_external_or_hashless_links_are_not_marks(href):
    assert flags(f'<p>web<sup><a href="{href}">4</a></sup> x</p>') == [("web4", 0), ("x", PARA_END)]


def test_no_word_sup_alone_or_link_alone_is_text():
    assert texts('<p>Three<sup>rd</sup> <a href="#n">link</a></p>') == ["Threerd", "link"]


def test_no_word_mark_inside_a_word_flags_that_word_and_does_not_split_it():
    assert flags('<p>ab<sup><a href="#n">1</a></sup>cd</p>') == [("abcd", NOTEREF | PARA_END)]


def test_no_word_mark_after_a_space_flags_the_previous_word():
    assert flags('<p>one <a epub:type="noteref" href="#n">2</a> two</p>') == [("one", NOTEREF), ("two", PARA_END)]


def test_no_word_css_is_ignored():
    assert texts('<p style="display:none">shown anyway</p>') == ["shown", "anyway"]


# --------------------------------------------------------------------------
# Word boundaries


@pytest.mark.parametrize("separator", sorted(words.SEPARATORS))
def test_boundary_separator(separator):
    assert tokenize(f"a{separator}b") == ["a", "b"]
    if separator != "\x0c":  # U+000C cannot appear in XML 1.0, even as a reference
        assert texts(f"<p>a&#{ord(separator)};b</p>") == ["a", "b"]


def test_boundary_separator_set_is_exactly_section_5():
    listed = {0x09, 0x0A, 0x0C, 0x0D, 0x20, 0xA0, 0x1680, *range(0x2000, 0x200C), 0x2028, 0x2029, 0x202F, 0x205F,
              0x3000}
    assert {ord(c) for c in words.SEPARATORS} == listed
    for other in ("‌", "‍", "⁠", "᠎", "\u0085"):  # not separators: they stay in the word
        assert tokenize(f"a{other}b") == [f"a{other}b"]


def test_boundary_nbsp_entity():
    assert texts("<p>Mr.&#160;Darcy</p>") == ["Mr.", "Darcy"]


@pytest.mark.parametrize("element", sorted(words.BOUNDARY_ELEMENTS - {"body", "html", "br", "hr", "img", "image"}))
def test_boundary_element(element):
    assert texts(f"<div>a<{element}>b</{element}>c</div>") == ["a", "b", "c"]


@pytest.mark.parametrize("element", ["br", "hr", "img", "image"])
def test_boundary_empty_element(element):
    assert texts(f"<p>a<{element}/>b</p>") == ["a", "b"]


@pytest.mark.parametrize("element", ["b", "i", "em", "strong", "span", "a", "sup", "sub", "small", "code", "abbr",
                                     "cite", "q", "mark", "u", "s", "label", "ruby", "font", "noscript", "template",
                                     "form", "fieldset", "menu", "dialog"])
def test_boundary_other_elements_do_not_split(element):
    assert texts(f"<p>a<{element}>b</{element}>c</p>") == ["abc"]


def test_boundary_element_names_are_case_insensitive():
    assert texts("<div>a<P>b</P>c<BR/>d</div>") == ["a", "b", "c", "d"]


def test_boundary_dash_splits_after_em_and_en_dash():
    assert tokenize("forgotten—which") == ["forgotten—", "which"]
    assert tokenize("1907–1925") == ["1907–", "1925"]
    assert tokenize("a——b") == ["a—", "—", "b"]
    assert tokenize("—a") == ["—", "a"]
    assert tokenize("end— next") == ["end—", "next"]  # nothing follows: no extra split
    assert tokenize("—") == ["—"]


def test_boundary_dash_split_across_inline_tags_and_soft_hyphens():
    assert texts("<p>forgotten<em>—</em>which</p>") == ["forgotten—", "which"]
    assert texts("<p>a—&#173;b</p>") == ["a—", "b"]


def test_boundary_hyphens_do_not_split():
    assert tokenize("well-known a‐b c‑d e−f") == ["well-known", "a‐b", "c‑d", "e−f"]


# --------------------------------------------------------------------------
# Inside a word


def test_inside_soft_hyphen_and_bom_are_removed():
    assert texts("<p>hy&#173;phens in&#xAD;side ﻿word&#65279;s</p>") == ["hyphens", "inside", "words"]
    assert texts("<p>&#173; &#65279;</p>") == []
    assert words.is_separator_only("­ ﻿ ")


def test_inside_nfc():
    assert tokenize("café") == ["café"]
    assert texts("<p>cafe<b>&#769;</b> Å</p>") == ["café", "Å"]


def test_inside_255_byte_split():
    s = stream("<p>" + "a" * 300 + " " + "b" * 255 + "</p>")
    assert [(len(w.text), w.flags) for w in s.words] == [(255, BYTE_SPLIT), (45, 0), (255, PARA_END)]
    two_byte = tokenize("é" * 200)  # 400 bytes; the cut falls at a codepoint boundary (254 bytes)
    assert [len(w.encode()) for w in two_byte] == [254, 146]
    assert "".join(two_byte) == "é" * 200


def test_inside_punctuation_only_tokens_are_words():
    assert tokenize("— { < ...") == ["—", "{", "<", "..."]


# --------------------------------------------------------------------------
# Flags


def test_flag_para_end():
    got = flags("<h1>Title</h1><p>a b</p><div>c<img src='x.png' alt='alt words'/>d<br/>e</div><p>f</p>")
    assert got == [("Title", HEADING | PARA_END), ("a", 0), ("b", PARA_END), ("c", 0), ("d", LINE_END),
                   ("e", PARA_END), ("f", PARA_END)]


def test_flag_para_end_on_the_chapter_last_word():
    assert flags("last words")[-1] == ("words", PARA_END)


def test_flag_line_end():
    assert flags("<p>one<br/>two<br/><br/>three</p>") == [("one", LINE_END), ("two", LINE_END),
                                                          ("three", PARA_END)]


def test_flag_heading():
    got = flags("<h2>A <em>b</em></h2><h6>c</h6><p>d</p>")
    assert got == [("A", HEADING), ("b", HEADING | PARA_END), ("c", HEADING | PARA_END), ("d", PARA_END)]


def test_flag_nonprose_and_regions_for_the_outermost_table_and_pre():
    s = stream("<p>x</p><table><caption>cap</caption><tr><td>a<table><tr><td>b</td></tr></table></td>"
               "<td><pre>c d</pre></td></tr></table><pre>e <code>f</code></pre><p>g <code>h</code></p>")
    assert s.texts == ["x", "cap", "a", "b", "c", "d", "e", "f", "g", "h"]
    assert [i for i, w in enumerate(s.words) if w.flags & NONPROSE] == [1, 2, 3, 4, 5, 6, 7]
    assert [(m.kind, m.start, m.end) for m in s.markers] == [("TABLE", 1, 6), ("CODE", 6, 8)]


def test_flag_noteref():
    assert flags('<p>a<sup><a href="#n">1</a></sup></p>') == [("a", NOTEREF | PARA_END)]


def test_flag_no_sentence_end():
    """Sentence ends are decided by the firmware alone (lib/RsvpCore): bit 0, its SENT_END, is never set."""
    assert "SENT_END" not in words.FLAGS and 1 not in words.FLAGS.values()
    assert all(w.flags & 1 == 0 for w in stream("<p>One. Two! Three? Four</p>").words)


# --------------------------------------------------------------------------
# Anchors


def test_anchor_index_is_words_started_before_it():
    s = stream('<p><span id="rsvp-s0002-start"></span>A b here.<span id="rsvp-s0002-end"></span></p>'
               '<p id="rsvp-x">c</p><p id="other">d</p>')
    assert [(a.id, a.index, a.inside_word) for a in s.anchors] == [
        ("rsvp-s0002-start", 0, False), ("rsvp-s0002-end", 3, False), ("rsvp-x", 3, False)]
    start, end = s.anchor("rsvp-s0002-start"), s.anchor("rsvp-s0002-end")
    assert s.texts[start.index : end.index] == ["A", "b", "here."]  # a section is [start, end)


def test_anchor_strictly_inside_a_word_is_reported():
    s = stream('<p>wo<span id="rsvp-a"></span>rd and<span id="rsvp-b"></span> more—<span id="rsvp-c"/>x</p>')
    assert [(a.id, a.index, a.inside_word) for a in s.anchors] == [
        ("rsvp-a", 1, True), ("rsvp-b", 2, False), ("rsvp-c", 3, False)]


def test_anchor_outside_body_or_in_head_is_ignored():
    data = HEAD.replace("<title>T</title>", '<title id="rsvp-t">T</title>') + "<body><p>a</p></body></html>"
    assert word_stream(data.encode()).anchors == []


# --------------------------------------------------------------------------
# Markers


def test_marker_image_at_the_next_word_with_no_alt_text():
    s = stream('<p>a</p><figure><img src="x.png" alt="never a word"/><figcaption>Cap tion</figcaption></figure>'
               '<p>b<img src="y.png" alt="z"/></p><svg xmlns="http://www.w3.org/2000/svg"><image href="i.png"/></svg>')
    assert s.texts == ["a", "Cap", "tion", "b"]
    assert [(m.kind, m.start, m.end) for m in s.markers] == [("IMAGE", 1, 1), ("IMAGE", 4, 4), ("IMAGE", 4, 4)]
    assert not s.words[1].flags & NONPROSE  # a caption is prose


def test_marker_no_image_marker_inside_a_table_or_pre():
    s = stream('<table><tr><td><img src="x.png" alt=""/>a</td></tr></table>')
    assert [(m.kind, m.start, m.end) for m in s.markers] == [("TABLE", 0, 1)]


@pytest.mark.parametrize("attrs", ['epub:type="footnote"', 'epub:type="endnote"', 'epub:type="endnotes"',
                                   'epub:type="x footnote"', 'role="doc-footnote"', 'role="doc-endnote"',
                                   'role="doc-endnotes"'])
@pytest.mark.parametrize("element", ["aside", "section", "div", "li", "p"])
def test_marker_note_region(attrs, element):
    s = stream(f"<p>text</p><{element} {attrs}><p>Note words.</p></{element}><p>after</p>")
    assert s.texts == ["text", "Note", "words.", "after"]  # note words stay in the stream
    assert [(m.kind, m.start, m.end) for m in s.markers] == [("NOTE", 1, 3)]


def test_marker_note_region_is_the_outermost_body():
    s = stream('<section epub:type="endnotes"><ol><li epub:type="endnote"><p>One.</p></li>'
               '<li epub:type="endnote"><p>Two.</p></li></ol></section>')
    assert [(m.kind, m.start, m.end) for m in s.markers] == [("NOTE", 0, 2)]


@pytest.mark.parametrize("attrs", ['epub:type="rearnote"', 'epub:type="note"', 'epub:type="footnotes"',
                                   'role="note"'])
def test_marker_other_note_types_are_not_note_regions(attrs):
    assert stream(f"<aside {attrs}><p>x</p></aside>").markers == []


# --------------------------------------------------------------------------
# Element ranges and plain-text splitting


def test_element_ranges_count_words_started_before_each_tag():
    data = f"{HEAD}<body><p id='a'>one two</p><p id='b'>th<b id='c'>ree</b> four</p></body></html>".encode()
    from tools.prep.xmltree import parse
    doc = parse(data)
    s = word_stream(doc)
    assert [s.range_of(doc.ids[i]) for i in "abc"] == [(0, 2), (2, 4), (3, 3)]


def test_tokenize_plain_text():
    assert tokenize("  Mr. Darcy—who hy­phen  ") == ["Mr.", "Darcy—", "who", "hyphen"]
    assert tokenize("") == []


# --------------------------------------------------------------------------
# Parity with the C++ prototype's worked example (test/spike_word_stream/)

# Copied by hand from test/spike_word_stream/SpikeWordStreamTest.cpp, test FixtureWordsAnchorsAndMarkers:
# the word list is lines 280-287, the anchors lines 292-301, the markers lines 303-312, the flag checks
# lines 314-319 (SENT_END checks at lines 316-317 left out: the Python side sets no sentence ends).
SPIKE_WORDS = [
    "Chapter", "One", "Spacing", "works", "because", "it", "forces", "effortful", "recall.", "It", "also",
    "exposes", "what", "was", "forgotten—", "which", "is", "useful.", "Mr.", "Darcy", "agreed,",
    "e.g.", "in", "the", "well-known", "letter.", "Soft", "hyphens", "stay", "inside", "one", "word,", "and",
    "boldface", "joins", "across", "inline", "tags.", "Figure", "1.", "Recall", "over", "time.", "Interval",
    "Recall", "1", "day", "80%", "for", "(int", "i", "=", "0;", "i", "<", "n;", "i++)", "{", "review(card[i]);",
    "}", "The", "last", "sentence", "of", "the", "section", "ends", "here.", "A", "new", "section", "begins",
    "with", "a", "page", "marker", "hidden", "in", "the", "middle.", "1.", "See", "the", "appendix."]
SPIKE_ANCHORS = [("rsvp-s0001-start", 2), ("rsvp-s0001-end", 68), ("rsvp-s0002-start", 68), ("rsvp-s0002-end", 80)]
SPIKE_MARKERS = [("IMAGE", 38, 38), ("TABLE", 43, 48), ("CODE", 48, 60)]
# The flags the prototype printed for this file (docs/spikes/S1-word-stream.md section 7, lines 323-377 of the
# output listing; SENT dropped). Words the listing shows with no flag other than SENT map to 0.
SPIKE_PRINTED_FLAGS = {
    0: HEADING, 1: PARA_END | HEADING, 2: 0, 8: NOTEREF, 9: 0, 14: 0, 15: 0, 16: 0, 17: 0, 18: 0, 19: 0, 20: 0,
    21: 0, 24: 0, 25: PARA_END, 26: 0, 27: 0, 33: 0, 37: PARA_END, 38: 0, 39: 0, 42: PARA_END,
    43: PARA_END | NONPROSE, 47: PARA_END | NONPROSE, 48: NONPROSE, 58: NONPROSE, 59: PARA_END | NONPROSE, 60: 0,
    67: PARA_END, 68: 0, 71: 0, 72: 0, 79: PARA_END, 80: 0, 81: 0, 82: 0, 83: PARA_END,
}


@pytest.fixture(scope="module")
def spike():
    return word_stream((SPIKE / "fixture_s1.xhtml").read_bytes(), "fixture_s1.xhtml")


def test_spike_parity_words(spike):
    assert spike.texts == SPIKE_WORDS


def test_spike_parity_anchors(spike):
    assert [(a.id, a.index) for a in spike.anchors] == SPIKE_ANCHORS
    assert not any(a.inside_word for a in spike.anchors)


def test_spike_parity_markers(spike):
    # The prototype has no NOTE kind; the Python pass adds the footnote body as one (the lead's decision).
    assert [(m.kind, m.start, m.end) for m in spike.markers if m.kind != "NOTE"] == SPIKE_MARKERS
    assert [(m.kind, m.start, m.end) for m in spike.markers if m.kind == "NOTE"] == [("NOTE", 80, 84)]


def test_spike_parity_flags(spike):
    w = spike.words
    assert w[8].flags & NOTEREF and w[25].flags & PARA_END and w[44].flags & NONPROSE  # the C++ test's checks
    assert {i: w[i].flags for i in SPIKE_PRINTED_FLAGS} == SPIKE_PRINTED_FLAGS
    assert [(r.mark, r.word) for r in spike.noterefs] == [("1", 8)]
