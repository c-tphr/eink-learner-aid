#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "lib/RsvpCore/FrameBuilder.h"
#include "lib/RsvpCore/WordSource.h"

namespace rsvptest {

struct OwnedToken {
  std::string text;
  uint32_t wordIndex = 0;
  uint8_t flags = 0;
};

// WordSource over a vector of words. Counts calls so tests can check the lookahead contract.
class VectorSource : public rsvp::WordSource {
 public:
  explicit VectorSource(std::vector<OwnedToken> tokens) : tokens(std::move(tokens)) {}

  bool peek(rsvp::Token& out) override {
    ++peeks;
    if (position >= tokens.size()) return false;
    // Copy into a scratch buffer that the next call overwrites, so a consumer that keeps a view
    // past the next call reads clobbered text and fails the tests.
    const std::string& text = tokens[position].text;
    const size_t length = text.size() < sizeof(scratch) ? text.size() : sizeof(scratch);
    std::memcpy(scratch, text.data(), length);
    out.text = std::string_view(scratch, length);
    out.wordIndex = tokens[position].wordIndex;
    out.flags = tokens[position].flags;
    return true;
  }

  void advance() override {
    std::memset(scratch, '#', sizeof(scratch));
    ++position;
  }

  void seek(size_t index) { position = index; }
  size_t consumed() const { return position; }

  std::vector<OwnedToken> tokens;
  size_t position = 0;
  size_t peeks = 0;

 private:
  char scratch[1024] = {};
};

// Tokens from space-separated text, numbered from 0, with no flags.
inline std::vector<OwnedToken> words(const std::string& text) {
  std::vector<OwnedToken> out;
  std::istringstream in(text);
  std::string w;
  while (in >> w) out.push_back({w, static_cast<uint32_t>(out.size()), 0});
  return out;
}

inline std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// Splits prose into words the way WordIndex does (docs/spikes/S1-word-stream.md, section 5):
// on ASCII whitespace, and after an em or en dash that is followed by another non-space
// character. Each non-empty line is one paragraph: its last word gets TOKEN_PARAGRAPH_END.
inline std::vector<OwnedToken> proseTokens(const std::string& text) {
  std::vector<OwnedToken> out;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream in(line);
    std::string w;
    bool any = false;
    while (in >> w) {
      size_t start = 0;
      for (size_t i = 0; i + 3 <= w.size(); ++i) {
        // U+2014 is E2 80 94, U+2013 is E2 80 93.
        const bool dash = static_cast<uint8_t>(w[i]) == 0xE2 && static_cast<uint8_t>(w[i + 1]) == 0x80 &&
                          (static_cast<uint8_t>(w[i + 2]) == 0x94 || static_cast<uint8_t>(w[i + 2]) == 0x93);
        if (dash && i + 3 < w.size()) {
          out.push_back({w.substr(start, i + 3 - start), static_cast<uint32_t>(out.size()), 0});
          start = i + 3;
        }
      }
      out.push_back({w.substr(start), static_cast<uint32_t>(out.size()), 0});
      any = true;
    }
    if (any) out.back().flags |= rsvp::TOKEN_PARAGRAPH_END;
  }
  return out;
}

// Runs a builder to the end of its source and returns every frame.
inline std::vector<rsvp::Frame> allFrames(rsvp::FrameBuilder& builder) {
  std::vector<rsvp::Frame> frames;
  rsvp::Frame frame;
  while (builder.next(frame)) frames.push_back(frame);
  return frames;
}

}  // namespace rsvptest
