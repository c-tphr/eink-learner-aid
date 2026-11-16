#pragma once

// The synthetic fixture book's ground truth (fixtures/books/fixture.expected.json) as word streams
// a WordIndex-backed source would supply: paragraph, non-prose, section and checkpoint flags set.

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "TestJson.h"
#include "TestSupport.h"
#include "lib/RsvpCore/WordSource.h"

namespace rsvptest {

inline const testjson::Value& loadFixtureJson(const char* name) {
  static std::map<std::string, testjson::Value> cache;
  auto it = cache.find(name);
  if (it == cache.end()) {
    const std::string text = readFile(std::string(RSVP_REPO_ROOT "/fixtures/books/") + name);
    testjson::Value value;
    testjson::Parser parser(text);
    EXPECT_TRUE(parser.parse(value)) << name;
    it = cache.emplace(name, std::move(value)).first;
  }
  return it->second;
}

inline const testjson::Value& fixtureExpected() { return loadFixtureJson("fixture.expected.json"); }

// The chapter's word stream as a WordIndex-backed source would supply it.
inline std::vector<OwnedToken> chapterTokens(const testjson::Value& document) {
  std::vector<OwnedToken> tokens;
  const auto& words = document["words"];
  for (size_t i = 0; i < words.size(); ++i) tokens.push_back({words[i].string, static_cast<uint32_t>(i), 0});
  const auto& blocks = document["blocks"];
  for (size_t i = 0; i < blocks.size(); ++i) {
    const auto& block = blocks[i];
    const std::string& kind = block["kind"].string;
    if (kind == "heading" || kind == "paragraph") {
      tokens[block["last_word"].asInt()].flags |= rsvp::TOKEN_PARAGRAPH_END;
    } else if (block["after_word"].asInt() >= 0) {
      tokens[block["after_word"].asInt()].flags |= rsvp::TOKEN_NON_PROSE_FOLLOWS;
    }
  }

  std::set<std::string> withCheckpoint;
  const auto& checkpoints = loadFixtureJson("fixture.draft.json")["checkpoints"];
  for (size_t i = 0; i < checkpoints.size(); ++i) withCheckpoint.insert(checkpoints[i]["section"].string);
  const auto& sections = fixtureExpected()["sections"];
  for (size_t i = 0; i < sections.size(); ++i) {
    const auto& section = sections[i];
    if (section["chapter_href"].string != document["href"].string) continue;
    uint8_t& flags = tokens[section["last_word"].asInt()].flags;
    flags |= rsvp::TOKEN_SECTION_END;
    if (withCheckpoint.count(section["id"].string)) flags |= rsvp::TOKEN_CHECKPOINT;
  }
  return tokens;
}

inline const testjson::Value& fixtureDocument(std::string_view href) {
  const auto& documents = fixtureExpected()["documents"];
  for (size_t i = 0; i < documents.size(); ++i) {
    if (documents[i]["href"].string == href) return documents[i];
  }
  static const testjson::Value missing;
  ADD_FAILURE() << "no document " << std::string(href);
  return missing;
}

}  // namespace rsvptest
