#pragma once

// The .wix on-SD format (README.md, "File format"): constants, flags, the header, and the
// little-endian and CRC-32 helpers shared by the builder and the reader.

#include <cstddef>
#include <cstdint>

namespace wordindex {

inline constexpr char MAGIC[4] = {'C', 'P', 'W', 'I'};
// Raised when the byte layout changes.
inline constexpr uint8_t FORMAT_VERSION = 1;
// Raised when a word rule changes in a way that can move a word index. Same number as
// fixtures/books/fixture.expected.json "rules_version" (docs/spikes/S1-word-stream.md section 5 plus
// NOTE regions).
inline constexpr uint8_t RULES_VERSION = 1;

// The file is a sequence of fixed-size slots. Slot 0 holds the header; the others hold word blocks
// (and, in large chapters, spill slots the builder used as scratch). The tables follow the slots.
inline constexpr uint32_t SLOT_BYTES = 512;
inline constexpr uint32_t HEADER_BYTES = 96;
inline constexpr uint32_t SLOT_HEADER_BYTES = 16;
inline constexpr uint32_t WORD_RECORD_BYTES = 7;
inline constexpr uint32_t DIRECTORY_ENTRY_BYTES = 12;
inline constexpr uint32_t MARKER_RECORD_BYTES = 20;
inline constexpr uint32_t ANCHOR_RECORD_FIXED_BYTES = 9;

// Longest word, in UTF-8 bytes (section 5: longer words are cut at a code point boundary).
inline constexpr uint32_t MAX_WORD_BYTES = 255;
// Longest anchor id kept, in bytes. The builder fails on a longer "rsvp-" id.
inline constexpr uint32_t MAX_ANCHOR_ID_BYTES = 255;
// A block holds at most this many words (every word costs at least a record and one text byte).
inline constexpr uint32_t MAX_BLOCK_WORDS = (SLOT_BYTES - SLOT_HEADER_BYTES) / (WORD_RECORD_BYTES + 1);
// A word's visible span is stored in 24 bits.
inline constexpr uint32_t MAX_WORD_SPAN = 0xFFFFFF;

enum SlotKind : uint8_t {
  SLOT_WORDS = 1,
  SLOT_SPILL = 2,  // builder scratch, never read by the reader
};

// Word flags (one byte per word). Bit values match tools/prep/words.py; bit 0 (the prototype's
// sentence end) is never set: lib/RsvpCore decides sentence ends.
enum WordFlag : uint8_t {
  PARA_END = 1 << 1,    // last word before the start or end of a block element (not br/img), and the last word
  LINE_END = 1 << 2,    // last word before <br>
  NONPROSE = 1 << 3,    // inside the outermost <table> or <pre>
  NOTEREF = 1 << 4,     // a footnote reference mark was removed from or right after this word
  HEADING = 1 << 5,     // inside h1-h6
  BYTE_SPLIT = 1 << 6,  // cut at MAX_WORD_BYTES; the next word continues it
  NOTE = 1 << 7,        // inside a footnote or endnote body (a NOTE region)
};

enum MarkerKind : uint8_t {
  MARKER_IMAGE = 1,  // img/image outside table and pre: zero words long, at the index of the next word
  MARKER_TABLE = 2,  // outermost <table>
  MARKER_CODE = 3,   // outermost <pre>
  MARKER_NOTE = 4,   // outermost footnote or endnote body
};

// Marker flag bits.
enum MarkerFlag : uint8_t {
  MARKER_IN_NOTE = 1 << 0,  // opened inside a NOTE region (an image or table within a footnote body)
};

// Bit masks for marker queries.
inline constexpr uint8_t kindBit(const uint8_t kind) { return static_cast<uint8_t>(1u << kind); }
inline constexpr uint8_t ANY_MARKER =
    kindBit(MARKER_IMAGE) | kindBit(MARKER_TABLE) | kindBit(MARKER_CODE) | kindBit(MARKER_NOTE);
inline constexpr uint8_t REGION_MARKERS = kindBit(MARKER_TABLE) | kindBit(MARKER_CODE) | kindBit(MARKER_NOTE);

struct Header {
  uint8_t formatVersion = FORMAT_VERSION;
  uint8_t rulesVersion = RULES_VERSION;
  uint32_t sourceBytes = 0;        // bytes of chapter XHTML read (the inflated zip entry size)
  uint32_t sourceCrc32 = 0;        // CRC-32 of those bytes (equals the zip entry's CRC-32)
  uint32_t visibleCodepoints = 0;  // the renderer's visible-text count for the whole chapter
  uint32_t wordCount = 0;
  uint32_t slotCount = 0;  // slots after the header slot
  uint32_t blockCount = 0;
  uint32_t directoryOffset = 0;
  uint32_t anchorCount = 0;
  uint32_t anchorOffset = 0;
  uint32_t anchorBytes = 0;
  uint32_t markerCount = 0;
  uint32_t markerOffset = 0;
  uint32_t fileBytes = 0;
  uint32_t tablesCrc32 = 0;  // CRC-32 of bytes [directoryOffset, fileBytes)
};

void encodeHeader(const Header& header, uint8_t out[HEADER_BYTES]);
// Checks magic, format version, header CRC and the fixed sizes, in that order; the reader checks
// the rest. `out` is filled only on Ok.
enum class HeaderCheck : uint8_t { Ok, BadMagic, OtherFormat, BadCrc, BadLayout };
HeaderCheck decodeHeader(const uint8_t in[HEADER_BYTES], Header& out);

// CRC-32 (IEEE 802.3, as zip and zlib compute it). Start with 0; feed chunks with the previous value.
uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length);

inline uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t readU24(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16);
}
inline uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
inline void writeU16(uint8_t* p, const uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
inline void writeU24(uint8_t* p, const uint32_t v) {
  writeU16(p, v);
  p[2] = static_cast<uint8_t>(v >> 16);
}
inline void writeU32(uint8_t* p, const uint32_t v) {
  writeU16(p, v);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}

}  // namespace wordindex
