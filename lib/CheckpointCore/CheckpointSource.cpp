#include "CheckpointSource.h"

#include <cstring>

namespace checkpoint {

MemoryCheckpointSource::MemoryCheckpointSource(const uint8_t* bytes, size_t size)
    : data(bytes), length(size > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(size)) {}

uint32_t MemoryCheckpointSource::size() const { return length; }

size_t MemoryCheckpointSource::read(uint32_t offset, uint8_t* out, size_t count) {
  if (offset >= length || out == nullptr) {
    return 0;
  }
  const size_t available = length - offset;
  const size_t n = count < available ? count : available;
  memcpy(out, data + offset, n);
  return n;
}

}  // namespace checkpoint
