// A truncated or edited index is detected or read safely; malformed XHTML and failing I/O give a
// defined error. The target runs under AddressSanitizer and UndefinedBehaviorSanitizer, so an
// over-read or overflow aborts the test.

#include <gtest/gtest.h>

#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "WordIndexTestSupport.h"

using namespace wordindex;

namespace {

std::string chapter2() { return wixtest::readFile(WIX_DATA_DIR "/original/OEBPS/text/chapter-2.xhtml"); }

// Reads everything an index offers. Returns true when every call succeeded; false when the reader
// reported an error (which must then be Corrupt or IoError and stick).
bool exercise(wixtest::MemoryFile& file) {
  WordIndexReader reader;
  if (reader.open(file) != ReadStatus::Ok) return false;
  bool ok = true;
  const uint32_t count = reader.wordCount();
  for (uint32_t i = 0; i < count; ++i) {
    WordInfo w;
    ok = reader.word(i, w) && ok;
  }
  for (uint32_t i = count; i-- > 0;) {
    WordInfo w;
    ok = reader.word(i, w) && ok;
  }
  for (uint32_t offset = 0; offset <= reader.header().visibleCodepoints; offset += 7) {
    uint32_t w = 0;
    ok = reader.firstWordEndingAfter(offset, w) && ok;
  }
  for (uint32_t i = 0; i < reader.markerCount(); ++i) {
    Marker m;
    ok = reader.marker(i, m) && ok;
  }
  for (uint32_t i = 0; i <= count; i += 5) {
    Marker m;
    reader.regionContaining(i, REGION_MARKERS, m);
    reader.nextMarker(i, ANY_MARKER, m);
    reader.imageAt(i);
  }
  AnchorCursor cursor;
  Anchor a;
  while (reader.nextAnchor(cursor, a)) {
  }
  reader.findAnchor("rsvp-s0004-end", a);
  if (reader.status() != ReadStatus::Ok) {
    EXPECT_TRUE(reader.status() == ReadStatus::Corrupt || reader.status() == ReadStatus::IoError);
    WordInfo w;
    EXPECT_FALSE(reader.word(0, w));  // the error sticks
    return false;
  }
  return ok;
}

// Bytes no reader looks at: the rest of the header slot, each block slot after its used bytes, and
// the builder's spill slots.
bool isPadding(const std::vector<uint8_t>& file, const Header& h, const size_t at) {
  if (at >= HEADER_BYTES && at < SLOT_BYTES) return true;
  if (at < SLOT_BYTES || at >= h.directoryOffset) return false;
  const size_t slot = at / SLOT_BYTES;
  if (file[slot * SLOT_BYTES] == SLOT_SPILL) return true;
  return at - slot * SLOT_BYTES >= readU16(file.data() + slot * SLOT_BYTES + 2);
}

wixtest::Built preparedChapter2() {
  return wixtest::buildIndex(wixtest::readFile(WIX_DATA_DIR "/prepared/OEBPS/text/chapter-2.xhtml"));
}

}  // namespace

TEST(Robustness, EveryTruncationIsDetected) {
  auto built = preparedChapter2();
  ASSERT_EQ(built.status, BuildStatus::Ok);
  const std::vector<uint8_t> whole = built.file.bytes;
  for (size_t length = 0; length < whole.size(); ++length) {
    wixtest::MemoryFile file;
    file.bytes.assign(whole.begin(), whole.begin() + static_cast<long>(length));
    WordIndexReader reader;
    const ReadStatus status = reader.open(file);
    ASSERT_NE(status, ReadStatus::Ok) << "length " << length;
    ASSERT_TRUE(status == ReadStatus::Truncated || status == ReadStatus::BadMagic) << readStatusName(status);
    WordInfo w;
    ASSERT_FALSE(reader.word(0, w));
  }
  // Bytes appended after the end are detected too.
  wixtest::MemoryFile longer;
  longer.bytes = whole;
  longer.bytes.push_back(0);
  WordIndexReader reader;
  EXPECT_EQ(reader.open(longer), ReadStatus::Corrupt);
}

TEST(Robustness, EverySingleByteChangeOutsidePaddingIsDetected) {
  auto built = preparedChapter2();
  ASSERT_EQ(built.status, BuildStatus::Ok);
  const std::vector<uint8_t> whole = built.file.bytes;
  const Header& h = built.report.header;
  size_t detected = 0;
  size_t padding = 0;
  for (size_t at = 0; at < whole.size(); ++at) {
    const bool unused = isPadding(whole, h, at);
    wixtest::MemoryFile file;
    file.bytes = whole;
    file.bytes[at] ^= 0x5A;
    const bool readOk = exercise(file);
    if (unused) {
      ++padding;
      EXPECT_TRUE(readOk) << "padding byte " << at;
      continue;
    }
    ASSERT_FALSE(readOk) << "byte " << at << " changed and not detected";
    ++detected;
  }
  std::printf("\n== Single-byte edits of a %zu-byte index: %zu detected, %zu in padding (read unchanged)\n",
              whole.size(), detected, padding);
}

TEST(Robustness, RandomEditsAreDetectedOrReadSafely) {
  auto built = wixtest::buildIndex(wixtest::readFile(WIX_DATA_DIR "/test_reader_menu_progress_chapter1.xhtml"));
  ASSERT_EQ(built.status, BuildStatus::Ok);
  const std::vector<uint8_t> whole = built.file.bytes;
  std::mt19937 rng(1234);
  size_t detected = 0;
  size_t clean = 0;
  for (int round = 0; round < 1500; ++round) {
    wixtest::MemoryFile file;
    file.bytes = whole;
    const int edits = 1 + static_cast<int>(rng() % 8);
    bool onlyPadding = true;
    for (int e = 0; e < edits; ++e) {
      const size_t at = rng() % file.bytes.size();
      file.bytes[at] = static_cast<uint8_t>(rng());
      onlyPadding = onlyPadding && (file.bytes[at] == whole[at] || isPadding(whole, built.report.header, at));
    }
    if (exercise(file)) {
      EXPECT_TRUE(onlyPadding) << "round " << round << ": an edit outside padding was not detected";
      ++clean;
    } else {
      ++detected;
    }
  }
  std::printf(
      "\n== 1500 random multi-byte edits of a %zu-byte index: %zu detected, %zu read without error "
      "(every one only in padding or spill slots)\n",
      whole.size(), detected, clean);
  EXPECT_GT(detected, clean);
}

TEST(Robustness, MalformedXhtmlGivesADefinedError) {
  struct Case {
    const char* name;
    std::string doc;
    BuildStatus expected;
  };
  std::string garbage;
  std::mt19937 rng(99);
  for (int i = 0; i < 20000; ++i) garbage += static_cast<char>(rng());
  std::string deep = "<?xml version=\"1.0\"?><html><body>";
  for (int i = 0; i < 100000; ++i) deep += "<div>";
  deep += "x";
  for (int i = 0; i < 100000; ++i) deep += "</div>";
  deep += "</body></html>";
  std::string deepInline = "<html><body>";
  for (int i = 0; i < 100000; ++i) deepInline += "<b>";

  const std::vector<Case> cases = {
      {"empty input", "", BuildStatus::ParseError},
      {"unclosed tags", "<?xml version=\"1.0\"?><html><body><p>one <b>two", BuildStatus::ParseError},
      {"mismatched tag", wixtest::xhtml("<p>one</div>"), BuildStatus::ParseError},
      {"bad character reference", wixtest::xhtml("<p>a&#xZZ;b</p>"), BuildStatus::ParseError},
      {"reference to a non-character", wixtest::xhtml("<p>a&#0;b</p>"), BuildStatus::ParseError},
      {"undeclared entity (HTML 5 doctype)", wixtest::xhtml("<p>a&nbsp;b</p>"), BuildStatus::ParseError},
      {"invalid UTF-8", wixtest::xhtml("<p>a\xC3\x28 b</p>"), BuildStatus::ParseError},
      {"NUL byte", wixtest::xhtml(std::string("<p>a\0b</p>", 9)), BuildStatus::ParseError},
      {"binary garbage", garbage, BuildStatus::ParseError},
      {"100,000 nested div", deep, BuildStatus::TooDeep},
      {"100,000 nested b, unclosed", deepInline, BuildStatus::TooDeep},
      {"anchor inside a word", wixtest::xhtml("<p>wo<span id=\"rsvp-x\"/>rd</p>"), BuildStatus::AnchorInsideWord},
  };
  for (const Case& c : cases) {
    SCOPED_TRACE(c.name);
    const auto built = wixtest::buildIndex(c.doc);
    EXPECT_EQ(built.status, c.expected) << buildStatusName(built.status);
    // A failed build never leaves a file that opens.
    if (built.status != BuildStatus::Ok) {
      wixtest::MemoryFile file = built.file;
      WordIndexReader reader;
      EXPECT_NE(reader.open(file), ReadStatus::Ok);
    }
  }
}

TEST(Robustness, ExtremelyLongWordsAreSplit) {
  const std::string word(1000000, 'x');
  auto built = wixtest::buildIndex(wixtest::xhtml("<p>a " + word + " b</p>"));
  ASSERT_EQ(built.status, BuildStatus::Ok);
  const auto c = wixtest::readAll(built.file);
  ASSERT_EQ(c.words.size(), 2u + (1000000 + 254) / 255);
  size_t total = 0;
  for (size_t i = 1; i + 1 < c.words.size(); ++i) {
    total += c.words[i].text.size();
    EXPECT_EQ((c.words[i].flags & BYTE_SPLIT) != 0, i + 2 < c.words.size()) << i;
  }
  EXPECT_EQ(total, word.size());
}

TEST(Robustness, AWordSpanningTooManyCodepointsIsTooLarge) {
  // Hidden text inside a word counts in its span; 2^24 code points do not fit the format.
  const std::string hidden(MAX_WORD_SPAN + 1, 'h');
  const auto built = wixtest::buildIndex(wixtest::xhtml("<p>a<span hidden=\"\">" + hidden + "</span>b</p>"));
  EXPECT_EQ(built.status, BuildStatus::TooLarge);
}

TEST(Robustness, ReadAndWriteErrorsAreReported) {
  const std::string doc = chapter2();
  for (size_t failAfter : {size_t{0}, size_t{100}, size_t{2000}, doc.size() - 1}) {
    wixtest::MemoryInput input(doc, 512);
    input.failAfter = failAfter;
    wixtest::MemoryFile file;
    WordIndexBuilder builder;
    EXPECT_EQ(builder.build(input, file), BuildStatus::ReadError) << failAfter;
  }
  // A sink that fails at every possible point.
  const auto good = wixtest::buildIndex(doc);
  ASSERT_EQ(good.status, BuildStatus::Ok);
  for (size_t limit = 0; limit < good.file.bytes.size(); limit += 97) {
    wixtest::MemoryInput input(doc);
    wixtest::MemoryFile file;
    file.failWritesAfter = limit;
    WordIndexBuilder builder;
    EXPECT_EQ(builder.build(input, file), BuildStatus::WriteError) << limit;
  }
}

TEST(Robustness, ABuilderCanBeReused) {
  WordIndexBuilder builder;
  wixtest::MemoryInput bad(wixtest::xhtml("<p>a</div>"));
  wixtest::MemoryFile badFile;
  EXPECT_EQ(builder.build(bad, badFile), BuildStatus::ParseError);
  wixtest::MemoryInput good(chapter2());
  wixtest::MemoryFile goodFile;
  EXPECT_EQ(builder.build(good, goodFile), BuildStatus::Ok);
  EXPECT_EQ(goodFile.bytes, wixtest::buildIndex(chapter2()).file.bytes);
}

TEST(Robustness, OtherVersionsAskForARebuild) {
  auto built = wixtest::buildIndex(chapter2());
  ASSERT_EQ(built.status, BuildStatus::Ok);
  {
    wixtest::MemoryFile file = built.file;
    file.bytes[4] = FORMAT_VERSION + 1;
    WordIndexReader reader;
    EXPECT_EQ(reader.open(file), ReadStatus::OtherFormat);
  }
  {
    // Another rules version, with a valid header CRC: what an older build of the firmware wrote.
    Header h = built.report.header;
    h.rulesVersion = RULES_VERSION + 1;
    wixtest::MemoryFile file = built.file;
    encodeHeader(h, file.bytes.data());
    WordIndexReader reader;
    EXPECT_EQ(reader.open(file), ReadStatus::OtherRules);
  }
  {
    wixtest::MemoryFile file = built.file;
    file.bytes[0] = 'X';
    WordIndexReader reader;
    EXPECT_EQ(reader.open(file), ReadStatus::BadMagic);
  }
}
