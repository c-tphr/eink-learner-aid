#include "ChapterWordIndex.h"

#if CROSSPOINT_RSVP_ENABLED

#include <Arduino.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <Memory.h>
#include <Print.h>
#include <WordIndexBuilder.h>
#include <ZipFile.h>

#include <string_view>

namespace reading {

namespace {

// Chapter bytes handed to the builder per write; as WordIndexBuilder's own input chunk.
constexpr size_t STREAM_CHUNK_BYTES = wordindex::INPUT_CHUNK_BYTES;

// wordindex::IndexSink over a HalFile opened O_RDWR | O_CREAT | O_TRUNC.
class FileSink final : public wordindex::IndexSink {
 public:
  explicit FileSink(HalFile& file) : file(file) {}
  bool write(const uint8_t* data, const size_t length) override {
    if (!file.seek(end) || file.write(data, length) != length) return false;
    end += length;
    return true;
  }
  bool writeAt(const uint32_t offset, const uint8_t* data, const size_t length) override {
    return offset + length <= end && file.seek(offset) && file.write(data, length) == length;
  }
  bool readAt(const uint32_t offset, uint8_t* data, const size_t length) override {
    return offset + length <= end && file.seek(offset) && file.read(data, length) == static_cast<int>(length);
  }

 private:
  HalFile& file;
  size_t end = 0;
};

// The EPUB's chapter stream (Epub::readItemContentsToStream) pushed into the builder.
class BuilderFeed final : public Print {
 public:
  explicit BuilderFeed(wordindex::WordIndexBuilder& builder) : builder(builder) {}
  size_t write(const uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, const size_t length) override {
    // A short write stops the stream: the build already failed.
    return builder.feed(data, length) ? length : 0;
  }

 private:
  wordindex::WordIndexBuilder& builder;
};

std::string indexDir(const Epub& epub) { return epub.getCachePath() + "/rsvp"; }

std::string indexPath(const std::string& dir, const int spineIndex) {
  return dir + "/" + std::to_string(spineIndex) + ".wix";
}

// The chapter's zip entry size and CRC-32, which a valid index records. False when the book is not a
// zip (TXT) or the entry is missing.
bool sourceIdentity(const Epub& epub, const std::string& href, uint32_t& bytes, uint32_t& crc) {
  size_t size = 0;
  if (!epub.getItemSize(href, &size)) return false;
  const std::string entry = FsHelpers::normalisePath(href);
  bool found = false;
  ZipFile zip(epub.getPath());
  zip.enumerateFileEntries([&](const std::string_view path, const uint32_t entryCrc, uint32_t) {
    if (!found && path == entry) {
      crc = entryCrc;
      found = true;
    }
  });
  bytes = static_cast<uint32_t>(size);
  return found;
}

}  // namespace

uint32_t ChapterWordIndex::FileAdapter::size() { return static_cast<uint32_t>(file.size()); }

bool ChapterWordIndex::FileAdapter::readAt(const uint32_t offset, uint8_t* data, const size_t length) {
  return file.seek(offset) && file.read(data, length) == static_cast<int>(length);
}

void ChapterWordIndex::close() {
  wordReader.close();
  if (file.file) file.file.close();
  opened = false;
}

bool ChapterWordIndex::tryOpen(const std::string& path, const uint32_t sourceBytes, const uint32_t sourceCrc,
                               const bool sourceKnown, IndexState& state) {
  close();
  const bool exists = Storage.exists(path.c_str());
  wordindex::ReadStatus status = wordindex::ReadStatus::NotOpen;
  if (exists && Storage.openFileForRead("RSVP", path, file.file)) status = wordReader.open(file);
  state = checkIndex(exists, status, sourceKnown, sourceKnown && wordReader.matchesSource(sourceBytes, sourceCrc));
  if (state == IndexState::Valid) {
    opened = true;
    return true;
  }
  if (exists) {
    LOG_INF("RSVP", "Index %s is %s (%s): rebuilding", path.c_str(), indexStateName(state),
            wordindex::readStatusName(status));
  }
  close();
  return false;
}

bool ChapterWordIndex::build(const Epub& epub, const std::string& dir, const std::string& path,
                             const uint32_t sourceBytes, const BuildNotice notice, void* context) {
  if (notice && sourceBytes >= NOTICE_SOURCE_BYTES) notice(context);
  const unsigned long startMs = millis();
  const std::string href = epub.getSpineItem(spine).href;
  const std::string tmpPath = path + ".tmp";
  Storage.mkdir(dir.c_str());
  if (Storage.exists(path.c_str())) Storage.remove(path.c_str());

  // 4.1 KB of builder state for the length of one build; heap rather than the render task's stack.
  auto builder = makeUniqueNoThrow<wordindex::WordIndexBuilder>();
  if (!builder) {
    LOG_ERR("RSVP", "Index build: allocation of %u bytes failed",
            static_cast<unsigned>(sizeof(wordindex::WordIndexBuilder)));
    return false;
  }
  HalFile out;
  if (!Storage.openFileForWrite("RSVP", tmpPath, out)) return false;
  FileSink sink(out);
  BuilderFeed feed(*builder);
  bool streamed = builder->begin(sink) == wordindex::BuildStatus::Ok;
  if (streamed) streamed = epub.readItemContentsToStream(href, feed, STREAM_CHUNK_BYTES);
  const wordindex::BuildStatus status = builder->end();
  const wordindex::BuildReport& report = builder->report();
  out.flush();
  out.close();

  if (!streamed || status != wordindex::BuildStatus::Ok) {
    LOG_ERR("RSVP", "Index build failed for spine %d (%s): %s, xml error %lu at line %lu", spine, href.c_str(),
            wordindex::buildStatusName(status), static_cast<unsigned long>(report.xmlError),
            static_cast<unsigned long>(report.line));
    Storage.remove(tmpPath.c_str());
    return false;
  }
  if (!Storage.rename(tmpPath.c_str(), path.c_str())) {
    LOG_ERR("RSVP", "Index build: rename %s failed", tmpPath.c_str());
    Storage.remove(tmpPath.c_str());
    return false;
  }
  LOG_DBG("RSVP", "Index built spine=%d words=%lu bytes=%lu source=%lu ms=%lu", spine,
          static_cast<unsigned long>(report.header.wordCount), static_cast<unsigned long>(report.header.fileBytes),
          static_cast<unsigned long>(report.header.sourceBytes), millis() - startMs);
  return true;
}

bool ChapterWordIndex::open(const Epub& epub, const int spineIndex, const BuildNotice notice, void* context) {
  close();
  spine = spineIndex;
  if (spineIndex < 0 || spineIndex >= epub.getSpineItemsCount()) return false;
  const std::string href = epub.getSpineItem(spineIndex).href;
  uint32_t sourceBytes = 0;
  uint32_t sourceCrc = 0;
  const bool sourceKnown = sourceIdentity(epub, href, sourceBytes, sourceCrc);
  const std::string dir = indexDir(epub);
  const std::string path = indexPath(dir, spineIndex);

  IndexState state = IndexState::Missing;
  if (tryOpen(path, sourceBytes, sourceCrc, sourceKnown, state)) {
    LOG_DBG("RSVP", "Index open spine=%d words=%lu (valid)", spine, static_cast<unsigned long>(wordReader.wordCount()));
    return true;
  }
  if (!build(epub, dir, path, sourceBytes, notice, context)) return false;
  // A fresh build of an unverifiable source is used as built.
  if (!tryOpen(path, sourceBytes, sourceCrc, sourceKnown, state) && state != IndexState::Unverifiable) {
    LOG_ERR("RSVP", "Index %s unusable after a build (%s)", path.c_str(), indexStateName(state));
    return false;
  }
  if (state == IndexState::Unverifiable) {
    if (!Storage.openFileForRead("RSVP", path, file.file) || wordReader.open(file) != wordindex::ReadStatus::Ok) {
      LOG_ERR("RSVP", "Index %s unreadable after a build", path.c_str());
      close();
      return false;
    }
    opened = true;
  }
  LOG_DBG("RSVP", "Index open spine=%d words=%lu (%s, rebuilt)", spine,
          static_cast<unsigned long>(wordReader.wordCount()), indexStateName(state));
  return true;
}

bool ChapterWordIndex::rebuild(const Epub& epub, const BuildNotice notice, void* context) {
  const int spineIndex = spine;
  close();
  const std::string path = indexPath(indexDir(epub), spineIndex);
  Storage.remove(path.c_str());
  LOG_ERR("RSVP", "Index of spine %d failed during use (%s): rebuilding", spineIndex, path.c_str());
  return open(epub, spineIndex, notice, context);
}

}  // namespace reading

#endif  // CROSSPOINT_RSVP_ENABLED
