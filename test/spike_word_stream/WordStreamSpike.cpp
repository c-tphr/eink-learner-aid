// THROWAWAY PROTOTYPE for spike S1 (issue #9). See WordStreamSpike.h.
#include "WordStreamSpike.h"

#include <Epub/VisibleTextUtils.h>
#include <Epub/htmlEntities.h>
#include <Utf8.h>
#include <XmlParserUtils.h>
#include <strings.h>

#include <algorithm>
#include <cstring>

namespace spike {
namespace {

// Elements where ChapterHtmlSlimParser itself flushes the word buffer (BLOCK_TAGS, HEADER_TAGS,
// table structure, IMAGE_TAGS, hr: ChapterHtmlSlimParser.cpp:55-61, 142-148, 2074, 2685-2691).
constexpr const char* PARSER_BREAK_TAGS[] = {"p",  "li", "div", "br", "blockquote", "ul", "ol", "pre", "figure",
                                             "h1", "h2", "h3",  "h4", "h5",         "h6", "table", "tr", "td",
                                             "th", "hr", "img", "image"};
// Other HTML block elements. The renderer does not break words at these, but real books
// always have whitespace there; breaking keeps RSVP words sane when they do not.
constexpr const char* EXTRA_BREAK_TAGS[] = {"section", "article", "aside",  "nav",   "header",  "footer",
                                            "main",    "figcaption", "caption", "dl", "dt", "dd", "address",
                                            "hgroup",  "details", "summary", "thead", "tbody", "tfoot",
                                            "body",    "html"};

const char* const ABBREVIATIONS[] = {"mr.",  "mrs.", "ms.",  "dr.",  "st.", "jr.", "sr.",  "prof.",
                                     "rev.", "gen.", "col.", "capt.", "lt.", "sgt.", "mt.", "no.",
                                     "vs.",  "e.g.", "i.e.", "cf.",  "fig.", "vol.", "pp.", "ch."};

template <size_t N>
bool inList(const std::string& name, const char* const (&list)[N]) {
  return std::any_of(std::begin(list), std::end(list), [&](const char* tag) { return name == tag; });
}

std::string localLower(const char* qName) {
  std::string out = xmlLocalName(qName);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

bool hasToken(const char* value, const char* token) {
  if (!value) return false;
  const size_t len = std::strlen(token);
  const char* p = value;
  while (*p) {
    while (*p == ' ' || *p == '\t' || *p == '\n') ++p;
    const char* start = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n') ++p;
    if (static_cast<size_t>(p - start) == len && std::strncmp(start, token, len) == 0) return true;
  }
  return false;
}

bool isInternalFragmentLink(const char* href) {
  if (!href || !std::strchr(href, '#')) return false;
  for (const char* scheme : {"http://", "https://", "mailto:", "ftp://", "tel:", "javascript:"}) {
    if (std::strncmp(href, scheme, std::strlen(scheme)) == 0) return false;
  }
  return true;
}

// Word separators: ASCII whitespace the parser splits on (ChapterHtmlSlimParser.cpp:62) plus
// Unicode spaces, including U+00A0 and U+202F (the renderer keeps those as glued space tokens).
bool isSeparator(const uint32_t cp) {
  return cp == 0x09 || cp == 0x0A || cp == 0x0C || cp == 0x0D || cp == 0x20 || cp == 0xA0 || cp == 0x1680 ||
         (cp >= 0x2000 && cp <= 0x200B) || cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F ||
         cp == 0x3000;
}

bool isCloser(const uint32_t cp) {
  return cp == '"' || cp == '\'' || cp == ')' || cp == ']' || cp == '}' || cp == 0x2019 || cp == 0x201D ||
         cp == 0xBB || cp == 0x203A;
}

bool isOpener(const uint32_t cp) {
  return cp == '"' || cp == '\'' || cp == '(' || cp == '[' || cp == '{' || cp == 0x2018 || cp == 0x201C ||
         cp == 0xAB || cp == 0x2039;
}

std::vector<uint32_t> codepoints(const std::string& text) {
  std::vector<uint32_t> out;
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  while (const uint32_t cp = utf8NextCodepoint(&p)) out.push_back(cp);
  return out;
}

// Returns the terminal punctuation after trailing closers are stripped, or 0.
uint32_t terminal(const std::string& text, std::string* coreOut) {
  auto cps = codepoints(text);
  while (!cps.empty() && isCloser(cps.back())) cps.pop_back();
  if (cps.empty()) return 0;
  const uint32_t last = cps.back();
  if (last != '.' && last != '!' && last != '?' && last != 0x2026) return 0;
  if (coreOut) {
    coreOut->clear();
    for (const uint32_t cp : cps) utf8AppendCodepoint(cp, *coreOut);
  }
  return last;
}

bool isAbbreviation(const std::string& core) {
  std::string lower = core;
  for (char& c : lower) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  if (lower.size() == 2 && lower[0] >= 'a' && lower[0] <= 'z') return true;  // initial "J."
  return inList(lower, ABBREVIATIONS);
}

bool startsUpperOrDigit(const std::string& text) {
  for (const uint32_t cp : codepoints(text)) {
    if (isOpener(cp)) continue;
    return (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9') || (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7);
  }
  return false;
}

}  // namespace

WordStreamBuilder::WordStreamBuilder() {
  parser_ = XML_ParserCreate(nullptr);
  if (!parser_) {
    ok_ = false;
    return;
  }
  // Same entity path as ChapterHtmlSlimParser::beginParse (ChapterHtmlSlimParser.cpp:2894).
  XML_SetDefaultHandlerExpand(parser_, onDefault);
  XML_SetUserData(parser_, this);
  XML_SetElementHandler(parser_, onStart, onEnd);
  XML_SetCharacterDataHandler(parser_, onChars);
}

WordStreamBuilder::~WordStreamBuilder() { destroyXmlParser(parser_); }

bool WordStreamBuilder::write(const char* data, const size_t len) {
  if (!ok_) return false;
  if (XML_Parse(parser_, data, static_cast<int>(len), XML_FALSE) == XML_STATUS_ERROR) ok_ = false;
  return ok_;
}

bool WordStreamBuilder::finish() {
  if (!ok_) return false;
  if (XML_Parse(parser_, "", 0, XML_TRUE) == XML_STATUS_ERROR) ok_ = false;
  markBoundary(PARA_END);
  return ok_;
}

void WordStreamBuilder::exclude(const uint32_t offset, const char reason) {
  if (!excluded_.empty() && excluded_.back().reason == reason && excluded_.back().end == offset) {
    excluded_.back().end = offset + 1;
  } else {
    excluded_.push_back({offset, offset + 1, reason});
  }
}

void WordStreamBuilder::appendCodepoint(const uint32_t cp, const uint32_t offset) {
  std::string bytes;
  utf8AppendCodepoint(cp, bytes);
  if (!cur_.empty() && cur_.size() + bytes.size() > MAX_WORD_BYTES) {
    const uint32_t start = curStart_;
    const uint32_t end = curEnd_;
    std::string text;
    text.swap(cur_);
    pushWord(std::move(text), start, end, BYTE_SPLIT);
  }
  if (cur_.empty()) curStart_ = offset;
  cur_ += bytes;
  curEnd_ = offset + 1;
  lastCp_ = cp;
}

void WordStreamBuilder::pushWord(std::string text, const uint32_t start, const uint32_t end, uint8_t flags) {
  if (nonproseCount_ > 0) flags |= NONPROSE;
  if (headingCount_ > 0) flags |= HEADING;
  Word word{utf8ComposeNfc(text), start, end, flags};
  if (!words_.empty()) decideSentence(words_.size() - 1, &word);
  words_.push_back(std::move(word));
}

void WordStreamBuilder::flushWord() {
  if (cur_.empty()) return;
  std::string text;
  text.swap(cur_);
  pushWord(std::move(text), curStart_, curEnd_, curNoteref_ ? NOTEREF : 0);
  curNoteref_ = false;
  lastCp_ = 0;
}

void WordStreamBuilder::markBoundary(const uint8_t flag) {
  flushWord();
  if (words_.empty()) return;
  words_.back().flags |= flag;
  decideSentence(words_.size() - 1, nullptr);
}

void WordStreamBuilder::decideSentence(const size_t index, const Word* next) {
  Word& word = words_[index];
  std::string core;
  const uint32_t last = terminal(word.text, &core);
  if (last == 0) return;
  const bool boundary = (word.flags & (PARA_END | LINE_END)) != 0;
  if (!boundary && !(next && startsUpperOrDigit(next->text))) return;
  if (last == '.' && isAbbreviation(core)) return;
  word.flags |= SENT_END;
}

void WordStreamBuilder::onText(const char* s, const int len) {
  const bool counted = insideBody_ && nonVisibleDepth_ == 0;
  const auto* p = reinterpret_cast<const unsigned char*>(s);
  const auto* end = p + len;
  while (p < end) {
    const uint32_t cp = utf8NextCodepoint(&p);
    const uint32_t offset = visibleOffset_;
    if (!counted) continue;
    visibleOffset_++;  // every body codepoint, as ChapterHtmlSlimParser.cpp:2320-2329

    if (hiddenUntilDepth_ < depth_) {
      exclude(offset, 'h');
      continue;
    }
    if (noterefActive()) {
      exclude(offset, 'n');
      if (!cur_.empty()) {
        curNoteref_ = true;
      } else if (!words_.empty()) {
        words_.back().flags |= NOTEREF;
      }
      continue;
    }
    if (isSeparator(cp)) {
      flushWord();
      continue;
    }
    if (cp == 0xAD || cp == 0xFEFF) {  // soft hyphen, BOM: dropped, never a boundary
      if (!cur_.empty()) curEnd_ = offset + 1;
      continue;
    }
    if (!cur_.empty() && (lastCp_ == 0x2014 || lastCp_ == 0x2013)) flushWord();  // "word—word"
    appendCodepoint(cp, offset);
  }
}

void XMLCALL WordStreamBuilder::onChars(void* user, const XML_Char* s, const int len) {
  static_cast<WordStreamBuilder*>(user)->onText(s, len);
}

void XMLCALL WordStreamBuilder::onDefault(void* user, const XML_Char* s, const int len) {
  // Mirrors ChapterHtmlSlimParser::defaultHandlerExpand (ChapterHtmlSlimParser.cpp:2591-2605).
  if (len >= 3 && s[0] == '&' && s[len - 1] == ';') {
    const char* value = lookupHtmlEntity(s, static_cast<size_t>(len));
    auto* self = static_cast<WordStreamBuilder*>(user);
    if (value) {
      self->onText(value, static_cast<int>(std::strlen(value)));
    } else {
      self->onText(s, len);
    }
  }
}

void XMLCALL WordStreamBuilder::onStart(void* user, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<WordStreamBuilder*>(user);
  // Visibility bookkeeping mirrors ChapterHtmlSlimParser.cpp:1352-1360 exactly.
  if (strcasecmp(name, "body") == 0) self->insideBody_ = true;
  if (self->insideBody_ && (self->nonVisibleDepth_ > 0 || VisibleTextUtils::isNonVisibleElement(name))) {
    self->nonVisibleDepth_++;
  }

  const char* id = nullptr;
  const char* role = nullptr;
  const char* epubType = nullptr;
  const char* href = nullptr;
  bool hidden = false;
  for (int i = 0; atts && atts[i]; i += 2) {
    if (std::strcmp(atts[i], "id") == 0) id = atts[i + 1];
    else if (std::strcmp(atts[i], "role") == 0) role = atts[i + 1];
    else if (std::strcmp(atts[i], "epub:type") == 0) epubType = atts[i + 1];
    else if (std::strcmp(atts[i], "href") == 0) href = atts[i + 1];
    else if (std::strcmp(atts[i], "hidden") == 0) hidden = true;
  }

  Element e;
  const bool active = self->insideBody_ && self->nonVisibleDepth_ == 0;
  if (active && id && std::strncmp(id, "rsvp-", 5) == 0) {
    self->anchors_.push_back({id, static_cast<uint32_t>(self->words_.size() + (self->cur_.empty() ? 0 : 1)),
                              self->visibleOffset_});
  }
  if (active && self->hiddenUntilDepth_ >= self->depth_) {
    const std::string local = localLower(name);
    // Same skip test as ChapterHtmlSlimParser.cpp:1463-1467 and 2006-2016; rt is ruby annotation.
    const bool pagebreak = (role && std::strcmp(role, "doc-pagebreak") == 0) ||
                           (epubType && std::strcmp(epubType, "pagebreak") == 0);
    if (hidden || pagebreak || local == "rt") {
      self->hiddenUntilDepth_ = self->depth_;
    } else {
      e.breaks = inList(local, PARSER_BREAK_TAGS) || inList(local, EXTRA_BREAK_TAGS);
      e.paragraph = e.breaks && local != "br" && local != "img" && local != "image";
      if (local == "br") {
        self->markBoundary(LINE_END);
      } else if (e.paragraph) {
        self->markBoundary(PARA_END);
      } else if (e.breaks) {
        self->flushWord();
      }
      if (local.size() == 2 && local[0] == 'h' && local[1] >= '1' && local[1] <= '6') {
        e.heading = true;
        self->headingCount_++;
      }
      if (local == "sup") {
        e.sup = true;
        self->supCount_++;
      }
      if (local == "a" && isInternalFragmentLink(href)) {
        e.internalLink = true;
        self->internalLinkCount_++;
      }
      if (local == "a" && (hasToken(epubType, "noteref") || hasToken(role, "doc-noteref"))) {
        e.noteref = true;
        self->explicitNoterefCount_++;
      }
      if (local == "table" || local == "pre") {
        e.nonprose = true;
        if (self->nonproseCount_++ == 0) {
          const auto at = static_cast<uint32_t>(self->words_.size());
          self->markers_.push_back({local == "table" ? MarkerKind::Table : MarkerKind::Code, at, at,
                                    self->visibleOffset_, self->visibleOffset_});
        }
      }
      if ((local == "img" || local == "image") && self->nonproseCount_ == 0) {
        const auto at = static_cast<uint32_t>(self->words_.size());
        self->markers_.push_back({MarkerKind::Image, at, at, self->visibleOffset_, self->visibleOffset_});
      }
    }
  }
  self->stack_.push_back(e);
  self->depth_++;
}

void XMLCALL WordStreamBuilder::onEnd(void* user, const XML_Char* name) {
  auto* self = static_cast<WordStreamBuilder*>(user);
  if (self->nonVisibleDepth_ > 0) self->nonVisibleDepth_--;  // ChapterHtmlSlimParser.cpp:2610-2612
  self->depth_--;
  const Element e = self->stack_.back();
  self->stack_.pop_back();

  if (e.paragraph) {
    self->markBoundary(PARA_END);
  } else if (e.breaks) {
    self->flushWord();
  }
  if (e.heading) self->headingCount_--;
  if (e.sup) self->supCount_--;
  if (e.internalLink) self->internalLinkCount_--;
  if (e.noteref) self->explicitNoterefCount_--;
  if (e.nonprose && --self->nonproseCount_ == 0) {
    Marker& region = self->markers_.back();
    region.wordEnd = static_cast<uint32_t>(self->words_.size());
    region.offsetEnd = self->visibleOffset_;
  }
  if (self->hiddenUntilDepth_ == self->depth_) self->hiddenUntilDepth_ = INT_MAX;
  if (std::strcmp(name, "body") == 0) {  // ChapterHtmlSlimParser.cpp:2840-2842
    self->markBoundary(PARA_END);
    self->insideBody_ = false;
  }
}

uint32_t firstWordAtOrAfter(const std::vector<Word>& words, const uint32_t offset) {
  const auto it = std::partition_point(words.begin(), words.end(), [&](const Word& w) { return w.end <= offset; });
  return static_cast<uint32_t>(it - words.begin());
}

uint16_t pageForOffset(const std::vector<uint32_t>& pageStarts, const uint32_t offset, const bool preferFirstAtOffset) {
  uint16_t result = 0;
  for (size_t i = 0; i < pageStarts.size(); i++) {
    if (preferFirstAtOffset && pageStarts[i] == offset) return static_cast<uint16_t>(i);
    if (pageStarts[i] > offset) break;
    result = static_cast<uint16_t>(i);
  }
  return result;
}

FormatSize estimateFormatSize(const WordStreamBuilder& stream) {
  constexpr size_t HEADER = 64;
  constexpr size_t DIRECTORY_ENTRY = 12;  // u32 firstWord, u32 fileOffset, u32 baseOffset
  constexpr size_t BLOCK_HEADER = 12;     // u32 baseOffset, u32 firstWord, u16 wordCount, u16 textBytes
  constexpr size_t WORD_RECORD = 6;       // u16 startDelta, u8 span, u8 flags, u16 textOffset
  constexpr size_t MAX_BLOCK_WORDS = 64;
  constexpr size_t MAX_BLOCK_TEXT = 2048;

  FormatSize size;
  size.headerBytes = HEADER;
  size_t blockWords = 0;
  size_t blockText = 0;
  uint32_t blockBase = 0;
  const auto closeBlock = [&]() {
    if (blockWords == 0) return;
    const size_t bytes = BLOCK_HEADER + blockWords * WORD_RECORD + blockText;
    size.blockBytes += bytes;
    size.maxBlockBytes = std::max(size.maxBlockBytes, bytes);
    size.blocks++;
    blockWords = 0;
    blockText = 0;
  };
  for (const Word& word : stream.words()) {
    const size_t text = word.text.size() + 1;
    if (blockWords == MAX_BLOCK_WORDS || blockText + text > MAX_BLOCK_TEXT ||
        (blockWords > 0 && word.start - blockBase > UINT16_MAX)) {
      closeBlock();
    }
    if (blockWords == 0) blockBase = word.start;
    blockWords++;
    blockText += text;
  }
  closeBlock();
  size.directoryBytes = size.blocks * DIRECTORY_ENTRY;
  for (const Anchor& anchor : stream.anchors()) size.anchorBytes += 4 + 4 + 1 + anchor.id.size();
  size.markerBytes = stream.markers().size() * 17;
  return size;
}

std::string flagString(const uint8_t flags) {
  std::string out;
  const auto add = [&](const uint8_t bit, const char* name) {
    if (!(flags & bit)) return;
    if (!out.empty()) out += ',';
    out += name;
  };
  add(SENT_END, "SENT");
  add(PARA_END, "PARA");
  add(LINE_END, "LINE");
  add(NONPROSE, "NONPROSE");
  add(NOTEREF, "NOTEREF");
  add(HEADING, "HEAD");
  add(BYTE_SPLIT, "SPLIT");
  return out;
}

}  // namespace spike
