#pragma once

// The pure decisions of the checkpoint data loader (CheckpointBook.h): what a parse result means for
// the book, the size limit checked before the file is copied, and the name of the cached copy.
// Host-tested in test/checkpoint_activity, on the shared samples of fixtures/checkpoints.

#include <CheckpointIndex.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace reading {

// What CheckpointBook::open() found.
enum class CheckpointData : uint8_t {
  None,                // no `rsvp-checkpoints` item: an ordinary book
  Valid,               // opened
  UnsupportedVersion,  // a newer file version: an ordinary book, one log line
  Invalid,             // rejected by the parser (logged with its issue code), or the item names no file
  TooLarge,            // larger than checkpoint::MAX_FILE_BYTES (issue code "limit"), not copied
  ReadFailed,          // the EPUB or the cached copy could not be read or written (issue code "io")
  NoMemory,            // no room for the index
};

const char* checkpointDataName(CheckpointData data);

// What the parser's verdict means for the book: Valid, UnsupportedVersion, ReadFailed for code `io`,
// Invalid for every other rejection. Only Valid opens the checkpoint screens.
CheckpointData checkpointDataFor(const checkpoint::OpenResult& result);

// A file of `bytes` is copied and parsed only within checkpoint::MAX_FILE_BYTES.
bool checkpointFileSizeAllowed(size_t bytes);

// The cached copy in the book's cache folder: "checkpoints-<crc-32, 8 hex digits>-<bytes>.json", so
// that a changed book (another zip entry) never reads an old copy.
std::string checkpointCopyName(uint32_t crc, uint32_t bytes);

}  // namespace reading
