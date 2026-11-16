#pragma once

// In-memory byte interfaces and helpers for the WordIndex host tests.

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "lib/WordIndex/WordIndexBuilder.h"
#include "lib/WordIndex/WordIndexReader.h"

namespace wixtest {

inline std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// Chapter bytes handed out in chunks of at most `chunk` bytes; optionally fails after `failAfter` bytes.
class MemoryInput : public wordindex::ChapterInput {
 public:
  explicit MemoryInput(std::string data, uint32_t chunk = UINT32_MAX) : data(std::move(data)), chunk(chunk) {}
  int32_t read(uint8_t* buffer, uint32_t capacity) override {
    if (position >= failAfter) return -1;
    size_t n = data.size() - position;
    if (n > capacity) n = capacity;
    if (n > chunk) n = chunk;
    std::memcpy(buffer, data.data() + position, n);
    position += n;
    return static_cast<int32_t>(n);
  }
  std::string data;
  uint32_t chunk;
  size_t position = 0;
  size_t failAfter = SIZE_MAX;
};

// A growable file: the builder's sink and the reader's file.
class MemoryFile : public wordindex::IndexSink, public wordindex::IndexFile {
 public:
  bool write(const uint8_t* data, size_t length) override {
    if (bytes.size() + length > failWritesAfter) return false;
    bytes.insert(bytes.end(), data, data + length);
    return true;
  }
  bool writeAt(uint32_t offset, const uint8_t* data, size_t length) override {
    if (static_cast<uint64_t>(offset) + length > bytes.size()) return false;
    std::memcpy(bytes.data() + offset, data, length);
    return true;
  }
  bool readAt(uint32_t offset, uint8_t* data, size_t length) override {
    ++reads;
    readBytes += length;
    if (static_cast<uint64_t>(offset) + length > bytes.size()) return false;
    std::memcpy(data, bytes.data() + offset, length);
    return true;
  }
  uint32_t size() override { return static_cast<uint32_t>(bytes.size()); }

  std::vector<uint8_t> bytes;
  size_t failWritesAfter = SIZE_MAX;
  size_t reads = 0;
  size_t readBytes = 0;
};

struct Built {
  wordindex::BuildStatus status = wordindex::BuildStatus::Ok;
  wordindex::BuildReport report;
  MemoryFile file;
};

inline Built buildIndex(const std::string& xhtml, uint32_t chunk = UINT32_MAX) {
  Built built;
  MemoryInput input(xhtml, chunk);
  wordindex::WordIndexBuilder builder;
  built.status = builder.build(input, built.file);
  built.report = builder.report();
  return built;
}

struct Word {
  std::string text;
  uint32_t start = 0;
  uint32_t end = 0;
  uint8_t flags = 0;
  bool operator==(const Word& o) const {
    return text == o.text && start == o.start && end == o.end && flags == o.flags;
  }
};

struct Contents {
  std::vector<Word> words;
  std::vector<wordindex::Marker> markers;
  std::vector<wordindex::Anchor> anchors;
};

// Everything in an index, read sequentially. Fails the test on a read error.
Contents readAll(MemoryFile& file);

// A small XHTML document around `body` (HTML 5 doctype, as tools/prep/tests/test_words.py).
inline std::string xhtml(const std::string& body, bool xhtml11Doctype = false) {
  const std::string doctype = xhtml11Doctype ? "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" "
                                               "\"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\">\n"
                                             : "<!DOCTYPE html>\n";
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" + doctype +
         "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\">"
         "<head><title>T</title></head><body>" +
         body + "</body></html>";
}

const char* markerKindName(uint8_t kind);

}  // namespace wixtest
