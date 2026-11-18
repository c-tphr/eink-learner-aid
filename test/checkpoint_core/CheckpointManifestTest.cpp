// Runs every sample in fixtures/checkpoints/ through the parser and compares
// with manifest.json, the expectations shared with tools/prep/checkpoints.py.

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <set>
#include <string>

#include "CheckpointTestSupport.h"

using checkpoint::CheckpointIndex;
using checkpoint::IssueCode;
using checkpoint::OpenResult;
using checkpoint::Verdict;
using checkpoint_test::JsonReader;
using checkpoint_test::JsonValue;

namespace {

const char* verdictName(Verdict v) {
  switch (v) {
    case Verdict::Valid:
      return "valid";
    case Verdict::Invalid:
      return "invalid";
    case Verdict::UnsupportedVersion:
      return "unsupported-version";
  }
  return "?";
}

std::string layerOfCode(const std::string& code) {
  if (code == "parse" || code == "duplicate-key") return "parse";
  if (code == "unsupported-version") return "version";
  if (code == "schema") return "schema";
  if (code.empty()) return "";
  return "cross-reference";
}

JsonValue loadManifest() {
  const std::vector<uint8_t> bytes = checkpoint_test::readFile(checkpoint_test::fixturePath("manifest.json"));
  const std::string text(bytes.begin(), bytes.end());
  JsonValue manifest;
  JsonReader reader(text);
  EXPECT_TRUE(reader.parse(manifest)) << "manifest.json did not parse";
  return manifest;
}

}  // namespace

TEST(CheckpointManifest, EverySampleFileIsListed) {
  const JsonValue manifest = loadManifest();
  const JsonValue* samples = manifest.get("samples");
  ASSERT_NE(samples, nullptr);
  std::set<std::string> listed;
  for (const JsonValue& sample : samples->items) listed.insert(sample.str("file"));

  size_t files = 0;
  for (const char* dir : {"valid", "invalid"}) {
    for (const auto& entry : std::filesystem::directory_iterator(checkpoint_test::fixturePath(dir))) {
      if (entry.path().extension() != ".json") continue;
      ++files;
      const std::string relative = std::string(dir) + "/" + entry.path().filename().string();
      EXPECT_EQ(listed.count(relative), 1u) << relative << " is not in manifest.json";
    }
  }
  EXPECT_EQ(files, listed.size());
}

// Agreement rules (task C1a): the verdict always matches. For the version and
// cross-reference layers the code and path match exactly. For the parse and
// schema layers a rejection with a code of the same layer is required; the
// table also shows whether code and path matched exactly.
TEST(CheckpointManifest, ParserAgreesWithEverySample) {
  const JsonValue manifest = loadManifest();
  const JsonValue* samples = manifest.get("samples");
  ASSERT_NE(samples, nullptr);
  ASSERT_FALSE(samples->items.empty());

  auto index = std::make_unique<CheckpointIndex>();
  int exact = 0;
  int coarse = 0;
  int mismatched = 0;

  printf("%-48s | %-19s %-32s %-34s | %-19s %-32s %-34s | %s\n", "sample", "expected verdict", "expected code",
         "expected path", "parser verdict", "parser code", "parser path", "match");
  for (const JsonValue& sample : samples->items) {
    const std::string file = sample.str("file");
    const std::string expectedVerdict = sample.str("verdict");
    const std::string layer = sample.str("layer");
    std::set<std::string> expectedCodes;
    std::set<std::string> expectedPaths;
    if (const JsonValue* issues = sample.get("issues")) {
      for (const JsonValue& issue : issues->items) {
        expectedCodes.insert(issue.str("code"));
        expectedPaths.insert(issue.str("path"));
      }
    }
    const std::string expectedCode = expectedCodes.empty() ? "" : *expectedCodes.begin();
    const std::string expectedPath = expectedPaths.empty() ? "" : *expectedPaths.begin();

    checkpoint_test::FileSource source(checkpoint_test::fixturePath(file));
    const OpenResult result = index->open(source);
    if (result.verdict == Verdict::Valid) {
      // A valid sample must also serve every prompt and answer.
      char text[checkpoint::ANSWER_BUFFER_BYTES];
      for (uint16_t c = 0; c < index->checkpointCount(); ++c) {
        for (uint16_t p = 0; p < index->promptCount(c); ++p) {
          EXPECT_TRUE(index->loadPromptId(c, p, text, sizeof(text))) << file;
          EXPECT_TRUE(index->loadPromptText(c, p, text, sizeof(text))) << file;
          EXPECT_TRUE(index->loadAnswerText(c, p, text, sizeof(text))) << file;
        }
      }
    }
    index->close();

    const std::string gotVerdict = verdictName(result.verdict);
    const std::string gotCode = checkpoint::issueCodeName(result.code);
    const std::string gotPath = result.path;

    const bool verdictOk = gotVerdict == expectedVerdict;
    const bool codeExact = expectedCodes.empty() ? result.code == IssueCode::None : expectedCodes.count(gotCode) == 1;
    const bool pathExact = expectedPaths.empty() ? gotPath.empty() : expectedPaths.count(gotPath) == 1;
    const bool isExact = verdictOk && codeExact && pathExact;
    const bool needsExact = layer.empty() || layer == "version" || layer == "cross-reference";
    const bool isCoarse = verdictOk && !needsExact && layerOfCode(gotCode) == layer;
    const char* match = isExact ? "yes (exact)" : (isCoarse ? "yes (same layer)" : "NO");
    if (isExact) {
      ++exact;
    } else if (isCoarse) {
      ++coarse;
    } else {
      ++mismatched;
    }
    printf("%-48s | %-19s %-32s %-34s | %-19s %-32s %-34s | %s\n", file.c_str(), expectedVerdict.c_str(),
           expectedCode.c_str(), expectedPath.c_str(), gotVerdict.c_str(), gotCode.c_str(), gotPath.c_str(), match);

    EXPECT_TRUE(isExact || isCoarse) << file << ": expected " << expectedVerdict << " " << expectedCode << " at '"
                                     << expectedPath << "', got " << gotVerdict << " " << gotCode << " at '" << gotPath
                                     << "' (" << result.detail << ", offset " << result.offset << ")";
  }
  printf("samples: %zu, exact: %d, same layer: %d, mismatched: %d\n", samples->items.size(), exact, coarse, mismatched);
  EXPECT_EQ(mismatched, 0);
}
