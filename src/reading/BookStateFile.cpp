#include "BookStateFile.h"

#if CROSSPOINT_RSVP_ENABLED

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <StateCodec.h>

#include <cstring>

namespace reading {

namespace {

// StateSink into an open file, through a small buffer (encodeState() writes many short pieces).
class FileStateSink final : public checkpoint::StateSink {
 public:
  explicit FileStateSink(HalFile& out) : file(out) {}

  bool write(const char* data, size_t length) override {
    while (length > 0) {
      if (used == sizeof(buffer) && !flush()) return false;
      const size_t room = sizeof(buffer) - used;
      const size_t n = length < room ? length : room;
      std::memcpy(buffer + used, data, n);
      used += n;
      data += n;
      length -= n;
    }
    return true;
  }

  bool flush() {
    if (used == 0) return true;
    const size_t written = file.write(reinterpret_cast<const uint8_t*>(buffer), used);
    const bool ok = written == used;
    used = 0;
    return ok;
  }

 private:
  HalFile& file;
  char buffer[128];
  size_t used = 0;
};

}  // namespace

std::string bookStatePath(const std::string& bookPath) { return bookPath + ".rsvp-state.json"; }

bool bookStateExists(const std::string& bookPath) {
  const std::string path = bookStatePath(bookPath);
  return Storage.exists(path.c_str()) || Storage.exists((path + ".tmp").c_str());
}

namespace {

StateLoad loadStateFile(const std::string& path, const BookIdentity& book, checkpoint::BookState& out) {
  out.reset();
  StateLoad load;
  HalFile file;
  if (!Storage.openFileForRead("RSVP", path, file)) {
    load.source = StateSource::Unreadable;
    LOG_ERR("RSVP", "state %s: cannot open", path.c_str());
    return load;
  }
  const size_t size = file.fileSize();
  if (size > checkpoint::STATE_DECODE_MAX_BYTES) {
    load.source = StateSource::Invalid;
    load.status = checkpoint::DecodeStatus::TooLarge;
    LOG_ERR("RSVP", "state %s: %u bytes, too large", path.c_str(), static_cast<unsigned>(size));
    return load;
  }
  // The file's bytes, freed on return: at most STATE_DECODE_MAX_BYTES, a few hundred for a book
  // without checkpoint data.
  auto bytes = makeUniqueNoThrow<uint8_t[]>(size > 0 ? size : 1);
  if (!bytes) {
    load.source = StateSource::NoMemory;
    LOG_ERR("RSVP", "OOM: state file buffer (%u bytes)", static_cast<unsigned>(size));
    return load;
  }
  size_t got = 0;
  while (got < size) {
    const int n = file.read(bytes.get() + got, size - got);
    if (n <= 0) break;
    got += static_cast<size_t>(n);
  }
  if (got != size) {
    load.source = StateSource::Unreadable;
    LOG_ERR("RSVP", "state %s: read %u of %u bytes", path.c_str(), static_cast<unsigned>(got),
            static_cast<unsigned>(size));
    return load;
  }
  return decodeForUse(bytes.get(), size, book.sha256, book.index, out);
}

}  // namespace

StateLoad loadBookState(const std::string& bookPath, const BookIdentity& book, checkpoint::BookState& out) {
  out.reset();
  const std::string path = bookStatePath(bookPath);
  if (!Storage.exists(path.c_str())) {
    // A cut between the old file's removal and the rename leaves only the new file, complete, as .tmp.
    // A .tmp beside a missing file can also be a first write cut short: only a valid one is used.
    const std::string tmp = path + ".tmp";
    if (!Storage.exists(tmp.c_str())) return StateLoad{};  // Missing
    StateLoad recovered = loadStateFile(tmp, book, out);
    if (recovered.source == StateSource::NoMemory) return recovered;  // the edit then leaves both alone
    if (recovered.source != StateSource::Read) {
      LOG_ERR("RSVP", "state %s missing; %s not usable (%s)", path.c_str(), tmp.c_str(),
              stateSourceName(recovered.source));
      out.reset();
      return StateLoad{};  // Missing: the next write replaces the .tmp
    }
    LOG_INF("RSVP", "state %s recovered from %s", path.c_str(), tmp.c_str());
    return recovered;
  }
  const StateLoad load = loadStateFile(path, book, out);
  if (load.source == StateSource::Invalid) {
    LOG_ERR("RSVP", "state %s: %s, using defaults", path.c_str(), checkpoint::decodeStatusName(load.status));
  } else if (load.shaMismatch) {
    LOG_INF("RSVP", "state %s: written for another edition; position dropped", path.c_str());
  }
  return load;
}

bool editBookState(const std::string& bookPath, const BookIdentity& book, const StateEdit edit, const void* context) {
  // 78 KB for the duration of the call (see the header).
  auto state = makeUniqueNoThrow<checkpoint::BookState>();
  if (!state) {
    LOG_ERR("RSVP", "OOM: book state (%u bytes)", static_cast<unsigned>(sizeof(checkpoint::BookState)));
    return false;
  }
  const std::string path = bookStatePath(bookPath);
  const StateLoad load = loadBookState(bookPath, book, *state);
  if (load.source == StateSource::NoMemory) return false;
  if (load.source == StateSource::Invalid || load.source == StateSource::Unreadable) {
    const std::string bad = path + ".bad";
    Storage.remove(bad.c_str());
    if (Storage.rename(path.c_str(), bad.c_str())) {
      LOG_INF("RSVP", "state %s set aside as %s", path.c_str(), bad.c_str());
    }
  }
  edit(*state, context);
  return writeBookState(bookPath, *state);
}

bool writeBookState(const std::string& bookPath, const checkpoint::BookState& state) {
  const std::string path = bookStatePath(bookPath);
  const std::string tmp = path + ".tmp";
  {
    HalFile file;
    if (!Storage.openFileForWrite("RSVP", tmp, file)) {
      LOG_ERR("RSVP", "state %s: cannot open for write", tmp.c_str());
      return false;
    }
    FileStateSink sink(file);
    if (!checkpoint::encodeState(state, sink) || !sink.flush()) {
      LOG_ERR("RSVP", "state %s: short write", tmp.c_str());
      file.close();
      Storage.remove(tmp.c_str());
      return false;
    }
    file.flush();
    // Closed here: SdFat does not rename a file that is still open.
    if (!file.close()) {
      LOG_ERR("RSVP", "state %s: close failed", tmp.c_str());
      Storage.remove(tmp.c_str());
      return false;
    }
  }
  // SdFat's rename does not replace, so the old file goes first (as ProgressFile::writeAtomic does).
  // A cut between the two leaves only the new file, complete, as .tmp: loadBookState() reads it.
  Storage.remove(path.c_str());
  if (!Storage.rename(tmp.c_str(), path.c_str())) {
    LOG_ERR("RSVP", "state %s: rename failed", path.c_str());
    return false;
  }
  return true;
}

}  // namespace reading

#endif  // CROSSPOINT_RSVP_ENABLED
