#pragma once

// Internal helpers shared by the validating pass and the on-demand loaders:
// strict JSON string decoding, a buffered reader over a CheckpointSource, and
// the FNV-1a hash used for ids and member names.

#include <cstddef>
#include <cstdint>

#include "CheckpointSource.h"

namespace checkpoint::detail {

inline constexpr uint32_t FNV_OFFSET_BASIS = 2166136261u;
inline constexpr uint32_t FNV_PRIME = 16777619u;

inline uint32_t fnvAdd(uint32_t hash, uint8_t byte) { return (hash ^ byte) * FNV_PRIME; }
uint32_t fnvBytes(const char* data, size_t length);

inline bool isSurrogate(uint32_t cp) { return cp >= 0xD800 && cp <= 0xDFFF; }

// Writes `cp` as UTF-8 and returns the byte count (1 to 4). A lone surrogate
// gets the 3-byte form so it still hashes and compares consistently.
uint8_t encodeUtf8(uint32_t cp, uint8_t out[4]);

// Decodes the bytes of one JSON string (after its opening quote) the way
// Python's json module does: strict UTF-8 (no overlong forms, no encoded
// surrogates, nothing above U+10FFFF), no raw control characters, the eight
// short escapes and \uXXXX. A high surrogate escape directly followed by a low
// surrogate escape forms one code point; any other surrogate escape is passed
// through alone and the caller decides what to do with it.
class JsonStringDecoder {
 public:
  enum class Status : uint8_t { More, End, Error };

  void reset();

  // Feeds one byte. Up to two decoded code points are written to `out` and
  // counted in `count`; they come before the End status when both happen.
  Status feed(uint8_t byte, uint32_t out[2], uint8_t& count);

 private:
  enum class State : uint8_t { Normal, Utf8, Escape, Hex, AfterHigh, AfterHighBackslash };

  Status feedNormal(uint8_t byte, uint32_t out[2], uint8_t& count);
  Status feedEscape(uint8_t byte, uint32_t out[2], uint8_t& count);

  State state = State::Normal;
  uint8_t continuationLeft = 0;
  uint8_t continuationLow = 0;
  uint8_t continuationHigh = 0;
  uint8_t hexLeft = 0;
  uint32_t value = 0;
  uint16_t pendingHigh = 0;
};

// Buffered sequential reader over [start, end) of a source.
class SourceReader {
 public:
  SourceReader(uint8_t* storage, uint16_t storageBytes) : buffer(storage), capacity(storageBytes) {}

  void begin(CheckpointSource& source, uint32_t start, uint32_t end);

  // Next byte without consuming it, or -1 at the end or after a read error.
  int peek() {
    if (pos - bufferStart < bufferLength) {
      return buffer[pos - bufferStart];
    }
    return fill() ? buffer[0] : -1;
  }
  int next() {
    const int c = peek();
    if (c >= 0) {
      ++pos;
    }
    return c;
  }
  uint32_t position() const { return pos; }
  bool failed() const { return readFailed; }

 private:
  bool fill();

  CheckpointSource* source = nullptr;
  uint8_t* buffer;
  uint16_t capacity;
  uint16_t bufferLength = 0;
  uint32_t bufferStart = 0;
  uint32_t pos = 0;
  uint32_t end = 0;
  bool readFailed = false;
};

// Decodes one JSON string stored at a known offset, one code point at a time.
// It re-checks everything, so a source that changed since validation yields an
// error instead of bad text.
class StringCursor {
 public:
  static constexpr int32_t END = -1;
  static constexpr int32_t FAILED = -2;

  StringCursor() : reader(buffer, sizeof(buffer)) {}
  StringCursor(const StringCursor&) = delete;
  StringCursor& operator=(const StringCursor&) = delete;

  // `offset` is the opening quote; at most `rawLength` bytes are read.
  void begin(CheckpointSource& source, uint32_t offset, uint32_t rawLength);

  // Next code point, END after the closing quote, or FAILED.
  int32_t next();

 private:
  uint8_t buffer[32];
  SourceReader reader;
  JsonStringDecoder decoder;
  uint32_t pending[2] = {0, 0};
  uint8_t pendingCount = 0;
  uint8_t pendingIndex = 0;
  bool ended = false;
  bool failed = false;
};

// True when the strings at the two offsets decode to the same code points.
bool sameJsonString(CheckpointSource& source, uint32_t offsetA, uint32_t rawLengthA, uint32_t offsetB,
                    uint32_t rawLengthB);

// Decodes the string at `offset` into `out` as NUL-terminated UTF-8. Fails
// (leaving "") on a decode or read error, a lone surrogate, or when the text
// does not fit.
bool loadJsonString(CheckpointSource& source, uint32_t offset, uint32_t rawLength, char* out, size_t outSize);

}  // namespace checkpoint::detail
