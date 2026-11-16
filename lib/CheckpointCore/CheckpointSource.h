#pragma once

#include <cstddef>
#include <cstdint>

namespace checkpoint {

// Random-access bytes of a checkpoint file, implemented by the caller (a memory
// block, a host file, or on the device a cached copy of rsvp/checkpoints.json).
// The parser reads it front to back once, then reads short ranges at stored
// offsets: to confirm a hash match during validation and to load text later.
class CheckpointSource {
 public:
  virtual ~CheckpointSource() = default;

  // Total size in bytes. Sources larger than 4 GiB report UINT32_MAX.
  virtual uint32_t size() const = 0;

  // Copies up to `length` bytes starting at `offset` into `out` and returns the
  // number copied. Returning 0 before the end of the file means a read error.
  virtual size_t read(uint32_t offset, uint8_t* out, size_t length) = 0;
};

// A source over bytes the caller keeps alive for the source's lifetime.
class MemoryCheckpointSource final : public CheckpointSource {
 public:
  MemoryCheckpointSource(const uint8_t* data, size_t size);

  uint32_t size() const override;
  size_t read(uint32_t offset, uint8_t* out, size_t length) override;

 private:
  const uint8_t* data;
  uint32_t length;
};

}  // namespace checkpoint
