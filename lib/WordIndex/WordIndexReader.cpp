#include "WordIndexReader.h"

#include <cstring>

namespace wordindex {

const char* readStatusName(const ReadStatus status) {
  switch (status) {
    case ReadStatus::Ok:
      return "Ok";
    case ReadStatus::NotOpen:
      return "NotOpen";
    case ReadStatus::IoError:
      return "IoError";
    case ReadStatus::Truncated:
      return "Truncated";
    case ReadStatus::BadMagic:
      return "BadMagic";
    case ReadStatus::OtherFormat:
      return "OtherFormat";
    case ReadStatus::OtherRules:
      return "OtherRules";
    case ReadStatus::Corrupt:
      return "Corrupt";
  }
  return "?";
}

bool WordIndexReader::fail(const ReadStatus status) {
  if (state == ReadStatus::Ok) state = status;
  return false;
}

bool WordIndexReader::readFile(const uint32_t offset, uint8_t* data, const size_t length) {
  if (state != ReadStatus::Ok) return false;
  if (static_cast<uint64_t>(offset) + length > head.fileBytes) return fail(ReadStatus::Corrupt);
  if (!file->readAt(offset, data, length)) return fail(ReadStatus::IoError);
  return true;
}

void WordIndexReader::close() {
  file = nullptr;
  state = ReadStatus::NotOpen;
  head = Header{};
  pageNumber = UINT32_MAX;
  pageEntries = 0;
  for (CachedBlock& block : blocks) block.valid = false;
}

ReadStatus WordIndexReader::open(IndexFile& source) {
  close();
  file = &source;
  const uint32_t size = file->size();
  if (size < SLOT_BYTES) return state = ReadStatus::Truncated;
  uint8_t raw[HEADER_BYTES];
  if (!file->readAt(0, raw, sizeof(raw))) return state = ReadStatus::IoError;
  switch (decodeHeader(raw, head)) {
    case HeaderCheck::Ok:
      break;
    case HeaderCheck::BadMagic:
      return state = ReadStatus::BadMagic;
    case HeaderCheck::OtherFormat:
      return state = ReadStatus::OtherFormat;
    case HeaderCheck::BadCrc:
    case HeaderCheck::BadLayout:
      return state = ReadStatus::Corrupt;
  }
  if (head.rulesVersion != RULES_VERSION) return state = ReadStatus::OtherRules;
  if (size < head.fileBytes) return state = ReadStatus::Truncated;
  if (size != head.fileBytes) return state = ReadStatus::Corrupt;

  // The layout is fully determined by the counts.
  const uint64_t directoryOffset = (1 + static_cast<uint64_t>(head.slotCount)) * SLOT_BYTES;
  const uint64_t anchorOffset = directoryOffset + static_cast<uint64_t>(head.blockCount) * DIRECTORY_ENTRY_BYTES;
  const uint64_t markerOffset = anchorOffset + head.anchorBytes;
  const uint64_t fileBytes = markerOffset + static_cast<uint64_t>(head.markerCount) * MARKER_RECORD_BYTES;
  const bool layoutOk = head.directoryOffset == directoryOffset && head.anchorOffset == anchorOffset &&
                        head.markerOffset == markerOffset && head.fileBytes == fileBytes &&
                        head.blockCount <= head.slotCount && (head.wordCount == 0) == (head.blockCount == 0) &&
                        head.blockCount <= head.wordCount &&
                        head.anchorBytes >= static_cast<uint64_t>(head.anchorCount) * ANCHOR_RECORD_FIXED_BYTES;
  if (!layoutOk) return state = ReadStatus::Corrupt;

  state = ReadStatus::Ok;
  const ReadStatus tables = checkTables();
  if (tables != ReadStatus::Ok) {
    state = tables;
    return state;
  }
  return state;
}

void WordIndexReader::decodeEntry(const uint8_t* p, DirectoryEntry& out) {
  out.firstWord = readU32(p);
  out.baseOffset = readU32(p + 4);
  out.slot = readU32(p + 8);
}

// Checks the tables' checksum, then their structure, so later lookups can trust them.
ReadStatus WordIndexReader::checkTables() {
  uint8_t* scratch = blocks[0].data;  // nothing is cached yet
  uint32_t crc = 0;
  for (uint32_t at = head.directoryOffset; at < head.fileBytes;) {
    const uint32_t length = head.fileBytes - at < SLOT_BYTES ? head.fileBytes - at : SLOT_BYTES;
    if (!readFile(at, scratch, length)) return state;
    crc = crc32Update(crc, scratch, length);
    at += length;
  }
  if (crc != head.tablesCrc32) return ReadStatus::Corrupt;

  // Directory: first words strictly increasing from 0, base offsets not decreasing, slots
  // increasing and before the tables.
  DirectoryEntry previous{};
  constexpr uint32_t PER_READ = SLOT_BYTES / DIRECTORY_ENTRY_BYTES;
  for (uint32_t i = 0; i < head.blockCount; i += PER_READ) {
    const uint32_t count = head.blockCount - i < PER_READ ? head.blockCount - i : PER_READ;
    if (!readFile(head.directoryOffset + i * DIRECTORY_ENTRY_BYTES, scratch, count * DIRECTORY_ENTRY_BYTES)) {
      return state;
    }
    for (uint32_t k = 0; k < count; ++k) {
      DirectoryEntry e;
      decodeEntry(scratch + k * DIRECTORY_ENTRY_BYTES, e);
      const bool first = i + k == 0;
      const bool ok = (first ? e.firstWord == 0 : e.firstWord > previous.firstWord) && e.firstWord < head.wordCount &&
                      (first || e.baseOffset >= previous.baseOffset) && e.baseOffset < head.visibleCodepoints &&
                      e.slot >= 1 && e.slot <= head.slotCount && (first || e.slot > previous.slot);
      if (!ok) return ReadStatus::Corrupt;
      previous = e;
    }
  }

  uint32_t offset = 0;
  for (uint32_t i = 0; i < head.anchorCount; ++i) {
    Anchor anchor;
    uint32_t bytes = 0;
    if (!readAnchorAt(offset, anchor, bytes)) return state == ReadStatus::Ok ? ReadStatus::Corrupt : state;
    offset += bytes;
  }
  if (offset != head.anchorBytes) return ReadStatus::Corrupt;

  uint32_t lastStart = 0;
  for (uint32_t i = 0; i < head.markerCount; ++i) {
    Marker m;
    if (!readMarker(i, m)) return state == ReadStatus::Ok ? ReadStatus::Corrupt : state;
    if (m.wordStart < lastStart) return ReadStatus::Corrupt;
    lastStart = m.wordStart;
  }
  return ReadStatus::Ok;
}

// ---------------------------------------------------------------------------------------------
// Directory and blocks

bool WordIndexReader::loadPage(const uint32_t number) {
  if (number == pageNumber) return true;
  const uint32_t first = number * DIRECTORY_PAGE_ENTRIES;
  if (first >= head.blockCount) return fail(ReadStatus::Corrupt);
  const uint32_t remaining = head.blockCount - first;
  pageEntries = remaining < DIRECTORY_PAGE_ENTRIES ? remaining : DIRECTORY_PAGE_ENTRIES;
  const uint32_t withNext = remaining > DIRECTORY_PAGE_ENTRIES ? pageEntries + 1 : pageEntries;
  uint8_t raw[(DIRECTORY_PAGE_ENTRIES + 1) * DIRECTORY_ENTRY_BYTES];
  pageNumber = UINT32_MAX;
  if (!readFile(head.directoryOffset + first * DIRECTORY_ENTRY_BYTES, raw, withNext * DIRECTORY_ENTRY_BYTES)) {
    return false;
  }
  for (uint32_t i = 0; i < withNext; ++i) decodeEntry(raw + i * DIRECTORY_ENTRY_BYTES, page[i]);
  pageNumber = number;
  return true;
}

bool WordIndexReader::entry(const uint32_t blockIndex, DirectoryEntry& out) {
  if (blockIndex >= head.blockCount) return fail(ReadStatus::Corrupt);
  if (!loadPage(blockIndex / DIRECTORY_PAGE_ENTRIES)) return false;
  out = page[blockIndex % DIRECTORY_PAGE_ENTRIES];
  return true;
}

bool WordIndexReader::findBlock(const bool byOffset, const uint32_t key, uint32_t& blockIndex, bool& found) {
  found = false;
  if (state != ReadStatus::Ok) return false;
  if (head.blockCount == 0) return true;
  const auto keyOf = [byOffset](const DirectoryEntry& e) { return byOffset ? e.baseOffset : e.firstWord; };
  const uint32_t pages = (head.blockCount + DIRECTORY_PAGE_ENTRIES - 1) / DIRECTORY_PAGE_ENTRIES;

  // The page holding the answer: the cached one when the key falls inside it, else a binary
  // search over the first entries of the pages.
  uint32_t number = pageNumber;
  const bool cachedFits =
      number < pages && keyOf(page[0]) <= key &&
      (pageEntries < DIRECTORY_PAGE_ENTRIES || number + 1 == pages || key < keyOf(page[DIRECTORY_PAGE_ENTRIES]));
  if (!cachedFits) {
    uint32_t lo = 0;
    uint32_t hi = pages;  // answer: last page whose first key <= key, in [lo, hi)
    while (hi - lo > 1) {
      const uint32_t mid = lo + (hi - lo) / 2;
      uint8_t raw[DIRECTORY_ENTRY_BYTES];
      if (!readFile(head.directoryOffset + mid * DIRECTORY_PAGE_ENTRIES * DIRECTORY_ENTRY_BYTES, raw, sizeof(raw))) {
        return false;
      }
      DirectoryEntry e;
      decodeEntry(raw, e);
      if (keyOf(e) <= key) {
        lo = mid;
      } else {
        hi = mid;
      }
    }
    number = lo;
    if (!loadPage(number)) return false;
  }
  if (keyOf(page[0]) > key) return true;  // before the first block
  uint32_t lo = 0;
  uint32_t hi = pageEntries;
  while (hi - lo > 1) {
    const uint32_t mid = lo + (hi - lo) / 2;
    if (keyOf(page[mid]) <= key) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  blockIndex = number * DIRECTORY_PAGE_ENTRIES + lo;
  found = true;
  return true;
}

WordIndexReader::CachedBlock* WordIndexReader::loadBlock(const uint32_t blockIndex) {
  for (CachedBlock& block : blocks) {
    if (block.valid && block.blockIndex == blockIndex) {
      block.lastUse = ++useClock;
      return &block;
    }
  }
  DirectoryEntry e;
  if (!entry(blockIndex, e)) return nullptr;
  uint32_t nextFirstWord = head.wordCount;
  if (blockIndex + 1 < head.blockCount) {
    DirectoryEntry next;
    if (!entry(blockIndex + 1, next)) return nullptr;
    nextFirstWord = next.firstWord;
  }

  CachedBlock& block = blocks[0].lastUse <= blocks[1].lastUse ? blocks[0] : blocks[1];
  block.valid = false;
  uint8_t* d = block.data;
  if (!readFile(e.slot * SLOT_BYTES, d, SLOT_BYTES)) return nullptr;
  const uint32_t count = d[1];
  const uint32_t used = readU16(d + 2);
  const uint32_t recordsEnd = SLOT_HEADER_BYTES + count * WORD_RECORD_BYTES;
  const bool shapeOk = d[0] == SLOT_WORDS && count >= 1 && count <= MAX_BLOCK_WORDS && used >= recordsEnd + count &&
                       used <= SLOT_BYTES && readU32(d + 4) == e.firstWord && readU32(d + 8) == e.baseOffset &&
                       nextFirstWord - e.firstWord == count;
  if (!shapeOk) {
    fail(ReadStatus::Corrupt);
    return nullptr;
  }
  const uint32_t crc = crc32Update(crc32Update(0, d, 12), d + SLOT_HEADER_BYTES, used - SLOT_HEADER_BYTES);
  if (crc != readU32(d + 12)) {
    fail(ReadStatus::Corrupt);
    return nullptr;
  }
  // Records: texts fill the rest exactly, words do not overlap and lie inside the chapter.
  uint32_t textAt = recordsEnd;
  uint64_t previousEnd = e.baseOffset;
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* r = d + SLOT_HEADER_BYTES + i * WORD_RECORD_BYTES;
    const uint64_t start = static_cast<uint64_t>(e.baseOffset) + readU16(r);
    const uint64_t end = start + readU24(r + 2);
    const uint32_t length = r[6];
    const bool ok = length >= 1 && textAt + length <= used && start >= previousEnd && end > start &&
                    end <= head.visibleCodepoints && (i > 0 || start == e.baseOffset) && (r[5] & 1) == 0;
    if (!ok) {
      fail(ReadStatus::Corrupt);
      return nullptr;
    }
    block.textAt[i] = static_cast<uint16_t>(textAt);
    textAt += length;
    previousEnd = end;
  }
  if (textAt != used) {
    fail(ReadStatus::Corrupt);
    return nullptr;
  }
  block.valid = true;
  block.blockIndex = blockIndex;
  block.firstWord = e.firstWord;
  block.wordCount = count;
  block.baseOffset = e.baseOffset;
  block.lastUse = ++useClock;
  return &block;
}

WordIndexReader::CachedBlock* WordIndexReader::blockForWord(const uint32_t index) {
  if (state != ReadStatus::Ok) return nullptr;
  if (index >= head.wordCount) return nullptr;
  for (CachedBlock& block : blocks) {
    if (block.valid && index >= block.firstWord && index - block.firstWord < block.wordCount) {
      block.lastUse = ++useClock;
      return &block;
    }
  }
  // The neighbours of a cached block are found without a search.
  for (const CachedBlock& block : blocks) {
    if (!block.valid) continue;
    if (index == block.firstWord + block.wordCount && block.blockIndex + 1 < head.blockCount) {
      return loadBlock(block.blockIndex + 1);
    }
    if (index + 1 == block.firstWord && block.blockIndex > 0) return loadBlock(block.blockIndex - 1);
  }
  uint32_t blockIndex = 0;
  bool found = false;
  if (!findBlock(false, index, blockIndex, found)) return nullptr;
  if (!found) {
    fail(ReadStatus::Corrupt);
    return nullptr;
  }
  CachedBlock* block = loadBlock(blockIndex);
  if (block != nullptr && (index < block->firstWord || index - block->firstWord >= block->wordCount)) {
    fail(ReadStatus::Corrupt);
    return nullptr;
  }
  return block;
}

void WordIndexReader::decode(const CachedBlock& block, const uint32_t slot, WordInfo& out) const {
  const uint8_t* r = block.data + SLOT_HEADER_BYTES + slot * WORD_RECORD_BYTES;
  out.index = block.firstWord + slot;
  out.start = block.baseOffset + readU16(r);
  out.end = out.start + readU24(r + 2);
  out.flags = r[5];
  out.text = std::string_view(reinterpret_cast<const char*>(block.data + block.textAt[slot]), r[6]);
}

bool WordIndexReader::word(const uint32_t index, WordInfo& out) {
  const CachedBlock* block = blockForWord(index);
  if (block == nullptr) return false;
  decode(*block, index - block->firstWord, out);
  return true;
}

bool WordIndexReader::wordFlags(const uint32_t index, uint8_t& out) {
  WordInfo w;
  if (!word(index, w)) return false;
  out = w.flags;
  return true;
}

bool WordIndexReader::wordStart(const uint32_t index, uint32_t& out) {
  WordInfo w;
  if (!word(index, w)) return false;
  out = w.start;
  return true;
}

bool WordIndexReader::firstWordEndingAfter(const uint32_t offset, uint32_t& out) {
  if (state != ReadStatus::Ok) return false;
  // Words before the last block starting at or before `offset` all end at or before it.
  uint32_t blockIndex = 0;
  bool found = false;
  if (!findBlock(true, offset, blockIndex, found)) return false;
  if (!found) {
    out = 0;
    return true;
  }
  const CachedBlock* block = loadBlock(blockIndex);
  if (block == nullptr) return false;
  for (uint32_t i = 0; i < block->wordCount; ++i) {
    WordInfo w;
    decode(*block, i, w);
    if (w.end > offset) {
      out = w.index;
      return true;
    }
  }
  out = block->firstWord + block->wordCount;  // the next block starts after `offset`
  return true;
}

// ---------------------------------------------------------------------------------------------
// Anchors

bool WordIndexReader::readAnchorAt(const uint32_t offset, Anchor& out, uint32_t& recordBytes) {
  if (static_cast<uint64_t>(offset) + ANCHOR_RECORD_FIXED_BYTES > head.anchorBytes) return fail(ReadStatus::Corrupt);
  uint8_t fixed[ANCHOR_RECORD_FIXED_BYTES];
  if (!readFile(head.anchorOffset + offset, fixed, sizeof(fixed))) return false;
  out.wordIndex = readU32(fixed);
  out.offset = readU32(fixed + 4);
  out.idLength = fixed[8];
  recordBytes = ANCHOR_RECORD_FIXED_BYTES + out.idLength;
  if (static_cast<uint64_t>(offset) + recordBytes > head.anchorBytes || out.wordIndex > head.wordCount ||
      out.offset > head.visibleCodepoints || out.idLength < 5) {
    return fail(ReadStatus::Corrupt);
  }
  if (!readFile(head.anchorOffset + offset + ANCHOR_RECORD_FIXED_BYTES, reinterpret_cast<uint8_t*>(out.id),
                out.idLength)) {
    return false;
  }
  out.id[out.idLength] = '\0';
  if (std::strncmp(out.id, "rsvp-", 5) != 0) return fail(ReadStatus::Corrupt);
  return true;
}

bool WordIndexReader::nextAnchor(AnchorCursor& cursor, Anchor& out) {
  if (state != ReadStatus::Ok || cursor.index >= head.anchorCount) return false;
  uint32_t bytes = 0;
  if (!readAnchorAt(cursor.offset, out, bytes)) return false;
  cursor.index++;
  cursor.offset += bytes;
  return true;
}

bool WordIndexReader::findAnchor(const std::string_view id, Anchor& out) {
  AnchorCursor cursor;
  while (nextAnchor(cursor, out)) {
    if (out.idView() == id) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------------------------
// Markers

bool WordIndexReader::readMarker(const uint32_t index, Marker& out) {
  if (index >= head.markerCount) return false;
  uint8_t r[MARKER_RECORD_BYTES];
  if (!readFile(head.markerOffset + index * MARKER_RECORD_BYTES, r, sizeof(r))) return false;
  out.kind = r[0];
  out.flags = r[1];
  out.wordStart = readU32(r + 4);
  out.wordEnd = readU32(r + 8);
  out.offsetStart = readU32(r + 12);
  out.offsetEnd = readU32(r + 16);
  const bool ok = out.kind >= MARKER_IMAGE && out.kind <= MARKER_NOTE && (out.flags & ~MARKER_IN_NOTE) == 0 &&
                  readU16(r + 2) == 0 && out.wordStart <= out.wordEnd && out.wordEnd <= head.wordCount &&
                  (out.kind != MARKER_IMAGE || out.wordStart == out.wordEnd) && out.offsetStart <= out.offsetEnd &&
                  out.offsetEnd <= head.visibleCodepoints;
  return ok || fail(ReadStatus::Corrupt);
}

bool WordIndexReader::marker(const uint32_t index, Marker& out) {
  return state == ReadStatus::Ok && readMarker(index, out);
}

bool WordIndexReader::nextMarker(const uint32_t index, const uint8_t kinds, Marker& out, uint32_t* position) {
  if (state != ReadStatus::Ok) return false;
  // First marker with wordStart >= index.
  uint32_t lo = 0;
  uint32_t hi = head.markerCount;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    Marker m;
    if (!readMarker(mid, m)) return false;
    if (m.wordStart < index) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  for (uint32_t i = lo; i < head.markerCount; ++i) {
    if (!readMarker(i, out)) return false;
    if (kinds & kindBit(out.kind)) {
      if (position != nullptr) *position = i;
      return true;
    }
  }
  return false;
}

bool WordIndexReader::imageAt(const uint32_t index, const bool includeInNote) {
  uint32_t position = 0;
  Marker m;
  if (!nextMarker(index, kindBit(MARKER_IMAGE), m, &position)) return false;
  for (uint32_t i = position; i < head.markerCount; ++i) {
    if (!readMarker(i, m) || m.wordStart != index) return false;
    if (m.kind == MARKER_IMAGE && (includeInNote || !(m.flags & MARKER_IN_NOTE))) return true;
  }
  return false;
}

bool WordIndexReader::regionContaining(const uint32_t index, const uint8_t kinds, Marker& out) {
  if (state != ReadStatus::Ok) return false;
  // Regions of one class (TABLE/CODE, or NOTE) never overlap, so for each class only the last
  // region starting at or before `index` can contain it. Walk back from there.
  const uint8_t nonprose = kindBit(MARKER_TABLE) | kindBit(MARKER_CODE);
  const uint8_t note = kindBit(MARKER_NOTE);
  uint8_t open = kinds & (nonprose | note);
  uint32_t i = head.markerCount;
  while (i > 0 && open != 0) {
    Marker m;
    if (!readMarker(--i, m)) return false;
    if (m.wordStart > index) continue;
    const uint8_t bit = kindBit(m.kind);
    const uint8_t cls = (bit & nonprose) ? nonprose : ((bit & note) ? note : 0);
    if (cls == 0 || (open & cls) == 0) continue;
    if (index < m.wordEnd && (kinds & bit)) {
      out = m;
      return true;
    }
    open &= static_cast<uint8_t>(~cls);
  }
  return false;
}

}  // namespace wordindex
