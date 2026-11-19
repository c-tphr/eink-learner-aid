#pragma once

// Byte interfaces the caller implements over its storage (SdFat files on the device, memory in
// tests). WordIndex never opens, names or closes a file itself.

#include <cstddef>
#include <cstdint>

namespace wordindex {

// The chapter XHTML, read once from start to end.
class ChapterInput {
 public:
  virtual ~ChapterInput() = default;
  // Reads up to `capacity` bytes into `buffer`. Returns the number read, 0 at the end, or a
  // negative value on a read error.
  virtual int32_t read(uint8_t* buffer, uint32_t capacity) = 0;
};

// Where the builder writes an index: a random-access file opened for reading and writing, empty
// at the start (on the device an FsFile opened O_RDWR | O_CREAT | O_TRUNC on a temporary path,
// renamed by the caller when the build succeeds). The builder appends with write(), and uses
// writeAt() and readAt() only on bytes it has already written: to patch the header last, and to
// read back the scratch slots in which it keeps tables too large for its RAM buffers.
class IndexSink {
 public:
  virtual ~IndexSink() = default;
  // Appends `length` bytes. False on a write error.
  virtual bool write(const uint8_t* data, size_t length) = 0;
  // Overwrites `length` already-written bytes at `offset`.
  virtual bool writeAt(uint32_t offset, const uint8_t* data, size_t length) = 0;
  // Reads `length` already-written bytes at `offset`.
  virtual bool readAt(uint32_t offset, uint8_t* data, size_t length) = 0;
};

// A finished index, read at random offsets.
class IndexFile {
 public:
  virtual ~IndexFile() = default;
  // Size of the file in bytes.
  virtual uint32_t size() = 0;
  // Reads exactly `length` bytes at `offset`. False if they cannot all be read.
  virtual bool readAt(uint32_t offset, uint8_t* data, size_t length) = 0;
};

}  // namespace wordindex
