#include "CheckpointBook.h"

#if CROSSPOINT_RSVP_ENABLED

#include <Arduino.h>
#include <Epub.h>
#include <Epub/parsers/ContainerParser.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <Memory.h>
#include <Print.h>
#include <Txt.h>
#include <ZipFile.h>

#include <cstdio>
#include <cstring>
#include <string_view>

#include "OpfItemFinder.h"

namespace reading {

namespace {

// container.xml and the OPF are streamed in chunks of this size (as lib/Epub reads them).
constexpr size_t XML_CHUNK_BYTES = 1024;

// The OPF pushed into the finder; a short write ends the stream once the manifest has been read.
class FinderFeed final : public Print {
 public:
  explicit FinderFeed(OpfItemFinder& finder) : finder(finder) {}
  size_t write(const uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, const size_t length) override { return finder.feed(data, length) ? length : 0; }

 private:
  OpfItemFinder& finder;
};

// The zip path of the package document, from META-INF/container.xml.
bool findPackage(const Epub& epub, std::string& opfPath) {
  constexpr const char* CONTAINER = "META-INF/container.xml";
  size_t size = 0;
  if (!epub.getItemSize(CONTAINER, &size)) return false;
  ContainerParser parser(size);
  if (!parser.setup() || !epub.readItemContentsToStream(CONTAINER, parser, XML_CHUNK_BYTES)) return false;
  opfPath = parser.fullPath;
  return !opfPath.empty();
}

// The zip entry's CRC-32, which names the cached copy so that a changed book gets a new copy.
bool entryCrc(const Epub& epub, const std::string& zipPath, uint32_t& crc) {
  bool found = false;
  ZipFile zip(epub.getPath());
  zip.enumerateFileEntries([&](const std::string_view path, const uint32_t entryCrc, uint32_t) {
    if (!found && path == zipPath) {
      crc = entryCrc;
      found = true;
    }
  });
  return found;
}

// `<cache>/rsvp/<copy name>`: the same folder as the word indexes.
std::string copyPath(const Epub& epub, const uint32_t crc, const uint32_t bytes) {
  return epub.getCachePath() + "/rsvp/" + checkpointCopyName(crc, bytes);
}

// Copies the item into the cache folder through a temporary file and a rename.
bool extractCopy(const Epub& epub, const std::string& zipPath, const std::string& path) {
  Storage.mkdir((epub.getCachePath() + "/rsvp").c_str());
  const std::string tmp = path + ".tmp";
  Storage.remove(tmp.c_str());
  if (!epub.extractItemToFile(zipPath, tmp)) return false;
  Storage.remove(path.c_str());
  if (!Storage.rename(tmp.c_str(), path.c_str())) {
    Storage.remove(tmp.c_str());
    return false;
  }
  return true;
}

bool openCopy(const std::string& path, HalFile& file, uint32_t& bytes) {
  if (!Storage.exists(path.c_str()) || !Storage.openFileForRead("CPT", path, file)) return false;
  bytes = static_cast<uint32_t>(file.fileSize());
  return true;
}

}  // namespace

size_t CheckpointBook::FileSource::read(const uint32_t offset, uint8_t* out, const size_t length) {
  if (offset >= bytes || !file.seek(offset)) return 0;
  size_t got = 0;
  while (got < length) {
    const int n = file.read(out + got, length - got);
    if (n <= 0) break;
    got += static_cast<size_t>(n);
  }
  return got;
}

CheckpointBook::~CheckpointBook() {
  checkpoints.close();
  if (source.file) source.file.close();
}

std::unique_ptr<CheckpointBook> CheckpointBook::open(const Epub& epub, CheckpointData* result) {
  CheckpointData scratch = CheckpointData::None;
  CheckpointData& outcome = result ? *result : scratch;
  outcome = CheckpointData::None;
  if (Txt::isTxtOrMd(epub.getPath())) return nullptr;
  const unsigned long startMs = millis();

  // 1. The manifest item (docs/PLAN.md section 5: "the firmware treats a book as prepared when that
  // manifest item exists").
  std::string opfPath;
  if (!findPackage(epub, opfPath)) {
    LOG_ERR("CPT", "checkpoint data: cannot read the package document");
    outcome = CheckpointData::ReadFailed;
    return nullptr;
  }
  OpfItemFinder finder(CHECKPOINT_ITEM_ID);
  if (!finder.begin()) {
    LOG_ERR("CPT", "OOM: OPF parser");
    outcome = CheckpointData::NoMemory;
    return nullptr;
  }
  FinderFeed feed(finder);
  epub.readItemContentsToStream(opfPath, feed, XML_CHUNK_BYTES, true);
  finder.finish();
  if (finder.failed()) {
    LOG_ERR("CPT", "checkpoint data: %s is not well-formed XML", opfPath.c_str());
    outcome = CheckpointData::ReadFailed;
    return nullptr;
  }
  if (!finder.found()) {
    LOG_DBG("CPT", "no checkpoint data (no %s item in %s)", CHECKPOINT_ITEM_ID, opfPath.c_str());
    return nullptr;
  }

  // 2. The file it names: size limit first, so an oversized file is never copied.
  const std::string zipPath = resolveOpfHref(opfPath, finder.href());
  size_t inflated = 0;
  if (zipPath.empty() || !epub.getItemSize(zipPath, &inflated)) {
    LOG_ERR("CPT", "checkpoint file missing: manifest item %s names \"%s\", which is not in the book",
            CHECKPOINT_ITEM_ID, finder.href().c_str());
    outcome = CheckpointData::Invalid;
    return nullptr;
  }
  if (finder.mediaType() != "application/json") {
    LOG_INF("CPT", "checkpoint file %s declared as \"%s\", read as JSON", zipPath.c_str(), finder.mediaType().c_str());
  }
  if (!checkpointFileSizeAllowed(inflated)) {
    LOG_ERR("CPT", "checkpoint file invalid: code=limit path=\"\" offset=%u detail=\"%u bytes, more than %u\"",
            static_cast<unsigned>(checkpoint::MAX_FILE_BYTES), static_cast<unsigned>(inflated),
            static_cast<unsigned>(checkpoint::MAX_FILE_BYTES));
    outcome = CheckpointData::TooLarge;
    return nullptr;
  }

  // 3. The cached copy, named by the entry's CRC-32 and size; made on the first open.
  uint32_t crc = 0;
  if (!entryCrc(epub, FsHelpers::normalisePath(zipPath), crc)) crc = 0;  // decrypted entries: size only
  const std::string path = copyPath(epub, crc, static_cast<uint32_t>(inflated));

  // About 72 KB for as long as the book is open (see the header).
  auto book = makeUniqueNoThrow<CheckpointBook>();
  if (!book) {
    LOG_ERR("CPT", "OOM: checkpoint index (%u bytes)", static_cast<unsigned>(sizeof(CheckpointBook)));
    outcome = CheckpointData::NoMemory;
    return nullptr;
  }
  book->opfPath = opfPath;

  bool fresh = false;
  checkpoint::OpenResult opened;
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (book->source.file) book->source.file.close();
    bool have = openCopy(path, book->source.file, book->source.bytes) && book->source.bytes == inflated;
    if (!have) {
      if (book->source.file) book->source.file.close();
      fresh = true;
      have = extractCopy(epub, zipPath, path) && openCopy(path, book->source.file, book->source.bytes);
    }
    if (!have) {
      LOG_ERR("CPT", "checkpoint file %s: cannot copy to %s", zipPath.c_str(), path.c_str());
      outcome = CheckpointData::ReadFailed;
      return nullptr;
    }
    opened = book->checkpoints.open(book->source);
    // A copy made earlier that does not read back as valid may be damaged: make it again, once.
    if (opened.verdict != checkpoint::Verdict::Invalid || fresh) break;
    LOG_INF("CPT", "checkpoint copy %s rejected (%s): copying again", path.c_str(),
            checkpoint::issueCodeName(opened.code));
    book->checkpoints.close();
    book->source.file.close();
    Storage.remove(path.c_str());
  }

  outcome = checkpointDataFor(opened);
  if (outcome == CheckpointData::UnsupportedVersion) {
    LOG_INF("CPT", "checkpoint file %s has a newer version: opened as an ordinary book", zipPath.c_str());
    return nullptr;
  }
  if (outcome != CheckpointData::Valid) {
    LOG_ERR("CPT", "checkpoint file invalid: code=%s path=\"%s\" offset=%lu detail=\"%s\"",
            checkpoint::issueCodeName(opened.code), opened.path, static_cast<unsigned long>(opened.offset),
            opened.detail);
    return nullptr;
  }

  const checkpoint::CheckpointIndex& index = book->checkpoints;
  uint32_t prompts = 0;
  for (uint16_t c = 0; c < index.checkpointCount(); ++c) prompts += index.promptCount(c);
  LOG_DBG("CPT",
          "checkpoint data %s: sections=%u checkpoints=%u prompts=%lu sha=%s bytes=%lu copy=%s book_bytes=%u ms=%lu",
          zipPath.c_str(), index.sectionCount(), index.checkpointCount(), static_cast<unsigned long>(prompts),
          index.sourceSha256(), static_cast<unsigned long>(book->source.bytes), fresh ? "made" : "cached",
          static_cast<unsigned>(sizeof(CheckpointBook)), millis() - startMs);
  return book;
}

bool CheckpointBook::anchor(const uint16_t section, const char* suffix, char* out, const size_t outSize) const {
  const char* id = checkpoints.sectionId(section);
  if (id == nullptr || *id == '\0' || outSize == 0) return false;
  const int n = snprintf(out, outSize, "rsvp-%s-%s", id, suffix);
  if (n < 0 || static_cast<size_t>(n) >= outSize) {
    out[0] = '\0';
    return false;
  }
  return true;
}

bool CheckpointBook::sectionStartAnchor(const uint16_t section, char* out, const size_t outSize) const {
  return anchor(section, "start", out, outSize);
}

bool CheckpointBook::sectionEndAnchor(const uint16_t section, char* out, const size_t outSize) const {
  return anchor(section, "end", out, outSize);
}

int CheckpointBook::sectionSpine(const Epub& epub, const uint16_t section) const {
  const char* href = checkpoints.sectionChapterHref(section);
  if (href == nullptr || *href == '\0') return -1;
  const std::string zipPath = resolveOpfHref(opfPath, href);
  if (zipPath.empty()) return -1;
  for (int i = 0; i < epub.getSpineItemsCount(); ++i) {
    if (FsHelpers::normalisePath(epub.getSpineItem(i).href) == zipPath) return i;
  }
  return -1;
}

}  // namespace reading

#endif  // CROSSPOINT_RSVP_ENABLED
