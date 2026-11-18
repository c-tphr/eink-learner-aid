#include "StateCodec.h"

#include <cstring>

#include "JsonText.h"

namespace checkpoint {

bool BufferStateSink::write(const char* data, size_t length) {
  if (overflow || length > capacity - used) {
    overflow = true;
    return false;
  }
  memcpy(buffer + used, data, length);
  used += length;
  return true;
}

const char* decodeStatusName(DecodeStatus status) {
  switch (status) {
    case DecodeStatus::Ok:
      return "ok";
    case DecodeStatus::TooLarge:
      return "too-large";
    case DecodeStatus::Syntax:
      return "syntax";
    case DecodeStatus::UnsupportedVersion:
      return "unsupported-version";
    case DecodeStatus::Limit:
      return "limit";
    case DecodeStatus::Schema:
      return "schema";
  }
  return "";
}

// ---------------------------------------------------------------------------
// Encoding

namespace {

size_t textLength(const char* text, size_t limit) {
  size_t n = 0;
  while (n < limit && text[n] != '\0') {
    ++n;
  }
  return n;
}

class Writer {
 public:
  explicit Writer(StateSink& target) : sink(target) {}

  void put(const char* text) { putN(text, strlen(text)); }
  void putN(const char* text, size_t length) {
    if (ok && length > 0) {
      ok = sink.write(text, length);
    }
  }
  void putUInt(uint32_t value) {
    char digits[10];
    size_t n = 0;
    do {
      digits[n++] = static_cast<char>('0' + value % 10);
      value /= 10;
    } while (value != 0);
    char out[10];
    for (size_t i = 0; i < n; ++i) {
      out[i] = digits[n - 1 - i];
    }
    putN(out, n);
  }
  // Values written by this encoder are ids, hex and fixed words: no escaping needed.
  void putString(const char* text, size_t length) {
    put("\"");
    putN(text, length);
    put("\"");
  }
  void putPlace(const Place& place, bool modeFirst) {
    put("{ ");
    if (modeFirst) {
      put("\"mode\": ");
      putString(readingModeName(place.mode), 4);
      put(", ");
    }
    put("\"chapter\": ");
    putUInt(place.position.chapter);
    put(", \"word\": ");
    putUInt(place.position.word);
    if (!modeFirst) {
      put(", \"mode\": ");
      putString(readingModeName(place.mode), 4);
    }
    put(" }");
  }

  bool ok = true;

 private:
  StateSink& sink;
};

}  // namespace

bool encodeState(const BookState& state, StateSink& sink) {
  Writer w(sink);
  const size_t shaLength = textLength(state.bookSha256, SHA256_HEX_CHARS);
  const bool writeGrades = shaLength > 0 || state.grades.count() > 0;

  w.put("{\n  \"version\": ");
  w.putUInt(STATE_VERSION);
  if (shaLength > 0) {
    w.put(",\n  \"book_sha256\": ");
    w.putString(state.bookSha256, shaLength);
  }
  w.put(",\n  \"overrides\": {");
  const Overrides& o = state.overrides;
  if (o.hasWpm || o.hasWordsPerUpdate) {
    w.put(" ");
    if (o.hasWpm) {
      w.put("\"wpm\": ");
      w.putUInt(o.wpm);
    }
    if (o.hasWordsPerUpdate) {
      w.put(o.hasWpm ? ", \"words_per_update\": " : "\"words_per_update\": ");
      w.putUInt(o.wordsPerUpdate);
    }
    w.put(" ");
  }
  w.put("}");
  if (state.hasPosition) {
    w.put(",\n  \"position\": ");
    w.putPlace(state.position, false);
  }
  if (writeGrades) {
    const GradeTable& g = state.grades;
    w.put(",\n  \"grades\": {");
    for (uint16_t i = 0; i < g.count(); ++i) {
      w.put(i == 0 ? "\n    " : ",\n    ");
      w.putString(g.idData(i), g.idLength(i));
      w.put(": { \"first\": ");
      w.putString(gradeName(g.grade(i)), strlen(gradeName(g.grade(i))));
      char at[TIMESTAMP_CHARS + 1];
      if (formatTimestamp(g.at(i), at)) {
        w.put(", \"at\": ");
        w.putString(at, TIMESTAMP_CHARS);
      }
      w.put(" }");
    }
    w.put(g.count() > 0 ? "\n  }" : "}");
  }
  if (state.hasActive) {
    const ActiveCheckpoint& a = state.active;
    w.put(",\n  \"active_checkpoint\": {\n    \"id\": ");
    w.putString(a.id, textLength(a.id, MAX_ID_CHARS));
    w.put(",\n    \"prompt_index\": ");
    w.putUInt(a.promptIndex);
    w.put(",\n    \"resume\": ");
    w.putPlace(a.resume, false);
    if (a.rereading) {
      w.put(",\n    \"reread\": ");
      w.putPlace(a.reread, true);
    }
    w.put("\n  }");
  }
  w.put("\n}\n");
  return w.ok;
}

// ---------------------------------------------------------------------------
// Decoding

namespace {

struct Text {
  char data[ID_BUFFER_BYTES];
  size_t length;
  bool overflow;

  bool is(const char* name) const {
    const size_t n = strlen(name);
    return !overflow && length == n && memcmp(data, name, n) == 0;
  }
};

// Member bits, one per known member of the object being read.
enum : uint8_t { BIT0 = 1, BIT1 = 2, BIT2 = 4, BIT3 = 8, BIT4 = 16, BIT5 = 32 };

class Decoder {
 public:
  Decoder(const uint8_t* data, size_t size, BookState& target) : p(data), n(size), out(target) {}

  DecodeResult run(const char* openedSha);

 private:
  // Scanner.
  int peek() const { return pos < n ? p[pos] : -1; }
  void ws() {
    while (pos < n && (p[pos] == ' ' || p[pos] == '\t' || p[pos] == '\n' || p[pos] == '\r')) {
      ++pos;
    }
  }
  bool syntax(const char* detail) {
    if (!stopped) {
      stopped = true;
      stopStatus = DecodeStatus::Syntax;
      stopOffset = pos;
      stopDetail = detail;
    }
    return false;
  }
  bool depthLimit() {
    if (!stopped) {
      stopped = true;
      stopStatus = DecodeStatus::Limit;
      stopOffset = pos;
      stopDetail = "nesting deeper than MAX_DEPTH";
    }
    return false;
  }
  void schema(size_t offset, const char* detail) {
    if (!schemaFound) {
      schemaFound = true;
      schemaOffset = offset;
      schemaDetail = detail;
    }
  }
  bool readString(Text* text);
  bool readNumber(uint32_t& value, bool& plain);
  bool readLiteral();
  bool skipValue(uint8_t depth);
  // Advances to the next member of an open object; `done` at '}'.
  bool nextMember(bool& first, Text& key, bool& done);
  bool openObject(uint8_t depth, bool& isObject);
  bool markSeen(uint8_t& seen, uint8_t bit, size_t offset);

  // Values of known members. Each consumes the value even when it is wrong.
  // `depth` is the depth the value would have if it were a container.
  bool readUInt(uint8_t depth, uint32_t max, uint32_t& value, bool& valid);
  bool readFixedString(uint8_t depth, Text& text, bool& isString);

  bool parseRoot();
  bool parseOverrides(uint8_t depth);
  bool parsePlace(uint8_t depth, Place& place, bool& valid);
  bool parseGrades(uint8_t depth);
  bool parseGrade(uint8_t depth, const Text& id, size_t idOffset);
  bool parseActive(uint8_t depth);

  const uint8_t* p;
  size_t n;
  size_t pos = 0;
  BookState& out;

  bool stopped = false;
  DecodeStatus stopStatus = DecodeStatus::Syntax;
  size_t stopOffset = 0;
  const char* stopDetail = "";

  bool rootIsObject = false;
  bool versionSeen = false;
  bool versionInteger = false;
  uint32_t version = 0;
  size_t versionOffset = 0;

  bool schemaFound = false;
  size_t schemaOffset = 0;
  const char* schemaDetail = "";

  bool limitFound = false;
  size_t limitOffset = 0;
};

bool Decoder::readString(Text* text) {
  if (text != nullptr) {
    text->length = 0;
    text->overflow = false;
  }
  if (peek() != '"') {
    return syntax("expected a string");
  }
  ++pos;
  detail::JsonStringDecoder decoder;
  decoder.reset();
  while (pos < n) {
    uint32_t cps[2];
    uint8_t count = 0;
    const detail::JsonStringDecoder::Status status = decoder.feed(p[pos], cps, count);
    if (status == detail::JsonStringDecoder::Status::Error) {
      return syntax("bad string: control character, escape or UTF-8");
    }
    ++pos;
    for (uint8_t i = 0; i < count; ++i) {
      if (detail::isSurrogate(cps[i])) {
        return syntax("lone surrogate escape");
      }
      if (text == nullptr) {
        continue;
      }
      uint8_t bytes[4];
      const uint8_t len = detail::encodeUtf8(cps[i], bytes);
      if (text->length + len >= sizeof(text->data)) {
        text->overflow = true;
        continue;
      }
      memcpy(text->data + text->length, bytes, len);
      text->length += len;
    }
    if (status == detail::JsonStringDecoder::Status::End) {
      if (text != nullptr) {
        text->data[text->length] = '\0';
      }
      return true;
    }
  }
  return syntax("unterminated string");
}

// Validates a JSON number. `plain` is true, and `value` holds the number, when
// it is a non-negative integer without fraction or exponent up to UINT32_MAX.
bool Decoder::readNumber(uint32_t& value, bool& plain) {
  uint64_t v = 0;
  plain = true;
  if (peek() == '-') {
    plain = false;
    ++pos;
  }
  if (peek() == '0') {
    ++pos;
  } else if (peek() >= '1' && peek() <= '9') {
    while (peek() >= '0' && peek() <= '9') {
      if (v <= 0xFFFFFFFFu) {
        v = v * 10 + static_cast<uint32_t>(peek() - '0');
      }
      ++pos;
    }
  } else {
    return syntax("bad number");
  }
  if (peek() == '.') {
    plain = false;
    ++pos;
    if (!(peek() >= '0' && peek() <= '9')) {
      return syntax("bad number fraction");
    }
    while (peek() >= '0' && peek() <= '9') {
      ++pos;
    }
  }
  if (peek() == 'e' || peek() == 'E') {
    plain = false;
    ++pos;
    if (peek() == '+' || peek() == '-') {
      ++pos;
    }
    if (!(peek() >= '0' && peek() <= '9')) {
      return syntax("bad number exponent");
    }
    while (peek() >= '0' && peek() <= '9') {
      ++pos;
    }
  }
  plain = plain && v <= 0xFFFFFFFFu;
  value = plain ? static_cast<uint32_t>(v) : 0;
  return true;
}

bool Decoder::readLiteral() {
  static const char* const LITERALS[] = {"true", "false", "null"};
  for (const char* literal : LITERALS) {
    const size_t len = strlen(literal);
    if (n - pos >= len && memcmp(p + pos, literal, len) == 0) {
      pos += len;
      return true;
    }
  }
  return syntax("unexpected character");
}

// Skips one value of any type without recursion. `depth` is the depth a
// container value would have.
bool Decoder::skipValue(uint8_t depth) {
  bool isObject[MAX_DEPTH] = {};
  int top = 0;  // open containers
  bool wantValue = true;
  while (true) {
    ws();
    if (wantValue) {
      const int c = peek();
      if (c == '{' || c == '[') {
        if (depth + top > MAX_DEPTH) {
          return depthLimit();
        }
        isObject[top++] = c == '{';
        ++pos;
        ws();
        if (peek() == (c == '{' ? '}' : ']')) {
          ++pos;
          --top;
          wantValue = false;
        } else if (c == '{') {
          if (!readString(nullptr)) return false;
          ws();
          if (peek() != ':') return syntax("expected ':'");
          ++pos;
        }
        continue;
      }
      if (c == '"') {
        if (!readString(nullptr)) return false;
      } else if (c == '-' || (c >= '0' && c <= '9')) {
        uint32_t ignored;
        bool plain;
        if (!readNumber(ignored, plain)) return false;
      } else if (!readLiteral()) {
        return false;
      }
      wantValue = false;
      continue;
    }
    if (top == 0) {
      return true;
    }
    const int c = peek();
    if (c == ',') {
      ++pos;
      wantValue = true;
      if (isObject[top - 1]) {
        ws();
        if (!readString(nullptr)) return false;
        ws();
        if (peek() != ':') return syntax("expected ':'");
        ++pos;
      }
    } else if (c == (isObject[top - 1] ? '}' : ']')) {
      ++pos;
      --top;
    } else {
      return syntax("expected ',' or the end of a container");
    }
  }
}

bool Decoder::openObject(uint8_t depth, bool& isObject) {
  ws();
  isObject = peek() == '{';
  if (!isObject) {
    return true;
  }
  if (depth > MAX_DEPTH) {
    return depthLimit();
  }
  ++pos;
  return true;
}

bool Decoder::nextMember(bool& first, Text& key, bool& done) {
  ws();
  done = false;
  if (peek() == '}') {
    ++pos;
    done = true;
    return true;
  }
  if (!first) {
    if (peek() != ',') {
      return syntax("expected ',' or '}'");
    }
    ++pos;
    ws();
  }
  first = false;
  if (!readString(&key)) {
    return false;
  }
  ws();
  if (peek() != ':') {
    return syntax("expected ':'");
  }
  ++pos;
  ws();
  return true;
}

bool Decoder::markSeen(uint8_t& seen, uint8_t bit, size_t offset) {
  if ((seen & bit) != 0) {
    schema(offset, "repeated member");
    return false;
  }
  seen = static_cast<uint8_t>(seen | bit);
  return true;
}

bool Decoder::readUInt(uint8_t depth, uint32_t max, uint32_t& value, bool& valid) {
  valid = false;
  value = 0;
  const size_t at = pos;
  const int c = peek();
  if (c == '-' || (c >= '0' && c <= '9')) {
    bool plain;
    if (!readNumber(value, plain)) return false;
    valid = plain && value <= max;
    if (!valid) {
      schema(at, "not an integer in range");
    }
    return true;
  }
  schema(at, "not an integer");
  return skipValue(depth);
}

bool Decoder::readFixedString(uint8_t depth, Text& text, bool& isString) {
  isString = peek() == '"';
  if (!isString) {
    schema(pos, "not a string");
    text.length = 0;
    text.overflow = false;
    text.data[0] = '\0';
    return skipValue(depth);
  }
  return readString(&text);
}

bool Decoder::parsePlace(uint8_t depth, Place& place, bool& valid) {
  const size_t at = pos;
  bool isObject;
  valid = false;
  if (!openObject(depth, isObject)) return false;
  if (!isObject) {
    schema(at, "not an object");
    return skipValue(depth);
  }
  uint8_t seen = 0;
  bool good = true;
  bool first = true;
  Text key;
  while (true) {
    bool done;
    if (!nextMember(first, key, done)) return false;
    if (done) break;
    const size_t valueAt = pos;
    bool ok = true;
    if (key.is("chapter")) {
      uint32_t v;
      if (!readUInt(static_cast<uint8_t>(depth + 1), NO_CHAPTER - 1u, v, ok)) return false;
      place.position.chapter = static_cast<uint16_t>(v);
      ok = markSeen(seen, BIT0, valueAt) && ok;
    } else if (key.is("word")) {
      uint32_t v;
      if (!readUInt(static_cast<uint8_t>(depth + 1), 0xFFFFFFFFu, v, ok)) return false;
      place.position.word = v;
      ok = markSeen(seen, BIT1, valueAt) && ok;
    } else if (key.is("mode")) {
      Text mode;
      bool isString;
      if (!readFixedString(static_cast<uint8_t>(depth + 1), mode, isString)) return false;
      ok = isString && (mode.is("rsvp") || mode.is("page"));
      if (isString && !ok) schema(valueAt, "mode is not \"rsvp\" or \"page\"");
      place.mode = mode.is("rsvp") ? ReadingMode::Rsvp : ReadingMode::Page;
      ok = markSeen(seen, BIT2, valueAt) && ok;
    } else if (!skipValue(static_cast<uint8_t>(depth + 1))) {
      return false;
    }
    good = good && ok;
  }
  if (seen != (BIT0 | BIT1 | BIT2)) {
    schema(at, "chapter, word or mode missing");
    good = false;
  }
  valid = good;
  return true;
}

bool Decoder::parseOverrides(uint8_t depth) {
  const size_t at = pos;
  bool isObject;
  if (!openObject(depth, isObject)) return false;
  if (!isObject) {
    schema(at, "overrides is not an object");
    return skipValue(depth);
  }
  uint8_t seen = 0;
  bool first = true;
  Text key;
  while (true) {
    bool done;
    if (!nextMember(first, key, done)) return false;
    if (done) break;
    const size_t valueAt = pos;
    bool ok;
    uint32_t v;
    if (key.is("wpm")) {
      if (!readUInt(static_cast<uint8_t>(depth + 1), 0xFFFF, v, ok)) return false;
      if (ok && v == 0) schema(valueAt, "wpm is 0");
      if (markSeen(seen, BIT0, valueAt) && ok && v > 0) {
        out.overrides.hasWpm = true;
        out.overrides.wpm = static_cast<uint16_t>(v);
      }
    } else if (key.is("words_per_update")) {
      if (!readUInt(static_cast<uint8_t>(depth + 1), 0xFF, v, ok)) return false;
      if (ok && v == 0) schema(valueAt, "words_per_update is 0");
      if (markSeen(seen, BIT1, valueAt) && ok && v > 0) {
        out.overrides.hasWordsPerUpdate = true;
        out.overrides.wordsPerUpdate = static_cast<uint8_t>(v);
      }
    } else if (!skipValue(static_cast<uint8_t>(depth + 1))) {
      return false;
    }
  }
  return true;
}

bool Decoder::parseGrade(uint8_t depth, const Text& id, size_t idOffset) {
  const size_t at = pos;
  bool isObject;
  if (!openObject(depth, isObject)) return false;
  if (!isObject) {
    schema(at, "grade is not an object");
    return skipValue(depth);
  }
  uint8_t seen = 0;
  bool good = true;
  bool first = true;
  Grade grade = Grade::Ungraded;
  Timestamp when = NO_TIMESTAMP;
  Text key;
  while (true) {
    bool done;
    if (!nextMember(first, key, done)) return false;
    if (done) break;
    const size_t valueAt = pos;
    if (key.is("first") || key.is("at")) {
      const bool isFirst = key.is("first");
      Text value;
      bool isString;
      if (!readFixedString(static_cast<uint8_t>(depth + 1), value, isString)) return false;
      bool ok = isString;
      if (isFirst) {
        grade = value.is("correct") ? Grade::Correct : value.is("incorrect") ? Grade::Incorrect : Grade::Ungraded;
        ok = ok && grade != Grade::Ungraded;
        if (isString && !ok) schema(valueAt, "first is not \"correct\" or \"incorrect\"");
      } else {
        ok = ok && !value.overflow && parseTimestamp(value.data, value.length, when);
        if (isString && !ok) schema(valueAt, "at is not YYYY-MM-DDTHH:MM:SS");
      }
      ok = markSeen(seen, isFirst ? BIT0 : BIT1, valueAt) && ok;
      good = good && ok;
    } else if (!skipValue(static_cast<uint8_t>(depth + 1))) {
      return false;
    }
  }
  if ((seen & BIT0) == 0) {
    schema(at, "first missing");
    good = false;
  }
  if (!good) {
    return true;
  }
  switch (out.grades.recordFirst(id.data, id.length, grade, when)) {
    case GradeTable::RecordResult::Recorded:
      break;
    case GradeTable::RecordResult::AlreadyGraded:
      schema(idOffset, "prompt id graded twice");
      break;
    case GradeTable::RecordResult::Full:
      if (!limitFound) {
        limitFound = true;
        limitOffset = idOffset;
      }
      break;
    case GradeTable::RecordResult::BadId:
    case GradeTable::RecordResult::BadGrade:
      schema(idOffset, "bad grade");
      break;
  }
  return true;
}

bool Decoder::parseGrades(uint8_t depth) {
  const size_t at = pos;
  bool isObject;
  if (!openObject(depth, isObject)) return false;
  if (!isObject) {
    schema(at, "grades is not an object");
    return skipValue(depth);
  }
  bool first = true;
  Text key;
  while (true) {
    const size_t keyAt = pos;
    bool done;
    if (!nextMember(first, key, done)) return false;
    if (done) break;
    if (key.overflow || !isValidId(key.data, key.length)) {
      schema(keyAt, "grade key is not a prompt id");
      if (!skipValue(static_cast<uint8_t>(depth + 1))) return false;
      continue;
    }
    if (!parseGrade(static_cast<uint8_t>(depth + 1), key, keyAt)) return false;
  }
  return true;
}

bool Decoder::parseActive(uint8_t depth) {
  const size_t at = pos;
  bool isObject;
  if (!openObject(depth, isObject)) return false;
  if (!isObject) {
    schema(at, "active_checkpoint is not an object");
    return skipValue(depth);
  }
  ActiveCheckpoint& a = out.active;
  uint8_t seen = 0;
  bool good = true;
  bool first = true;
  Text key;
  while (true) {
    bool done;
    if (!nextMember(first, key, done)) return false;
    if (done) break;
    const size_t valueAt = pos;
    bool ok = true;
    if (key.is("id")) {
      Text id;
      bool isString;
      if (!readFixedString(static_cast<uint8_t>(depth + 1), id, isString)) return false;
      ok = isString && !id.overflow && isValidId(id.data, id.length);
      if (isString && !ok) schema(valueAt, "id is not a checkpoint id");
      if (ok) memcpy(a.id, id.data, id.length + 1);
      ok = markSeen(seen, BIT0, valueAt) && ok;
    } else if (key.is("prompt_index")) {
      uint32_t v;
      if (!readUInt(static_cast<uint8_t>(depth + 1), MAX_PROMPTS_PER_CHECKPOINT - 1u, v, ok)) return false;
      a.promptIndex = static_cast<uint16_t>(v);
      ok = markSeen(seen, BIT1, valueAt) && ok;
    } else if (key.is("resume")) {
      if (!parsePlace(static_cast<uint8_t>(depth + 1), a.resume, ok)) return false;
      ok = markSeen(seen, BIT2, valueAt) && ok;
    } else if (key.is("reread")) {
      if (!parsePlace(static_cast<uint8_t>(depth + 1), a.reread, ok)) return false;
      a.rereading = ok;
      ok = markSeen(seen, BIT3, valueAt) && ok;
    } else if (!skipValue(static_cast<uint8_t>(depth + 1))) {
      return false;
    }
    good = good && ok;
  }
  if ((seen & (BIT0 | BIT1 | BIT2)) != (BIT0 | BIT1 | BIT2)) {
    schema(at, "id, prompt_index or resume missing");
    good = false;
  }
  out.hasActive = good;
  return true;
}

bool Decoder::parseRoot() {
  ws();
  bool isObject;
  if (!openObject(1, isObject)) return false;
  if (!isObject) {
    schema(pos, "top level is not an object");
    return skipValue(1);
  }
  rootIsObject = true;
  uint8_t seen = 0;
  bool first = true;
  Text key;
  while (true) {
    bool done;
    if (!nextMember(first, key, done)) return false;
    if (done) break;
    const size_t valueAt = pos;
    if (key.is("version")) {
      const int c = peek();
      if (c == '-' || (c >= '0' && c <= '9')) {
        uint32_t v;
        bool plain;
        if (!readNumber(v, plain)) return false;
        if (!versionSeen) {
          versionInteger = plain;
          version = v;
          versionOffset = valueAt;
        }
      } else if (!skipValue(2)) {
        return false;
      }
      if (versionSeen) schema(valueAt, "repeated member");
      versionSeen = true;
    } else if (key.is("book_sha256")) {
      Text sha;
      bool isString;
      if (!readFixedString(2, sha, isString)) return false;
      const bool ok = isString && !sha.overflow && isSha256Hex(sha.data, sha.length);
      if (isString && !ok) schema(valueAt, "book_sha256 is not 64 lowercase hex digits");
      if (markSeen(seen, BIT0, valueAt) && ok) memcpy(out.bookSha256, sha.data, SHA256_HEX_CHARS + 1);
    } else if (key.is("overrides")) {
      if (markSeen(seen, BIT1, valueAt)) {
        if (!parseOverrides(2)) return false;
      } else if (!skipValue(2)) {
        return false;
      }
    } else if (key.is("position")) {
      Place place;
      bool ok;
      if (!parsePlace(2, place, ok)) return false;
      if (markSeen(seen, BIT2, valueAt) && ok) {
        out.hasPosition = true;
        out.position = place;
      }
    } else if (key.is("grades")) {
      if (markSeen(seen, BIT3, valueAt)) {
        if (!parseGrades(2)) return false;
      } else if (!skipValue(2)) {
        return false;
      }
    } else if (key.is("active_checkpoint")) {
      if (markSeen(seen, BIT4, valueAt)) {
        if (!parseActive(2)) return false;
      } else if (!skipValue(2)) {
        return false;
      }
    } else if (!skipValue(2)) {
      return false;
    }
  }
  return true;
}

DecodeResult Decoder::run(const char* openedSha) {
  out.reset();
  DecodeResult result;
  if (n > STATE_DECODE_MAX_BYTES) {
    result.status = DecodeStatus::TooLarge;
    result.detail = "file larger than STATE_DECODE_MAX_BYTES";
    return result;
  }
  if (parseRoot()) {
    ws();
    if (pos != n) {
      syntax("data after the top-level value");
    }
  }
  if (stopped) {
    result.status = stopStatus;
    result.offset = static_cast<uint32_t>(stopOffset);
    result.detail = stopDetail;
  } else if (rootIsObject && versionSeen && versionInteger && version > STATE_VERSION) {
    result.status = DecodeStatus::UnsupportedVersion;
    result.offset = static_cast<uint32_t>(versionOffset);
    result.detail = "newer state file version";
  } else if (rootIsObject && (!versionSeen || !versionInteger || version != STATE_VERSION)) {
    result.status = DecodeStatus::Schema;
    result.offset = static_cast<uint32_t>(versionSeen ? versionOffset : 0);
    result.detail = "version missing or not 1";
  } else if (limitFound) {
    result.status = DecodeStatus::Limit;
    result.offset = static_cast<uint32_t>(limitOffset);
    result.detail = "more grades than GRADE_CAPACITY";
  } else if (schemaFound) {
    result.status = DecodeStatus::Schema;
    result.offset = static_cast<uint32_t>(schemaOffset);
    result.detail = schemaDetail;
  } else {
    result.status = DecodeStatus::Ok;
    result.shaMismatch = openedSha != nullptr && openedSha[0] != '\0' && out.bookSha256[0] != '\0' &&
                         strncmp(openedSha, out.bookSha256, SHA256_HEX_CHARS + 1) != 0;
  }
  if (result.status != DecodeStatus::Ok) {
    out.reset();
  }
  return result;
}

}  // namespace

DecodeResult decodeState(const uint8_t* data, size_t size, const char* openedBookSha256, BookState& out) {
  if (data == nullptr) {
    size = 0;
  }
  Decoder decoder(data, size, out);
  return decoder.run(openedBookSha256);
}

}  // namespace checkpoint
