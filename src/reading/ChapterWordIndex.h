#pragma once

// A chapter's word index on demand (lib/WordIndex/README.md, "For the integration tasks"): opens
// /.crosspoint/epub_<hash>/rsvp/<spine>.wix when it is valid for the chapter's current zip entry, and
// otherwise builds it by streaming the chapter out of the EPUB through WordIndexBuilder, then opens it.
// RSVP uses it per chapter; the checkpoint tasks read anchors from the same reader.
//
// Working memory of a build (all freed when open() returns): the builder (4.1 KB, heap), expat's
// parser (measured peak 8.7-9.7 KB, heap), the EPUB's inflate stream (32 KB ring plus two 1 KB
// buffers, ZipFile::readFileToStream) and about 1 KB of stack. While open: the reader (1.9 KB, a
// member) and one open file handle. Call from one task at a time (RSVP: the render task).

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <HalStorage.h>
#include <WordIndexReader.h>

#include <cstdint>
#include <string>

#include "WordIndexCheck.h"

class Epub;

namespace reading {

class ChapterWordIndex {
 public:
  // Called once before a build that is likely to take noticeable time, so the caller can show
  // "Preparing...". Runs on the calling task.
  using BuildNotice = void (*)(void* context);

  // Source bytes from which a build shows the notice: about a tenth of a second of streaming.
  static constexpr uint32_t NOTICE_SOURCE_BYTES = 32 * 1024;

  ChapterWordIndex() = default;
  ChapterWordIndex(const ChapterWordIndex&) = delete;
  ChapterWordIndex& operator=(const ChapterWordIndex&) = delete;
  ~ChapterWordIndex() { close(); }

  // Makes the index of `spineIndex` usable, building it if needed. False (logged) if it cannot be
  // built or opened; the reader is then closed.
  bool open(const Epub& epub, int spineIndex, BuildNotice notice = nullptr, void* context = nullptr);

  // After a read error during use (status() Corrupt or IoError): deletes the file and builds it again.
  bool rebuild(const Epub& epub, BuildNotice notice = nullptr, void* context = nullptr);

  void close();
  bool isOpen() const { return opened; }
  int spineIndex() const { return spine; }
  wordindex::WordIndexReader& reader() { return wordReader; }

 private:
  // wordindex::IndexFile over a HalFile opened for reading.
  class FileAdapter final : public wordindex::IndexFile {
   public:
    HalFile file;
    uint32_t size() override;
    bool readAt(uint32_t offset, uint8_t* data, size_t length) override;
  };

  bool tryOpen(const std::string& path, uint32_t sourceBytes, uint32_t sourceCrc, bool sourceKnown,
               IndexState& state);
  bool build(const Epub& epub, const std::string& dir, const std::string& path, uint32_t sourceBytes,
             BuildNotice notice, void* context);

  FileAdapter file;
  wordindex::WordIndexReader wordReader;
  bool opened = false;
  int spine = -1;
};

}  // namespace reading

#endif  // CROSSPOINT_RSVP_ENABLED
