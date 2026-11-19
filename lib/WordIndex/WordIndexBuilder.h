#pragma once

// Builds a chapter's .wix index in one streaming expat pass over its XHTML (no DOM). The word rules
// are docs/spikes/S1-word-stream.md section 5 plus NOTE regions, as tools/prep/words.py implements
// them; visible offsets count exactly as ChapterHtmlSlimParser counts them. See README.md.

#include <expat.h>

#include <cstdint>

#include "WordIndexFormat.h"
#include "WordIndexIo.h"

namespace wordindex {

// Elements nested deeper than this fail the build (TooDeep) instead of growing expat's tag stack.
inline constexpr uint32_t MAX_ELEMENT_DEPTH = 256;
// XHTML bytes handed to expat per step, as the renderer reads them.
inline constexpr uint32_t INPUT_CHUNK_BYTES = 1024;

enum class BuildStatus : uint8_t {
  Ok,
  ReadError,         // ChapterInput::read failed
  WriteError,        // IndexSink failed
  ParseError,        // not well-formed XML; BuildReport has expat's error code and position
  OutOfMemory,       // expat could not allocate
  TooDeep,           // more than MAX_ELEMENT_DEPTH nested elements
  AnchorInsideWord,  // an "rsvp-" anchor strictly inside a word (a preparation error)
  AnchorIdTooLong,   // an "rsvp-" id longer than MAX_ANCHOR_ID_BYTES
  TooLarge,          // the source or index would pass 4 GB, or one word spans more than MAX_WORD_SPAN codepoints
};

const char* buildStatusName(BuildStatus status);

struct BuildReport {
  BuildStatus status = BuildStatus::Ok;
  // ParseError: expat's XML_Error code. ParseError, TooDeep and the anchor errors: where in the XHTML.
  uint32_t xmlError = 0;
  uint32_t line = 0;
  uint32_t column = 0;
  // AnchorInsideWord: the anchor and the index of the word it is inside.
  uint32_t wordIndex = 0;
  char anchorId[MAX_ANCHOR_ID_BYTES + 1] = {};
  // Ok: the header written, and sizes for measurement.
  Header header;
  uint32_t spillSlots = 0;     // scratch slots used for tables that outgrew the RAM buffers
  uint32_t maxBlockBytes = 0;  // largest used part of a word block
};

class WordIndexBuilder {
 public:
  // `memory` routes expat's allocations (for example to PSRAM); null uses malloc.
  explicit WordIndexBuilder(const XML_Memory_Handling_Suite* memorySuite = nullptr) : memory(memorySuite) {}
  ~WordIndexBuilder();
  WordIndexBuilder(const WordIndexBuilder&) = delete;
  WordIndexBuilder& operator=(const WordIndexBuilder&) = delete;

  // Reads the whole chapter from `input` and writes its index to `sink` (empty at the start). On any
  // status other than Ok the sink holds an unusable partial file whose header is all zero bytes.
  BuildStatus build(ChapterInput& input, IndexSink& sink);

  // The same, pushed by the caller (for a Print-style stream such as Epub::readItemContentsToStream):
  // begin(), feed() the XHTML in any chunking, then end(). feed() returns false once the build failed.
  BuildStatus begin(IndexSink& sink);
  bool feed(const uint8_t* data, size_t length);
  BuildStatus end();
  const BuildReport& report() const { return result; }

 private:
  // A table buffered in RAM, spilled to scratch slots when it outgrows one slot's payload.
  struct Table {
    uint8_t data[SLOT_BYTES - SLOT_HEADER_BYTES];
    uint32_t used = 0;
    uint32_t records = 0;       // in `data`
    uint32_t totalRecords = 0;  // including spilled ones
    uint32_t totalBytes = 0;    // including spilled ones
    uint32_t firstSpill = 0;    // slot index, 0 for none
    uint32_t lastSpill = 0;
  };
  enum TableId : uint8_t { TABLE_DIRECTORY = 1, TABLE_ANCHORS = 2, TABLE_MARKERS = 3 };

  // Where an open region's marker record is, so its end can be filled in when the region closes.
  struct OpenMarker {
    bool open = false;
    bool inRam = true;
    uint32_t at = 0;  // byte offset in the marker table's RAM buffer, or in the file
  };

  struct Word {
    char text[MAX_WORD_BYTES + 1];
    uint32_t length = 0;
    uint32_t start = 0;
    uint32_t end = 0;
    uint8_t flags = 0;
  };

  void reset(IndexSink& sink);
  void parse(const uint8_t* data, size_t length, bool inExpatBuffer);
  void fail(BuildStatus status);
  bool failed() const { return result.status != BuildStatus::Ok; }

  // Word pass.
  uint32_t started() const { return emitted + (curLength > 0 ? 1 : 0); }
  bool noterefActive() const { return explicitNoterefs > 0 || (sups > 0 && links > 0); }
  void onText(const char* s, int length);
  void append(uint32_t cp, uint32_t offset);
  void flushWord();
  void push(uint8_t flags);
  void boundary(uint8_t flag);
  void recordAnchor(const char* id, uint32_t index);
  void openMarker(uint8_t kind, OpenMarker* region);
  void closeMarker(OpenMarker& region, uint32_t wordEnd);

  // Output.
  void commitPending();
  void closeBlock();
  bool appendBytes(const uint8_t* data, size_t length);
  bool addRecord(Table& table, TableId id, const uint8_t* record, uint32_t length);
  bool spill(Table& table, TableId id);
  bool copyTable(const Table& table, TableId id, uint32_t& crc);
  void finish();

  static void XMLCALL onStart(void* user, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL onEnd(void* user, const XML_Char* name);
  static void XMLCALL onCharacters(void* user, const XML_Char* s, int length);
  static void XMLCALL onDefault(void* user, const XML_Char* s, int length);

  const XML_Memory_Handling_Suite* memory;
  XML_Parser parser = nullptr;
  IndexSink* out = nullptr;
  BuildReport result;

  // Renderer state, mirrored exactly (ChapterHtmlSlimParser startElement/characterData/endElement).
  bool parsing = true;  // false after a parse error following </html>
  bool insideBody = false;
  bool htmlEnded = false;
  uint32_t nonVisibleDepth = 0;
  uint32_t visibleOffset = 0;

  // Word-rule state.
  uint16_t elements[MAX_ELEMENT_DEPTH] = {};
  uint32_t depth = 0;
  bool hidden = false;
  uint32_t headings = 0;
  uint32_t sups = 0;
  uint32_t links = 0;
  uint32_t explicitNoterefs = 0;
  uint32_t nonprose = 0;
  uint32_t notes = 0;
  OpenMarker nonproseRegion;
  OpenMarker noteRegion;

  char cur[MAX_WORD_BYTES + 5] = {};  // the word in progress, room for one more code point and a NUL
  uint32_t curLength = 0;
  uint32_t curStart = 0;
  uint32_t curEnd = 0;
  uint32_t lastCp = 0;
  bool curNoteref = false;
  bool curInNote = false;
  bool anchorPending = false;  // an anchor was met while the current word was in progress

  // The last word stays here until the next one arrives: boundaries and footnote marks after it
  // still add flags.
  Word pending;
  bool hasPending = false;
  uint32_t emitted = 0;  // words pushed, including the pending one

  // The open word block: records at their final place in `block`, texts in `blockText`.
  uint8_t block[SLOT_BYTES] = {};
  uint8_t blockText[SLOT_BYTES - SLOT_HEADER_BYTES] = {};
  uint32_t blockWords = 0;
  uint32_t blockTextBytes = 0;
  uint32_t blockFirstWord = 0;
  uint32_t blockBase = 0;
  uint32_t committed = 0;  // words written into blocks

  Table directory;
  Table anchors;
  Table markers;
  uint32_t nextSlot = 1;
  uint64_t fileBytes = 0;
  uint64_t sourceBytes = 0;
  uint32_t sourceCrc = 0;
};

}  // namespace wordindex
