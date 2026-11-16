// THROWAWAY PROTOTYPE for spike S1 (issue #9). Not part of the host test suite.
//
// Builds the RSVP word stream for a chapter with a separate expat pass (WordStreamSpike.cpp), lays
// the same chapter out with the real ChapterHtmlSlimParser at two layouts, and maps pages to word
// indexes through the per-page visible-text offsets the parser already reports. Every rendered token
// is checked against the word stream, so a divergence between the two passes fails the test.
#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "Epub/parsers/ChapterHtmlSlimParser.h"
#include "WordStreamSpike.h"

namespace {

using spike::Word;

std::string readFile(const char* path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

std::unique_ptr<spike::WordStreamBuilder> buildStream(const char* path) {
  const std::string xhtml = readFile(path);
  EXPECT_FALSE(xhtml.empty()) << path;
  auto stream = std::make_unique<spike::WordStreamBuilder>();
  constexpr size_t CHUNK = 1024;  // the parser reads 1 KB per step (ChapterHtmlSlimParser.cpp:29)
  for (size_t at = 0; at < xhtml.size(); at += CHUNK) {
    EXPECT_TRUE(stream->write(xhtml.data() + at, std::min(CHUNK, xhtml.size() - at)));
  }
  EXPECT_TRUE(stream->finish());
  return stream;
}

struct Layout {
  const char* name;
  uint16_t width;
  uint16_t height;
  int lineHeight;
  bool hyphenation;
  bool focusReading;
};

// A: roomy page, no hyphenation. B: small page, taller lines, hyphenation and Focus Reading on,
// standing in for a larger font (the host renderer stub has fixed 8 px glyphs).
constexpr Layout LAYOUT_A{"A 480x800 line16", 480, 800, 16, false, false};
constexpr Layout LAYOUT_B{"B 240x300 line24 hyph+focus", 240, 300, 24, true, true};

struct LaidOutPage {
  uint32_t start;  // the visibleTextOffset the parser hands to Section's completePageFn
  std::unique_ptr<Page> page;
};

std::vector<LaidOutPage> layoutChapter(const char* path, const Layout& layout) {
  GfxRenderer renderer;
  renderer.fontLineHeights[0] = layout.lineHeight;
  renderer.fontSizePixels[0] = static_cast<float>(layout.lineHeight);
  auto epub = std::make_shared<Epub>();
  epub->imageBytes = {'G', 'I', 'F', '8', '9', 'a', 200, 0, 150, 0};  // 200x150 GIF header
  Epub::enableGifProbeForTest = true;
  Hyphenator::setPreferredLanguage("en");

  std::vector<LaidOutPage> pages;
  const std::string filepath = path;  // the parser keeps a reference
  const std::string imageBase = (std::filesystem::temp_directory_path() / "spike-s1-img-").string();
  ChapterHtmlSlimParser parser(
      epub, filepath, renderer, 0, 1.0f, false, 0, layout.width, layout.height, layout.hyphenation,
      layout.focusReading,
      [&](std::unique_ptr<Page> page, uint16_t, uint16_t, const uint32_t visibleTextOffset) {
        pages.push_back({visibleTextOffset, std::move(page)});
      },
      false, "", imageBase, 0, {}, nullptr, nullptr);
  EXPECT_TRUE(parser.parseAndBuildPages());
  Epub::enableGifProbeForTest = false;
  return pages;
}

std::vector<uint32_t> pageStarts(const std::vector<LaidOutPage>& pages) {
  std::vector<uint32_t> starts;
  for (const auto& page : pages) starts.push_back(page.start);
  return starts;
}

std::string stripSoftHyphens(std::string text) {
  for (size_t at; (at = text.find("\xC2\xAD")) != std::string::npos;) text.erase(at, 2);
  return text;
}

bool isSpaceToken(const std::string& text) {
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == ' ') continue;
    if (text.compare(i, 2, "\xC2\xA0") == 0) {
      ++i;
      continue;
    }
    return false;
  }
  return true;
}

struct Parity {
  size_t matched = 0;
  size_t excluded = 0;   // noteref / hidden text: counted in offsets, deliberately not a word
  size_t synthetic = 0;  // renderer-made tokens with no source text (list bullets)
  size_t spaces = 0;     // no-break space tokens
  size_t mismatched = 0;
  std::vector<std::string> problems;
  std::vector<int> firstPageOfWord;  // page where each word's first rendered token appears
};

// Every rendered token must fall inside a stream word and be a substring of it.
Parity checkParity(const spike::WordStreamBuilder& stream, const std::vector<LaidOutPage>& pages) {
  const auto& words = stream.words();
  Parity parity;
  parity.firstPageOfWord.assign(words.size(), -1);
  for (size_t p = 0; p < pages.size(); ++p) {
    for (const auto& element : pages[p].page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const TextBlock& block = *static_cast<const PageLine&>(*element).getBlock();
      for (uint16_t i = 0; i < block.wordCount(); ++i) {
        std::string text = stripSoftHyphens(block.wordText(i));
        if (block.wordHasDiscretionaryHyphen(i) && !text.empty() && text.back() == '-') text.pop_back();
        const auto range = block.wordSourceRange(i);
        if (isSpaceToken(text)) {
          parity.spaces++;
          continue;
        }
        if (range.start == UINT32_MAX || range.start == range.end) {
          parity.synthetic++;
          continue;
        }
        const uint32_t w = spike::firstWordAtOrAfter(words, range.start);
        if (w < words.size() && words[w].start <= range.start) {
          if (words[w].text.find(text) != std::string::npos) {
            parity.matched++;
            if (parity.firstPageOfWord[w] < 0) parity.firstPageOfWord[w] = static_cast<int>(p);
            continue;
          }
        } else {
          const bool excluded = std::any_of(stream.excluded().begin(), stream.excluded().end(), [&](const auto& run) {
            return range.start >= run.start && range.start < run.end;
          });
          if (excluded) {
            parity.excluded++;
            continue;
          }
          if (text == "\xE2\x80\xA2") {
            parity.synthetic++;
            continue;
          }
        }
        parity.mismatched++;
        parity.problems.push_back("page " + std::to_string(p) + " token '" + text + "' @" +
                                  std::to_string(range.start) +
                                  (w < words.size() ? " word '" + words[w].text + "'" : std::string(" no word")));
      }
    }
  }
  return parity;
}

void printStream(const char* label, const spike::WordStreamBuilder& stream, const size_t maxWords) {
  const auto& words = stream.words();
  std::printf("\n== Word stream: %s\n   %zu words, %zu anchors, %zu markers, %u visible codepoints\n", label,
              words.size(), stream.anchors().size(), stream.markers().size(), stream.visibleCodepoints());
  for (size_t w = 0; w < words.size() && w < maxWords; ++w) {
    for (const auto& anchor : stream.anchors()) {
      if (anchor.wordIndex == w) std::printf("   ANCHOR %s -> before w%zu (offset %u)\n", anchor.id.c_str(), w, anchor.offset);
    }
    for (const auto& marker : stream.markers()) {
      if (marker.wordStart != w) continue;
      static const char* kinds[] = {"", "IMAGE", "TABLE", "CODE"};
      std::printf("   MARKER %s words [%u,%u) offsets [%u,%u)\n", kinds[static_cast<int>(marker.kind)], marker.wordStart,
                  marker.wordEnd, marker.offsetStart, marker.offsetEnd);
    }
    std::printf("   w%-4zu @%-5u+%-2u %-22s %s\n", w, words[w].start, words[w].end - words[w].start,
                ("\"" + words[w].text + "\"").c_str(), spike::flagString(words[w].flags).c_str());
  }
  for (const auto& anchor : stream.anchors()) {
    if (anchor.wordIndex >= words.size() || anchor.wordIndex >= maxWords) {
      std::printf("   ANCHOR %s -> before w%u (offset %u)\n", anchor.id.c_str(), anchor.wordIndex, anchor.offset);
    }
  }
  if (words.size() > maxWords) std::printf("   ... %zu more words\n", words.size() - maxWords);
}

// Prints page -> first word and checks the round trip word -> page against where the renderer put it.
void mapPages(const char* label, const spike::WordStreamBuilder& stream, const Layout& layout,
              const std::vector<LaidOutPage>& pages, const size_t maxPagesShown) {
  const auto& words = stream.words();
  const auto starts = pageStarts(pages);
  std::printf("\n== Layout %s, %s: %zu pages\n", layout.name, label, pages.size());
  size_t midWordStarts = 0;
  for (size_t p = 0; p < pages.size(); ++p) {
    if (p > 0) EXPECT_LE(starts[p - 1], starts[p]) << "page offsets must not decrease";
    const uint32_t w = spike::firstWordAtOrAfter(words, starts[p]);
    const bool midWord = w < words.size() && words[w].start < starts[p];
    if (midWord) midWordStarts++;
    if (p < maxPagesShown || midWord) {
      std::printf("   page %-3zu offset %-6u -> first word w%-5u %s%s\n", p, starts[p], w,
                  w < words.size() ? ("\"" + words[w].text + "\"").c_str() : "(end)",
                  midWord ? "  (page starts inside this word)" : "");
    }
  }
  if (pages.size() > maxPagesShown) std::printf("   ... (%zu pages not shown)\n", pages.size() - maxPagesShown);
  std::printf("   pages starting inside a word: %zu\n", midWordStarts);

  for (const auto& anchor : stream.anchors()) {
    // An end anchor's page is the page holding the last codepoint of the section's last word.
    const bool isEnd = anchor.id.size() > 4 && anchor.id.compare(anchor.id.size() - 4, 4, "-end") == 0;
    uint16_t page;
    if (isEnd && anchor.wordIndex > 0) {
      page = spike::pageForOffset(starts, words[anchor.wordIndex - 1].end - 1);
    } else {
      page = spike::pageForOffset(starts, anchor.wordIndex < words.size() ? words[anchor.wordIndex].start : anchor.offset);
    }
    std::printf("   anchor %-18s word %-5u -> page %u\n", anchor.id.c_str(), anchor.wordIndex, page);
  }
  for (const auto& marker : stream.markers()) {
    static const char* kinds[] = {"", "IMAGE", "TABLE", "CODE"};
    std::printf("   marker %-5s at word %-5u -> page %u\n", kinds[static_cast<int>(marker.kind)], marker.wordStart,
                spike::pageForOffset(starts, marker.offsetStart, /*preferFirstAtOffset=*/true));
  }

  const Parity parity = checkParity(stream, pages);
  std::printf("   parity: %zu tokens matched, %zu excluded (noteref/hidden), %zu synthetic, %zu nbsp, %zu mismatched\n",
              parity.matched, parity.excluded, parity.synthetic, parity.spaces, parity.mismatched);
  for (const auto& problem : parity.problems) std::printf("   MISMATCH %s\n", problem.c_str());
  EXPECT_EQ(parity.mismatched, 0u);

  size_t roundTripChecked = 0;
  size_t roundTripFailed = 0;
  size_t unrendered = 0;
  for (size_t w = 0; w < words.size(); ++w) {
    if (parity.firstPageOfWord[w] < 0) {
      unrendered++;
      continue;
    }
    roundTripChecked++;
    const uint16_t page = spike::pageForOffset(starts, words[w].start);
    if (page != parity.firstPageOfWord[w]) {
      roundTripFailed++;
      std::printf("   ROUND TRIP w%zu \"%s\": offset maps to page %u, renderer drew it first on page %d\n", w,
                  words[w].text.c_str(), page, parity.firstPageOfWord[w]);
    }
  }
  std::printf("   word -> page (Section::getPageForVisibleTextOffset rule): %zu words checked, %zu wrong, %zu not drawn\n",
              roundTripChecked, roundTripFailed, unrendered);
  EXPECT_EQ(roundTripFailed, 0u);
}

void printSize(const char* label, const spike::WordStreamBuilder& stream) {
  const spike::FormatSize size = spike::estimateFormatSize(stream);
  std::printf("\n== Proposed word file size, %s: %zu bytes total (%zu words, %zu blocks, max block %zu bytes; "
              "header %zu, directory %zu, blocks %zu, anchors %zu, markers %zu)\n",
              label, size.total(), stream.words().size(), size.blocks, size.maxBlockBytes, size.headerBytes,
              size.directoryBytes, size.blockBytes, size.anchorBytes, size.markerBytes);
}

}  // namespace

TEST(SpikeWordStream, FixtureWordsAnchorsAndMarkers) {
  const auto stream = buildStream(SPIKE_FIXTURE);
  printStream("fixture_s1.xhtml", *stream, 1000);
  printSize("fixture_s1.xhtml", *stream);

  // The word sequence a preparation-tool fixture would list for this file.
  const std::vector<std::string> expected = {
      "Chapter", "One", "Spacing", "works", "because", "it", "forces", "effortful", "recall.", "It", "also",
      "exposes", "what", "was", "forgotten\xE2\x80\x94", "which", "is", "useful.", "Mr.", "Darcy", "agreed,",
      "e.g.", "in", "the", "well-known", "letter.", "Soft", "hyphens", "stay", "inside", "one", "word,", "and",
      "boldface", "joins", "across", "inline", "tags.", "Figure", "1.", "Recall", "over", "time.", "Interval",
      "Recall", "1", "day", "80%", "for", "(int", "i", "=", "0;", "i", "<", "n;", "i++)", "{", "review(card[i]);",
      "}", "The", "last", "sentence", "of", "the", "section", "ends", "here.", "A", "new", "section", "begins",
      "with", "a", "page", "marker", "hidden", "in", "the", "middle.", "1.", "See", "the", "appendix."};
  std::vector<std::string> actual;
  for (const Word& word : stream->words()) actual.push_back(word.text);
  EXPECT_EQ(actual, expected);

  const auto& anchors = stream->anchors();
  ASSERT_EQ(anchors.size(), 4u);
  EXPECT_EQ(anchors[0].id, "rsvp-s0001-start");
  EXPECT_EQ(anchors[0].wordIndex, 2u);
  EXPECT_EQ(anchors[1].id, "rsvp-s0001-end");
  EXPECT_EQ(anchors[1].wordIndex, 68u);
  EXPECT_EQ(anchors[2].id, "rsvp-s0002-start");
  EXPECT_EQ(anchors[2].wordIndex, 68u);
  EXPECT_EQ(anchors[3].id, "rsvp-s0002-end");
  EXPECT_EQ(anchors[3].wordIndex, 80u);

  const auto& markers = stream->markers();
  ASSERT_EQ(markers.size(), 3u);
  EXPECT_EQ(markers[0].kind, spike::MarkerKind::Image);
  EXPECT_EQ(markers[0].wordStart, 38u);
  EXPECT_EQ(markers[1].kind, spike::MarkerKind::Table);
  EXPECT_EQ(markers[1].wordStart, 43u);
  EXPECT_EQ(markers[1].wordEnd, 48u);
  EXPECT_EQ(markers[2].kind, spike::MarkerKind::Code);
  EXPECT_EQ(markers[2].wordStart, 48u);
  EXPECT_EQ(markers[2].wordEnd, 60u);

  const auto& words = stream->words();
  EXPECT_TRUE(words[8].flags & spike::NOTEREF);     // "recall." carried the footnote mark
  EXPECT_TRUE(words[8].flags & spike::SENT_END);
  EXPECT_FALSE(words[18].flags & spike::SENT_END);  // "Mr."
  EXPECT_TRUE(words[25].flags & spike::PARA_END);
  EXPECT_TRUE(words[44].flags & spike::NONPROSE);
}

TEST(SpikeWordStream, FixturePagesMapToWordsAtTwoLayouts) {
  const auto stream = buildStream(SPIKE_FIXTURE);
  const auto pagesA = layoutChapter(SPIKE_FIXTURE, LAYOUT_A);
  const auto pagesB = layoutChapter(SPIKE_FIXTURE, LAYOUT_B);
  mapPages("fixture_s1.xhtml", *stream, LAYOUT_A, pagesA, 100);
  mapPages("fixture_s1.xhtml", *stream, LAYOUT_B, pagesB, 100);
  EXPECT_NE(pagesA.size(), pagesB.size());
}

TEST(SpikeWordStream, EpubChapterPagesMapToWordsAtTwoLayouts) {
  const auto stream = buildStream(SPIKE_EPUB_CHAPTER);
  printStream("test_reader_menu_progress.epub OEBPS/chapter1.xhtml", *stream, 12);
  printSize("test_reader_menu_progress.epub chapter1", *stream);
  const auto pagesA = layoutChapter(SPIKE_EPUB_CHAPTER, LAYOUT_A);
  const auto pagesB = layoutChapter(SPIKE_EPUB_CHAPTER, LAYOUT_B);
  mapPages("test_reader_menu_progress.epub chapter1", *stream, LAYOUT_A, pagesA, 8);
  mapPages("test_reader_menu_progress.epub chapter1", *stream, LAYOUT_B, pagesB, 8);
  EXPECT_GT(pagesB.size(), pagesA.size());
}
