#pragma once

// Writing a device-test file on the SD card (tasks D3a and D3b): through a temporary file and a
// rename, as src/reading/BookStateFile.cpp does, so a cut-off write never replaces the last good file.
// Used by DeviceTestActivity.cpp and DeviceTestAcceptance.cpp only.

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "BenchJson.h"

namespace devicetest {

// A JsonSink over an open file, through a small buffer.
class FileSink final : public JsonSink {
 public:
  explicit FileSink(HalFile& target) : file(target) {}
  void write(const char* data, size_t length) override {
    while (length > 0) {
      const size_t n = length < sizeof(buffer) - used ? length : sizeof(buffer) - used;
      memcpy(buffer + used, data, n);
      used += n;
      data += n;
      length -= n;
      if (used == sizeof(buffer)) flush();
    }
  }
  bool finish() {
    flush();
    return ok;
  }
  size_t bytes() const { return total; }

 private:
  void flush() {
    if (used == 0) return;
    if (file.write(buffer, used) != used) ok = false;
    total += used;
    used = 0;
  }
  HalFile& file;
  char buffer[256] = {};
  size_t used = 0;
  size_t total = 0;
  bool ok = true;
};

// Writes `path` through `<path>.tmp` and a rename. `encode(JsonSink&)` writes the content.
template <typename Encode>
bool writeAtomically(const char* path, Encode encode, size_t& bytes) {
  char tmp[64];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  {
    HalFile file;
    if (!Storage.openFileForWrite("DTS", tmp, file)) {
      LOG_ERR("DTS", "results %s: cannot open for write", tmp);
      return false;
    }
    FileSink sink(file);
    encode(sink);
    const bool ok = sink.finish();
    bytes = sink.bytes();
    file.close();  // SdFat does not rename a file that is still open
    if (!ok) {
      LOG_ERR("DTS", "results %s: short write", tmp);
      Storage.remove(tmp);
      return false;
    }
  }
  // SdFat's rename does not replace, so the old file goes first.
  Storage.remove(path);
  if (!Storage.rename(tmp, path)) {
    LOG_ERR("DTS", "results %s: rename failed", path);
    return false;
  }
  return true;
}

}  // namespace devicetest

#endif  // CROSSPOINT_RSVP_ENABLED
