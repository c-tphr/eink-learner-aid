#pragma once

// Minimal JSON reader for the fixture files (test-only; strings with \uXXXX escapes, numbers,
// objects, arrays, true/false/null). Throws nothing: a parse error sets `ok` to false.

#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace testjson {

struct Value {
  enum class Kind { NUL, BOOL, NUMBER, STRING, ARRAY, OBJECT } kind = Kind::NUL;
  bool boolean = false;
  double number = 0;
  std::string string;
  std::vector<Value> array;
  std::map<std::string, Value> object;

  const Value& operator[](const std::string& key) const {
    static const Value missing;
    const auto it = object.find(key);
    return it == object.end() ? missing : it->second;
  }
  const Value& operator[](size_t i) const { return array[i]; }
  size_t size() const { return kind == Kind::ARRAY ? array.size() : object.size(); }
  long asInt() const { return static_cast<long>(number); }
};

class Parser {
 public:
  explicit Parser(const std::string& text) : s(text) {}

  bool parse(Value& out) {
    ok = true;
    pos = 0;
    value(out);
    skipSpace();
    return ok && pos == s.size();
  }

 private:
  void skipSpace() {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\n' || s[pos] == '\r' || s[pos] == '\t')) ++pos;
  }

  void fail() { ok = false; }

  void appendUtf8(uint32_t cp, std::string& out) {
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
      fail();
      return 0;
    }
    const uint32_t v = static_cast<uint32_t>(std::strtoul(s.substr(pos, 4).c_str(), nullptr, 16));
    pos += 4;
    return v;
  }

  void str(std::string& out) {
    ++pos;  // opening quote
    while (ok && pos < s.size() && s[pos] != '"') {
      if (s[pos] != '\\') {
        out += s[pos++];
        continue;
      }
      ++pos;
      if (pos >= s.size()) return fail();
      const char e = s[pos++];
      switch (e) {
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
          if (cp >= 0xD800 && cp <= 0xDBFF && pos + 6 <= s.size() && s[pos] == '\\' && s[pos + 1] == 'u') {
            pos += 2;
            const uint32_t low = hex4();
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
          }
          appendUtf8(cp, out);
          break;
        }
        default:
          out += e;
      }
    }
    if (pos >= s.size()) return fail();
    ++pos;  // closing quote
  }

  void value(Value& out) {
    skipSpace();
    if (pos >= s.size()) return fail();
    const char c = s[pos];
    if (c == '{') {
      out.kind = Value::Kind::OBJECT;
      ++pos;
      skipSpace();
      if (pos < s.size() && s[pos] == '}') {
        ++pos;
        return;
      }
      while (ok) {
        skipSpace();
        if (pos >= s.size() || s[pos] != '"') return fail();
        std::string key;
        str(key);
        skipSpace();
        if (pos >= s.size() || s[pos] != ':') return fail();
        ++pos;
        value(out.object[key]);
        skipSpace();
        if (pos < s.size() && s[pos] == ',') {
          ++pos;
          continue;
        }
        if (pos < s.size() && s[pos] == '}') {
          ++pos;
          return;
        }
        return fail();
      }
    } else if (c == '[') {
      out.kind = Value::Kind::ARRAY;
      ++pos;
      skipSpace();
      if (pos < s.size() && s[pos] == ']') {
        ++pos;
        return;
      }
      while (ok) {
        out.array.emplace_back();
        value(out.array.back());
        skipSpace();
        if (pos < s.size() && s[pos] == ',') {
          ++pos;
          continue;
        }
        if (pos < s.size() && s[pos] == ']') {
          ++pos;
          return;
        }
        return fail();
      }
    } else if (c == '"') {
      out.kind = Value::Kind::STRING;
      str(out.string);
    } else if (s.compare(pos, 4, "true") == 0) {
      out.kind = Value::Kind::BOOL;
      out.boolean = true;
      pos += 4;
    } else if (s.compare(pos, 5, "false") == 0) {
      out.kind = Value::Kind::BOOL;
      pos += 5;
    } else if (s.compare(pos, 4, "null") == 0) {
      pos += 4;
    } else {
      out.kind = Value::Kind::NUMBER;
      char* end = nullptr;
      out.number = std::strtod(s.c_str() + pos, &end);
      if (end == s.c_str() + pos) return fail();
      pos = static_cast<size_t>(end - s.c_str());
    }
  }

  const std::string& s;
  size_t pos = 0;
  bool ok = true;
};

}  // namespace testjson
