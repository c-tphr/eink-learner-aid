#pragma once

// The synthetic fixture book's ground truth (fixtures/books/fixture.expected.json, version 2) as the
// word streams a WordIndex-backed source would supply to RsvpCore.
//
// fixture.expected.json lists every word of a chapter in the firmware's word model
// (docs/spikes/S1-word-stream.md section 5): table and code words inside TABLE and CODE regions,
// footnote bodies inside NOTE regions, an IMAGE marker at the index of the word after the image.
// chapterTokens() turns that into what RsvpCore is handed:
//   - prose words only, each with its chapter word index (so indexes skip over regions);
//   - TABLE and CODE words are not handed out; the last prose word before a TABLE, CODE or IMAGE
//     marker carries TOKEN_NON_PROSE_FOLLOWS, which is where the reader pauses;
//   - NOTE words are skipped silently: no flag, no stop;
//   - TOKEN_PARAGRAPH_END from the PARA_END flag runs;
//   - TOKEN_SECTION_END (and TOKEN_CHECKPOINT when the draft has a checkpoint for the section) on
//     the last prose word before each section's end anchor, sections[].end - 1 in the fixture.
// A document whose every word lies in a NOTE region (the fixture's endnotes document) gives an
// empty list: RsvpCore has nothing to play there. Callers that play documents must handle it.
//
// Every member read here is required: a missing member or a value of the wrong type is a test
// failure, never a silent zero.

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "TestJson.h"
#include "TestSupport.h"
#include "lib/RsvpCore/WordSource.h"

namespace rsvptest {

inline constexpr long FIXTURE_EXPECTED_VERSION = 2;
inline constexpr long FIXTURE_RULES_VERSION = 1;

inline const testjson::Value& missingJson() {
  static const testjson::Value missing;
  return missing;
}

inline const char* jsonKindName(testjson::Value::Kind kind) {
  switch (kind) {
    case testjson::Value::Kind::NUL:
      return "null";
    case testjson::Value::Kind::BOOL:
      return "boolean";
    case testjson::Value::Kind::NUMBER:
      return "number";
    case testjson::Value::Kind::STRING:
      return "string";
    case testjson::Value::Kind::ARRAY:
      return "array";
    case testjson::Value::Kind::OBJECT:
      return "object";
  }
  return "?";
}

// object[key], which must exist and be of `kind`; otherwise a test failure naming `where`.
// (Names are std::string_view, passed by value, so that GCC's -Wdangling-reference does not take
// the returned reference for one into a temporary argument.)
inline const testjson::Value& requireMember(const testjson::Value& object, std::string_view key,
                                            testjson::Value::Kind kind, std::string_view where) {
  if (object.kind != testjson::Value::Kind::OBJECT) {
    ADD_FAILURE() << where << " is a " << jsonKindName(object.kind) << ", not an object (looking for '" << key << "')";
    return missingJson();
  }
  const auto it = object.object.find(std::string(key));
  if (it == object.object.end()) {
    ADD_FAILURE() << where << " has no member '" << key << "'";
    return missingJson();
  }
  if (it->second.kind != kind) {
    ADD_FAILURE() << where << "." << key << " is a " << jsonKindName(it->second.kind) << ", expected a "
                  << jsonKindName(kind);
    return missingJson();
  }
  return it->second;
}

inline const testjson::Value& requireArray(const testjson::Value& object, std::string_view key,
                                           std::string_view where) {
  return requireMember(object, key, testjson::Value::Kind::ARRAY, where);
}

inline const std::string& requireString(const testjson::Value& object, std::string_view key,
                                        std::string_view where) {
  return requireMember(object, key, testjson::Value::Kind::STRING, where).string;
}

// A non-negative integer member; -1 (after a test failure) when missing or not an integer.
inline long requireIndex(const testjson::Value& object, std::string_view key, std::string_view where) {
  const testjson::Value& value = requireMember(object, key, testjson::Value::Kind::NUMBER, where);
  if (value.kind != testjson::Value::Kind::NUMBER) return -1;
  if (value.number < 0 || value.number != static_cast<double>(value.asInt())) {
    ADD_FAILURE() << where << "." << key << " is not a non-negative integer: " << value.number;
    return -1;
  }
  return value.asInt();
}

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

// fixture.expected.json; fails the test when it is not the version these readers understand.
inline const testjson::Value& fixtureExpected() {
  const testjson::Value& expected = loadFixtureJson("fixture.expected.json");
  EXPECT_EQ(requireIndex(expected, "version", "fixture.expected.json"), FIXTURE_EXPECTED_VERSION)
      << "fixture.expected.json has another format version: update test/rsvp_core/FixtureBook.h";
  EXPECT_EQ(requireIndex(expected, "rules_version", "fixture.expected.json"), FIXTURE_RULES_VERSION)
      << "the word rules changed: review the fixture tests";
  return expected;
}

inline const testjson::Value& fixtureDocument(std::string_view href) {
  const auto& documents = requireArray(fixtureExpected(), "documents", "fixture.expected.json");
  for (size_t i = 0; i < documents.size(); ++i) {
    if (requireString(documents[i], "href", "documents[" + std::to_string(i) + "]") == href) return documents[i];
  }
  ADD_FAILURE() << "no document " << std::string(href);
  return missingJson();
}

// A marker of a document: IMAGE (start == end), TABLE, CODE or NOTE as [start, end).
struct FixtureMarker {
  std::string kind;
  size_t start = 0;
  size_t end = 0;
};

inline std::vector<FixtureMarker> fixtureMarkers(const testjson::Value& document) {
  const std::string& href = requireString(document, "href", "document");
  const size_t words = requireArray(document, "words", href).size();
  const auto& markers = requireArray(document, "markers", href);
  std::vector<FixtureMarker> out;
  for (size_t i = 0; i < markers.size(); ++i) {
    const std::string where = href + " markers[" + std::to_string(i) + "]";
    const std::string& kind = requireString(markers[i], "kind", where);
    const long start = requireIndex(markers[i], "start", where);
    const long end = requireIndex(markers[i], "end", where);
    if (start < 0 || end < start || static_cast<size_t>(end) > words) {
      ADD_FAILURE() << where << ": bad range [" << start << ", " << end << ") for " << words << " words";
      continue;
    }
    if (kind != "IMAGE" && kind != "TABLE" && kind != "CODE" && kind != "NOTE") {
      ADD_FAILURE() << where << ": unknown marker kind " << kind;
      continue;
    }
    if (kind == "IMAGE" && end != start) ADD_FAILURE() << where << ": an IMAGE marker is zero words long";
    out.push_back({kind, static_cast<size_t>(start), static_cast<size_t>(end)});
  }
  return out;
}

// The [start, end) runs of word indexes carrying `flag`.
inline std::vector<std::pair<size_t, size_t>> fixtureFlagRuns(const testjson::Value& document, const char* flag) {
  const std::string& href = requireString(document, "href", "document");
  const size_t words = requireArray(document, "words", href).size();
  const std::string flagsWhere = href + " flags";
  const auto& runs =
      requireArray(requireMember(document, "flags", testjson::Value::Kind::OBJECT, href), flag, flagsWhere);
  std::vector<std::pair<size_t, size_t>> out;
  for (size_t i = 0; i < runs.size(); ++i) {
    if (runs[i].kind != testjson::Value::Kind::ARRAY || runs[i].size() != 2) {
      ADD_FAILURE() << href << " flags." << flag << "[" << i << "] is not a [start, end) pair";
      continue;
    }
    const auto start = static_cast<size_t>(runs[i][0].asInt());
    const auto end = static_cast<size_t>(runs[i][1].asInt());
    if (end <= start || end > words) {
      ADD_FAILURE() << href << " flags." << flag << "[" << i << "]: bad run";
      continue;
    }
    out.push_back({start, end});
  }
  return out;
}

// Word indexes the source does not hand to RsvpCore: TABLE, CODE and NOTE regions.
inline std::vector<bool> fixtureSkippedWords(const testjson::Value& document) {
  std::vector<bool> skipped(requireArray(document, "words", "document").size(), false);
  for (const FixtureMarker& m : fixtureMarkers(document)) {
    if (m.kind == "IMAGE") continue;
    for (size_t w = m.start; w < m.end; ++w) skipped[w] = true;
  }
  return skipped;
}

// Position in `tokens` of the token with chapter word index `wordIndex`, or tokens.size().
inline size_t tokenPosition(const std::vector<OwnedToken>& tokens, size_t wordIndex) {
  for (size_t p = 0; p < tokens.size(); ++p) {
    if (tokens[p].wordIndex == wordIndex) return p;
  }
  return tokens.size();
}

// Position of the last token whose word index is below `wordIndex`, or tokens.size() if none.
inline size_t lastTokenBefore(const std::vector<OwnedToken>& tokens, size_t wordIndex) {
  size_t found = tokens.size();
  for (size_t p = 0; p < tokens.size() && tokens[p].wordIndex < wordIndex; ++p) found = p;
  return found;
}

// Sections of fixture.expected.json, with whether the draft gives each a checkpoint.
inline std::set<std::string> fixtureSectionsWithCheckpoint() {
  std::set<std::string> out;
  const auto& checkpoints = requireArray(loadFixtureJson("fixture.draft.json"), "checkpoints", "fixture.draft.json");
  for (size_t i = 0; i < checkpoints.size(); ++i) {
    out.insert(requireString(checkpoints[i], "section", "checkpoints[" + std::to_string(i) + "]"));
  }
  return out;
}

inline const testjson::Value& fixtureSection(std::string_view id) {
  const auto& sections = requireArray(fixtureExpected(), "sections", "fixture.expected.json");
  for (size_t i = 0; i < sections.size(); ++i) {
    if (requireString(sections[i], "id", "sections[" + std::to_string(i) + "]") == id) return sections[i];
  }
  ADD_FAILURE() << "no section " << std::string(id);
  return missingJson();
}

// The chapter's word stream as a WordIndex-backed source would hand it to RsvpCore (see the top).
inline std::vector<OwnedToken> chapterTokens(const testjson::Value& document) {
  const std::string& href = requireString(document, "href", "document");
  const auto& words = requireArray(document, "words", href);
  const std::vector<bool> skipped = fixtureSkippedWords(document);
  std::vector<bool> paragraphEnd(words.size(), false);
  for (const auto& run : fixtureFlagRuns(document, "PARA_END")) {
    for (size_t w = run.first; w < run.second; ++w) paragraphEnd[w] = true;
  }

  std::vector<OwnedToken> tokens;
  for (size_t i = 0; i < words.size(); ++i) {
    if (words[i].kind != testjson::Value::Kind::STRING) {
      ADD_FAILURE() << href << " words[" << i << "] is not a string";
      continue;
    }
    if (skipped[i]) continue;
    tokens.push_back({words[i].string, static_cast<uint32_t>(i),
                      static_cast<uint8_t>(paragraphEnd[i] ? rsvp::TOKEN_PARAGRAPH_END : 0)});
  }

  // The reader pauses where an image, table or code block starts: after the last prose word before it.
  // (A marker before the chapter's first prose word has no word to carry the stop.)
  for (const FixtureMarker& m : fixtureMarkers(document)) {
    if (m.kind == "NOTE") continue;
    const size_t p = lastTokenBefore(tokens, m.start);
    if (p < tokens.size()) tokens[p].flags |= rsvp::TOKEN_NON_PROSE_FOLLOWS;
  }

  const std::set<std::string> withCheckpoint = fixtureSectionsWithCheckpoint();
  const auto& sections = requireArray(fixtureExpected(), "sections", "fixture.expected.json");
  for (size_t i = 0; i < sections.size(); ++i) {
    const std::string where = "sections[" + std::to_string(i) + "]";
    if (requireString(sections[i], "chapter_href", where) != href) continue;
    const long end = requireIndex(sections[i], "end", where);
    if (end <= 0) {
      ADD_FAILURE() << where << ": no words before the end anchor";
      continue;
    }
    const size_t p = lastTokenBefore(tokens, static_cast<size_t>(end));  // sections[].end - 1, or the prose before
    if (p >= tokens.size()) {
      ADD_FAILURE() << where << ": no prose word before the end anchor";
      continue;
    }
    uint8_t& flags = tokens[p].flags;
    flags |= rsvp::TOKEN_SECTION_END;
    if (withCheckpoint.count(requireString(sections[i], "id", where))) flags |= rsvp::TOKEN_CHECKPOINT;
  }
  return tokens;
}

// The token case named `name` (fixture.expected.json token_cases); a test failure if absent.
inline const testjson::Value& fixtureTokenCase(std::string_view name) {
  const auto& cases = requireArray(fixtureExpected(), "token_cases", "fixture.expected.json");
  for (size_t i = 0; i < cases.size(); ++i) {
    if (requireString(cases[i], "case", "token_cases[" + std::to_string(i) + "]") == name) return cases[i];
  }
  ADD_FAILURE() << "no token case named \"" << std::string(name) << "\"";
  return missingJson();
}

}  // namespace rsvptest
