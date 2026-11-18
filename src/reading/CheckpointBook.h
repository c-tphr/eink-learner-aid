#pragma once

// A prepared book's checkpoint data on the device (docs/PLAN.md section 5; lib/CheckpointCore/README.md,
// "The source on the device"). open() finds the package manifest item with id `rsvp-checkpoints`,
// copies the file it names (`rsvp/checkpoints.json` next to the OPF) out of the EPUB into the book's
// cache folder once, and opens lib/CheckpointCore's index over that copy. Prompt and answer text is
// read from the copy on demand through the index's load*() calls.
//
// A book without the item is an ordinary book. A file that is invalid, too large, of a newer version
// or unreadable is logged (with the library's issue code) and the book is also treated as ordinary:
// open() returns nullptr and reading goes on.
//
// Memory: one heap object of about 72 KB while the book is open (the fixed-size CheckpointIndex, which
// does not depend on the file; on the Picco a block this size comes from PSRAM), plus one open file
// handle. open() streams with bounded RAM: expat for container.xml and the OPF (freed on return) and,
// on the first open only, the EPUB's inflate stream for the copy (Epub::extractItemToFile: a 32 KB
// ring and two 8 KB buffers, freed on return). No buffer holds the whole file.
//
// Thread safety: none, as for CheckpointIndex. Use from one task at a time (the activities take the
// RenderLock around every call).

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <CheckpointIndex.h>
#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "CheckpointBookRules.h"

class Epub;

namespace reading {

// The manifest item id that makes a book a prepared book (docs/PLAN.md section 5).
inline constexpr const char* CHECKPOINT_ITEM_ID = "rsvp-checkpoints";

class CheckpointBook {
 public:
  // Finds and opens the checkpoint data of `epub` (loaded). nullptr unless the result is Valid;
  // `result`, when given, says why. Every outcome but None logs one line.
  static std::unique_ptr<CheckpointBook> open(const Epub& epub, CheckpointData* result = nullptr);

  CheckpointBook() = default;
  CheckpointBook(const CheckpointBook&) = delete;
  CheckpointBook& operator=(const CheckpointBook&) = delete;
  ~CheckpointBook();

  // The open index: sections, checkpoints, prompts (lib/CheckpointCore/README.md, "API").
  const checkpoint::CheckpointIndex& index() const { return checkpoints; }

  // `book.source_sha256`: the state file's `book_sha256` for this book.
  const char* sourceSha256() const { return checkpoints.sourceSha256(); }

  // Lookups for the trigger code (task C2). -1 when nothing matches.
  int findCheckpoint(const char* id) const { return checkpoints.findCheckpoint(id); }
  int sectionCheckpoint(const uint16_t section) const { return checkpoints.sectionCheckpoint(section); }
  // The section's anchor ids, "rsvp-<id>-start" and "rsvp-<id>-end" (the parser has checked that the
  // file names them so). False when `section` is out of range or `out` is too small.
  bool sectionStartAnchor(uint16_t section, char* out, size_t outSize) const;
  bool sectionEndAnchor(uint16_t section, char* out, size_t outSize) const;
  // The reader chapter (spine index) holding a section: its chapter_href resolved against the OPF's
  // folder and looked up in the spine. -1 when it is not in the spine.
  int sectionSpine(const Epub& epub, uint16_t section) const;

  // The zip path of the package document (OPF).
  const std::string& packagePath() const { return opfPath; }
  // Bytes of the checkpoint file.
  uint32_t fileBytes() const { return source.size(); }

 private:
  // Random reads of the cached copy (CheckpointSource). A short read is a read error (code `io`).
  class FileSource final : public checkpoint::CheckpointSource {
   public:
    HalFile file;
    uint32_t bytes = 0;
    uint32_t size() const override { return bytes; }
    size_t read(uint32_t offset, uint8_t* out, size_t length) override;
  };

  bool anchor(uint16_t section, const char* suffix, char* out, size_t outSize) const;

  FileSource source;
  checkpoint::CheckpointIndex checkpoints;
  std::string opfPath;
};

}  // namespace reading

#endif  // CROSSPOINT_RSVP_ENABLED
