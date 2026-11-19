#pragma once

// An SD card in memory for BookStateFileTest, with a power cut: after `opsLeft` more changes to the
// card (open for write, each write call, remove, rename), every further change is lost, as when the
// battery is pulled. Reads still work, so a test can "reboot" by clearing the cut and reading back.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

namespace fakesd {

inline std::map<std::string, std::string> files;
inline int opsLeft = -1;  // changes allowed before the cut; -1: no cut
inline int ops = 0;       // changes asked for since reset()
inline bool cut = false;

inline void reset() {
  files.clear();
  opsLeft = -1;
  ops = 0;
  cut = false;
}

// One change to the card: false once the power is cut.
inline bool change() {
  ++ops;
  if (cut) return false;
  if (opsLeft == 0) {
    cut = true;
    return false;
  }
  if (opsLeft > 0) --opsLeft;
  return true;
}

}  // namespace fakesd

class HalFile {
 public:
  std::string path;
  size_t pos = 0;
  bool opened = false;

  bool isOpen() const { return opened; }
  explicit operator bool() const { return opened; }
  size_t fileSize() const {
    const auto f = fakesd::files.find(path);
    return f == fakesd::files.end() ? 0 : f->second.size();
  }
  int read(void* out, const size_t count) {
    const auto f = fakesd::files.find(path);
    if (!opened || f == fakesd::files.end()) return -1;
    const size_t n = pos >= f->second.size() ? 0 : std::min(count, f->second.size() - pos);
    std::memcpy(out, f->second.data() + pos, n);
    pos += n;
    return static_cast<int>(n);
  }
  size_t write(const uint8_t* data, const size_t count) {
    if (!opened || !fakesd::change()) return 0;
    std::string& bytes = fakesd::files[path];
    if (bytes.size() < pos + count) bytes.resize(pos + count);
    std::memcpy(&bytes[pos], data, count);
    pos += count;
    return count;
  }
  void flush() {}
  bool close() {
    opened = false;
    return true;
  }
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }
  bool exists(const char* path) const { return fakesd::files.count(path) != 0; }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    file = HalFile{};
    if (!exists(path.c_str())) return false;
    file.path = path;
    file.opened = true;
    return true;
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    file = HalFile{};
    if (!fakesd::change()) return false;
    fakesd::files[path].clear();  // created or truncated
    file.path = path;
    file.opened = true;
    return true;
  }
  bool remove(const char* path) {
    if (!exists(path) || !fakesd::change()) return false;
    fakesd::files.erase(path);
    return true;
  }
  // As SdFat: does not replace an existing file.
  bool rename(const char* from, const char* to) {
    if (!exists(from) || exists(to) || !fakesd::change()) return false;
    fakesd::files[to] = fakesd::files[from];
    fakesd::files.erase(from);
    return true;
  }
};

#define Storage HalStorage::getInstance()
