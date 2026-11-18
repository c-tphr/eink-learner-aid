#include "CheckpointParser.h"

#include <cstdio>
#include <cstring>

namespace checkpoint::detail {

namespace {

constexpr char FORMAT_NAME[] = "rsvp-checkpoints";
constexpr uint32_t FORMAT_NAME_LENGTH = sizeof(FORMAT_NAME) - 1;
constexpr uint32_t SHA256_HEX_CHARS = 64;
constexpr uint32_t MAX_WORD_COUNT = 2147483647u;

// What the container being parsed is, in schema terms.
enum Context : uint8_t {
  CTX_UNKNOWN,
  CTX_ROOT,
  CTX_BOOK,
  CTX_SECTIONS,
  CTX_SECTION,
  CTX_CHECKPOINTS,
  CTX_CHECKPOINT,
  CTX_PROMPTS,
  CTX_PROMPT,
};

// What the value being parsed means, in schema terms.
enum Slot : uint8_t {
  SLOT_UNKNOWN,
  SLOT_DOCUMENT,
  SLOT_FORMAT,
  SLOT_VERSION,
  SLOT_BOOK,
  SLOT_SECTIONS,
  SLOT_CHECKPOINTS,
  SLOT_TITLE,
  SLOT_LANGUAGE,
  SLOT_SHA256,
  SLOT_INSTRUCTIONS_VERSION,
  SLOT_SECTION,
  SLOT_SECTION_ID,
  SLOT_CHAPTER_HREF,
  SLOT_START_ANCHOR,
  SLOT_END_ANCHOR,
  SLOT_LABEL,
  SLOT_WORD_COUNT,
  SLOT_CHECKPOINT,
  SLOT_CHECKPOINT_ID,
  SLOT_CHECKPOINT_SECTION,
  SLOT_PROMPTS,
  SLOT_PROMPT,
  SLOT_PROMPT_ID,
  SLOT_PROMPT_TEXT,
  SLOT_ANSWER_TEXT,
};

enum class Kind : uint8_t { Any, Object, Array, String, Integer };

struct Member {
  const char* name;
  uint8_t slot;
};

constexpr Member ROOT_MEMBERS[] = {{"format", SLOT_FORMAT},
                                   {"version", SLOT_VERSION},
                                   {"book", SLOT_BOOK},
                                   {"sections", SLOT_SECTIONS},
                                   {"checkpoints", SLOT_CHECKPOINTS}};
constexpr Member BOOK_MEMBERS[] = {{"title", SLOT_TITLE},
                                   {"language", SLOT_LANGUAGE},
                                   {"source_sha256", SLOT_SHA256},
                                   {"instructions_version", SLOT_INSTRUCTIONS_VERSION}};
constexpr Member SECTION_MEMBERS[] = {{"id", SLOT_SECTION_ID},
                                      {"chapter_href", SLOT_CHAPTER_HREF},
                                      {"start_anchor", SLOT_START_ANCHOR},
                                      {"end_anchor", SLOT_END_ANCHOR},
                                      {"label", SLOT_LABEL},
                                      {"word_count", SLOT_WORD_COUNT}};
constexpr Member CHECKPOINT_MEMBERS[] = {
    {"id", SLOT_CHECKPOINT_ID}, {"section", SLOT_CHECKPOINT_SECTION}, {"prompts", SLOT_PROMPTS}};
constexpr Member PROMPT_MEMBERS[] = {
    {"id", SLOT_PROMPT_ID}, {"prompt", SLOT_PROMPT_TEXT}, {"answer", SLOT_ANSWER_TEXT}};

// Every member of every known object is required by the schema.
const Member* membersOf(uint8_t context, uint8_t& count) {
  switch (context) {
    case CTX_ROOT:
      count = sizeof(ROOT_MEMBERS) / sizeof(Member);
      return ROOT_MEMBERS;
    case CTX_BOOK:
      count = sizeof(BOOK_MEMBERS) / sizeof(Member);
      return BOOK_MEMBERS;
    case CTX_SECTION:
      count = sizeof(SECTION_MEMBERS) / sizeof(Member);
      return SECTION_MEMBERS;
    case CTX_CHECKPOINT:
      count = sizeof(CHECKPOINT_MEMBERS) / sizeof(Member);
      return CHECKPOINT_MEMBERS;
    case CTX_PROMPT:
      count = sizeof(PROMPT_MEMBERS) / sizeof(Member);
      return PROMPT_MEMBERS;
    default:
      count = 0;
      return nullptr;
  }
}

Kind kindOf(uint8_t slot) {
  switch (slot) {
    case SLOT_DOCUMENT:
    case SLOT_BOOK:
    case SLOT_SECTION:
    case SLOT_CHECKPOINT:
    case SLOT_PROMPT:
      return Kind::Object;
    case SLOT_SECTIONS:
    case SLOT_CHECKPOINTS:
    case SLOT_PROMPTS:
      return Kind::Array;
    case SLOT_VERSION:
    case SLOT_WORD_COUNT:
      return Kind::Integer;
    case SLOT_UNKNOWN:
      return Kind::Any;
    default:
      return Kind::String;
  }
}

uint8_t contextOf(uint8_t slot) {
  switch (slot) {
    case SLOT_DOCUMENT:
      return CTX_ROOT;
    case SLOT_BOOK:
      return CTX_BOOK;
    case SLOT_SECTIONS:
      return CTX_SECTIONS;
    case SLOT_SECTION:
      return CTX_SECTION;
    case SLOT_CHECKPOINTS:
      return CTX_CHECKPOINTS;
    case SLOT_CHECKPOINT:
      return CTX_CHECKPOINT;
    case SLOT_PROMPTS:
      return CTX_PROMPTS;
    case SLOT_PROMPT:
      return CTX_PROMPT;
    default:
      return CTX_UNKNOWN;
  }
}

uint8_t itemSlotOf(uint8_t context) {
  switch (context) {
    case CTX_SECTIONS:
      return SLOT_SECTION;
    case CTX_CHECKPOINTS:
      return SLOT_CHECKPOINT;
    case CTX_PROMPTS:
      return SLOT_PROMPT;
    default:
      return SLOT_UNKNOWN;
  }
}

bool isIdSlot(uint8_t slot) {
  return slot == SLOT_SECTION_ID || slot == SLOT_CHECKPOINT_ID || slot == SLOT_CHECKPOINT_SECTION ||
         slot == SLOT_PROMPT_ID;
}

bool isAsciiLetter(uint32_t cp) { return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'); }
bool isAsciiDigit(uint32_t cp) { return cp >= '0' && cp <= '9'; }
bool isAsciiHexDigit(uint32_t cp) { return isAsciiDigit(cp) || (cp >= 'a' && cp <= 'f') || (cp >= 'A' && cp <= 'F'); }

// Unicode White_Space property (tools/prep/checkpoints.py WHITE_SPACE).
bool isWhiteSpace(uint32_t cp) {
  return (cp >= 0x09 && cp <= 0x0D) || cp == 0x20 || cp == 0x85 || cp == 0xA0 || cp == 0x1680 ||
         (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

// Unicode general category Cc other than LINE FEED.
bool isForbiddenControl(uint32_t cp) { return (cp <= 0x1F && cp != 0x0A) || (cp >= 0x7F && cp <= 0x9F); }

// States of the search for a complete character reference:
// &name; &#123; &#x1F; (tools/prep/checkpoints.py _ENTITY_RE).
enum RefState : uint8_t { REF_NONE, REF_AMP, REF_NAME, REF_HASH, REF_DEC, REF_HEX_START, REF_HEX };

constexpr uint8_t TEXT_PRESENT = 1;
constexpr uint8_t TEXT_NOT_PLAIN = 2;
constexpr uint8_t TEXT_BLANK = 4;

bool anchorMatches(const char* anchor, uint8_t length, const char* id, size_t idLength, const char* suffix) {
  const size_t suffixLength = strlen(suffix);
  return length == 5 + idLength + suffixLength && memcmp(anchor, "rsvp-", 5) == 0 &&
         memcmp(anchor + 5, id, idLength) == 0 && memcmp(anchor + 5 + idLength, suffix, suffixLength) == 0;
}

}  // namespace

struct CheckpointParser::StringState {
  uint8_t slot = SLOT_UNKNOWN;
  bool isKey = false;
  uint32_t offset = 0;
  uint32_t surrogatePathLength = 0;
  uint32_t chars = 0;
  uint32_t bytes = 0;
  uint32_t hash = FNV_OFFSET_BASIS;
  bool idOk = true;
  bool sha256Ok = true;
  bool formatOk = true;
  // Pool region after index.poolUsed, committed by commitPoolString().
  bool toPool = false;
  bool poolOverflow = false;
  uint32_t poolCharLimit = 0;
  uint32_t poolLength = 0;
  // Fixed buffer in the index (language, instructions version, sha256).
  char* fixedText = nullptr;
  uint32_t fixedCharLimit = 0;
  uint32_t fixedCapacity = 0;  // bytes, excluding the NUL
  uint32_t fixedLength = 0;
  // Capture for member names and anchors.
  char* capture = nullptr;
  uint32_t captureCapacity = 0;
  uint32_t captureLength = 0;
  bool captureOverflow = false;
  // Plain-text and blank-text rules.
  bool checkText = false;
  bool notPlain = false;
  bool allWhiteSpace = true;
  bool previousLessThan = false;
  uint8_t refState = REF_NONE;
};

CheckpointParser::CheckpointParser(CheckpointIndex& target, CheckpointSource& input)
    : index(target), st(target.parseState), source(input), reader(target.parseState.readBuffer, READ_BUFFER_BYTES) {}

OpenResult CheckpointParser::run() {
  size = source.size();
  if (size > MAX_FILE_BYTES) {
    stop = Stop::Limit;
    stopDetail = "file larger than MAX_FILE_BYTES";
    return finish();
  }
  uint8_t head[3] = {0, 0, 0};
  if (size >= 3 && source.read(0, head, 3) == 3 && head[0] == 0xEF && head[1] == 0xBB && head[2] == 0xBF) {
    stop = Stop::Syntax;
    stopDetail = "file starts with a UTF-8 byte order mark";
    return finish();
  }
  reader.begin(source, 0, size);
  scan();
  return finish();
}

// ---------------------------------------------------------------------------
// Scanner

void CheckpointParser::scan() {
  enum class Step : uint8_t { Value, FirstMember, FirstItem, AfterValue };
  Step step = Step::Value;
  uint8_t slot = SLOT_DOCUMENT;

  while (stop == Stop::None) {
    switch (step) {
      case Step::Value: {
        skipWhitespace();
        const int c = reader.peek();
        const uint32_t at = reader.position();
        if (c == '{' || c == '[') {
          reader.next();
          if (!pushContainer(c == '{', slot, at)) {
            return;
          }
          step = c == '{' ? Step::FirstMember : Step::FirstItem;
        } else {
          if (!readScalar(slot)) {
            return;
          }
          step = Step::AfterValue;
        }
        break;
      }

      case Step::FirstMember:
        skipWhitespace();
        if (reader.peek() == '}') {
          reader.next();
          popContainer();
          step = Step::AfterValue;
        } else {
          if (!readMember(slot)) {
            return;
          }
          step = Step::Value;
        }
        break;

      case Step::FirstItem:
        skipWhitespace();
        if (reader.peek() == ']') {
          reader.next();
          popContainer();
          step = Step::AfterValue;
        } else {
          slot = startItem();
          step = Step::Value;
        }
        break;

      case Step::AfterValue: {
        skipWhitespace();
        if (depth == 0) {
          if (reader.peek() >= 0 || reader.failed()) {
            syntaxError("data after the JSON value");
          }
          return;
        }
        const bool inObject = st.frames[depth - 1].isObject != 0;
        const int c = reader.peek();
        if (c == ',') {
          reader.next();
          if (inObject) {
            skipWhitespace();
            if (!readMember(slot)) {
              return;
            }
          } else {
            slot = startItem();
          }
          step = Step::Value;
        } else if (c == (inObject ? '}' : ']')) {
          reader.next();
          popContainer();
        } else {
          syntaxError(inObject ? "expected ',' or '}'" : "expected ',' or ']'");
          return;
        }
        break;
      }
    }
  }
}

void CheckpointParser::skipWhitespace() {
  while (true) {
    const int c = reader.peek();
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
      return;
    }
    reader.next();
  }
}

bool CheckpointParser::pushContainer(bool isObject, uint8_t slot, uint32_t offset) {
  if (depth >= MAX_DEPTH) {
    structuralLimit(offset, "objects and arrays nested deeper than MAX_DEPTH");
    return false;
  }
  const uint8_t context = containerContext(isObject, slot, offset);
  ParseFrame& frame = st.frames[depth++];
  frame.count = 0;
  frame.offset = offset;
  frame.pathLength = pathLength;
  frame.keyBase = keyTop;
  frame.isObject = isObject ? 1 : 0;
  frame.context = context;
  frame.seen = 0;
  return true;
}

void CheckpointParser::popContainer() {
  const ParseFrame frame = st.frames[--depth];
  keyTop = frame.keyBase;
  pathLength = frame.pathLength;
  closeContext(frame);
}

bool CheckpointParser::readMember(uint8_t& slot) {
  ParseFrame& frame = st.frames[depth - 1];
  if (reader.peek() != '"') {
    syntaxError("expected a member name");
    return false;
  }
  const uint32_t at = reader.position();
  pathLength = frame.pathLength;
  pathPut('/');

  StringState s;
  s.isKey = true;
  s.surrogatePathLength = frame.pathLength;
  s.capture = st.key;
  s.captureCapacity = KEY_CAPTURE_BYTES - 1;
  if (!readString(s)) {
    return false;
  }
  skipWhitespace();
  if (reader.peek() != ':') {
    syntaxError("expected ':'");
    return false;
  }
  reader.next();

  if (!duplicateKey) {
    for (uint16_t k = frame.keyBase; k < keyTop; ++k) {
      if (st.keys[k].hash == s.hash &&
          sameJsonString(source, st.keys[k].offset, size - st.keys[k].offset, at, size - at)) {
        duplicateKey = true;
        duplicateKeyOffset = at;
        break;
      }
    }
  }
  if (keyTop >= MAX_OPEN_MEMBERS) {
    structuralLimit(at, "more than MAX_OPEN_MEMBERS members in open objects");
    return false;
  }
  st.keys[keyTop].hash = s.hash;
  st.keys[keyTop].offset = at;
  ++keyTop;
  ++frame.count;

  slot = SLOT_UNKNOWN;
  uint8_t count = 0;
  const Member* members = membersOf(frame.context, count);
  if (!s.captureOverflow) {
    for (uint8_t i = 0; i < count; ++i) {
      if (strlen(members[i].name) == s.captureLength && memcmp(members[i].name, st.key, s.captureLength) == 0) {
        slot = members[i].slot;
        frame.seen |= static_cast<uint8_t>(1u << i);
        break;
      }
    }
  }
  return true;
}

uint8_t CheckpointParser::startItem() {
  ParseFrame& frame = st.frames[depth - 1];
  pathLength = frame.pathLength;
  pathPut('/');
  pathPutIndex(frame.count);
  ++frame.count;
  return itemSlotOf(frame.context);
}

bool CheckpointParser::readScalar(uint8_t slot) {
  const int c = reader.peek();
  if (c == '"') {
    StringState s;
    s.slot = slot;
    s.surrogatePathLength = pathLength;
    switch (slot) {
      case SLOT_SECTION_ID:
      case SLOT_CHECKPOINT_ID:
      case SLOT_CHECKPOINT_SECTION:
        s.toPool = true;
        s.poolCharLimit = MAX_ID_CHARS;
        break;
      case SLOT_CHAPTER_HREF:
        s.toPool = true;
        s.poolCharLimit = MAX_HREF_CHARS;
        break;
      case SLOT_LANGUAGE:
        s.fixedText = index.languageText;
        s.fixedCharLimit = MAX_LANGUAGE_CHARS;
        s.fixedCapacity = sizeof(index.languageText) - 1;
        break;
      case SLOT_INSTRUCTIONS_VERSION:
        s.fixedText = index.instructionsVersionText;
        s.fixedCharLimit = MAX_INSTRUCTIONS_VERSION_CHARS;
        s.fixedCapacity = sizeof(index.instructionsVersionText) - 1;
        break;
      case SLOT_SHA256:
        s.fixedText = index.sha256Text;
        s.fixedCharLimit = SHA256_HEX_CHARS;
        s.fixedCapacity = sizeof(index.sha256Text) - 1;
        break;
      case SLOT_START_ANCHOR:
        s.capture = st.startAnchor;
        s.captureCapacity = ANCHOR_CAPTURE_BYTES - 1;
        break;
      case SLOT_END_ANCHOR:
        s.capture = st.endAnchor;
        s.captureCapacity = ANCHOR_CAPTURE_BYTES - 1;
        break;
      case SLOT_PROMPT_TEXT:
      case SLOT_ANSWER_TEXT:
        s.checkText = true;
        break;
      default:
        break;
    }
    const uint32_t at = reader.position();
    if (!readString(s)) {
      return false;
    }
    finishString(s, at, reader.position() - at);
    return true;
  }
  if (c == '-' || (c >= '0' && c <= '9')) {
    return readNumber(slot);
  }
  if (c == 't' || c == 'f' || c == 'n') {
    return readLiteral(slot);
  }
  syntaxError("expected a JSON value");
  return false;
}

bool CheckpointParser::readString(StringState& s) {
  s.offset = reader.position();
  reader.next();  // opening quote
  JsonStringDecoder decoder;
  decoder.reset();
  while (true) {
    const int c = reader.peek();
    if (c < 0) {
      syntaxError("unterminated string");
      return false;
    }
    uint32_t cps[2];
    uint8_t count = 0;
    const JsonStringDecoder::Status status = decoder.feed(static_cast<uint8_t>(c), cps, count);
    if (status == JsonStringDecoder::Status::Error) {
      syntaxError("invalid UTF-8, control character or escape in a string");
      return false;
    }
    reader.next();
    for (uint8_t i = 0; i < count; ++i) {
      onCodePoint(s, cps[i]);
    }
    if (status == JsonStringDecoder::Status::End) {
      return true;
    }
  }
}

bool CheckpointParser::readNumber(uint8_t slot) {
  const uint32_t at = reader.position();
  char text[MAX_NUMBER_CHARS];
  uint8_t length = 0;
  bool tooLong = false;
  auto take = [&]() {
    if (length >= MAX_NUMBER_CHARS) {
      tooLong = true;
      reader.next();
      return;
    }
    text[length++] = static_cast<char>(reader.next());
  };
  auto digitNext = [&]() {
    const int c = reader.peek();
    return c >= '0' && c <= '9';
  };

  bool negative = false;
  bool integer = true;
  if (reader.peek() == '-') {
    negative = true;
    take();
  }
  if (reader.peek() == '0') {
    take();
  } else if (digitNext()) {
    while (digitNext() && !tooLong) take();
  } else {
    syntaxError("invalid number");
    return false;
  }
  if (!tooLong && reader.peek() == '.') {
    integer = false;
    take();
    if (!digitNext()) {
      syntaxError("invalid number");
      return false;
    }
    while (digitNext() && !tooLong) take();
  }
  if (!tooLong && (reader.peek() == 'e' || reader.peek() == 'E')) {
    integer = false;
    take();
    if (reader.peek() == '+' || reader.peek() == '-') take();
    if (!digitNext()) {
      syntaxError("invalid number");
      return false;
    }
    while (digitNext() && !tooLong) take();
  }
  if (tooLong) {
    structuralLimit(at, "number longer than MAX_NUMBER_CHARS");
    return false;
  }

  const char* digits = text + (negative ? 1 : 0);
  const uint8_t digitCount = static_cast<uint8_t>(length - (negative ? 1 : 0));
  if (slot == SLOT_VERSION) {
    const bool isZeroOrOne = digitCount == 1 && (digits[0] == '0' || digits[0] == '1');
    if (!(integer && !negative && digitCount == 1 && digits[0] == '1')) {
      schemaError(at, "version must be the integer 1");
    }
    if (integer && !negative && !isZeroOrOne) {
      versionAboveOne = true;
      versionOffset = at;
    }
  } else if (slot == SLOT_WORD_COUNT) {
    uint64_t value = 0;
    bool ok = integer && !negative && digitCount <= 10;
    for (uint8_t i = 0; ok && i < digitCount; ++i) {
      value = value * 10 + static_cast<uint64_t>(digits[i] - '0');
    }
    ok = ok && value >= 1 && value <= MAX_WORD_COUNT;
    if (!ok) {
      schemaError(at, "word_count must be an integer from 1 to 2147483647");
    } else if (currentSection != NO_INDEX) {
      index.sections[currentSection].wordCount = static_cast<uint32_t>(value);
    }
  } else if (slot != SLOT_UNKNOWN) {
    schemaError(at, "value has the wrong type");
  }
  return true;
}

bool CheckpointParser::readLiteral(uint8_t slot) {
  const uint32_t at = reader.position();
  const int first = reader.peek();
  const char* word = first == 't' ? "true" : (first == 'f' ? "false" : "null");
  for (const char* p = word; *p != '\0'; ++p) {
    if (reader.peek() != *p) {
      syntaxError("invalid literal");
      return false;
    }
    reader.next();
  }
  if (slot != SLOT_UNKNOWN) {
    schemaError(at, "value has the wrong type");
  }
  return true;
}

void CheckpointParser::syntaxError(const char* detail) {
  if (stop != Stop::None) {
    return;
  }
  stopOffset = reader.position();
  if (reader.failed()) {
    stop = Stop::Io;
    stopDetail = "read error";
  } else {
    stop = Stop::Syntax;
    stopDetail = detail;
  }
}

void CheckpointParser::structuralLimit(uint32_t offset, const char* detail) {
  if (stop != Stop::None) {
    return;
  }
  stop = Stop::Limit;
  stopOffset = offset;
  stopDetail = detail;
}

// ---------------------------------------------------------------------------
// Semantics

uint8_t CheckpointParser::containerContext(bool isObject, uint8_t slot, uint32_t offset) {
  const Kind kind = kindOf(slot);
  if (kind == Kind::Any) {
    return CTX_UNKNOWN;
  }
  if (kind != (isObject ? Kind::Object : Kind::Array)) {
    schemaError(offset, "value has the wrong type");
    return CTX_UNKNOWN;
  }
  const uint8_t context = contextOf(slot);
  switch (context) {
    case CTX_ROOT:
      rootIsObject = true;
      break;
    case CTX_SECTION: {
      if (index.sectionTotal >= MAX_SECTIONS) {
        semanticLimit(offset, "more than MAX_SECTIONS sections");
        return CTX_UNKNOWN;
      }
      currentSection = index.sectionTotal++;
      CheckpointIndex::SectionRecord& record = index.sections[currentSection];
      record = {};
      record.chapter = NO_INDEX;
      record.checkpoint = NO_INDEX;
      sectionIdValid = false;
      startAnchorValid = false;
      endAnchorValid = false;
      break;
    }
    case CTX_CHECKPOINT: {
      if (index.checkpointTotal >= MAX_CHECKPOINTS) {
        semanticLimit(offset, "more than MAX_CHECKPOINTS checkpoints");
        return CTX_UNKNOWN;
      }
      currentCheckpoint = index.checkpointTotal++;
      CheckpointIndex::CheckpointRecord& record = index.checkpoints[currentCheckpoint];
      record = {};
      record.section = NO_INDEX;
      record.firstPrompt = index.promptTotal;
      break;
    }
    case CTX_PROMPT: {
      if (currentCheckpoint == NO_INDEX) {
        return CTX_UNKNOWN;
      }
      CheckpointIndex::CheckpointRecord& owner = index.checkpoints[currentCheckpoint];
      if (owner.promptCount >= MAX_PROMPTS_PER_CHECKPOINT) {
        semanticLimit(offset, "more than MAX_PROMPTS_PER_CHECKPOINT prompts in a checkpoint");
        return CTX_UNKNOWN;
      }
      if (index.promptTotal >= MAX_PROMPTS) {
        semanticLimit(offset, "more than MAX_PROMPTS prompts");
        return CTX_UNKNOWN;
      }
      currentPrompt = index.promptTotal++;
      ++owner.promptCount;
      index.prompts[currentPrompt] = {};
      promptTextFlags = 0;
      answerTextFlags = 0;
      break;
    }
    default:
      break;
  }
  return context;
}

void CheckpointParser::closeContext(const ParseFrame& frame) {
  uint8_t count = 0;
  membersOf(frame.context, count);
  if (count > 0) {
    const uint8_t required = static_cast<uint8_t>((1u << count) - 1u);
    if ((frame.seen & required) != required) {
      schemaError(frame.offset, "a required member is missing");
    }
  }
  switch (frame.context) {
    case CTX_SECTIONS:
      if (frame.count == 0) {
        schemaError(frame.offset, "sections is empty");
      }
      break;
    case CTX_PROMPTS:
      if (frame.count == 0) {
        schemaError(frame.offset, "prompts is empty");
      }
      break;
    case CTX_SECTION:
      checkAnchors();
      currentSection = NO_INDEX;
      break;
    case CTX_CHECKPOINT:
      currentCheckpoint = NO_INDEX;
      break;
    case CTX_PROMPT:
      checkPromptText();
      currentPrompt = NO_INDEX;
      break;
    default:
      break;
  }
}

void CheckpointParser::onCodePoint(StringState& s, uint32_t cp) {
  if (isSurrogate(cp) && !loneSurrogate) {
    loneSurrogate = true;
    loneSurrogateOffset = s.offset;
    loneSurrogatePathLength = s.surrogatePathLength;
    copyPath(st.surrogatePath, st.path, s.surrogatePathLength);
  }
  ++s.chars;

  uint8_t bytes[4];
  const uint8_t n = encodeUtf8(cp, bytes);
  for (uint8_t i = 0; i < n; ++i) {
    const uint8_t b = bytes[i];
    s.hash = fnvAdd(s.hash, b);
    if (s.slot == SLOT_FORMAT && (s.bytes >= FORMAT_NAME_LENGTH || b != static_cast<uint8_t>(FORMAT_NAME[s.bytes]))) {
      s.formatOk = false;
    }
    ++s.bytes;
    if (s.isKey) {
      if (b == '~') {
        pathPut('~');
        pathPut('0');
      } else if (b == '/') {
        pathPut('~');
        pathPut('1');
      } else {
        pathPut(static_cast<char>(b));
      }
    }
    if (s.toPool && s.chars <= s.poolCharLimit) {
      const uint32_t at = static_cast<uint32_t>(index.poolUsed) + s.poolLength;
      if (at + 2 > STRING_POOL_BYTES) {
        s.poolOverflow = true;
      } else {
        index.pool[at] = static_cast<char>(b);
        ++s.poolLength;
      }
    }
    if (s.fixedText != nullptr && s.fixedLength < s.fixedCapacity) {
      s.fixedText[s.fixedLength++] = static_cast<char>(b);
    }
    if (s.capture != nullptr) {
      if (s.captureLength < s.captureCapacity) {
        s.capture[s.captureLength++] = static_cast<char>(b);
      } else {
        s.captureOverflow = true;
      }
    }
  }

  if (isIdSlot(s.slot)) {
    const bool alnum = isAsciiLetter(cp) || isAsciiDigit(cp);
    if (s.chars > MAX_ID_CHARS || !(alnum || (s.chars > 1 && (cp == '_' || cp == '-')))) {
      s.idOk = false;
    }
  } else if (s.slot == SLOT_SHA256) {
    if (s.chars > SHA256_HEX_CHARS || !(isAsciiDigit(cp) || (cp >= 'a' && cp <= 'f'))) {
      s.sha256Ok = false;
    }
  }

  if (s.checkText) {
    if (isForbiddenControl(cp)) {
      s.notPlain = true;
    }
    if (s.previousLessThan && (isAsciiLetter(cp) || cp == '/' || cp == '!' || cp == '?')) {
      s.notPlain = true;
    }
    s.previousLessThan = cp == '<';
    if (!isWhiteSpace(cp)) {
      s.allWhiteSpace = false;
    }
    uint8_t next = REF_NONE;
    if (cp == '&') {
      next = REF_AMP;
    } else {
      switch (s.refState) {
        case REF_AMP:
          if (isAsciiLetter(cp)) next = REF_NAME;
          if (cp == '#') next = REF_HASH;
          break;
        case REF_NAME:
          if (isAsciiLetter(cp) || isAsciiDigit(cp)) next = REF_NAME;
          if (cp == ';') s.notPlain = true;
          break;
        case REF_HASH:
          if (isAsciiDigit(cp)) next = REF_DEC;
          if (cp == 'x' || cp == 'X') next = REF_HEX_START;
          break;
        case REF_DEC:
          if (isAsciiDigit(cp)) next = REF_DEC;
          if (cp == ';') s.notPlain = true;
          break;
        case REF_HEX_START:
        case REF_HEX:
          if (isAsciiHexDigit(cp)) next = REF_HEX;
          if (cp == ';' && s.refState == REF_HEX) s.notPlain = true;
          break;
        default:
          break;
      }
    }
    s.refState = next;
  }
}

bool CheckpointParser::commitPoolString(const StringState& s, uint16_t& poolOffset) {
  if (s.poolOverflow) {
    semanticLimit(s.offset, "ids and chapter hrefs exceed STRING_POOL_BYTES");
    return false;
  }
  const uint32_t start = index.poolUsed;
  index.pool[start + s.poolLength] = '\0';
  poolOffset = static_cast<uint16_t>(start);
  index.poolUsed = static_cast<uint16_t>(start + s.poolLength + 1);
  return true;
}

void CheckpointParser::finishString(StringState& s, uint32_t offset, uint32_t rawLength) {
  const bool empty = s.chars == 0;
  const bool validId = s.idOk && !empty && s.chars <= MAX_ID_CHARS;
  switch (s.slot) {
    case SLOT_UNKNOWN:
      return;

    case SLOT_FORMAT:
      formatMatches = s.formatOk && s.bytes == FORMAT_NAME_LENGTH;
      if (!formatMatches) {
        schemaError(offset, "format must be \"rsvp-checkpoints\"");
      }
      return;

    case SLOT_TITLE:
      if (empty) {
        schemaError(offset, "title is empty");
      } else if (s.chars > MAX_TITLE_CHARS) {
        semanticLimit(offset, "title longer than MAX_TITLE_CHARS");
      } else {
        index.titleOffset = offset;
        index.titleLength = static_cast<uint16_t>(rawLength);
      }
      return;

    case SLOT_LANGUAGE:
    case SLOT_INSTRUCTIONS_VERSION:
      if (empty) {
        schemaError(offset, "a book text member is empty");
      } else if (s.chars > s.fixedCharLimit) {
        semanticLimit(offset, s.slot == SLOT_LANGUAGE
                                  ? "language longer than MAX_LANGUAGE_CHARS"
                                  : "instructions_version longer than MAX_INSTRUCTIONS_VERSION_CHARS");
      } else {
        s.fixedText[s.fixedLength] = '\0';
      }
      return;

    case SLOT_SHA256:
      if (s.sha256Ok && s.chars == SHA256_HEX_CHARS) {
        s.fixedText[s.fixedLength] = '\0';
      } else {
        schemaError(offset, "source_sha256 must be 64 lowercase hex digits");
      }
      return;

    case SLOT_SECTION_ID:
      if (!validId) {
        schemaError(offset, "section id has the wrong syntax");
      } else if (currentSection != NO_INDEX) {
        CheckpointIndex::SectionRecord& record = index.sections[currentSection];
        if (commitPoolString(s, record.id)) {
          record.idHash = s.hash;
          record.idOffset = offset;
          sectionIdValid = true;
        }
      }
      return;

    case SLOT_CHAPTER_HREF: {
      if (empty) {
        schemaError(offset, "chapter_href is empty");
        return;
      }
      if (s.chars > MAX_HREF_CHARS) {
        semanticLimit(offset, "chapter_href longer than MAX_HREF_CHARS");
        return;
      }
      if (currentSection == NO_INDEX) {
        return;
      }
      if (s.poolOverflow) {
        semanticLimit(offset, "ids and chapter hrefs exceed STRING_POOL_BYTES");
        return;
      }
      const char* href = index.pool + index.poolUsed;
      index.pool[index.poolUsed + s.poolLength] = '\0';
      for (uint16_t i = 0; i < index.chapterTotal; ++i) {
        if (index.chapters[i].hash == s.hash && strcmp(index.pool + index.chapters[i].href, href) == 0) {
          index.sections[currentSection].chapter = i;
          return;
        }
      }
      if (index.chapterTotal >= MAX_CHAPTERS) {
        semanticLimit(offset, "more than MAX_CHAPTERS distinct chapter hrefs");
        return;
      }
      CheckpointIndex::ChapterRecord& chapter = index.chapters[index.chapterTotal];
      if (commitPoolString(s, chapter.href)) {
        chapter.hash = s.hash;
        index.sections[currentSection].chapter = index.chapterTotal++;
      }
      return;
    }

    case SLOT_START_ANCHOR:
    case SLOT_END_ANCHOR: {
      if (empty) {
        schemaError(offset, "anchor is empty");
        return;
      }
      const bool isStart = s.slot == SLOT_START_ANCHOR;
      (isStart ? startAnchorValid : endAnchorValid) = true;
      (isStart ? startAnchorOverflow : endAnchorOverflow) = s.captureOverflow;
      (isStart ? startAnchorLength : endAnchorLength) = static_cast<uint8_t>(s.captureLength);
      (isStart ? startAnchorOffset : endAnchorOffset) = offset;
      return;
    }

    case SLOT_LABEL:
      if (empty) {
        schemaError(offset, "label is empty");
      } else if (s.chars > MAX_LABEL_CHARS) {
        semanticLimit(offset, "label longer than MAX_LABEL_CHARS");
      } else if (currentSection != NO_INDEX) {
        index.sections[currentSection].labelOffset = offset;
        index.sections[currentSection].labelLength = static_cast<uint16_t>(rawLength);
      }
      return;

    case SLOT_CHECKPOINT_ID:
    case SLOT_CHECKPOINT_SECTION:
      if (!validId) {
        schemaError(offset, "id has the wrong syntax");
      } else if (currentCheckpoint != NO_INDEX) {
        CheckpointIndex::CheckpointRecord& record = index.checkpoints[currentCheckpoint];
        if (s.slot == SLOT_CHECKPOINT_ID) {
          if (commitPoolString(s, record.id)) {
            record.idHash = s.hash;
            record.idOffset = offset;
          }
        } else if (commitPoolString(s, record.sectionRef)) {
          record.sectionHash = s.hash;
          record.sectionOffset = offset;
        }
      }
      return;

    case SLOT_PROMPT_ID:
      if (!validId) {
        schemaError(offset, "prompt id has the wrong syntax");
      } else if (currentPrompt != NO_INDEX) {
        CheckpointIndex::PromptRecord& record = index.prompts[currentPrompt];
        record.idHash = s.hash;
        record.idOffset = offset;
        record.idLength = static_cast<uint16_t>(rawLength);
      }
      return;

    case SLOT_PROMPT_TEXT:
    case SLOT_ANSWER_TEXT: {
      const bool isPrompt = s.slot == SLOT_PROMPT_TEXT;
      if (empty || s.chars > (isPrompt ? MAX_PROMPT_CHARS : MAX_ANSWER_CHARS)) {
        schemaError(offset, isPrompt ? "prompt must have 1 to 300 characters" : "answer must have 1 to 600 characters");
        return;
      }
      if (currentPrompt == NO_INDEX) {
        return;
      }
      CheckpointIndex::PromptRecord& record = index.prompts[currentPrompt];
      const uint8_t flags =
          static_cast<uint8_t>(TEXT_PRESENT | (s.notPlain ? TEXT_NOT_PLAIN : 0) | (s.allWhiteSpace ? TEXT_BLANK : 0));
      if (isPrompt) {
        record.promptOffset = offset;
        record.promptLength = static_cast<uint16_t>(rawLength);
        promptTextFlags = flags;
      } else {
        record.answerOffset = offset;
        record.answerLength = static_cast<uint16_t>(rawLength);
        answerTextFlags = flags;
      }
      return;
    }

    default:
      schemaError(offset, "value has the wrong type");
      return;
  }
}

void CheckpointParser::schemaError(uint32_t offset, const char* detail) {
  // tools/prep sorts schema issues by path; keep the first in that order.
  if (schemaFound && comparePath(st.path, pathLength, st.schemaPath, schemaPathLength) >= 0) {
    return;
  }
  schemaFound = true;
  schemaOffset = offset;
  schemaDetail = detail;
  schemaPathLength = pathLength;
  copyPath(st.schemaPath, st.path, pathLength);
}

void CheckpointParser::semanticLimit(uint32_t offset, const char* detail) {
  if (limitFound) {
    return;
  }
  limitFound = true;
  limitOffset = offset;
  limitDetail = detail;
}

void CheckpointParser::checkAnchors() {
  if (anchorIssueSection != NO_INDEX || currentSection == NO_INDEX || !sectionIdValid || !startAnchorValid ||
      !endAnchorValid) {
    return;
  }
  const char* id = index.pool + index.sections[currentSection].id;
  const size_t idLength = strlen(id);
  if (startAnchorOverflow || !anchorMatches(st.startAnchor, startAnchorLength, id, idLength, "-start")) {
    anchorIssueSection = currentSection;
    anchorIssueIsEnd = false;
    anchorIssueOffset = startAnchorOffset;
  } else if (endAnchorOverflow || !anchorMatches(st.endAnchor, endAnchorLength, id, idLength, "-end")) {
    anchorIssueSection = currentSection;
    anchorIssueIsEnd = true;
    anchorIssueOffset = endAnchorOffset;
  }
}

void CheckpointParser::checkPromptText() {
  if (textIssuePrompt != NO_INDEX || currentPrompt == NO_INDEX) {
    return;
  }
  const CheckpointIndex::PromptRecord& record = index.prompts[currentPrompt];
  const uint8_t order[4][2] = {{0, TEXT_NOT_PLAIN}, {0, TEXT_BLANK}, {1, TEXT_NOT_PLAIN}, {1, TEXT_BLANK}};
  for (const auto& entry : order) {
    const bool isAnswer = entry[0] == 1;
    if (((isAnswer ? answerTextFlags : promptTextFlags) & entry[1]) != 0) {
      textIssuePrompt = currentPrompt;
      textIssueIsAnswer = isAnswer;
      textIssueCode = entry[1] == TEXT_NOT_PLAIN ? IssueCode::NotPlainText : IssueCode::BlankText;
      textIssueOffset = isAnswer ? record.answerOffset : record.promptOffset;
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// Cross-reference rules, in the order validate_cross_references() reports them.

bool CheckpointParser::crossReference(OpenResult& result) {
  auto report = [&](IssueCode code, uint32_t offset, const char* detail) {
    result.verdict = Verdict::Invalid;
    result.code = code;
    result.offset = offset;
    result.detail = detail;
    return true;
  };
  CheckpointIndex::SectionRecord* sections = index.sections;
  CheckpointIndex::CheckpointRecord* checkpoints = index.checkpoints;
  const CheckpointIndex::PromptRecord* prompts = index.prompts;
  const char* pool = index.pool;

  for (uint16_t i = 0; i < index.sectionTotal; ++i) {
    for (uint16_t j = 0; j < i; ++j) {
      if (sections[j].idHash == sections[i].idHash && strcmp(pool + sections[j].id, pool + sections[i].id) == 0) {
        snprintf(result.path, sizeof(result.path), "/sections/%u/id", static_cast<unsigned>(i));
        return report(IssueCode::DuplicateId, sections[i].idOffset, "section id is used more than once");
      }
    }
    if (anchorIssueSection == i) {
      snprintf(result.path, sizeof(result.path), "/sections/%u/%s", static_cast<unsigned>(i),
               anchorIssueIsEnd ? "end_anchor" : "start_anchor");
      return report(IssueCode::AnchorName, anchorIssueOffset, "anchor is not rsvp-<section id>-start/-end");
    }
  }

  for (uint16_t c = 0; c < index.checkpointTotal; ++c) {
    CheckpointIndex::CheckpointRecord& checkpoint = checkpoints[c];
    for (uint16_t d = 0; d < c; ++d) {
      if (checkpoints[d].idHash == checkpoint.idHash && strcmp(pool + checkpoints[d].id, pool + checkpoint.id) == 0) {
        snprintf(result.path, sizeof(result.path), "/checkpoints/%u/id", static_cast<unsigned>(c));
        return report(IssueCode::DuplicateId, checkpoint.idOffset, "checkpoint id is used more than once");
      }
    }
    uint16_t target = NO_INDEX;
    for (uint16_t i = 0; i < index.sectionTotal; ++i) {
      if (sections[i].idHash == checkpoint.sectionHash &&
          strcmp(pool + sections[i].id, pool + checkpoint.sectionRef) == 0) {
        target = i;
        break;
      }
    }
    if (target == NO_INDEX) {
      snprintf(result.path, sizeof(result.path), "/checkpoints/%u/section", static_cast<unsigned>(c));
      return report(IssueCode::UnknownSection, checkpoint.sectionOffset,
                    "checkpoint names a section that does not exist");
    }
    if (sections[target].checkpoint != NO_INDEX) {
      snprintf(result.path, sizeof(result.path), "/checkpoints/%u/section", static_cast<unsigned>(c));
      return report(IssueCode::MultipleCheckpointsForSection, checkpoint.sectionOffset,
                    "section already has a checkpoint");
    }
    sections[target].checkpoint = c;
    checkpoint.section = target;

    for (uint16_t k = 0; k < checkpoint.promptCount; ++k) {
      const uint16_t g = static_cast<uint16_t>(checkpoint.firstPrompt + k);
      for (uint16_t q = 0; q < g; ++q) {
        if (prompts[q].idHash == prompts[g].idHash && sameJsonString(source, prompts[q].idOffset, prompts[q].idLength,
                                                                     prompts[g].idOffset, prompts[g].idLength)) {
          snprintf(result.path, sizeof(result.path), "/checkpoints/%u/prompts/%u/id", static_cast<unsigned>(c),
                   static_cast<unsigned>(k));
          return report(IssueCode::DuplicateId, prompts[g].idOffset, "prompt id is used more than once");
        }
      }
      if (textIssuePrompt == g) {
        snprintf(result.path, sizeof(result.path), "/checkpoints/%u/prompts/%u/%s", static_cast<unsigned>(c),
                 static_cast<unsigned>(k), textIssueIsAnswer ? "answer" : "prompt");
        return report(textIssueCode, textIssueOffset,
                      textIssueCode == IssueCode::NotPlainText ? "control character, markup or character reference"
                                                               : "text is only white space");
      }
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Paths and result

void CheckpointParser::pathPut(char c) {
  if (pathLength < PATH_BUFFER_BYTES - 1) {
    st.path[pathLength] = c;
  }
  ++pathLength;
}

void CheckpointParser::pathPutIndex(uint32_t value) {
  char digits[11];
  const int n = snprintf(digits, sizeof(digits), "%u", static_cast<unsigned>(value));
  for (int i = 0; i < n; ++i) {
    pathPut(digits[i]);
  }
}

void CheckpointParser::copyPath(char* out, const char* from, uint32_t length) const {
  const uint32_t kept = length < PATH_BUFFER_BYTES - 1 ? length : PATH_BUFFER_BYTES - 1;
  memcpy(out, from, kept);
  out[kept] = '\0';
  if (length > kept) {
    memcpy(out + kept - 3, "...", 3);
  }
}

int CheckpointParser::comparePath(const char* a, uint32_t lengthA, const char* b, uint32_t lengthB) const {
  uint32_t n = lengthA < lengthB ? lengthA : lengthB;
  if (n > PATH_BUFFER_BYTES - 1) {
    n = PATH_BUFFER_BYTES - 1;
  }
  const int order = memcmp(a, b, n);
  if (order != 0) {
    return order;
  }
  return lengthA < lengthB ? -1 : (lengthA > lengthB ? 1 : 0);
}

OpenResult CheckpointParser::finish() {
  OpenResult result;
  result.verdict = Verdict::Invalid;
  auto set = [&](IssueCode code, uint32_t offset, const char* detail, const char* path) {
    result.code = code;
    result.offset = offset;
    result.detail = detail;
    snprintf(result.path, sizeof(result.path), "%s", path);
  };

  if (stop == Stop::Io) {
    set(IssueCode::Io, stopOffset, stopDetail, "");
  } else if (stop == Stop::Limit) {
    set(IssueCode::Limit, stopOffset, stopDetail, "");
  } else if (stop == Stop::Syntax) {
    set(IssueCode::Parse, stopOffset, stopDetail, "");
  } else if (duplicateKey) {
    set(IssueCode::DuplicateKey, duplicateKeyOffset, "an object repeats a member name", "");
  } else if (loneSurrogate) {
    set(IssueCode::Parse, loneSurrogateOffset, "a string contains an unpaired surrogate escape", st.surrogatePath);
  } else if (rootIsObject && formatMatches && versionAboveOne) {
    result.verdict = Verdict::UnsupportedVersion;
    set(IssueCode::UnsupportedVersion, versionOffset, "version is newer than 1", "/version");
  } else if (limitFound) {
    set(IssueCode::Limit, limitOffset, limitDetail, "");
  } else if (schemaFound) {
    set(IssueCode::Schema, schemaOffset, schemaDetail, st.schemaPath);
  } else if (!crossReference(result)) {
    result.verdict = Verdict::Valid;
    set(IssueCode::None, 0, "", "");
  }
  return result;
}

}  // namespace checkpoint::detail
