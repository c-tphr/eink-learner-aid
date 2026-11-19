#include "WordIndexBuilder.h"

#include <Epub/VisibleTextUtils.h>
#include <Epub/htmlEntities.h>
#include <Utf8.h>
#include <XmlParserUtils.h>
#include <strings.h>

#include <cstring>

namespace wordindex {

namespace {

// Per-element bits, pushed at the start tag and acted on at the end tag.
enum ElementBit : uint16_t {
  E_PROCESSED = 1 << 0,  // in the visible body and not inside hidden content: the word rules apply
  E_BOUNDARY = 1 << 1,   // a word boundary at both tags
  E_PARAGRAPH = 1 << 2,  // the boundary also sets PARA_END
  E_HEADING = 1 << 3,
  E_SUP = 1 << 4,
  E_LINK = 1 << 5,      // <a> with an internal "#" href
  E_EXPLICIT = 1 << 6,  // <a> with epub:type noteref or role doc-noteref
  E_NONPROSE = 1 << 7,  // table or pre
  E_NOTE = 1 << 8,      // footnote or endnote body
  E_HIDDEN = 1 << 9,    // the outermost hidden, page-break or rt element
};

// Section 5, "Element boundaries". br, img and image break words without ending a paragraph.
constexpr const char* BOUNDARY_ELEMENTS[] = {
    "p",       "li",      "div",    "br",      "blockquote", "ul",     "ol",    "pre",        "figure",  "h1",  "h2",
    "h3",      "h4",      "h5",     "h6",      "table",      "tr",     "td",    "th",         "hr",      "img", "image",
    "section", "article", "aside",  "nav",     "header",     "footer", "main",  "figcaption", "caption", "dl",  "dt",
    "dd",      "address", "hgroup", "details", "summary",    "thead",  "tbody", "tfoot",      "body",    "html"};

constexpr const char* EXTERNAL_SCHEMES[] = {"http:", "https:", "mailto:", "ftp:", "tel:", "javascript:"};
constexpr const char* NOTE_TYPES[] = {"footnote", "endnote", "endnotes"};
constexpr const char* NOTE_ROLES[] = {"doc-footnote", "doc-endnote", "doc-endnotes"};

// Element names longer than this match no rule, so they need not be compared in full.
constexpr size_t MAX_RULE_NAME = 15;

template <size_t N>
bool inList(const char* name, const char* const (&list)[N]) {
  for (const char* item : list) {
    if (std::strcmp(name, item) == 0) return true;
  }
  return false;
}

// Lowercased local name, or "" when it is longer than any name a rule mentions.
void ruleName(const char* qName, char (&out)[MAX_RULE_NAME + 1]) {
  const char* local = xmlLocalName(qName);
  size_t i = 0;
  for (; local[i] != '\0'; ++i) {
    if (i == MAX_RULE_NAME) {
      out[0] = '\0';
      return;
    }
    const char c = local[i];
    out[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  }
  out[i] = '\0';
}

bool isTokenSpace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

// True when the whitespace-separated token list `value` holds one of `tokens`.
template <size_t N>
bool hasAnyToken(const char* value, const char* const (&tokens)[N]) {
  if (value == nullptr) return false;
  const char* p = value;
  while (*p != '\0') {
    while (isTokenSpace(*p)) ++p;
    const char* start = p;
    while (*p != '\0' && !isTokenSpace(*p)) ++p;
    const size_t length = static_cast<size_t>(p - start);
    if (length == 0) continue;
    for (const char* token : tokens) {
      if (std::strlen(token) == length && std::strncmp(start, token, length) == 0) return true;
    }
  }
  return false;
}

bool isInternalLink(const char* href) {
  if (href == nullptr || std::strchr(href, '#') == nullptr) return false;
  for (const char* scheme : EXTERNAL_SCHEMES) {
    if (std::strncmp(href, scheme, std::strlen(scheme)) == 0) return false;
  }
  return true;
}

// Section 5, "Separators".
bool isSeparator(const uint32_t cp) {
  return cp == 0x09 || cp == 0x0A || cp == 0x0C || cp == 0x0D || cp == 0x20 || cp == 0xA0 || cp == 0x1680 ||
         (cp >= 0x2000 && cp <= 0x200B) || cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

void copyId(char (&out)[MAX_ANCHOR_ID_BYTES + 1], const char* id) {
  const size_t length = std::strlen(id);
  const size_t kept = length < MAX_ANCHOR_ID_BYTES ? length : MAX_ANCHOR_ID_BYTES;
  std::memcpy(out, id, kept);
  out[kept] = '\0';
}

}  // namespace

const char* buildStatusName(const BuildStatus status) {
  switch (status) {
    case BuildStatus::Ok:
      return "Ok";
    case BuildStatus::ReadError:
      return "ReadError";
    case BuildStatus::WriteError:
      return "WriteError";
    case BuildStatus::ParseError:
      return "ParseError";
    case BuildStatus::OutOfMemory:
      return "OutOfMemory";
    case BuildStatus::TooDeep:
      return "TooDeep";
    case BuildStatus::AnchorInsideWord:
      return "AnchorInsideWord";
    case BuildStatus::AnchorIdTooLong:
      return "AnchorIdTooLong";
    case BuildStatus::TooLarge:
      return "TooLarge";
  }
  return "?";
}

WordIndexBuilder::~WordIndexBuilder() { destroyXmlParser(parser); }

void WordIndexBuilder::reset(IndexSink& sink) {
  out = &sink;
  result = BuildReport{};
  insideBody = false;
  htmlEnded = false;
  nonVisibleDepth = 0;
  visibleOffset = 0;
  depth = 0;
  hidden = false;
  headings = sups = links = explicitNoterefs = nonprose = notes = 0;
  nonproseRegion = OpenMarker{};
  noteRegion = OpenMarker{};
  curLength = curStart = curEnd = lastCp = 0;
  curNoteref = curInNote = anchorPending = false;
  hasPending = false;
  emitted = 0;
  blockWords = blockTextBytes = blockFirstWord = blockBase = committed = 0;
  directory = Table{};
  anchors = Table{};
  markers = Table{};
  nextSlot = 1;
  fileBytes = 0;
  sourceBytes = 0;
  sourceCrc = 0;
}

void WordIndexBuilder::fail(const BuildStatus status) {
  if (failed()) return;
  result.status = status;
  if (parser != nullptr) {
    result.line = static_cast<uint32_t>(XML_GetCurrentLineNumber(parser));
    result.column = static_cast<uint32_t>(XML_GetCurrentColumnNumber(parser));
    XML_StopParser(parser, XML_FALSE);
  }
}

// ---------------------------------------------------------------------------------------------
// Word pass

void WordIndexBuilder::onText(const char* s, const int length) {
  // Counted exactly as ChapterHtmlSlimParser::characterData counts visibleTextOffset.
  if (!insideBody || nonVisibleDepth != 0) return;
  const auto* p = reinterpret_cast<const unsigned char*>(s);
  const auto* end = p + length;
  while (p < end && !failed()) {
    const uint32_t cp = utf8NextCodepoint(&p);
    const uint32_t offset = visibleOffset++;
    if (hidden) continue;
    if (noterefActive()) {
      if (curLength > 0) {
        curNoteref = true;
      } else if (hasPending) {
        pending.flags |= NOTEREF;
      }
      continue;
    }
    if (isSeparator(cp)) {
      flushWord();
      continue;
    }
    if (cp == 0xAD || cp == 0xFEFF) {  // removed, never a boundary; the word's end covers it
      if (curLength > 0) curEnd = offset + 1;
      continue;
    }
    if (curLength > 0 && (lastCp == 0x2014 || lastCp == 0x2013)) flushWord();
    append(cp, offset);
  }
}

void WordIndexBuilder::append(const uint32_t cp, const uint32_t offset) {
  if (anchorPending) {
    result.wordIndex = emitted;
    fail(BuildStatus::AnchorInsideWord);
    return;
  }
  char bytes[4];
  const auto length = static_cast<uint32_t>(utf8AppendCodepoint(bytes, cp) - bytes);
  if (curLength > 0 && curLength + length > MAX_WORD_BYTES) {
    push(BYTE_SPLIT);
    curLength = 0;
  }
  if (curLength == 0) {
    curStart = offset;
    curInNote = notes > 0;
  }
  std::memcpy(cur + curLength, bytes, length);
  curLength += length;
  curEnd = offset + 1;
  lastCp = cp;
}

void WordIndexBuilder::flushWord() {
  anchorPending = false;
  if (curLength == 0) return;
  push(curNoteref ? NOTEREF : 0);
  curLength = 0;
  curNoteref = false;
  lastCp = 0;
}

// Pushes the word in `cur` as the new pending word.
void WordIndexBuilder::push(uint8_t flags) {
  if (nonprose > 0) flags |= NONPROSE;
  if (headings > 0) flags |= HEADING;
  if (curInNote) flags |= NOTE;
  if (curEnd - curStart > MAX_WORD_SPAN) {
    fail(BuildStatus::TooLarge);
    return;
  }
  commitPending();
  cur[curLength] = '\0';
  utf8ComposeNfcInPlace(cur);
  pending.length = static_cast<uint32_t>(std::strlen(cur));
  std::memcpy(pending.text, cur, pending.length);
  pending.start = curStart;
  pending.end = curEnd;
  pending.flags = flags;
  hasPending = true;
  ++emitted;
}

void WordIndexBuilder::boundary(const uint8_t flag) {
  flushWord();
  if (hasPending) pending.flags |= flag;
}

void WordIndexBuilder::recordAnchor(const char* id, const uint32_t index) {
  const size_t idLength = std::strlen(id);
  if (idLength > MAX_ANCHOR_ID_BYTES) {
    copyId(result.anchorId, id);
    fail(BuildStatus::AnchorIdTooLong);
    return;
  }
  if (curLength > 0 && !anchorPending) {
    anchorPending = true;
    copyId(result.anchorId, id);
  }
  uint8_t record[ANCHOR_RECORD_FIXED_BYTES + MAX_ANCHOR_ID_BYTES];
  writeU32(record, index);
  writeU32(record + 4, visibleOffset);
  record[8] = static_cast<uint8_t>(idLength);
  std::memcpy(record + ANCHOR_RECORD_FIXED_BYTES, id, idLength);
  addRecord(anchors, TABLE_ANCHORS, record, static_cast<uint32_t>(ANCHOR_RECORD_FIXED_BYTES + idLength));
}

void WordIndexBuilder::openMarker(const uint8_t kind, OpenMarker* region) {
  uint8_t record[MARKER_RECORD_BYTES] = {};
  const uint32_t at = started();
  record[0] = kind;
  record[1] = notes > 0 ? MARKER_IN_NOTE : 0;
  writeU32(record + 4, at);
  writeU32(record + 8, at);
  writeU32(record + 12, visibleOffset);
  writeU32(record + 16, visibleOffset);
  if (!addRecord(markers, TABLE_MARKERS, record, MARKER_RECORD_BYTES)) return;
  if (region != nullptr) {
    region->open = true;
    region->inRam = true;
    region->at = markers.used - MARKER_RECORD_BYTES;
  }
}

void WordIndexBuilder::closeMarker(OpenMarker& region, const uint32_t wordEnd) {
  if (!region.open) return;
  region.open = false;
  uint8_t ends[4];
  writeU32(ends, wordEnd);
  uint8_t offsetEnd[4];
  writeU32(offsetEnd, visibleOffset);
  if (region.inRam) {
    std::memcpy(markers.data + region.at + 8, ends, 4);
    std::memcpy(markers.data + region.at + 16, offsetEnd, 4);
    return;
  }
  if (!out->writeAt(region.at + 8, ends, 4) || !out->writeAt(region.at + 16, offsetEnd, 4)) {
    fail(BuildStatus::WriteError);
  }
}

void XMLCALL WordIndexBuilder::onStart(void* user, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<WordIndexBuilder*>(user);
  if (self->failed()) return;
  if (self->depth == MAX_ELEMENT_DEPTH) {
    self->fail(BuildStatus::TooDeep);
    return;
  }

  // Visibility bookkeeping mirrors ChapterHtmlSlimParser::startElement exactly (qualified names).
  if (strcasecmp(name, "body") == 0) self->insideBody = true;
  if (self->insideBody && (self->nonVisibleDepth > 0 || VisibleTextUtils::isNonVisibleElement(name))) {
    self->nonVisibleDepth++;
  }

  const char* id = nullptr;
  const char* role = nullptr;
  const char* epubType = nullptr;
  const char* href = nullptr;
  bool hiddenAttribute = false;
  for (int i = 0; atts != nullptr && atts[i] != nullptr; i += 2) {
    if (std::strcmp(atts[i], "id") == 0) {
      id = atts[i + 1];
    } else if (std::strcmp(atts[i], "role") == 0) {
      role = atts[i + 1];
    } else if (std::strcmp(atts[i], "epub:type") == 0) {
      epubType = atts[i + 1];
    } else if (std::strcmp(atts[i], "href") == 0) {
      href = atts[i + 1];
    } else if (std::strcmp(atts[i], "hidden") == 0) {
      hiddenAttribute = true;
    }
  }

  char local[MAX_RULE_NAME + 1];
  ruleName(name, local);
  uint16_t bits = 0;
  if (self->insideBody && self->nonVisibleDepth == 0) {
    if (id != nullptr && std::strncmp(id, "rsvp-", 5) == 0) self->recordAnchor(id, self->started());
    // Hidden, page-break and rt content: its text is in no word, and its own tags and every tag
    // inside it are ignored (no boundary, flag or marker). Exact values, as the renderer tests them.
    const bool hides = hiddenAttribute || (role != nullptr && std::strcmp(role, "doc-pagebreak") == 0) ||
                       (epubType != nullptr && std::strcmp(epubType, "pagebreak") == 0) ||
                       std::strcmp(local, "rt") == 0;
    if (!self->hidden && hides) {
      self->hidden = true;
      bits |= E_HIDDEN;
    }
    if (!self->hidden) bits |= E_PROCESSED;
  }

  if (bits & E_PROCESSED) {
    if (std::strcmp(local, "br") == 0) {
      bits |= E_BOUNDARY;
      self->boundary(LINE_END);
    } else if (inList(local, BOUNDARY_ELEMENTS)) {
      bits |= E_BOUNDARY;
      if (std::strcmp(local, "img") == 0 || std::strcmp(local, "image") == 0) {
        self->flushWord();
      } else {
        bits |= E_PARAGRAPH;
        self->boundary(PARA_END);
      }
    }
    if (local[0] == 'h' && local[1] >= '1' && local[1] <= '6' && local[2] == '\0') {
      bits |= E_HEADING;
      self->headings++;
    }
    if (std::strcmp(local, "sup") == 0) {
      bits |= E_SUP;
      self->sups++;
    }
    if (std::strcmp(local, "a") == 0) {
      if (isInternalLink(href)) {
        bits |= E_LINK;
        self->links++;
      }
      static constexpr const char* NOTEREF_TYPES[] = {"noteref"};
      static constexpr const char* NOTEREF_ROLES[] = {"doc-noteref"};
      if (hasAnyToken(epubType, NOTEREF_TYPES) || hasAnyToken(role, NOTEREF_ROLES)) {
        bits |= E_EXPLICIT;
        self->explicitNoterefs++;
      }
    }
    const bool isTable = std::strcmp(local, "table") == 0;
    if (isTable || std::strcmp(local, "pre") == 0) {
      bits |= E_NONPROSE;
      if (self->nonprose++ == 0) self->openMarker(isTable ? MARKER_TABLE : MARKER_CODE, &self->nonproseRegion);
    }
    if ((std::strcmp(local, "img") == 0 || std::strcmp(local, "image") == 0) && self->nonprose == 0) {
      self->openMarker(MARKER_IMAGE, nullptr);
    }
    if (hasAnyToken(epubType, NOTE_TYPES) || hasAnyToken(role, NOTE_ROLES)) {
      bits |= E_NOTE;
      if (self->notes == 0) self->openMarker(MARKER_NOTE, &self->noteRegion);
      self->notes++;
    }
  }
  self->elements[self->depth++] = bits;
}

void XMLCALL WordIndexBuilder::onEnd(void* user, const XML_Char* name) {
  auto* self = static_cast<WordIndexBuilder*>(user);
  if (self->failed() || self->depth == 0) return;
  if (self->nonVisibleDepth > 0) self->nonVisibleDepth--;  // as ChapterHtmlSlimParser::endElement
  const uint16_t bits = self->elements[--self->depth];

  if (bits & E_PROCESSED) {
    if (bits & E_BOUNDARY) {
      if (bits & E_PARAGRAPH) {
        self->boundary(PARA_END);
      } else {
        self->flushWord();
      }
    }
    if (bits & E_HEADING) self->headings--;
    if (bits & E_SUP) self->sups--;
    if (bits & E_LINK) self->links--;
    if (bits & E_EXPLICIT) self->explicitNoterefs--;
    if ((bits & E_NONPROSE) && --self->nonprose == 0) self->closeMarker(self->nonproseRegion, self->emitted);
    if ((bits & E_NOTE) && --self->notes == 0) self->closeMarker(self->noteRegion, self->started());
  }
  if (bits & E_HIDDEN) self->hidden = false;
  if (std::strcmp(name, "body") == 0) self->insideBody = false;
  if (std::strcmp(name, "html") == 0) self->htmlEnded = true;
}

void XMLCALL WordIndexBuilder::onCharacters(void* user, const XML_Char* s, const int length) {
  auto* self = static_cast<WordIndexBuilder*>(user);
  if (!self->failed()) self->onText(s, length);
}

void XMLCALL WordIndexBuilder::onDefault(void* user, const XML_Char* s, const int length) {
  // Named HTML entities expat hands over undeclared, as ChapterHtmlSlimParser::defaultHandlerExpand.
  auto* self = static_cast<WordIndexBuilder*>(user);
  if (self->failed() || length < 3 || s[0] != '&' || s[length - 1] != ';') return;
  const char* value = lookupHtmlEntity(s, static_cast<size_t>(length));
  if (value != nullptr) {
    self->onText(value, static_cast<int>(std::strlen(value)));
  } else {
    self->onText(s, length);
  }
}

// ---------------------------------------------------------------------------------------------
// Output

bool WordIndexBuilder::appendBytes(const uint8_t* data, const size_t length) {
  if (fileBytes + length > UINT32_MAX) {
    fail(BuildStatus::TooLarge);
    return false;
  }
  if (!out->write(data, length)) {
    fail(BuildStatus::WriteError);
    return false;
  }
  fileBytes += length;
  return true;
}

void WordIndexBuilder::commitPending() {
  if (!hasPending || failed()) return;
  hasPending = false;
  const Word& word = pending;
  const bool fits =
      SLOT_HEADER_BYTES + (blockWords + 1) * WORD_RECORD_BYTES + blockTextBytes + word.length <= SLOT_BYTES;
  if (blockWords > 0 && (!fits || word.start - blockBase > UINT16_MAX)) closeBlock();
  if (blockWords == 0) {
    blockFirstWord = committed;
    blockBase = word.start;
  }
  uint8_t* record = block + SLOT_HEADER_BYTES + blockWords * WORD_RECORD_BYTES;
  writeU16(record, word.start - blockBase);
  writeU24(record + 2, word.end - word.start);
  record[5] = word.flags;
  record[6] = static_cast<uint8_t>(word.length);
  std::memcpy(blockText + blockTextBytes, word.text, word.length);
  blockTextBytes += word.length;
  ++blockWords;
  ++committed;
}

void WordIndexBuilder::closeBlock() {
  if (blockWords == 0 || failed()) return;
  const uint32_t recordsEnd = SLOT_HEADER_BYTES + blockWords * WORD_RECORD_BYTES;
  const uint32_t used = recordsEnd + blockTextBytes;
  std::memcpy(block + recordsEnd, blockText, blockTextBytes);
  std::memset(block + used, 0, SLOT_BYTES - used);
  block[0] = SLOT_WORDS;
  block[1] = static_cast<uint8_t>(blockWords);
  writeU16(block + 2, used);
  writeU32(block + 4, blockFirstWord);
  writeU32(block + 8, blockBase);
  const uint32_t crc = crc32Update(crc32Update(0, block, 12), block + SLOT_HEADER_BYTES, used - SLOT_HEADER_BYTES);
  writeU32(block + 12, crc);
  if (used > result.maxBlockBytes) result.maxBlockBytes = used;

  const uint32_t slot = nextSlot;
  if (!appendBytes(block, SLOT_BYTES)) return;
  ++nextSlot;
  ++result.header.blockCount;
  blockWords = 0;
  blockTextBytes = 0;

  uint8_t entry[DIRECTORY_ENTRY_BYTES];
  writeU32(entry, blockFirstWord);
  writeU32(entry + 4, blockBase);
  writeU32(entry + 8, slot);
  addRecord(directory, TABLE_DIRECTORY, entry, DIRECTORY_ENTRY_BYTES);
}

bool WordIndexBuilder::addRecord(Table& table, const TableId id, const uint8_t* record, const uint32_t length) {
  if (failed()) return false;
  if (table.used + length > sizeof(table.data) && !spill(table, id)) return false;
  std::memcpy(table.data + table.used, record, length);
  table.used += length;
  table.records++;
  table.totalRecords++;
  table.totalBytes += length;
  return true;
}

// Writes the table's RAM buffer as the next slot (a scratch slot, chained to the previous one).
bool WordIndexBuilder::spill(Table& table, const TableId id) {
  uint8_t header[SLOT_HEADER_BYTES] = {};
  header[0] = SLOT_SPILL;
  header[1] = id;
  writeU16(header + 2, table.used);
  writeU32(header + 8, table.records);
  const uint32_t slot = nextSlot;
  const uint32_t slotOffset = slot * SLOT_BYTES;
  uint8_t padding[SLOT_BYTES - SLOT_HEADER_BYTES] = {};
  if (!appendBytes(header, sizeof(header)) || !appendBytes(table.data, table.used) ||
      !appendBytes(padding, sizeof(padding) - table.used)) {
    return false;
  }
  ++nextSlot;
  ++result.spillSlots;
  if (table.lastSpill != 0) {
    uint8_t next[4];
    writeU32(next, slot);
    if (!out->writeAt(table.lastSpill * SLOT_BYTES + 4, next, sizeof(next))) {
      fail(BuildStatus::WriteError);
      return false;
    }
  } else {
    table.firstSpill = slot;
  }
  table.lastSpill = slot;
  // An open region's marker record now lives in the file.
  if (id == TABLE_MARKERS) {
    for (OpenMarker* region : {&nonproseRegion, &noteRegion}) {
      if (region->open && region->inRam) {
        region->inRam = false;
        region->at += slotOffset + SLOT_HEADER_BYTES;
      }
    }
  }
  table.used = 0;
  table.records = 0;
  return true;
}

// Appends the table (spilled slots, then the RAM buffer) to the file, updating `crc`.
bool WordIndexBuilder::copyTable(const Table& table, const TableId id, uint32_t& crc) {
  uint32_t slot = table.firstSpill;
  uint32_t copied = 0;
  while (slot != 0 && !failed()) {
    // The open block is closed by now, so its buffer is free.
    if (!out->readAt(slot * SLOT_BYTES, block, SLOT_BYTES)) {
      fail(BuildStatus::WriteError);
      return false;
    }
    const uint32_t used = readU16(block + 2);
    if (block[0] != SLOT_SPILL || block[1] != id || used > SLOT_BYTES - SLOT_HEADER_BYTES ||
        copied + used > table.totalBytes) {
      fail(BuildStatus::WriteError);  // the sink did not return what was written
      return false;
    }
    crc = crc32Update(crc, block + SLOT_HEADER_BYTES, used);
    if (!appendBytes(block + SLOT_HEADER_BYTES, used)) return false;
    copied += used;
    const uint32_t next = readU32(block + 4);
    if (next != 0 && next <= slot) {
      fail(BuildStatus::WriteError);
      return false;
    }
    slot = next;
  }
  crc = crc32Update(crc, table.data, table.used);
  if (!appendBytes(table.data, table.used)) return false;
  if (copied + table.used != table.totalBytes) {
    fail(BuildStatus::WriteError);
    return false;
  }
  return true;
}

void WordIndexBuilder::finish() {
  boundary(PARA_END);  // the chapter's last word
  commitPending();
  closeBlock();
  if (failed()) return;

  Header& h = result.header;
  h.sourceBytes = static_cast<uint32_t>(sourceBytes);
  h.sourceCrc32 = sourceCrc;
  h.visibleCodepoints = visibleOffset;
  h.wordCount = committed;
  h.slotCount = nextSlot - 1;
  h.directoryOffset = static_cast<uint32_t>(fileBytes);
  uint32_t crc = 0;
  if (!copyTable(directory, TABLE_DIRECTORY, crc)) return;
  h.anchorCount = anchors.totalRecords;
  h.anchorOffset = static_cast<uint32_t>(fileBytes);
  h.anchorBytes = anchors.totalBytes;
  if (!copyTable(anchors, TABLE_ANCHORS, crc)) return;
  h.markerCount = markers.totalRecords;
  h.markerOffset = static_cast<uint32_t>(fileBytes);
  if (!copyTable(markers, TABLE_MARKERS, crc)) return;
  h.fileBytes = static_cast<uint32_t>(fileBytes);
  h.tablesCrc32 = crc;

  uint8_t header[HEADER_BYTES];
  encodeHeader(h, header);
  if (!out->writeAt(0, header, sizeof(header))) fail(BuildStatus::WriteError);
}

BuildStatus WordIndexBuilder::begin(IndexSink& sink) {
  reset(sink);
  destroyXmlParser(parser);
  parsing = true;
  parser = memory != nullptr ? XML_ParserCreate_MM(nullptr, memory, nullptr) : XML_ParserCreate(nullptr);
  if (parser == nullptr) {
    result.status = BuildStatus::OutOfMemory;
    return result.status;
  }
  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, onStart, onEnd);
  XML_SetCharacterDataHandler(parser, onCharacters);
  XML_SetDefaultHandlerExpand(parser, onDefault);
  // Slot 0 stays zero until the header is written last, so an unfinished file never opens.
  const uint8_t zeros[SLOT_BYTES] = {};
  appendBytes(zeros, sizeof(zeros));
  return result.status;
}

// `data` is either the caller's bytes (copied by XML_Parse) or expat's own buffer from XML_GetBuffer.
void WordIndexBuilder::parse(const uint8_t* data, const size_t length, const bool inExpatBuffer) {
  if (failed() || parser == nullptr) return;
  sourceBytes += length;
  if (sourceBytes > UINT32_MAX) {
    fail(BuildStatus::TooLarge);
    return;
  }
  sourceCrc = crc32Update(sourceCrc, data, length);
  if (!parsing) return;  // after </html>: counted for the source identity only
  const bool last = length == 0;
  const auto status = inExpatBuffer ? XML_ParseBuffer(parser, static_cast<int>(length), last ? XML_TRUE : XML_FALSE)
                                    : XML_Parse(parser, reinterpret_cast<const char*>(data), static_cast<int>(length),
                                                last ? XML_TRUE : XML_FALSE);
  if (status != XML_STATUS_ERROR || failed()) return;
  const XML_Error error = XML_GetErrorCode(parser);
  if (error == XML_ERROR_NO_MEMORY) {
    fail(BuildStatus::OutOfMemory);
  } else if (htmlEnded) {
    parsing = false;  // as the renderer: data after </html> is ignored
  } else {
    result.xmlError = static_cast<uint32_t>(error);
    fail(BuildStatus::ParseError);
  }
}

bool WordIndexBuilder::feed(const uint8_t* data, const size_t length) {
  // Large writes go to expat in chunks, so its input buffer stays at INPUT_CHUNK_BYTES.
  for (size_t at = 0; at < length && !failed();) {
    const size_t n = length - at < INPUT_CHUNK_BYTES ? length - at : INPUT_CHUNK_BYTES;
    parse(data + at, n, false);
    at += n;
  }
  return !failed();
}

BuildStatus WordIndexBuilder::end() {
  parse(nullptr, 0, false);
  if (!failed()) finish();
  destroyXmlParser(parser);
  return result.status;
}

BuildStatus WordIndexBuilder::build(ChapterInput& input, IndexSink& sink) {
  if (begin(sink) != BuildStatus::Ok) return result.status;
  while (!failed()) {
    uint8_t drain[128];
    void* buffer = parsing ? XML_GetBuffer(parser, INPUT_CHUNK_BYTES) : drain;
    if (buffer == nullptr) {
      fail(BuildStatus::OutOfMemory);
      break;
    }
    const int32_t length = input.read(static_cast<uint8_t*>(buffer), parsing ? INPUT_CHUNK_BYTES : sizeof(drain));
    if (length < 0) {
      fail(BuildStatus::ReadError);
      break;
    }
    if (length == 0) break;
    parse(static_cast<const uint8_t*>(buffer), static_cast<size_t>(length), parsing);
  }
  if (failed()) {
    destroyXmlParser(parser);
    return result.status;
  }
  return end();
}

}  // namespace wordindex
