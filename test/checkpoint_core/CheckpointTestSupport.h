#pragma once

// Test-only helpers: file access, sources with injected faults, and a small
// DOM JSON reader for fixtures/checkpoints/manifest.json. Library code never
// uses these.

#include <CheckpointIndex.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace checkpoint_test {

inline std::string fixturePath(const std::string& relative) {
  return std::string(CHECKPOINT_FIXTURES_DIR) + "/" + relative;
}

inline std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline std::vector<uint8_t> bytesOf(const std::string& text) { return std::vector<uint8_t>(text.begin(), text.end()); }

// A source over a host file, the way firmware reads a cached file.
class FileSource final : public checkpoint::CheckpointSource {
 public:
  explicit FileSource(const std::string& path) : in(path, std::ios::binary) {
    in.seekg(0, std::ios::end);
    length = in ? static_cast<uint32_t>(in.tellg()) : 0;
  }
  uint32_t size() const override { return length; }
  size_t read(uint32_t offset, uint8_t* out, size_t count) override {
    if (offset >= length) return 0;
    in.clear();
    in.seekg(offset);
    in.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(count));
    return static_cast<size_t>(in.gcount());
  }

 private:
  std::ifstream in;
  uint32_t length = 0;
};

// Serves the first `goodBytes` bytes, then fails every read.
class FailingSource final : public checkpoint::CheckpointSource {
 public:
  FailingSource(const std::vector<uint8_t>& data, uint32_t goodBytes) : data(data), goodBytes(goodBytes) {}
  uint32_t size() const override { return static_cast<uint32_t>(data.size()); }
  size_t read(uint32_t offset, uint8_t* out, size_t count) override {
    if (offset >= goodBytes) return 0;
    const size_t n = std::min<size_t>(count, goodBytes - offset);
    memcpy(out, data.data() + offset, n);
    return n;
  }

 private:
  const std::vector<uint8_t>& data;
  uint32_t goodBytes;
};

// Reports a size without holding the bytes.
class HugeSource final : public checkpoint::CheckpointSource {
 public:
  explicit HugeSource(uint32_t bytes) : bytes(bytes) {}
  uint32_t size() const override { return bytes; }
  size_t read(uint32_t offset, uint8_t* out, size_t count) override {
    if (offset >= bytes) return 0;
    memset(out, ' ', count);
    return count;
  }

 private:
  uint32_t bytes;
};

inline checkpoint::OpenResult openBytes(checkpoint::CheckpointIndex& index, const std::vector<uint8_t>& bytes) {
  checkpoint::MemoryCheckpointSource source(bytes.data(), bytes.size());
  const checkpoint::OpenResult result = index.open(source);
  index.close();  // the source dies here
  return result;
}

// Minimal JSON DOM for the manifest. Throws nothing; sets `ok` false on error.
struct JsonValue {
  enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
  bool boolean = false;
  double number = 0;
  std::string text;
  std::vector<JsonValue> items;
  std::vector<std::pair<std::string, JsonValue>> members;

  const JsonValue* get(const std::string& key) const {
    for (const auto& m : members) {
      if (m.first == key) return &m.second;
    }
    return nullptr;
  }
  std::string str(const std::string& key) const {
    const JsonValue* v = get(key);
    return v != nullptr && v->type == Type::String ? v->text : std::string();
  }
};

class JsonReader {
 public:
  explicit JsonReader(const std::string& text) : s(text) {}

  bool parse(JsonValue& out) {
    ok = true;
    value(out, 0);
    ws();
    return ok && pos == s.size();
  }

 private:
  void ws() {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\n' || s[pos] == '\r' || s[pos] == '\t')) ++pos;
  }
  bool eat(char c) {
    ws();
    if (pos < s.size() && s[pos] == c) {
      ++pos;
      return true;
    }
    return false;
  }
  static void put(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }
  uint32_t hex4() {
    if (pos + 4 > s.size()) {
      ok = false;
      return 0;
    }
    const uint32_t v = static_cast<uint32_t>(std::stoul(s.substr(pos, 4), nullptr, 16));
    pos += 4;
    return v;
  }
  std::string string() {
    std::string out;
    ++pos;  // quote
    while (ok && pos < s.size() && s[pos] != '"') {
      char c = s[pos++];
      if (c != '\\') {
        out += c;
        continue;
      }
      if (pos >= s.size()) break;
      c = s[pos++];
      switch (c) {
        case 'n':
          out += '\n';
          break;
        case 't':
          out += '\t';
          break;
        case 'r':
          out += '\r';
          break;
        case 'b':
          out += '\b';
          break;
        case 'f':
          out += '\f';
          break;
        case 'u': {
          uint32_t cp = hex4();
          if (cp >= 0xD800 && cp <= 0xDBFF && s.compare(pos, 2, "\\u") == 0) {
            pos += 2;
            const uint32_t low = hex4();
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
          }
          put(out, cp);
          break;
        }
        default:
          out += c;
      }
    }
    if (pos >= s.size()) ok = false;
    ++pos;
    return out;
  }
  void value(JsonValue& v, int depth) {
    ws();
    if (!ok || pos >= s.size() || depth > 64) {
      ok = false;
      return;
    }
    const char c = s[pos];
    if (c == '{') {
      ++pos;
      v.type = JsonValue::Type::Object;
      if (eat('}')) return;
      do {
        ws();
        if (pos >= s.size() || s[pos] != '"') {
          ok = false;
          return;
        }
        std::string key = string();
        if (!eat(':')) {
          ok = false;
          return;
        }
        JsonValue child;
        value(child, depth + 1);
        v.members.emplace_back(std::move(key), std::move(child));
      } while (ok && eat(','));
      if (!eat('}')) ok = false;
    } else if (c == '[') {
      ++pos;
      v.type = JsonValue::Type::Array;
      if (eat(']')) return;
      do {
        JsonValue child;
        value(child, depth + 1);
        v.items.push_back(std::move(child));
      } while (ok && eat(','));
      if (!eat(']')) ok = false;
    } else if (c == '"') {
      v.type = JsonValue::Type::String;
      v.text = string();
    } else if (s.compare(pos, 4, "null") == 0) {
      pos += 4;
    } else if (s.compare(pos, 4, "true") == 0) {
      v.type = JsonValue::Type::Bool;
      v.boolean = true;
      pos += 4;
    } else if (s.compare(pos, 5, "false") == 0) {
      v.type = JsonValue::Type::Bool;
      pos += 5;
    } else {
      size_t used = 0;
      v.type = JsonValue::Type::Number;
      v.number = std::stod(s.substr(pos, 32), &used);
      if (used == 0) ok = false;
      pos += used;
    }
  }

  const std::string& s;
  size_t pos = 0;
  bool ok = true;
};

}  // namespace checkpoint_test
