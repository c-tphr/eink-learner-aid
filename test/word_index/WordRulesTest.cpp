// The builder against docs/spikes/S1-word-stream.md section 5 (plus NOTE regions), rule by rule.
// Test names are tools/prep/tests/test_words.py's, in CamelCase (test_input_only_body_text_counts ->
// InputOnlyBodyTextCounts), so the two suites can be compared line by line. Python-only tests
// (element ranges, the entity table comparison) have no counterpart here; see README.md.

#include <Utf8.h>
#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "WordIndexTestSupport.h"

using namespace wordindex;
using wixtest::xhtml;

namespace {

using Flagged = std::vector<std::pair<std::string, int>>;

wixtest::Contents stream(const std::string& body, const bool xhtml11 = false) {
  wixtest::Built built = wixtest::buildIndex(xhtml(body, xhtml11));
  EXPECT_EQ(built.status, BuildStatus::Ok) << buildStatusName(built.status) << " for " << body;
  if (built.status != BuildStatus::Ok) return {};
  return wixtest::readAll(built.file);
}

std::vector<std::string> texts(const std::string& body, const bool xhtml11 = false) {
  std::vector<std::string> out;
  for (const auto& w : stream(body, xhtml11).words) out.push_back(w.text);
  return out;
}

Flagged flags(const std::string& body) {
  Flagged out;
  for (const auto& w : stream(body).words) out.emplace_back(w.text, w.flags);
  return out;
}

// Plain text in one paragraph: the counterpart of words.tokenize().
std::vector<std::string> tokenize(const std::string& text) { return texts("<p>" + text + "</p>"); }

using Region = std::tuple<std::string, uint32_t, uint32_t>;
std::vector<Region> regions(const wixtest::Contents& c) {
  std::vector<Region> out;
  for (const auto& m : c.markers) out.emplace_back(wixtest::markerKindName(m.kind), m.wordStart, m.wordEnd);
  return out;
}

std::string utf8(const uint32_t cp) {
  char buffer[4];
  const char* end = utf8AppendCodepoint(buffer, cp);
  return std::string(static_cast<const char*>(buffer), end);
}

}  // namespace

// ------------------------------------------------------------------------------------------------
// Input text

TEST(WordRules, InputOnlyBodyTextCounts) {
  const std::string doc =
      "<?xml version=\"1.0\"?><!DOCTYPE html><html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>Title "
      "words</title><style>p {}</style></head><body><p>Body words</p></body></html>";
  wixtest::Built built = wixtest::buildIndex(doc);
  ASSERT_EQ(built.status, BuildStatus::Ok);
  const auto c = wixtest::readAll(built.file);
  ASSERT_EQ(c.words.size(), 2u);
  EXPECT_EQ(c.words[0].text, "Body");
  EXPECT_EQ(c.words[1].text, "words");
  EXPECT_EQ(c.words[0].start, 0u);  // offsets count from the body
}

TEST(WordRules, InputNonVisibleElementsInsideBodyHoldNoWords) {
  for (const char* element : {"style", "script", "title", "head", "rp"}) {
    SCOPED_TRACE(element);
    const std::string e = element;
    // Not a boundary either: the words on both sides join.
    EXPECT_EQ(texts("<p>a<" + e + ">x y</" + e + ">b</p>"), std::vector<std::string>{"ab"});
    // And not counted in offsets (as the renderer): "b" follows "a" directly.
    const auto c = stream("<p>a<" + e + ">x y</" + e + ">b</p>");
    ASSERT_EQ(c.words.size(), 1u);
    EXPECT_EQ(c.words[0].end - c.words[0].start, 2u);
  }
}

TEST(WordRules, InputXmlEntitiesAndNumericReferences) {
  EXPECT_EQ(texts("<p>a&amp;b &lt;c&gt; caf&#233; x&#x2014;y &#65;</p>"),
            (std::vector<std::string>{"a&b", "<c>", "caf\xC3\xA9", "x\xE2\x80\x94", "y", "A"}));
}

TEST(WordRules, InputNamedHtmlEntitiesExpandThroughTheFirmwareTable) {
  const auto got = texts("<p>Mr.&nbsp;Darcy &hellip; &lang;x&rang; &check; &foo; &mdash;</p>", true);
  EXPECT_EQ(got, (std::vector<std::string>{"Mr.", "Darcy", "\xE2\x80\xA6", "\xE3\x80\x88x\xE3\x80\x89", "&check;",
                                           "&foo;", "\xE2\x80\x94"}));
}

TEST(WordRules, InputUndeclaredEntityUnderTheHtml5DoctypeIsAnError) {
  const wixtest::Built built = wixtest::buildIndex(xhtml("<p>a&nbsp;b</p>"));
  EXPECT_EQ(built.status, BuildStatus::ParseError);
  EXPECT_EQ(built.report.xmlError, static_cast<uint32_t>(XML_ERROR_UNDEFINED_ENTITY));
  EXPECT_GT(built.report.line, 0u);
}

// ------------------------------------------------------------------------------------------------
// Text that is in the text but in no word

TEST(WordRules, NoWordHiddenAttribute) {
  EXPECT_EQ(texts("<p>one <span hidden=\"hidden\">two</span> three</p>"), (std::vector<std::string>{"one", "three"}));
}

TEST(WordRules, NoWordPageBreakMarkers) {
  for (const char* attrs : {"epub:type=\"pagebreak\"", "role=\"doc-pagebreak\"",
                            "epub:type=\"pagebreak\" role=\"doc-pagebreak\" id=\"p12\" aria-label=\"12\""}) {
    SCOPED_TRACE(attrs);
    EXPECT_EQ(texts(std::string("<p>one <span ") + attrs + ">12</span> two</p>"),
              (std::vector<std::string>{"one", "two"}));
  }
}

TEST(WordRules, NoWordTagsInsideHiddenContentAreIgnored) {
  const std::string body =
      "<div>c<div hidden=\"\">y<p>z</p><br/></div>d<span hidden=\"\"><img src=\"i.png\" alt=\"\"/>"
      "<table><tr><td>t</td></tr></table><aside epub:type=\"footnote\"><p>n</p></aside></span>e</div>"
      "<p>f<span epub:type=\"pagebreak\"><h1>12</h1></span>g <ruby>k<rt><br/>K</rt>j</ruby></p>";
  const auto c = stream(body + "<div>h<span hidden=\"\"><span id=\"other\"></span></span>i</div>");
  EXPECT_EQ(Flagged({{c.words[0].text, c.words[0].flags},
                     {c.words[1].text, c.words[1].flags},
                     {c.words[2].text, c.words[2].flags},
                     {c.words[3].text, c.words[3].flags}}),
            (Flagged{{"cde", PARA_END}, {"fg", 0}, {"kj", PARA_END}, {"hi", PARA_END}}));
  EXPECT_TRUE(c.markers.empty());
  // An anchor inside hidden content is still recorded; here it is inside the word "hi", which the
  // Python pass reports as inside_word and the firmware as an error.
  const wixtest::Built built =
      wixtest::buildIndex(xhtml(body + "<div>h<span hidden=\"\"><span id=\"rsvp-x\"></span></span>i</div>"));
  EXPECT_EQ(built.status, BuildStatus::AnchorInsideWord);
  EXPECT_STREQ(built.report.anchorId, "rsvp-x");
  EXPECT_EQ(built.report.wordIndex, 3u);
}

TEST(WordRules, NoWordPageBreakValuesAreExact) {
  EXPECT_EQ(texts("<p>a <span epub:type=\"pagebreak other\">12</span> b</p>"),
            (std::vector<std::string>{"a", "12", "b"}));
  EXPECT_EQ(texts("<p>a <span role=\"doc-pagebreak x\">12</span> b</p>"), (std::vector<std::string>{"a", "12", "b"}));
}

TEST(WordRules, NoWordRt) {
  EXPECT_EQ(texts("<p><ruby>kan<rp>(</rp><rt>KAN</rt><rp>)</rp></ruby> ji</p>"),
            (std::vector<std::string>{"kan", "ji"}));
}

TEST(WordRules, NoWordTextNeverSplitsAWord) {
  const auto c =
      stream("<p>light<span epub:type=\"pagebreak\">12</span>house and in<span hidden=\"\">x y</span>side</p>");
  ASSERT_EQ(c.words.size(), 3u);
  EXPECT_EQ(c.words[0].text, "lighthouse");
  EXPECT_EQ(c.words[2].text, "inside");
  // The hidden text is counted in offsets and inside the word's span.
  EXPECT_EQ(c.words[0].end - c.words[0].start, 12u);
  EXPECT_EQ(c.words[2].end - c.words[2].start, 9u);
}

TEST(WordRules, NoWordFootnoteReferenceMarks) {
  for (const char* mark :
       {"<a epub:type=\"noteref\" href=\"#n\">1</a>", "<a epub:type=\"other noteref\" href=\"notes.xhtml#n\">1</a>",
        "<a role=\"doc-noteref\" href=\"#n\">1</a>", "<a epub:type=\"noteref\">1</a>",
        "<sup><a href=\"#n\">1</a></sup>", "<a href=\"#n\"><sup>1</sup></a>",
        "<sup><a href=\"notes.xhtml#n\">1</a></sup>"}) {
    SCOPED_TRACE(mark);
    EXPECT_EQ(flags(std::string("<p>recall.") + mark + " Next</p>"),
              (Flagged{{"recall.", NOTEREF}, {"Next", PARA_END}}));
  }
}

TEST(WordRules, NoWordExternalOrHashlessLinksAreNotMarks) {
  for (const char* href : {"https://example.org/#n", "http://example.org/#n", "mailto:a@b#n", "ftp://example.org/#n",
                           "tel:123#n", "javascript:go()#n", "notes.xhtml"}) {
    SCOPED_TRACE(href);
    EXPECT_EQ(flags(std::string("<p>web<sup><a href=\"") + href + "\">4</a></sup> x</p>"),
              (Flagged{{"web4", 0}, {"x", PARA_END}}));
  }
}

TEST(WordRules, NoWordSupAloneOrLinkAloneIsText) {
  EXPECT_EQ(texts("<p>Three<sup>rd</sup> <a href=\"#n\">link</a></p>"), (std::vector<std::string>{"Threerd", "link"}));
}

TEST(WordRules, NoWordMarkInsideAWordFlagsThatWordAndDoesNotSplitIt) {
  EXPECT_EQ(flags("<p>ab<sup><a href=\"#n\">1</a></sup>cd</p>"), (Flagged{{"abcd", NOTEREF | PARA_END}}));
}

TEST(WordRules, NoWordMarkAfterASpaceFlagsThePreviousWord) {
  EXPECT_EQ(flags("<p>one <a epub:type=\"noteref\" href=\"#n\">2</a> two</p>"),
            (Flagged{{"one", NOTEREF}, {"two", PARA_END}}));
}

TEST(WordRules, NoWordCssIsIgnored) {
  EXPECT_EQ(texts("<p style=\"display:none\">shown anyway</p>"), (std::vector<std::string>{"shown", "anyway"}));
}

// ------------------------------------------------------------------------------------------------
// Word boundaries

TEST(WordRules, BoundarySeparator) {
  for (const uint32_t cp :
       {0x09u,   0x0Au,   0x0Du,   0x20u,   0xA0u,   0x1680u, 0x2000u, 0x2001u, 0x2002u, 0x2003u, 0x2004u, 0x2005u,
        0x2006u, 0x2007u, 0x2008u, 0x2009u, 0x200Au, 0x200Bu, 0x2028u, 0x2029u, 0x202Fu, 0x205Fu, 0x3000u}) {
    SCOPED_TRACE(cp);
    EXPECT_EQ(tokenize("a" + utf8(cp) + "b"), (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(tokenize("a&#" + std::to_string(cp) + ";b"), (std::vector<std::string>{"a", "b"}));
  }
  // U+000C cannot appear in XML 1.0, even as a reference: the build fails.
  EXPECT_EQ(wixtest::buildIndex(xhtml("<p>a&#12;b</p>")).status, BuildStatus::ParseError);
}

TEST(WordRules, BoundarySeparatorSetIsExactlySection5) {
  // Not separators: they stay in the word.
  for (const uint32_t cp : {0x200Cu, 0x200Du, 0x2060u, 0x180Eu, 0x85u}) {
    SCOPED_TRACE(cp);
    EXPECT_EQ(tokenize("a" + utf8(cp) + "b"), std::vector<std::string>{"a" + utf8(cp) + "b"});
  }
}

TEST(WordRules, BoundaryNbspEntity) {
  EXPECT_EQ(texts("<p>Mr.&#160;Darcy</p>"), (std::vector<std::string>{"Mr.", "Darcy"}));
}

TEST(WordRules, BoundaryElement) {
  for (const char* element :
       {"address",    "article", "aside",  "blockquote", "caption", "dd", "details", "div",     "dl",      "dt",
        "figcaption", "figure",  "footer", "h1",         "h2",      "h3", "h4",      "h5",      "h6",      "header",
        "hgroup",     "li",      "main",   "nav",        "ol",      "p",  "pre",     "section", "summary", "table",
        "tbody",      "td",      "tfoot",  "th",         "thead",   "tr", "ul"}) {
    SCOPED_TRACE(element);
    const std::string e = element;
    EXPECT_EQ(texts("<div>a<" + e + ">b</" + e + ">c</div>"), (std::vector<std::string>{"a", "b", "c"}));
  }
}

TEST(WordRules, BoundaryEmptyElement) {
  for (const char* element : {"br", "hr", "img", "image"}) {
    SCOPED_TRACE(element);
    EXPECT_EQ(texts(std::string("<p>a<") + element + "/>b</p>"), (std::vector<std::string>{"a", "b"}));
  }
}

TEST(WordRules, BoundaryOtherElementsDoNotSplit) {
  for (const char* element :
       {"b",    "i", "em", "strong", "span", "a",    "sup",      "sub",      "small", "code",     "abbr", "cite",  "q",
        "mark", "u", "s",  "label",  "ruby", "font", "noscript", "template", "form",  "fieldset", "menu", "dialog"}) {
    SCOPED_TRACE(element);
    const std::string e = element;
    EXPECT_EQ(texts("<p>a<" + e + ">b</" + e + ">c</p>"), std::vector<std::string>{"abc"});
  }
}

TEST(WordRules, BoundaryElementNamesAreCaseInsensitive) {
  EXPECT_EQ(texts("<div>a<P>b</P>c<BR/>d</div>"), (std::vector<std::string>{"a", "b", "c", "d"}));
}

TEST(WordRules, BoundaryDashSplitsAfterEmAndEnDash) {
  const std::string em = "\xE2\x80\x94";
  const std::string en = "\xE2\x80\x93";
  EXPECT_EQ(tokenize("forgotten" + em + "which"), (std::vector<std::string>{"forgotten" + em, "which"}));
  EXPECT_EQ(tokenize("1907" + en + "1925"), (std::vector<std::string>{"1907" + en, "1925"}));
  EXPECT_EQ(tokenize("a" + em + em + "b"), (std::vector<std::string>{"a" + em, em, "b"}));
  EXPECT_EQ(tokenize(em + "a"), (std::vector<std::string>{em, "a"}));
  EXPECT_EQ(tokenize("end" + em + " next"), (std::vector<std::string>{"end" + em, "next"}));
  EXPECT_EQ(tokenize(em), (std::vector<std::string>{em}));
}

TEST(WordRules, BoundaryDashSplitAcrossInlineTagsAndSoftHyphens) {
  EXPECT_EQ(texts("<p>forgotten<em>\xE2\x80\x94</em>which</p>"),
            (std::vector<std::string>{"forgotten\xE2\x80\x94", "which"}));
  EXPECT_EQ(texts("<p>a\xE2\x80\x94&#173;b</p>"), (std::vector<std::string>{"a\xE2\x80\x94", "b"}));
}

TEST(WordRules, BoundaryHyphensDoNotSplit) {
  EXPECT_EQ(tokenize("well-known a\xE2\x80\x90"
                     "b c\xE2\x80\x91"
                     "d e\xE2\x88\x92"
                     "f"),
            (std::vector<std::string>{"well-known",
                                      "a\xE2\x80\x90"
                                      "b",
                                      "c\xE2\x80\x91"
                                      "d",
                                      "e\xE2\x88\x92"
                                      "f"}));
}

// ------------------------------------------------------------------------------------------------
// Inside a word

TEST(WordRules, InsideSoftHyphenAndBomAreRemoved) {
  EXPECT_EQ(texts("<p>hy&#173;phens in&#xAD;side \xEF\xBB\xBFword&#65279;s</p>"),
            (std::vector<std::string>{"hyphens", "inside", "words"}));
  EXPECT_TRUE(texts("<p>&#173; &#65279;</p>").empty());
  // The removed code points stay inside the word's span: "hy&#173;phens" is 8 code points wide.
  const auto c = stream("<p>hy&#173;phens</p>");
  ASSERT_EQ(c.words.size(), 1u);
  EXPECT_EQ(c.words[0].end - c.words[0].start, 8u);
}

TEST(WordRules, InsideNfc) {
  EXPECT_EQ(tokenize("cafe\xCC\x81"), std::vector<std::string>{"caf\xC3\xA9"});
  EXPECT_EQ(texts("<p>cafe<b>&#769;</b> A&#778;</p>"), (std::vector<std::string>{"caf\xC3\xA9", "\xC3\x85"}));
}

TEST(WordRules, Inside255ByteSplit) {
  const auto c = stream("<p>" + std::string(300, 'a') + " " + std::string(255, 'b') + "</p>");
  ASSERT_EQ(c.words.size(), 3u);
  EXPECT_EQ(std::make_pair(c.words[0].text.size(), static_cast<int>(c.words[0].flags)),
            std::make_pair(size_t{255}, static_cast<int>(BYTE_SPLIT)));
  EXPECT_EQ(std::make_pair(c.words[1].text.size(), static_cast<int>(c.words[1].flags)), std::make_pair(size_t{45}, 0));
  EXPECT_EQ(std::make_pair(c.words[2].text.size(), static_cast<int>(c.words[2].flags)),
            std::make_pair(size_t{255}, static_cast<int>(PARA_END)));
  // The pieces are contiguous in the offset coordinate.
  EXPECT_EQ(c.words[0].end, c.words[1].start);
  std::string twoByte;
  for (int i = 0; i < 200; ++i) twoByte += "\xC3\xA9";  // 400 bytes; the cut falls at a code point boundary
  const auto pieces = tokenize(twoByte);
  ASSERT_EQ(pieces.size(), 2u);
  EXPECT_EQ(pieces[0].size(), 254u);
  EXPECT_EQ(pieces[1].size(), 146u);
  EXPECT_EQ(pieces[0] + pieces[1], twoByte);
}

TEST(WordRules, InsidePunctuationOnlyTokensAreWords) {
  EXPECT_EQ(tokenize("\xE2\x80\x94 { &lt; ..."), (std::vector<std::string>{"\xE2\x80\x94", "{", "<", "..."}));
}

// ------------------------------------------------------------------------------------------------
// Flags

TEST(WordRules, FlagParaEnd) {
  EXPECT_EQ(flags("<h1>Title</h1><p>a b</p><div>c<img src='x.png' alt='alt words'/>d<br/>e</div><p>f</p>"),
            (Flagged{{"Title", HEADING | PARA_END},
                     {"a", 0},
                     {"b", PARA_END},
                     {"c", 0},
                     {"d", LINE_END},
                     {"e", PARA_END},
                     {"f", PARA_END}}));
}

TEST(WordRules, FlagParaEndOnTheChapterLastWord) {
  const auto f = flags("last words");
  ASSERT_FALSE(f.empty());
  EXPECT_EQ(f.back(), (std::pair<std::string, int>{"words", PARA_END}));
}

TEST(WordRules, FlagLineEnd) {
  EXPECT_EQ(flags("<p>one<br/>two<br/><br/>three</p>"),
            (Flagged{{"one", LINE_END}, {"two", LINE_END}, {"three", PARA_END}}));
}

TEST(WordRules, FlagHeading) {
  EXPECT_EQ(flags("<h2>A <em>b</em></h2><h6>c</h6><p>d</p>"),
            (Flagged{{"A", HEADING}, {"b", HEADING | PARA_END}, {"c", HEADING | PARA_END}, {"d", PARA_END}}));
}

TEST(WordRules, FlagNonproseAndRegionsForTheOutermostTableAndPre) {
  const auto c = stream(
      "<p>x</p><table><caption>cap</caption><tr><td>a<table><tr><td>b</td></tr></table></td>"
      "<td><pre>c d</pre></td></tr></table><pre>e <code>f</code></pre><p>g <code>h</code></p>");
  std::vector<std::string> t;
  std::vector<size_t> nonprose;
  for (size_t i = 0; i < c.words.size(); ++i) {
    t.push_back(c.words[i].text);
    if (c.words[i].flags & NONPROSE) nonprose.push_back(i);
  }
  EXPECT_EQ(t, (std::vector<std::string>{"x", "cap", "a", "b", "c", "d", "e", "f", "g", "h"}));
  EXPECT_EQ(nonprose, (std::vector<size_t>{1, 2, 3, 4, 5, 6, 7}));
  EXPECT_EQ(regions(c), (std::vector<Region>{{"TABLE", 1, 6}, {"CODE", 6, 8}}));
}

TEST(WordRules, FlagNoteref) {
  EXPECT_EQ(flags("<p>a<sup><a href=\"#n\">1</a></sup></p>"), (Flagged{{"a", NOTEREF | PARA_END}}));
}

TEST(WordRules, FlagNoSentenceEnd) {
  for (const auto& w : stream("<p>One. Two! Three? Four</p>").words) EXPECT_EQ(w.flags & 1, 0) << w.text;
}

// ------------------------------------------------------------------------------------------------
// Anchors

TEST(WordRules, AnchorIndexIsWordsStartedBeforeIt) {
  const auto c = stream(
      "<p><span id=\"rsvp-s0002-start\"></span>A b here.<span id=\"rsvp-s0002-end\"></span></p>"
      "<p id=\"rsvp-x\">c</p><p id=\"other\">d</p>");
  std::vector<std::pair<std::string, uint32_t>> got;
  for (const auto& a : c.anchors) got.emplace_back(std::string(a.idView()), a.wordIndex);
  EXPECT_EQ(got, (std::vector<std::pair<std::string, uint32_t>>{
                     {"rsvp-s0002-start", 0}, {"rsvp-s0002-end", 3}, {"rsvp-x", 3}}));
}

TEST(WordRules, AnchorStrictlyInsideAWordIsReported) {
  const std::string em = "\xE2\x80\x94";
  const wixtest::Built inside = wixtest::buildIndex(xhtml(
      "<p>wo<span id=\"rsvp-a\"></span>rd and<span id=\"rsvp-b\"></span> more" + em + "<span id=\"rsvp-c\"/>x</p>"));
  EXPECT_EQ(inside.status, BuildStatus::AnchorInsideWord);
  EXPECT_STREQ(inside.report.anchorId, "rsvp-a");
  EXPECT_EQ(inside.report.wordIndex, 0u);
  // Without the first: after a word followed by a space, and after a dash split, are boundaries.
  const auto c = stream("<p>word and<span id=\"rsvp-b\"></span> more" + em + "<span id=\"rsvp-c\"/>x</p>");
  ASSERT_EQ(c.anchors.size(), 2u);
  EXPECT_EQ(c.anchors[0].wordIndex, 2u);
  EXPECT_EQ(c.anchors[1].wordIndex, 3u);
}

TEST(WordRules, AnchorOutsideBodyOrInHeadIsIgnored) {
  const std::string doc =
      "<?xml version=\"1.0\"?><!DOCTYPE html><html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title "
      "id=\"rsvp-t\">T</title></head><body><p>a</p></body></html>";
  wixtest::Built built = wixtest::buildIndex(doc);
  ASSERT_EQ(built.status, BuildStatus::Ok);
  EXPECT_TRUE(wixtest::readAll(built.file).anchors.empty());
}

TEST(WordRules, AnchorIdLongerThanTheFormatAllowsIsAnError) {
  const wixtest::Built built =
      wixtest::buildIndex(xhtml("<p>a<span id=\"rsvp-" + std::string(300, 'x') + "\"/> b</p>"));
  EXPECT_EQ(built.status, BuildStatus::AnchorIdTooLong);
}

// ------------------------------------------------------------------------------------------------
// Markers

TEST(WordRules, MarkerImageAtTheNextWordWithNoAltText) {
  const auto c = stream(
      "<p>a</p><figure><img src=\"x.png\" alt=\"never a word\"/><figcaption>Cap tion</figcaption></figure>"
      "<p>b<img src=\"y.png\" alt=\"z\"/></p><svg xmlns=\"http://www.w3.org/2000/svg\"><image href=\"i.png\"/></svg>");
  std::vector<std::string> t;
  for (const auto& w : c.words) t.push_back(w.text);
  EXPECT_EQ(t, (std::vector<std::string>{"a", "Cap", "tion", "b"}));
  EXPECT_EQ(regions(c), (std::vector<Region>{{"IMAGE", 1, 1}, {"IMAGE", 4, 4}, {"IMAGE", 4, 4}}));
  EXPECT_FALSE(c.words[1].flags & NONPROSE);  // a caption is prose
}

TEST(WordRules, MarkerNoImageMarkerInsideATableOrPre) {
  const auto c = stream("<table><tr><td><img src=\"x.png\" alt=\"\"/>a</td></tr></table>");
  EXPECT_EQ(regions(c), (std::vector<Region>{{"TABLE", 0, 1}}));
}

TEST(WordRules, MarkerNoteRegion) {
  for (const char* attrs :
       {"epub:type=\"footnote\"", "epub:type=\"endnote\"", "epub:type=\"endnotes\"", "epub:type=\"x footnote\"",
        "role=\"doc-footnote\"", "role=\"doc-endnote\"", "role=\"doc-endnotes\""}) {
    for (const char* element : {"aside", "section", "div", "li", "p"}) {
      SCOPED_TRACE(std::string(element) + " " + attrs);
      const std::string e = element;
      const auto c = stream("<p>text</p><" + e + " " + attrs + "><p>Note words.</p></" + e + "><p>after</p>");
      std::vector<std::string> t;
      std::vector<int> f;
      for (const auto& w : c.words) {
        t.push_back(w.text);
        f.push_back(w.flags & NOTE);
      }
      EXPECT_EQ(t, (std::vector<std::string>{"text", "Note", "words.", "after"}));  // note words stay in the stream
      EXPECT_EQ(f, (std::vector<int>{0, NOTE, NOTE, 0}));
      EXPECT_EQ(regions(c), (std::vector<Region>{{"NOTE", 1, 3}}));
    }
  }
}

TEST(WordRules, MarkerNoteRegionIsTheOutermostBody) {
  const auto c = stream(
      "<section epub:type=\"endnotes\"><ol><li epub:type=\"endnote\"><p>One.</p></li>"
      "<li epub:type=\"endnote\"><p>Two.</p></li></ol></section>");
  EXPECT_EQ(regions(c), (std::vector<Region>{{"NOTE", 0, 2}}));
}

TEST(WordRules, MarkerOtherNoteTypesAreNotNoteRegions) {
  for (const char* attrs :
       {"epub:type=\"rearnote\"", "epub:type=\"note\"", "epub:type=\"footnotes\"", "role=\"note\""}) {
    SCOPED_TRACE(attrs);
    EXPECT_TRUE(stream(std::string("<aside ") + attrs + "><p>x</p></aside>").markers.empty());
  }
}

// Beyond the Python suite: what the firmware adds.

TEST(WordRules, MarkerInsideANoteIsFlagged) {
  const auto c = stream(
      "<p>a</p><aside epub:type=\"footnote\"><p>n<img src=\"x.png\" alt=\"\"/>m</p><table><tr><td>t</td></tr></table>"
      "</aside><p>b</p>");
  ASSERT_EQ(c.markers.size(), 3u);
  EXPECT_EQ(regions(c), (std::vector<Region>{{"NOTE", 1, 4}, {"IMAGE", 2, 2}, {"TABLE", 3, 4}}));
  EXPECT_EQ(c.markers[0].flags, 0);
  EXPECT_EQ(c.markers[1].flags, MARKER_IN_NOTE);
  EXPECT_EQ(c.markers[2].flags, MARKER_IN_NOTE);
  EXPECT_EQ(c.words[3].flags & (NOTE | NONPROSE), NOTE | NONPROSE);
}

TEST(WordRules, NestedTablesAreOneRegion) {
  const auto c = stream(
      "<table><tr><td><table><tr><td>a</td></tr></table></td></tr></table><table><tr><td>b<pre>c</pre></td></tr>"
      "</table><p>d</p>");
  EXPECT_EQ(regions(c), (std::vector<Region>{{"TABLE", 0, 1}, {"TABLE", 1, 3}}));
}

TEST(WordRules, NoterefCrossDocumentLinks) {
  // A mark linking to an endnote in another document is detected the same way (fixture forms).
  EXPECT_EQ(flags("<p>disturbed.<sup><a href=\"endnotes.xhtml#fn2\">2</a></sup></p><p>engine.<a "
                  "href=\"endnotes.xhtml#fn3\"><sup>3</sup></a> More</p>"),
            (Flagged{{"disturbed.", NOTEREF | PARA_END}, {"engine.", NOTEREF}, {"More", PARA_END}}));
}

TEST(WordRules, DocumentThatIsEntirelyANoteRegion) {
  const auto c = stream(
      "<section epub:type=\"endnotes\" role=\"doc-endnotes\"><h1>Notes</h1><ol><li epub:type=\"endnote\" "
      "id=\"fn2\"><p>One two.</p></li></ol></section>");
  ASSERT_EQ(c.words.size(), 3u);
  for (const auto& w : c.words) EXPECT_TRUE(w.flags & NOTE) << w.text;
  EXPECT_EQ(regions(c), (std::vector<Region>{{"NOTE", 0, 3}}));
}

TEST(WordRules, TextAfterTheEndOfHtmlIsIgnoredAsByTheRenderer) {
  wixtest::Built built = wixtest::buildIndex(xhtml("<p>a b</p>") + "\n<p>trailing junk");
  ASSERT_EQ(built.status, BuildStatus::Ok);
  EXPECT_EQ(wixtest::readAll(built.file).words.size(), 2u);
}

// ------------------------------------------------------------------------------------------------
// Plain-text splitting

TEST(WordRules, TokenizePlainText) {
  EXPECT_EQ(tokenize("  Mr. Darcy\xE2\x80\x94who hy\xC2\xADphen  "),
            (std::vector<std::string>{"Mr.", "Darcy\xE2\x80\x94", "who", "hyphen"}));
  EXPECT_TRUE(tokenize("").empty());
}

// ------------------------------------------------------------------------------------------------
// The C++ prototype's worked example (test/spike_word_stream/fixture_s1.xhtml), as
// tools/prep/tests/test_words.py checks it.

namespace {

const std::vector<std::string> SPIKE_WORDS = {"Chapter",
                                              "One",
                                              "Spacing",
                                              "works",
                                              "because",
                                              "it",
                                              "forces",
                                              "effortful",
                                              "recall.",
                                              "It",
                                              "also",
                                              "exposes",
                                              "what",
                                              "was",
                                              "forgotten\xE2\x80\x94",
                                              "which",
                                              "is",
                                              "useful.",
                                              "Mr.",
                                              "Darcy",
                                              "agreed,",
                                              "e.g.",
                                              "in",
                                              "the",
                                              "well-known",
                                              "letter.",
                                              "Soft",
                                              "hyphens",
                                              "stay",
                                              "inside",
                                              "one",
                                              "word,",
                                              "and",
                                              "boldface",
                                              "joins",
                                              "across",
                                              "inline",
                                              "tags.",
                                              "Figure",
                                              "1.",
                                              "Recall",
                                              "over",
                                              "time.",
                                              "Interval",
                                              "Recall",
                                              "1",
                                              "day",
                                              "80%",
                                              "for",
                                              "(int",
                                              "i",
                                              "=",
                                              "0;",
                                              "i",
                                              "<",
                                              "n;",
                                              "i++)",
                                              "{",
                                              "review(card[i]);",
                                              "}",
                                              "The",
                                              "last",
                                              "sentence",
                                              "of",
                                              "the",
                                              "section",
                                              "ends",
                                              "here.",
                                              "A",
                                              "new",
                                              "section",
                                              "begins",
                                              "with",
                                              "a",
                                              "page",
                                              "marker",
                                              "hidden",
                                              "in",
                                              "the",
                                              "middle.",
                                              "1.",
                                              "See",
                                              "the",
                                              "appendix."};

wixtest::Contents spike() {
  wixtest::Built built = wixtest::buildIndex(wixtest::readFile(WIX_SPIKE_FIXTURE));
  EXPECT_EQ(built.status, BuildStatus::Ok);
  return wixtest::readAll(built.file);
}

}  // namespace

TEST(WordRules, SpikeParityWords) {
  std::vector<std::string> t;
  for (const auto& w : spike().words) t.push_back(w.text);
  EXPECT_EQ(t, SPIKE_WORDS);
}

TEST(WordRules, SpikeParityAnchors) {
  std::vector<std::pair<std::string, uint32_t>> got;
  for (const auto& a : spike().anchors) got.emplace_back(std::string(a.idView()), a.wordIndex);
  EXPECT_EQ(got,
            (std::vector<std::pair<std::string, uint32_t>>{
                {"rsvp-s0001-start", 2}, {"rsvp-s0001-end", 68}, {"rsvp-s0002-start", 68}, {"rsvp-s0002-end", 80}}));
}

TEST(WordRules, SpikeParityMarkers) {
  EXPECT_EQ(regions(spike()),
            (std::vector<Region>{{"IMAGE", 38, 38}, {"TABLE", 43, 48}, {"CODE", 48, 60}, {"NOTE", 80, 84}}));
}

TEST(WordRules, SpikeParityFlags) {
  const auto c = spike();
  ASSERT_EQ(c.words.size(), SPIKE_WORDS.size());
  // tools/prep/tests/test_words.py SPIKE_PRINTED_FLAGS, plus NOTE on the footnote body (words 80-83).
  const std::vector<std::pair<int, int>> printed = {{0, HEADING},
                                                    {1, PARA_END | HEADING},
                                                    {2, 0},
                                                    {8, NOTEREF},
                                                    {9, 0},
                                                    {14, 0},
                                                    {15, 0},
                                                    {16, 0},
                                                    {17, 0},
                                                    {18, 0},
                                                    {19, 0},
                                                    {20, 0},
                                                    {21, 0},
                                                    {24, 0},
                                                    {25, PARA_END},
                                                    {26, 0},
                                                    {27, 0},
                                                    {33, 0},
                                                    {37, PARA_END},
                                                    {38, 0},
                                                    {39, 0},
                                                    {42, PARA_END},
                                                    {43, PARA_END | NONPROSE},
                                                    {47, PARA_END | NONPROSE},
                                                    {48, NONPROSE},
                                                    {58, NONPROSE},
                                                    {59, PARA_END | NONPROSE},
                                                    {60, 0},
                                                    {67, PARA_END},
                                                    {68, 0},
                                                    {71, 0},
                                                    {72, 0},
                                                    {79, PARA_END},
                                                    {80, NOTE},
                                                    {81, NOTE},
                                                    {82, NOTE},
                                                    {83, PARA_END | NOTE}};
  for (const auto& [index, expected] : printed) EXPECT_EQ(c.words[index].flags, expected) << index;
}
