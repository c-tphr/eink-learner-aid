// The .wix format: determinism, round trip, random access against sequential reading, every query
// against a linear scan, spill slots for large tables, and the size and RAM measurements quoted in
// lib/WordIndex/README.md.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "WordIndexTestSupport.h"

using namespace wordindex;

namespace {

std::string fixtureChapter(const char* name) {
  return wixtest::readFile(std::string(WIX_DATA_DIR "/original/OEBPS/text/") + name);
}
std::string bigChapter() { return wixtest::readFile(WIX_DATA_DIR "/test_reader_menu_progress_chapter1.xhtml"); }

// Exactly 10,000 words: the 3,051-word chapter's body three times, then 847 more words.
std::string tenThousandWords() {
  const std::string doc = bigChapter();
  const size_t open = doc.find("<body");
  const size_t start = doc.find('>', open) + 1;
  const size_t end = doc.find("</body>");
  std::string body;
  for (int i = 0; i < 3; ++i) body += doc.substr(start, end - start);
  body += "<p>";
  for (int i = 0; i < 847; ++i) body += "filler ";
  body += "</p>";
  return doc.substr(0, start) + body + doc.substr(end);
}

// A synthetic chapter with many blocks, anchors and markers, so every table spills, and with an
// open NOTE region whose marker record is spilled before the region ends.
std::string syntheticChapter(uint32_t* expectedImages = nullptr) {
  std::mt19937 rng(20);
  std::string body;
  uint32_t images = 0;
  for (int p = 0; p < 400; ++p) {
    body += "<p id=\"rsvp-s" + std::to_string(p) + "-start\">";
    const int words = 5 + static_cast<int>(rng() % 40);
    for (int w = 0; w < words; ++w) body += "word" + std::to_string(rng() % 1000) + (w + 1 < words ? " " : "");
    body += "<span id=\"rsvp-s" + std::to_string(p) + "-end\"></span></p>";
    if (p % 3 == 0) {
      body += "<img src=\"i.png\" alt=\"\"/>";
      ++images;
    }
    if (p % 50 == 7) body += "<table><tr><td>cell one</td><td>cell two</td></tr></table>";
    if (p % 97 == 5) body += "<pre>code line\nmore code</pre>";
  }
  // A long note body holding enough markers to spill the marker table while it is open.
  body += "<aside epub:type=\"footnote\"><p>note start</p>";
  for (int i = 0; i < 60; ++i) body += "<p>n" + std::to_string(i) + "<img src=\"n.png\" alt=\"\"/></p>";
  body += "</aside><p>the end</p>";
  if (expectedImages) *expectedImages = images;
  return wixtest::xhtml(body);
}

struct Reference {
  std::vector<wixtest::Word> words;
  std::vector<Marker> markers;
  std::vector<Anchor> anchors;
};

}  // namespace

TEST(Format, Crc32IsTheZipCrc) {
  const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  EXPECT_EQ(crc32Update(0, check, sizeof(check)), 0xCBF43926u);
  EXPECT_EQ(crc32Update(crc32Update(0, check, 4), check + 4, 5), 0xCBF43926u);
}

TEST(Format, OutputDoesNotDependOnInputChunking) {
  for (const std::string& doc : {fixtureChapter("chapter-2.xhtml"), bigChapter(), syntheticChapter()}) {
    const auto whole = wixtest::buildIndex(doc);
    ASSERT_EQ(whole.status, BuildStatus::Ok);
    for (const uint32_t chunk : {1u, 3u, 64u, 1000u}) {
      const auto chunked = wixtest::buildIndex(doc, chunk);
      ASSERT_EQ(chunked.status, BuildStatus::Ok);
      EXPECT_EQ(chunked.file.bytes, whole.file.bytes) << "chunk " << chunk;
    }
    // Pushed with begin/feed/end, in uneven pieces and in one piece.
    for (const size_t piece : {size_t{1}, size_t{777}, doc.size()}) {
      wixtest::MemoryFile pushed;
      WordIndexBuilder builder;
      ASSERT_EQ(builder.begin(pushed), BuildStatus::Ok);
      for (size_t at = 0; at < doc.size(); at += piece) {
        ASSERT_TRUE(builder.feed(reinterpret_cast<const uint8_t*>(doc.data()) + at, std::min(piece, doc.size() - at)));
      }
      ASSERT_EQ(builder.end(), BuildStatus::Ok);
      EXPECT_EQ(pushed.bytes, whole.file.bytes) << "piece " << piece;
    }
  }
}

TEST(Format, HeaderAndLayout) {
  auto built = wixtest::buildIndex(bigChapter());
  ASSERT_EQ(built.status, BuildStatus::Ok);
  const Header& h = built.report.header;
  EXPECT_EQ(h.wordCount, 3051u);
  EXPECT_EQ(h.fileBytes, built.file.bytes.size());
  EXPECT_EQ(h.directoryOffset, (1 + h.slotCount) * SLOT_BYTES);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(built.file.bytes.data()), 4), "CPWI");
  EXPECT_EQ(built.file.bytes[4], FORMAT_VERSION);
  EXPECT_EQ(built.file.bytes[5], RULES_VERSION);
  // Directory entries outgrew one RAM buffer, so spill slots were used and are in slotCount.
  EXPECT_GT(h.blockCount, WordIndexReader::DIRECTORY_PAGE_ENTRIES);
  EXPECT_EQ(h.slotCount, h.blockCount + built.report.spillSlots);
  WordIndexReader reader;
  ASSERT_EQ(reader.open(built.file), ReadStatus::Ok);
  EXPECT_TRUE(reader.matchesSource(h.sourceBytes, h.sourceCrc32));
  EXPECT_FALSE(reader.matchesSource(h.sourceBytes + 1, h.sourceCrc32));
  EXPECT_FALSE(reader.matchesSource(h.sourceBytes, h.sourceCrc32 ^ 1));
}

TEST(Format, EmptyChapterIsAValidIndex) {
  auto built = wixtest::buildIndex(wixtest::xhtml("<p> </p><img src=\"x.png\" alt=\"\"/>"));
  ASSERT_EQ(built.status, BuildStatus::Ok);
  WordIndexReader reader;
  ASSERT_EQ(reader.open(built.file), ReadStatus::Ok);
  EXPECT_EQ(reader.wordCount(), 0u);
  WordInfo w;
  EXPECT_FALSE(reader.word(0, w));
  uint32_t first = 99;
  ASSERT_TRUE(reader.firstWordEndingAfter(0, first));
  EXPECT_EQ(first, 0u);
  EXPECT_TRUE(reader.imageAt(0));
  EXPECT_EQ(reader.status(), ReadStatus::Ok);
}

// Every query against a linear scan of the sequentially read contents.
TEST(Format, RandomAccessEqualsSequential) {
  uint32_t images = 0;
  for (const std::string& doc : {fixtureChapter("chapter-1.xhtml"), fixtureChapter("chapter-3.xhtml"), bigChapter(),
                                 syntheticChapter(&images)}) {
    auto built = wixtest::buildIndex(doc);
    ASSERT_EQ(built.status, BuildStatus::Ok);
    const wixtest::Contents all = wixtest::readAll(built.file);
    const auto& words = all.words;
    ASSERT_FALSE(words.empty());

    WordIndexReader reader;
    ASSERT_EQ(reader.open(built.file), ReadStatus::Ok);
    // Backward.
    for (uint32_t i = reader.wordCount(); i-- > 0;) {
      WordInfo w;
      ASSERT_TRUE(reader.word(i, w));
      ASSERT_EQ(wixtest::Word({std::string(w.text), w.start, w.end, w.flags}), words[i]) << i;
      ASSERT_EQ(w.index, i);
    }
    // Random order, with a fresh reader every so often.
    std::mt19937 rng(7);
    for (int k = 0; k < 2000; ++k) {
      if (k % 500 == 0) {
        ASSERT_EQ(reader.open(built.file), ReadStatus::Ok);
      }
      const uint32_t i = static_cast<uint32_t>(rng() % words.size());
      WordInfo w;
      ASSERT_TRUE(reader.word(i, w));
      ASSERT_EQ(wixtest::Word({std::string(w.text), w.start, w.end, w.flags}), words[i]) << i;
      uint32_t start = 0;
      ASSERT_TRUE(reader.wordStart(i, start));
      ASSERT_EQ(start, words[i].start);
    }
    // Page -> word for every offset, against a linear scan.
    const uint32_t visible = reader.header().visibleCodepoints;
    size_t w = 0;
    for (uint32_t offset = 0; offset <= visible + 1; ++offset) {
      while (w < words.size() && words[w].end <= offset) ++w;
      uint32_t got = 0;
      ASSERT_TRUE(reader.firstWordEndingAfter(offset, got));
      ASSERT_EQ(got, w) << "offset " << offset;
    }
    // Markers.
    for (uint32_t i = 0; i <= words.size(); ++i) {
      bool image = false;
      bool imageOutsideNotes = false;
      for (const auto& m : all.markers) {
        image = image || (m.kind == MARKER_IMAGE && m.wordStart == i);
        imageOutsideNotes =
            imageOutsideNotes || (m.kind == MARKER_IMAGE && m.wordStart == i && !(m.flags & MARKER_IN_NOTE));
      }
      ASSERT_EQ(reader.imageAt(i), image) << i;
      ASSERT_EQ(reader.imageAt(i, false), imageOutsideNotes) << i;
      for (const uint8_t kinds :
           {REGION_MARKERS, kindBit(MARKER_NOTE), static_cast<uint8_t>(kindBit(MARKER_TABLE) | kindBit(MARKER_CODE)),
            kindBit(MARKER_CODE)}) {
        const Marker* want = nullptr;
        for (const auto& m : all.markers) {
          if ((kinds & kindBit(m.kind)) && m.kind != MARKER_IMAGE && m.wordStart <= i && i < m.wordEnd) {
            want = &m;
            break;
          }
        }
        Marker got;
        const bool found = reader.regionContaining(i, kinds, got);
        ASSERT_EQ(found, want != nullptr) << i;
        if (found) {
          ASSERT_EQ(std::make_pair(got.wordStart, got.kind), std::make_pair(want->wordStart, want->kind));
        }
      }
      for (const uint8_t kinds : {ANY_MARKER, kindBit(MARKER_IMAGE), REGION_MARKERS}) {
        const Marker* want = nullptr;
        for (const auto& m : all.markers) {
          if ((kinds & kindBit(m.kind)) && m.wordStart >= i) {
            want = &m;
            break;
          }
        }
        Marker got;
        const bool found = reader.nextMarker(i, kinds, got);
        ASSERT_EQ(found, want != nullptr) << i;
        if (found) {
          ASSERT_EQ(std::make_pair(got.wordStart, got.kind), std::make_pair(want->wordStart, want->kind));
        }
      }
    }
    // Anchors: every id is found with the enumerated values.
    for (const auto& a : all.anchors) {
      Anchor got;
      ASSERT_TRUE(reader.findAnchor(a.idView(), got));
      ASSERT_EQ(got.wordIndex, a.wordIndex);
      ASSERT_EQ(got.offset, a.offset);
    }
    Anchor none;
    EXPECT_FALSE(reader.findAnchor("rsvp-missing", none));
    EXPECT_EQ(reader.status(), ReadStatus::Ok);
  }
}

TEST(Format, SpilledTablesRoundTrip) {
  uint32_t images = 0;
  const std::string doc = syntheticChapter(&images);
  auto built = wixtest::buildIndex(doc);
  ASSERT_EQ(built.status, BuildStatus::Ok);
  EXPECT_GT(built.report.spillSlots, 3u);
  const auto c = wixtest::readAll(built.file);
  // 800 anchors, each start anchor at the paragraph's first word and each end after its last.
  ASSERT_EQ(c.anchors.size(), 800u);
  for (size_t p = 0; p < 400; ++p) {
    const Anchor& start = c.anchors[2 * p];
    const Anchor& end = c.anchors[2 * p + 1];
    ASSERT_EQ(std::string(start.idView()), "rsvp-s" + std::to_string(p) + "-start");
    ASSERT_LT(start.wordIndex, end.wordIndex);
    ASSERT_TRUE(c.words[end.wordIndex - 1].flags & PARA_END);
  }
  size_t imageMarkers = 0;
  size_t inNote = 0;
  const Marker* note = nullptr;
  for (const auto& m : c.markers) {
    if (m.kind == MARKER_IMAGE) ++imageMarkers;
    if (m.flags & MARKER_IN_NOTE) ++inNote;
    if (m.kind == MARKER_NOTE) note = &m;
  }
  EXPECT_EQ(imageMarkers, images + 60);
  EXPECT_EQ(inNote, 60u);
  ASSERT_NE(note, nullptr);
  // The NOTE region's end was patched into a spilled record: it covers "note start" to "n59".
  EXPECT_EQ(c.words[note->wordStart].text, "note");
  EXPECT_EQ(c.words[note->wordEnd - 1].text, "n59");
  EXPECT_EQ(c.words[note->wordEnd].text, "the");
}

TEST(Format, ReaderRamAndFileSizes) {
  struct Input {
    const char* name;
    std::string doc;
  };
  const std::vector<Input> inputs = {{"fixture title.xhtml", fixtureChapter("title.xhtml")},
                                     {"fixture chapter-1.xhtml", fixtureChapter("chapter-1.xhtml")},
                                     {"fixture chapter-2.xhtml", fixtureChapter("chapter-2.xhtml")},
                                     {"fixture chapter-3.xhtml", fixtureChapter("chapter-3.xhtml")},
                                     {"fixture endnotes.xhtml", fixtureChapter("endnotes.xhtml")},
                                     {"S1 fixture_s1.xhtml", wixtest::readFile(WIX_SPIKE_FIXTURE)},
                                     {"test_reader_menu_progress ch1", bigChapter()},
                                     {"10,000 words (ch1 x3 + 847)", tenThousandWords()}};
  std::printf(
      "\n== Index size and reader RAM (sizeof(WordIndexReader) = %zu bytes, sizeof(WordIndexBuilder) = %zu bytes)\n",
      sizeof(WordIndexReader), sizeof(WordIndexBuilder));
  std::printf("   %-30s %7s %7s %7s %6s %6s %9s %10s %12s\n", "chapter", "words", "XHTML", "file", "B/word", "blocks",
              "spill", "max block", "seq. reads");
  for (const auto& in : inputs) {
    auto built = wixtest::buildIndex(in.doc);
    ASSERT_EQ(built.status, BuildStatus::Ok);
    const Header& h = built.report.header;
    WordIndexReader reader;
    ASSERT_EQ(reader.open(built.file), ReadStatus::Ok);
    built.file.reads = 0;
    for (uint32_t i = 0; i < reader.wordCount(); ++i) {
      WordInfo w;
      ASSERT_TRUE(reader.word(i, w));
    }
    std::printf("   %-30s %7u %7u %7u %6.1f %6u %9u %10u %12zu\n", in.name, h.wordCount, h.sourceBytes, h.fileBytes,
                h.wordCount ? static_cast<double>(h.fileBytes) / h.wordCount : 0.0, h.blockCount,
                built.report.spillSlots, built.report.maxBlockBytes, built.file.reads);
  }
  // The reader's RAM does not depend on the chapter: header, one directory page, two blocks.
  EXPECT_LT(sizeof(WordIndexReader), 2300u);
  auto ten = wixtest::buildIndex(tenThousandWords());
  ASSERT_EQ(ten.status, BuildStatus::Ok);
  EXPECT_EQ(ten.report.header.wordCount, 10000u);
}

namespace {

// Counting allocator for expat: current and peak bytes.
struct HeapCount {
  size_t current = 0;
  size_t peak = 0;
};
HeapCount heap;
void* countMalloc(size_t size) {
  auto* p = static_cast<size_t*>(std::malloc(size + sizeof(size_t)));
  if (p == nullptr) return nullptr;
  *p = size;
  heap.current += size;
  heap.peak = std::max(heap.peak, heap.current);
  return p + 1;
}
void countFree(void* ptr) {
  if (ptr == nullptr) return;
  auto* p = static_cast<size_t*>(ptr) - 1;
  heap.current -= *p;
  std::free(p);
}
void* countRealloc(void* ptr, size_t size) {
  if (ptr == nullptr) return countMalloc(size);
  auto* p = static_cast<size_t*>(ptr) - 1;
  const size_t old = *p;
  auto* q = static_cast<size_t*>(std::realloc(p, size + sizeof(size_t)));
  if (q == nullptr) return nullptr;
  *q = size;
  heap.current = heap.current - old + size;
  heap.peak = std::max(heap.peak, heap.current);
  return q + 1;
}

}  // namespace

TEST(Format, ExpatHeapDuringABuild) {
  const XML_Memory_Handling_Suite suite = {countMalloc, countRealloc, countFree};
  std::printf("\n== expat heap during a build (firmware expat flags XML_GE=0, XML_CONTEXT_BYTES=0)\n");
  for (const auto& [name, doc] : {std::make_pair("fixture chapter-2.xhtml", fixtureChapter("chapter-2.xhtml")),
                                  std::make_pair("test_reader_menu_progress ch1", bigChapter()),
                                  std::make_pair("synthetic (400 paragraphs)", syntheticChapter())}) {
    heap = HeapCount{};
    wixtest::MemoryInput input(doc);
    wixtest::MemoryFile file;
    WordIndexBuilder builder(&suite);
    ASSERT_EQ(builder.build(input, file), BuildStatus::Ok);
    EXPECT_EQ(heap.current, 0u);  // all freed
    std::printf("   %-32s XHTML %7zu bytes, expat peak %6zu bytes\n", name, doc.size(), heap.peak);
    EXPECT_LT(heap.peak, 32u * 1024u);
  }
}
