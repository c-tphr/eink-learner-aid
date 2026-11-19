#pragma once

// Reads a chapter's .wix index through an IndexFile without loading the chapter: it keeps the
// header, one page of the block directory and two word blocks in RAM (README.md, "RAM").

#include <cstdint>
#include <string_view>

#include "WordIndexFormat.h"
#include "WordIndexIo.h"

namespace wordindex {

enum class ReadStatus : uint8_t {
  Ok,
  NotOpen,
  IoError,      // the file could not be read
  Truncated,    // shorter than its header says, or too short for a header
  BadMagic,     // not an index, or an unfinished build
  OtherFormat,  // another format version: rebuild
  OtherRules,   // built with other word rules: rebuild
  Corrupt,      // a checksum or consistency check failed
};

const char* readStatusName(ReadStatus status);

struct WordInfo {
  std::string_view text;  // UTF-8, not NUL-terminated; valid until the next call on the reader
  uint32_t index = 0;
  uint32_t start = 0;  // visible offset of the first code point
  uint32_t end = 0;    // visible offset after the last code point
  uint8_t flags = 0;   // WordFlag bits
};

struct Marker {
  uint8_t kind = 0;   // MarkerKind
  uint8_t flags = 0;  // MarkerFlag bits
  uint32_t wordStart = 0;
  uint32_t wordEnd = 0;  // == wordStart for an image
  uint32_t offsetStart = 0;
  uint32_t offsetEnd = 0;
};

struct Anchor {
  uint32_t wordIndex = 0;  // words started before the anchor's start tag
  uint32_t offset = 0;     // visible offset at the start tag
  uint8_t idLength = 0;
  char id[MAX_ANCHOR_ID_BYTES + 1] = {};
  std::string_view idView() const { return {id, idLength}; }
};

// Position of a sequential walk through the anchor table.
struct AnchorCursor {
  uint32_t index = 0;
  uint32_t offset = 0;  // byte offset of record `index` in the table
};

class WordIndexReader {
 public:
  // Directory entries kept in RAM: one page.
  static constexpr uint32_t DIRECTORY_PAGE_ENTRIES = 42;

  // Reads and checks the header and the tables (directory, anchors, markers) against their
  // checksum. Word blocks are checked when they are first read. `file` must outlive the reader.
  ReadStatus open(IndexFile& file);
  void close();
  // Ok while usable. Becomes Corrupt or IoError if a later read fails; every call then fails.
  ReadStatus status() const { return state; }

  const Header& header() const { return head; }
  uint32_t wordCount() const { return head.wordCount; }
  // True when the index was built from these source bytes (zip entry size and CRC-32).
  bool matchesSource(uint32_t sourceBytes, uint32_t sourceCrc32) const {
    return state == ReadStatus::Ok && head.sourceBytes == sourceBytes && head.sourceCrc32 == sourceCrc32;
  }

  // --- Words. Sequential calls (index +1 or -1) stay within the cached blocks.
  bool word(uint32_t index, WordInfo& out);
  bool wordFlags(uint32_t index, uint8_t& out);
  bool wordStart(uint32_t index, uint32_t& out);  // word -> page: pass to getPageForVisibleTextOffset
  // First word whose end is past `offset` (page -> word), or wordCount() if none.
  bool firstWordEndingAfter(uint32_t offset, uint32_t& out);

  // --- Anchors ("rsvp-" ids), in document order.
  uint32_t anchorCount() const { return head.anchorCount; }
  bool findAnchor(std::string_view id, Anchor& out);
  // Reads the anchor at `cursor` and moves the cursor on. False at the end or on an error.
  bool nextAnchor(AnchorCursor& cursor, Anchor& out);

  // --- Markers, in document order of their start (wordStart does not decrease).
  uint32_t markerCount() const { return head.markerCount; }
  bool marker(uint32_t index, Marker& out);
  // An IMAGE marker sits right before word `index` (`index` may be wordCount()).
  bool imageAt(uint32_t index, bool includeInNote = true);
  // The region (TABLE, CODE or NOTE, by `kinds` bit mask) containing word `index`.
  bool regionContaining(uint32_t index, uint8_t kinds, Marker& out);
  // First marker of `kinds` with wordStart >= `index`; `position` gets its index in the table.
  bool nextMarker(uint32_t index, uint8_t kinds, Marker& out, uint32_t* position = nullptr);

 private:
  struct CachedBlock {
    bool valid = false;
    uint32_t blockIndex = 0;
    uint32_t firstWord = 0;
    uint32_t wordCount = 0;
    uint32_t baseOffset = 0;
    uint32_t lastUse = 0;
    uint8_t data[SLOT_BYTES];
    uint16_t textAt[MAX_BLOCK_WORDS];  // byte offset of each word's text in `data`
  };

  struct DirectoryEntry {
    uint32_t firstWord;
    uint32_t baseOffset;
    uint32_t slot;
  };

  bool readFile(uint32_t offset, uint8_t* data, size_t length);
  ReadStatus checkTables();
  bool fail(ReadStatus status);
  static void decodeEntry(const uint8_t* p, DirectoryEntry& out);
  bool loadPage(uint32_t pageNumber);
  bool entry(uint32_t blockIndex, DirectoryEntry& out);
  // Last block whose key (first word, or base offset) is <= `key`. `found` is false when none is.
  bool findBlock(bool byOffset, uint32_t key, uint32_t& blockIndex, bool& found);
  CachedBlock* loadBlock(uint32_t blockIndex);
  CachedBlock* blockForWord(uint32_t index);
  void decode(const CachedBlock& block, uint32_t slot, WordInfo& out) const;
  bool readMarker(uint32_t index, Marker& out);
  bool readAnchorAt(uint32_t offset, Anchor& out, uint32_t& recordBytes);

  IndexFile* file = nullptr;
  ReadStatus state = ReadStatus::NotOpen;
  Header head;

  // One directory page, plus the first entry of the next page (to know where the page ends).
  DirectoryEntry page[DIRECTORY_PAGE_ENTRIES + 1] = {};
  uint32_t pageNumber = UINT32_MAX;
  uint32_t pageEntries = 0;  // entries of the page itself, without the next page's first

  CachedBlock blocks[2];
  uint32_t useClock = 0;
};

}  // namespace wordindex
