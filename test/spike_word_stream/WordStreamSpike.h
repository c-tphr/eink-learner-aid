// THROWAWAY PROTOTYPE for spike S1 (issue #9). Not production code: do not include it from
// firmware, do not build on it. It exists to test the design in docs/spikes/S1-word-stream.md.
//
// A second, layout-free expat pass over a chapter's XHTML that produces the RSVP word stream
// in the same coordinate system the page renderer already records per page: zero-based
// visible Unicode codepoints in <body> (ChapterHtmlSlimParser::visibleTextOffset).
#pragma once

#include <expat.h>

#include <climits>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace spike {

enum WordFlag : uint8_t {
  SENT_END = 1 << 0,   // last word of a sentence
  PARA_END = 1 << 1,   // last word before a block boundary
  LINE_END = 1 << 2,   // last word before <br>
  NONPROSE = 1 << 3,   // inside a table or code block (RSVP pauses, page view shows it)
  NOTEREF = 1 << 4,    // a footnote reference mark was removed from or right after this word
  HEADING = 1 << 5,    // inside h1-h6
  BYTE_SPLIT = 1 << 6  // cut at the byte cap; the next word continues it
};

struct Word {
  std::string text;  // NFC, soft hyphens and noteref text removed
  uint32_t start;    // visible offset of the first codepoint
  uint32_t end;      // visible offset after the last codepoint
  uint8_t flags;
};

struct Anchor {
  std::string id;
  uint32_t wordIndex;  // index of the first word that starts after the anchor
  uint32_t offset;     // visible offset where the element opened
};

enum class MarkerKind : uint8_t { Image = 1, Table = 2, Code = 3 };

struct Marker {
  MarkerKind kind;
  uint32_t wordStart;  // first word inside the region (or the word after an image)
  uint32_t wordEnd;    // one past the last word inside (== wordStart for an image)
  uint32_t offsetStart;
  uint32_t offsetEnd;
};

// Visible text that is counted in offsets but is not part of any word.
struct ExcludedRun {
  uint32_t start;
  uint32_t end;
  char reason;  // 'n' noteref, 'h' hidden/pagebreak/rt
};

class WordStreamBuilder {
 public:
  static constexpr size_t MAX_WORD_BYTES = 255;

  WordStreamBuilder();
  ~WordStreamBuilder();
  WordStreamBuilder(const WordStreamBuilder&) = delete;
  WordStreamBuilder& operator=(const WordStreamBuilder&) = delete;

  // Feed the chapter XHTML in any chunking (the firmware would feed 1 KB reads or a Print sink).
  bool write(const char* data, size_t len);
  bool finish();

  const std::vector<Word>& words() const { return words_; }
  const std::vector<Anchor>& anchors() const { return anchors_; }
  const std::vector<Marker>& markers() const { return markers_; }
  const std::vector<ExcludedRun>& excluded() const { return excluded_; }
  uint32_t visibleCodepoints() const { return visibleOffset_; }

 private:
  struct Element {
    bool breaks = false;      // word boundary at start and end
    bool paragraph = false;   // boundary also ends a paragraph
    bool heading = false;
    bool sup = false;
    bool internalLink = false;
    bool noteref = false;
    bool nonprose = false;
  };

  XML_Parser parser_ = nullptr;
  bool ok_ = true;
  bool insideBody_ = false;
  int depth_ = 0;
  int nonVisibleDepth_ = 0;
  int hiddenUntilDepth_ = INT_MAX;
  int headingCount_ = 0;
  int supCount_ = 0;
  int internalLinkCount_ = 0;
  int explicitNoterefCount_ = 0;
  int nonproseCount_ = 0;
  MarkerKind openRegionKind_ = MarkerKind::Table;
  std::vector<Element> stack_;
  uint32_t visibleOffset_ = 0;

  std::string cur_;
  uint32_t curStart_ = 0;
  uint32_t curEnd_ = 0;
  uint32_t lastCp_ = 0;
  bool curNoteref_ = false;

  std::vector<Word> words_;
  std::vector<Anchor> anchors_;
  std::vector<Marker> markers_;
  std::vector<ExcludedRun> excluded_;

  bool noterefActive() const { return explicitNoterefCount_ > 0 || (supCount_ > 0 && internalLinkCount_ > 0); }
  void exclude(uint32_t offset, char reason);
  void appendCodepoint(uint32_t cp, uint32_t offset);
  void flushWord();
  void pushWord(std::string text, uint32_t start, uint32_t end, uint8_t flags);
  void markBoundary(uint8_t flag);
  void decideSentence(size_t index, const Word* next);
  void onText(const char* s, int len);

  static void XMLCALL onStart(void* user, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL onEnd(void* user, const XML_Char* name);
  static void XMLCALL onChars(void* user, const XML_Char* s, int len);
  static void XMLCALL onDefault(void* user, const XML_Char* s, int len);
};

// First word with any codepoint at or after `offset` (page start -> first word).
uint32_t firstWordAtOrAfter(const std::vector<Word>& words, uint32_t offset);

// Same result as Section::getPageForVisibleTextOffset (lib/Epub/Epub/Section.cpp:1167-1181)
// over an in-RAM copy of the section's visible-offset LUT.
uint16_t pageForOffset(const std::vector<uint32_t>& pageStarts, uint32_t offset, bool preferFirstAtOffset = false);

// Bytes the proposed on-SD word file (docs/spikes/S1-word-stream.md, "Word file") would take.
struct FormatSize {
  size_t blocks = 0;
  size_t headerBytes = 0;
  size_t directoryBytes = 0;
  size_t blockBytes = 0;
  size_t maxBlockBytes = 0;
  size_t anchorBytes = 0;
  size_t markerBytes = 0;
  size_t total() const { return headerBytes + directoryBytes + blockBytes + anchorBytes + markerBytes; }
};
FormatSize estimateFormatSize(const WordStreamBuilder& stream);

std::string flagString(uint8_t flags);

}  // namespace spike
