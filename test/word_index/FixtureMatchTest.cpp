// Every spine document of fixtures/books/fixture.epub, indexed and compared with
// fixtures/books/fixture.expected.json (version 2): word texts, every flag run, every marker, block
// boundaries, footnote marks, token cases; and the fixture as tools/prep builds it (its anchors) with
// each section's [start, end). A member missing from the expected file fails the test (FixtureBook.h).

#include <gtest/gtest.h>

#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "FixtureBook.h"
#include "WordIndexTestSupport.h"

using namespace wordindex;

namespace {

constexpr const char* FLAG_NAMES[] = {"PARA_END", "LINE_END", "NONPROSE", "NOTEREF", "HEADING", "BYTE_SPLIT"};

uint8_t flagBit(const std::string& name) {
  if (name == "PARA_END") return PARA_END;
  if (name == "LINE_END") return LINE_END;
  if (name == "NONPROSE") return NONPROSE;
  if (name == "NOTEREF") return NOTEREF;
  if (name == "HEADING") return HEADING;
  if (name == "BYTE_SPLIT") return BYTE_SPLIT;
  ADD_FAILURE() << "unknown flag name " << name;
  return 0;
}

std::vector<std::pair<size_t, size_t>> runs(const std::vector<wixtest::Word>& words, const uint8_t bit) {
  std::vector<std::pair<size_t, size_t>> out;
  for (size_t i = 0; i < words.size(); ++i) {
    if (!(words[i].flags & bit)) continue;
    if (!out.empty() && out.back().second == i) {
      out.back().second = i + 1;
    } else {
      out.emplace_back(i, i + 1);
    }
  }
  return out;
}

// "<file> <size> <crc32>" lines written by prepare_test_data.py from the zip central directories.
std::map<std::string, std::pair<uint32_t, uint32_t>> zipSources() {
  std::map<std::string, std::pair<uint32_t, uint32_t>> out;
  std::istringstream in(wixtest::readFile(WIX_DATA_DIR "/sources.txt"));
  std::string name;
  uint64_t size = 0;
  uint64_t crc = 0;
  while (in >> name >> size >> crc) out[name] = {static_cast<uint32_t>(size), static_cast<uint32_t>(crc)};
  EXPECT_FALSE(out.empty());
  return out;
}

struct Tally {
  size_t documents = 0;
  size_t words = 0;
  size_t flagRuns = 0;
  size_t markers = 0;
  size_t blocks = 0;
  size_t noterefs = 0;
  size_t tokenCases = 0;
  size_t anchors = 0;
  size_t differences = 0;
};

#define COMPARE_EQ(tally, a, b)        \
  do {                                 \
    const bool same = (a) == (b);      \
    if (!same) ++(tally).differences;  \
    EXPECT_TRUE(same) << #a " != " #b; \
  } while (0)

}  // namespace

TEST(FixtureMatch, EveryDocumentMatchesTheExpectedFile) {
  const testjson::Value& expected = rsvptest::fixtureExpected();
  EXPECT_EQ(rsvptest::requireIndex(expected, "rules_version", "fixture.expected.json"), RULES_VERSION);
  const auto& documents = rsvptest::requireArray(expected, "documents", "fixture.expected.json");
  ASSERT_EQ(documents.size(), 5u);
  const auto sources = zipSources();
  Tally t;

  for (size_t d = 0; d < documents.size(); ++d) {
    const auto& document = documents[d];
    const std::string& href = rsvptest::requireString(document, "href", "document");
    const std::string& zipPath = rsvptest::requireString(document, "zip_path", href);
    SCOPED_TRACE(href);
    wixtest::Built built = wixtest::buildIndex(wixtest::readFile(std::string(WIX_DATA_DIR "/original/") + zipPath));
    ASSERT_EQ(built.status, BuildStatus::Ok) << buildStatusName(built.status);
    const wixtest::Contents c = wixtest::readAll(built.file);
    ++t.documents;

    // Source identity: what the builder saw is the zip entry (size and CRC-32 from its directory).
    const auto source = sources.at("original/" + zipPath);
    EXPECT_EQ(built.report.header.sourceBytes, source.first);
    EXPECT_EQ(built.report.header.sourceCrc32, source.second);

    // Words, in order.
    const auto& words = rsvptest::requireArray(document, "words", href);
    EXPECT_EQ(rsvptest::requireIndex(document, "word_count", href), static_cast<long>(words.size()));
    COMPARE_EQ(t, c.words.size(), words.size());
    for (size_t i = 0; i < words.size() && i < c.words.size(); ++i) {
      COMPARE_EQ(t, c.words[i].text, words[i].string);
      EXPECT_EQ(c.words[i].flags & 1, 0) << "bit 0 (sentence end) is never set";
    }
    t.words += c.words.size();

    // Every flag run. The expected file must list exactly the six flags.
    const auto& flagObject = rsvptest::requireMember(document, "flags", testjson::Value::Kind::OBJECT, href);
    EXPECT_EQ(flagObject.object.size(), std::size(FLAG_NAMES));
    for (const char* name : FLAG_NAMES) {
      const auto want = rsvptest::fixtureFlagRuns(document, name);
      COMPARE_EQ(t, runs(c.words, flagBit(name)), want);
      t.flagRuns += want.size();
    }

    // Every marker, in document order. NOTE words carry the NOTE flag, and only they do.
    const auto markers = rsvptest::fixtureMarkers(document);
    COMPARE_EQ(t, c.markers.size(), markers.size());
    std::vector<bool> inNote(c.words.size(), false);
    for (size_t i = 0; i < markers.size() && i < c.markers.size(); ++i) {
      COMPARE_EQ(t, std::string(wixtest::markerKindName(c.markers[i].kind)), markers[i].kind);
      COMPARE_EQ(t, c.markers[i].wordStart, markers[i].start);
      COMPARE_EQ(t, c.markers[i].wordEnd, markers[i].end);
      EXPECT_EQ(c.markers[i].flags, 0);  // the fixture has no image or table inside a note
      if (markers[i].kind == "NOTE") {
        for (size_t w = markers[i].start; w < markers[i].end && w < inNote.size(); ++w) inNote[w] = true;
      }
      ++t.markers;
    }
    for (size_t w = 0; w < c.words.size(); ++w) EXPECT_EQ((c.words[w].flags & NOTE) != 0, inNote[w]) << w;

    // Blocks: each starts after a paragraph end (or at word 0) and ends on one; a table or code block
    // is exactly its region, an image block starts at its IMAGE marker.
    const auto& blocks = rsvptest::requireArray(document, "blocks", href);
    for (size_t b = 0; b < blocks.size(); ++b) {
      const std::string where = href + " blocks[" + std::to_string(b) + "]";
      const long first = rsvptest::requireIndex(blocks[b], "first_word", where);
      const long count = rsvptest::requireIndex(blocks[b], "word_count", where);
      const std::string& kind = rsvptest::requireString(blocks[b], "kind", where);
      ASSERT_GE(first, 0);
      ASSERT_LE(static_cast<size_t>(first + count), c.words.size()) << where;
      const bool startsAfterParagraphEnd = first == 0 || (c.words[static_cast<size_t>(first - 1)].flags & PARA_END);
      COMPARE_EQ(t, startsAfterParagraphEnd, true);
      if (count > 0) COMPARE_EQ(t, (c.words[static_cast<size_t>(first + count - 1)].flags & PARA_END) != 0, true);
      if (kind == "table" || kind == "code") {
        bool found = false;
        for (const auto& m : c.markers) {
          found = found ||
                  (m.wordStart == static_cast<uint32_t>(first) && m.wordEnd == static_cast<uint32_t>(first + count));
        }
        COMPARE_EQ(t, found, true);
      }
      if (kind == "image") {
        bool found = false;
        for (const auto& m : c.markers)
          found = found || (m.kind == MARKER_IMAGE && m.wordStart == static_cast<uint32_t>(first));
        COMPARE_EQ(t, found, true);
      }
      if (kind == "footnote") {
        for (long w = first; w < first + count; ++w)
          COMPARE_EQ(t, (c.words[static_cast<size_t>(w)].flags & NOTE) != 0, true);
      }
      ++t.blocks;
    }

    // Footnote marks: the word each mark follows carries NOTEREF, and no other word does.
    const auto& noterefs = rsvptest::requireArray(document, "noterefs", href);
    std::set<size_t> markWords;
    for (size_t i = 0; i < noterefs.size(); ++i) {
      markWords.insert(static_cast<size_t>(rsvptest::requireIndex(noterefs[i], "word", href + " noterefs")));
      ++t.noterefs;
    }
    std::set<size_t> flagged;
    for (size_t w = 0; w < c.words.size(); ++w) {
      if (c.words[w].flags & NOTEREF) flagged.insert(w);
    }
    COMPARE_EQ(t, flagged, markWords);

    // Footnote bodies lie inside NOTE regions.
    const auto& footnotes = rsvptest::requireArray(document, "footnotes", href);
    for (size_t i = 0; i < footnotes.size(); ++i) {
      const long start = rsvptest::requireIndex(footnotes[i], "start", href + " footnotes");
      const long end = rsvptest::requireIndex(footnotes[i], "end", href + " footnotes");
      for (long w = start; w < end && static_cast<size_t>(w) < c.words.size(); ++w) {
        COMPARE_EQ(t, (c.words[static_cast<size_t>(w)].flags & NOTE) != 0, true);
      }
    }

    // Inline elements are inside their word; a page-break marker's text is in no word.
    for (const char* member : {"inline_elements", "page_breaks"}) {
      const auto& items = rsvptest::requireArray(document, member, href);
      for (size_t i = 0; i < items.size(); ++i) {
        const long w = rsvptest::requireIndex(items[i], "word", href + " " + member);
        const std::string& text = rsvptest::requireString(items[i], "text", href + " " + member);
        ASSERT_LT(static_cast<size_t>(w), c.words.size());
        const bool inside = c.words[static_cast<size_t>(w)].text.find(text) != std::string::npos;
        COMPARE_EQ(t, inside, std::string(member) == "inline_elements");
      }
    }
  }

  // Token cases: the word at the index, with exactly the listed flags.
  const auto& cases = rsvptest::requireArray(expected, "token_cases", "fixture.expected.json");
  std::map<std::string, wixtest::Contents> byHref;
  for (size_t i = 0; i < cases.size(); ++i) {
    const std::string where = "token_cases[" + std::to_string(i) + "]";
    const std::string& href = rsvptest::requireString(cases[i], "href", where);
    if (!byHref.count(href)) {
      const std::string& zipPath = rsvptest::requireString(rsvptest::fixtureDocument(href), "zip_path", href);
      wixtest::Built built = wixtest::buildIndex(wixtest::readFile(std::string(WIX_DATA_DIR "/original/") + zipPath));
      byHref[href] = wixtest::readAll(built.file);
    }
    const auto& c = byHref[href];
    const long index = rsvptest::requireIndex(cases[i], "word_index", where);
    ASSERT_LT(static_cast<size_t>(index), c.words.size()) << where;
    const wixtest::Word& w = c.words[static_cast<size_t>(index)];
    COMPARE_EQ(t, w.text, rsvptest::requireString(cases[i], "word", where));
    uint8_t want = 0;
    const auto& names = rsvptest::requireArray(cases[i], "flags", where);
    for (size_t k = 0; k < names.size(); ++k) want |= flagBit(names[k].string);
    COMPARE_EQ(t, static_cast<int>(w.flags & ~NOTE), static_cast<int>(want));
    ++t.tokenCases;
  }

  // The prepared book: each section's anchors resolve to [start, end), and the words are unchanged.
  const auto& sections = rsvptest::requireArray(expected, "sections", "fixture.expected.json");
  ASSERT_GT(sections.size(), 0u);
  for (size_t i = 0; i < sections.size(); ++i) {
    const std::string where = "sections[" + std::to_string(i) + "]";
    const std::string& href = rsvptest::requireString(sections[i], "chapter_href", where);
    SCOPED_TRACE(where + " " + href);
    const std::string& zipPath = rsvptest::requireString(rsvptest::fixtureDocument(href), "zip_path", href);
    wixtest::MemoryFile prepared;
    {
      wixtest::MemoryInput input(wixtest::readFile(std::string(WIX_DATA_DIR "/prepared/") + zipPath));
      WordIndexBuilder builder;
      ASSERT_EQ(builder.build(input, prepared), BuildStatus::Ok) << buildStatusName(builder.report().status);
      const auto source = sources.at("prepared/" + zipPath);
      EXPECT_EQ(builder.report().header.sourceCrc32, source.second);
    }
    WordIndexReader reader;
    ASSERT_EQ(reader.open(prepared), ReadStatus::Ok);
    Anchor start;
    Anchor end;
    ASSERT_TRUE(reader.findAnchor(rsvptest::requireString(sections[i], "start_anchor", where), start));
    ASSERT_TRUE(reader.findAnchor(rsvptest::requireString(sections[i], "end_anchor", where), end));
    COMPARE_EQ(t, static_cast<long>(start.wordIndex), rsvptest::requireIndex(sections[i], "start", where));
    COMPARE_EQ(t, static_cast<long>(end.wordIndex), rsvptest::requireIndex(sections[i], "end", where));
    COMPARE_EQ(t, static_cast<long>(end.wordIndex - start.wordIndex),
               rsvptest::requireIndex(sections[i], "word_count", where));
    WordInfo first;
    WordInfo last;
    ASSERT_TRUE(reader.word(start.wordIndex, first));
    COMPARE_EQ(t, std::string(first.text), rsvptest::requireString(sections[i], "first_word_text", where));
    ASSERT_TRUE(reader.word(end.wordIndex - 1, last));
    COMPARE_EQ(t, std::string(last.text), rsvptest::requireString(sections[i], "last_word_text", where));
    t.anchors += 2;
  }

  // Anchors change no word: the prepared documents index to the same words and flags.
  for (size_t d = 0; d < documents.size(); ++d) {
    const std::string& zipPath = rsvptest::requireString(documents[d], "zip_path", "document");
    wixtest::Built original = wixtest::buildIndex(wixtest::readFile(std::string(WIX_DATA_DIR "/original/") + zipPath));
    wixtest::Built prepared = wixtest::buildIndex(wixtest::readFile(std::string(WIX_DATA_DIR "/prepared/") + zipPath));
    const auto a = wixtest::readAll(original.file);
    const auto b = wixtest::readAll(prepared.file);
    std::vector<std::pair<std::string, int>> wa;
    std::vector<std::pair<std::string, int>> wb;
    for (const auto& w : a.words) wa.emplace_back(w.text, w.flags);
    for (const auto& w : b.words) wb.emplace_back(w.text, w.flags);
    COMPARE_EQ(t, wa, wb);
  }

  std::printf(
      "\n== Fixture match: %zu documents, %zu words, %zu flag runs, %zu markers, %zu blocks, %zu footnote marks, "
      "%zu token cases, %zu section anchors (prepared book) compared; %zu differences\n",
      t.documents, t.words, t.flagRuns, t.markers, t.blocks, t.noterefs, t.tokenCases, t.anchors, t.differences);
  EXPECT_EQ(t.differences, 0u);
}

// The expected file's endnotes document is one NOTE region: zero prose words.
TEST(FixtureMatch, EndnotesDocumentHasNoProseWord) {
  const auto& document = rsvptest::fixtureDocument("text/endnotes.xhtml");
  const std::string& zipPath = rsvptest::requireString(document, "zip_path", "endnotes");
  wixtest::Built built = wixtest::buildIndex(wixtest::readFile(std::string(WIX_DATA_DIR "/original/") + zipPath));
  ASSERT_EQ(built.status, BuildStatus::Ok);
  const auto c = wixtest::readAll(built.file);
  ASSERT_FALSE(c.words.empty());
  for (const auto& w : c.words) EXPECT_TRUE(w.flags & NOTE) << w.text;
}
