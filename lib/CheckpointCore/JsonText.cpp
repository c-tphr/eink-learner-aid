#include "JsonText.h"

namespace checkpoint::detail {

namespace {

int hexDigit(uint8_t c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool isHighSurrogate(uint32_t cp) { return cp >= 0xD800 && cp <= 0xDBFF; }
bool isLowSurrogate(uint32_t cp) { return cp >= 0xDC00 && cp <= 0xDFFF; }

}  // namespace

uint32_t fnvBytes(const char* data, size_t length) {
  uint32_t hash = FNV_OFFSET_BASIS;
  for (size_t i = 0; i < length; ++i) {
    hash = fnvAdd(hash, static_cast<uint8_t>(data[i]));
  }
  return hash;
}

uint8_t encodeUtf8(uint32_t cp, uint8_t out[4]) {
  if (cp < 0x80) {
    out[0] = static_cast<uint8_t>(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<uint8_t>(0xC0 | (cp >> 6));
    out[1] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = static_cast<uint8_t>(0xE0 | (cp >> 12));
    out[1] = static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = static_cast<uint8_t>(0xF0 | ((cp >> 18) & 0x07));
  out[1] = static_cast<uint8_t>(0x80 | ((cp >> 12) & 0x3F));
  out[2] = static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F));
  out[3] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
  return 4;
}

// ---------------------------------------------------------------------------
// JsonStringDecoder

void JsonStringDecoder::reset() {
  state = State::Normal;
  continuationLeft = 0;
  hexLeft = 0;
  value = 0;
  pendingHigh = 0;
}

JsonStringDecoder::Status JsonStringDecoder::feedNormal(uint8_t byte, uint32_t out[2], uint8_t& count) {
  if (byte == '"') {
    return Status::End;
  }
  if (byte == '\\') {
    state = State::Escape;
    return Status::More;
  }
  if (byte < 0x20) {
    return Status::Error;
  }
  if (byte < 0x80) {
    out[count++] = byte;
    return Status::More;
  }
  // Well-formed UTF-8 sequences, Unicode table 3-7.
  continuationLow = 0x80;
  continuationHigh = 0xBF;
  if (byte >= 0xC2 && byte <= 0xDF) {
    continuationLeft = 1;
    value = byte & 0x1F;
  } else if (byte >= 0xE0 && byte <= 0xEF) {
    continuationLeft = 2;
    value = byte & 0x0F;
    if (byte == 0xE0) continuationLow = 0xA0;
    if (byte == 0xED) continuationHigh = 0x9F;
  } else if (byte >= 0xF0 && byte <= 0xF4) {
    continuationLeft = 3;
    value = byte & 0x07;
    if (byte == 0xF0) continuationLow = 0x90;
    if (byte == 0xF4) continuationHigh = 0x8F;
  } else {
    return Status::Error;
  }
  state = State::Utf8;
  return Status::More;
}

JsonStringDecoder::Status JsonStringDecoder::feedEscape(uint8_t byte, uint32_t out[2], uint8_t& count) {
  state = State::Normal;
  switch (byte) {
    case '"':
    case '\\':
    case '/':
      out[count++] = byte;
      return Status::More;
    case 'b':
      out[count++] = 0x08;
      return Status::More;
    case 'f':
      out[count++] = 0x0C;
      return Status::More;
    case 'n':
      out[count++] = 0x0A;
      return Status::More;
    case 'r':
      out[count++] = 0x0D;
      return Status::More;
    case 't':
      out[count++] = 0x09;
      return Status::More;
    case 'u':
      state = State::Hex;
      hexLeft = 4;
      value = 0;
      return Status::More;
    default:
      return Status::Error;
  }
}

JsonStringDecoder::Status JsonStringDecoder::feed(uint8_t byte, uint32_t out[2], uint8_t& count) {
  count = 0;
  switch (state) {
    case State::Normal:
      return feedNormal(byte, out, count);

    case State::Utf8:
      if (byte < continuationLow || byte > continuationHigh) {
        return Status::Error;
      }
      value = (value << 6) | (byte & 0x3F);
      continuationLow = 0x80;
      continuationHigh = 0xBF;
      if (--continuationLeft == 0) {
        out[count++] = value;
        state = State::Normal;
      }
      return Status::More;

    case State::Escape:
      return feedEscape(byte, out, count);

    case State::Hex: {
      const int digit = hexDigit(byte);
      if (digit < 0) {
        return Status::Error;
      }
      value = (value << 4) | static_cast<uint32_t>(digit);
      if (--hexLeft > 0) {
        return Status::More;
      }
      state = State::Normal;
      if (pendingHigh != 0) {
        if (isLowSurrogate(value)) {
          out[count++] = 0x10000 + ((static_cast<uint32_t>(pendingHigh) - 0xD800) << 10) + (value - 0xDC00);
          pendingHigh = 0;
          return Status::More;
        }
        out[count++] = pendingHigh;
        pendingHigh = 0;
      }
      if (isHighSurrogate(value)) {
        pendingHigh = static_cast<uint16_t>(value);
        state = State::AfterHigh;
      } else {
        out[count++] = value;
      }
      return Status::More;
    }

    case State::AfterHigh:
      if (byte == '\\') {
        state = State::AfterHighBackslash;
        return Status::More;
      }
      out[count++] = pendingHigh;
      pendingHigh = 0;
      state = State::Normal;
      return feedNormal(byte, out, count);

    case State::AfterHighBackslash:
      if (byte == 'u') {
        state = State::Hex;
        hexLeft = 4;
        value = 0;
        return Status::More;
      }
      out[count++] = pendingHigh;
      pendingHigh = 0;
      return feedEscape(byte, out, count);
  }
  return Status::Error;
}

// ---------------------------------------------------------------------------
// SourceReader

void SourceReader::begin(CheckpointSource& src, uint32_t start, uint32_t stop) {
  source = &src;
  pos = start;
  end = stop;
  bufferStart = start;
  bufferLength = 0;
  readFailed = false;
}

bool SourceReader::fill() {
  if (readFailed || source == nullptr || pos >= end) {
    return false;
  }
  const uint32_t remaining = end - pos;
  const size_t want = remaining < capacity ? remaining : capacity;
  const size_t got = source->read(pos, buffer, want);
  if (got == 0 || got > want) {
    readFailed = true;
    bufferLength = 0;
    return false;
  }
  bufferStart = pos;
  bufferLength = static_cast<uint16_t>(got);
  return true;
}

// ---------------------------------------------------------------------------
// StringCursor

void StringCursor::begin(CheckpointSource& source, uint32_t offset, uint32_t rawLength) {
  const uint32_t size = source.size();
  uint32_t stop = offset + rawLength;
  if (stop < offset || stop > size) {
    stop = size;
  }
  reader.begin(source, offset, stop);
  decoder.reset();
  pendingCount = 0;
  pendingIndex = 0;
  ended = false;
  failed = reader.next() != '"';
}

int32_t StringCursor::next() {
  while (true) {
    if (pendingIndex < pendingCount) {
      return static_cast<int32_t>(pending[pendingIndex++]);
    }
    if (failed) {
      return FAILED;
    }
    if (ended) {
      return END;
    }
    const int c = reader.next();
    if (c < 0) {
      failed = true;
      continue;
    }
    pendingIndex = 0;
    const JsonStringDecoder::Status status = decoder.feed(static_cast<uint8_t>(c), pending, pendingCount);
    if (status == JsonStringDecoder::Status::Error) {
      pendingCount = 0;
      failed = true;
    } else if (status == JsonStringDecoder::Status::End) {
      ended = true;
    }
  }
}

bool sameJsonString(CheckpointSource& source, uint32_t offsetA, uint32_t rawLengthA, uint32_t offsetB,
                    uint32_t rawLengthB) {
  StringCursor a;
  StringCursor b;
  a.begin(source, offsetA, rawLengthA);
  b.begin(source, offsetB, rawLengthB);
  while (true) {
    const int32_t ca = a.next();
    const int32_t cb = b.next();
    if (ca == StringCursor::FAILED || cb == StringCursor::FAILED || ca != cb) {
      return false;
    }
    if (ca == StringCursor::END) {
      return true;
    }
  }
}

bool loadJsonString(CheckpointSource& source, uint32_t offset, uint32_t rawLength, char* out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return false;
  }
  out[0] = '\0';
  StringCursor cursor;
  cursor.begin(source, offset, rawLength);
  size_t length = 0;
  while (true) {
    const int32_t cp = cursor.next();
    if (cp == StringCursor::END) {
      out[length] = '\0';
      return true;
    }
    if (cp < 0 || isSurrogate(static_cast<uint32_t>(cp))) {
      out[0] = '\0';
      return false;
    }
    uint8_t bytes[4];
    const uint8_t n = encodeUtf8(static_cast<uint32_t>(cp), bytes);
    if (length + n + 1 > outSize) {
      out[0] = '\0';
      return false;
    }
    for (uint8_t i = 0; i < n; ++i) {
      out[length++] = static_cast<char>(bytes[i]);
    }
  }
}

}  // namespace checkpoint::detail
