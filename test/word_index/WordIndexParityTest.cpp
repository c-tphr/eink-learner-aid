// Renderer parity (ported from the S1 prototype's central test, docs/spikes/S1-word-stream.md
// section 7). The real, unmodified ChapterHtmlSlimParser lays each chapter out at three layouts;
// every rendered token must fall inside a word of the chapter's .wix index at the same visible
// offset and be part of its text, and page -> first word and word -> page (with
// Section::getPageForVisibleTextOffset's rule) must be exact.

#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Epub/parsers/ChapterHtmlSlimParser.h"
#include "WordIndexTestSupport.h"

using namespace wordindex;

namespace {

struct Layout {
  const char* name;
  uint16_t width;
  uint16_t height;
  int lineHeight;
  bool hyphenation;
  bool focusReading;
};

// The host renderer stub has fixed 8 px glyphs, so viewport and line height stand in for fonts.
constexpr Layout LAYOUTS[] = {
    {"A 480x800 line16", 480, 800, 16, false, false},
    {"B 240x300 line24 hyph+focus", 240, 300, 24, true, true},
    {"C 160x240 line20 hyph", 160, 240, 20, true, false},
};

struct LaidOutPage {
  uint32_t start;  // the visibleTextOffset the parser hands to Section's completePageFn
  std::unique_ptr<Page> page;
};

std::vector<LaidOutPage> layoutChapter(const std::string& path, const Layout& layout) {
  GfxRenderer renderer;
  renderer.fontLineHeights[0] = layout.lineHeight;
  renderer.fontSizePixels[0] = static_cast<float>(layout.lineHeight);
  auto epub = std::make_shared<Epub>();
  epub->imageBytes = {'G', 'I', 'F', '8', '9', 'a', 200, 0, 150, 0};  // a 200x150 GIF header
  Epub::enableGifProbeForTest = true;
  Hyphenator::setPreferredLanguage("en");

  std::vector<LaidOutPage> pages;
  const std::string& filepath = path;  // the parser keeps a reference
  const std::string imageBase = (std::filesystem::temp_directory_path() / "wix-parity-img-").string();
  ChapterHtmlSlimParser parser(
      epub, filepath, renderer, 0, 1.0f, false, 0, layout.width, layout.height, layout.hyphenation, layout.focusReading,
      [&](std::unique_ptr<Page> page, uint16_t, uint16_t, const uint32_t visibleTextOffset) {
        pages.push_back({visibleTextOffset, std::move(page)});
      },
      false, "", imageBase, 0, {}, nullptr, nullptr);
  EXPECT_TRUE(parser.parseAndBuildPages());
  Epub::enableGifProbeForTest = false;
  return pages;
}

// Section::getPageForVisibleTextOffset (lib/Epub/Epub/Section.cpp) over the in-RAM page offsets.
uint32_t pageForOffset(const std::vector<uint32_t>& starts, const uint32_t offset,
                       const bool preferFirstAtOffset = false) {
  uint32_t result = 0;
  for (size_t i = 0; i < starts.size(); i++) {
    if (preferFirstAtOffset && starts[i] == offset) return static_cast<uint32_t>(i);
    if (starts[i] > offset) break;
    result = static_cast<uint32_t>(i);
  }
  return result;
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

bool isListMarker(const std::string& text) {
  if (text == "\xE2\x80\xA2") return true;
  if (text.size() < 2 || text.back() != '.') return false;
  return std::all_of(text.begin(), text.end() - 1, [](const char c) { return c >= '0' && c <= '9'; });
}

struct Row {
  std::string input;
  std::string layout;
  size_t pages = 0;
  size_t matched = 0;
  size_t excluded = 0;   // footnote marks: counted in offsets, deliberately in no word
  size_t synthetic = 0;  // renderer-made tokens with no source text (bullets, image placeholders)
  size_t spaces = 0;     // no-break space tokens
  size_t mismatched = 0;
  size_t joined = 0;  // one token over two words split after a dash (Focus Reading keeps "1907–1925,")
  size_t pageToWordErrors = 0;
  size_t wordToPageErrors = 0;
  size_t sectionEndErrors = 0;
  size_t sectionEnds = 0;
  size_t notDrawn = 0;
  size_t midWordStarts = 0;
  // Grid tables (TABLE regions only): words drawn on another page than their offset gives, because a
  // row split across pages is drawn line by line across its cells, or not drawn at all.
  size_t tableWordsOffPage = 0;
  size_t tableWordsNotDrawn = 0;
};

std::vector<Row>& rows() {
  static std::vector<Row> all;
  return all;
}

void checkParity(const std::string& label, const std::string& path, const Layout& layout) {
  SCOPED_TRACE(label + ", " + layout.name);
  Row row{label, layout.name};

  // The index, through the reader as the firmware will use it.
  wixtest::Built built = wixtest::buildIndex(wixtest::readFile(path));
  ASSERT_EQ(built.status, BuildStatus::Ok) << buildStatusName(built.status);
  WordIndexReader reader;
  ASSERT_EQ(reader.open(built.file), ReadStatus::Ok);
  const uint32_t wordCount = reader.wordCount();
  auto word = [&](uint32_t i) {
    WordInfo w;
    EXPECT_TRUE(reader.word(i, w));
    return wixtest::Word{std::string(w.text), w.start, w.end, w.flags};
  };

  const auto pages = layoutChapter(path, layout);
  row.pages = pages.size();
  std::vector<uint32_t> starts;
  for (const auto& p : pages) starts.push_back(p.start);

  // Every token: the word whose span holds its first offset must contain its text.
  std::vector<int> firstPage(wordCount, -1);
  std::vector<int> lastPage(wordCount, -1);
  for (size_t p = 0; p < pages.size(); ++p) {
    if (p > 0) {
      EXPECT_LE(starts[p - 1], starts[p]) << "page offsets must not decrease";
    }
    for (const auto& element : pages[p].page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const TextBlock& block = *static_cast<const PageLine&>(*element).getBlock();
      for (uint16_t i = 0; i < block.wordCount(); ++i) {
        std::string text = stripSoftHyphens(block.wordText(i));
        if (block.wordHasDiscretionaryHyphen(i) && !text.empty() && text.back() == '-') text.pop_back();
        const auto range = block.wordSourceRange(i);
        if (isSpaceToken(text)) {
          row.spaces++;
          continue;
        }
        if (range.start == UINT32_MAX || range.start == range.end) {
          row.synthetic++;
          continue;
        }
        uint32_t w = 0;
        ASSERT_TRUE(reader.firstWordEndingAfter(range.start, w));
        const bool inside = w < wordCount && word(w).start <= range.start;
        if (inside) {
          // The token is part of word w, or of w and the words after it that touch it in the offset
          // coordinate (no separator between: a dash split), up to the token's end.
          std::string joined = word(w).text;
          uint32_t last = w;
          while (joined.find(text) == std::string::npos && last + 1 < wordCount &&
                 word(last + 1).start == word(last).end && word(last + 1).start < range.end) {
            joined += word(++last).text;
          }
          if (joined.find(text) != std::string::npos && range.end <= word(last).end) {
            if (last == w) {
              row.matched++;
            } else {
              row.joined++;
            }
            for (uint32_t k = w; k <= last; ++k) {
              if (firstPage[k] < 0) firstPage[k] = static_cast<int>(p);
              lastPage[k] = static_cast<int>(p);
            }
            continue;
          }
        }
        // A footnote mark: in no word, right after (or inside) a word flagged NOTEREF.
        const bool afterNoteref = (inside && (word(w).flags & NOTEREF)) || (w > 0 && (word(w - 1).flags & NOTEREF));
        if (afterNoteref && (!inside || word(w).text.find(text) == std::string::npos)) {
          row.excluded++;
          continue;
        }
        if (isListMarker(text)) {  // the renderer's own "•" or "1." for <li>, at the item's offset
          row.synthetic++;
          continue;
        }
        row.mismatched++;
        ADD_FAILURE() << "page " << p << " token '" << text << "' @" << range.start
                      << (w < wordCount ? " word '" + word(w).text + "'" : std::string(" no word"));
      }
    }
  }

  // Page -> first word: the first word with content on that page or later. Word -> page: the page
  // where the renderer first drew the word.
  for (size_t p = 0; p < pages.size(); ++p) {
    uint32_t got = 0;
    ASSERT_TRUE(reader.firstWordEndingAfter(starts[p], got));
    uint32_t want = wordCount;
    for (uint32_t w = 0; w < wordCount; ++w) {
      if (lastPage[w] >= static_cast<int>(p)) {
        want = w;
        break;
      }
    }
    if (got != want) {
      row.pageToWordErrors++;
      ADD_FAILURE() << "page " << p << " (offset " << starts[p] << "): index says word " << got << ", renderer "
                    << want;
    }
    if (got < wordCount && word(got).start < starts[p]) row.midWordStarts++;
  }
  std::vector<bool> inTable(wordCount, false);
  for (uint32_t i = 0; i < reader.markerCount(); ++i) {
    Marker m;
    ASSERT_TRUE(reader.marker(i, m));
    if (m.kind == MARKER_TABLE) {
      for (uint32_t w = m.wordStart; w < m.wordEnd; ++w) inTable[w] = true;
    }
  }
  for (uint32_t w = 0; w < wordCount; ++w) {
    if (firstPage[w] < 0) {
      if (inTable[w]) {
        row.tableWordsNotDrawn++;
        continue;
      }
      row.notDrawn++;
      ADD_FAILURE() << "word " << w << " '" << word(w).text << "' is not drawn";
      continue;
    }
    uint32_t start = 0;
    ASSERT_TRUE(reader.wordStart(w, start));
    if (pageForOffset(starts, start) != static_cast<uint32_t>(firstPage[w])) {
      if (inTable[w]) {
        row.tableWordsOffPage++;
        continue;
      }
      row.wordToPageErrors++;
      ADD_FAILURE() << "word " << w << " '" << word(w).text << "' maps to page " << pageForOffset(starts, start)
                    << ", renderer drew it first on page " << firstPage[w];
    }
  }
  // A section's end in page view: the page showing the last code point of its last word.
  AnchorCursor cursor;
  Anchor anchor;
  while (reader.nextAnchor(cursor, anchor)) {
    const std::string id(anchor.idView());
    if (id.size() < 4 || id.compare(id.size() - 4, 4, "-end") != 0 || anchor.wordIndex == 0) continue;
    const wixtest::Word last = word(anchor.wordIndex - 1);
    row.sectionEnds++;
    if (pageForOffset(starts, last.end - 1) != static_cast<uint32_t>(lastPage[anchor.wordIndex - 1])) {
      row.sectionEndErrors++;
      ADD_FAILURE() << id << ": last word '" << last.text << "' maps to page " << pageForOffset(starts, last.end - 1)
                    << ", renderer drew its end on page " << lastPage[anchor.wordIndex - 1];
    }
  }
  EXPECT_EQ(row.mismatched, 0u);
  EXPECT_EQ(row.notDrawn, 0u);
  rows().push_back(row);
}

struct Input {
  std::string label;
  std::string path;
};

std::vector<Input> inputs() {
  std::vector<Input> out = {{"S1 fixture_s1.xhtml", WIX_SPIKE_FIXTURE}};
  for (const char* name : {"title", "chapter-1", "chapter-2", "chapter-3", "endnotes"}) {
    out.push_back({std::string("fixture (prepared) ") + name,
                   std::string(WIX_DATA_DIR "/prepared/OEBPS/text/") + name + ".xhtml"});
  }
  out.push_back({"test_reader_menu_progress ch1", WIX_DATA_DIR "/test_reader_menu_progress_chapter1.xhtml"});
  return out;
}

}  // namespace

TEST(RendererParity, EveryTokenAndPageMapsExactly) {
  for (const Input& in : inputs()) {
    for (const Layout& layout : LAYOUTS) checkParity(in.label, in.path, layout);
  }
  std::printf("\n== Renderer parity (ChapterHtmlSlimParser, unmodified)\n");
  std::printf(
      "| Input | Layout | Pages | Tokens matched | Joined at a dash | Footnote marks | Synthetic | NBSP | "
      "Mismatches | Page->word errors | Word->page errors | Section-end page errors | Pages starting inside a "
      "word | Grid-table words off page / not drawn |\n");
  std::printf("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |\n");
  size_t pagesStartingInsideAWord = 0;
  for (const Row& r : rows()) {
    std::printf("| %s | %s | %zu | %zu | %zu | %zu | %zu | %zu | %zu | %zu | %zu | %zu of %zu | %zu | %zu / %zu |\n",
                r.input.c_str(), r.layout.c_str(), r.pages, r.matched, r.joined, r.excluded, r.synthetic, r.spaces,
                r.mismatched, r.pageToWordErrors, r.wordToPageErrors, r.sectionEndErrors, r.sectionEnds,
                r.midWordStarts, r.tableWordsOffPage, r.tableWordsNotDrawn);
    pagesStartingInsideAWord += r.midWordStarts;
  }
  // Hyphenation must actually split words across pages somewhere, or the page -> word rule for a
  // page starting inside a word is untested.
  EXPECT_GT(pagesStartingInsideAWord, 0u);
}
