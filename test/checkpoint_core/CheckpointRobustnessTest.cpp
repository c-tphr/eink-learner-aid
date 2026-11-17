// Hostile and truncated input: the parser must return a verdict without
// crashing, overrunning a buffer, recursing or looping. Run under ASan/UBSan.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "CheckpointTestSupport.h"

using checkpoint::CheckpointIndex;
using checkpoint::IssueCode;
using checkpoint::MemoryCheckpointSource;
using checkpoint::OpenResult;
using checkpoint::Verdict;
using checkpoint_test::bytesOf;

namespace {

std::vector<std::vector<uint8_t>> validSamples() {
  std::vector<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator(checkpoint_test::fixturePath("valid"))) {
    names.push_back(entry.path().string());
  }
  std::sort(names.begin(), names.end());
  std::vector<std::vector<uint8_t>> samples;
  for (const auto& name : names) samples.push_back(checkpoint_test::readFile(name));
  return samples;
}

// Opens `bytes` and, when valid, reads every text back through the index.
OpenResult openAndExercise(CheckpointIndex& index, const std::vector<uint8_t>& bytes) {
  MemoryCheckpointSource source(bytes.data(), bytes.size());
  const OpenResult result = index.open(source);
  if (result.verdict == Verdict::Valid) {
    char text[checkpoint::ANSWER_BUFFER_BYTES];
    EXPECT_TRUE(index.loadTitle(text, sizeof(text)));
    for (uint16_t s = 0; s < index.sectionCount(); ++s) {
      EXPECT_TRUE(index.loadSectionLabel(s, text, sizeof(text)));
      EXPECT_EQ(index.findSection(index.sectionId(s)) >= 0, true);
    }
    for (uint16_t c = 0; c < index.checkpointCount(); ++c) {
      for (uint16_t p = 0; p < index.promptCount(c); ++p) {
        EXPECT_TRUE(index.loadPromptId(c, p, text, sizeof(text)));
        EXPECT_TRUE(index.loadPromptText(c, p, text, sizeof(text)));
        EXPECT_TRUE(index.loadAnswerText(c, p, text, sizeof(text)));
      }
    }
  } else {
    EXPECT_NE(result.code, IssueCode::None);
    EXPECT_LT(strlen(result.path), checkpoint::PATH_BUFFER_BYTES);
  }
  index.close();
  return result;
}

bool onlyWhitespace(const std::vector<uint8_t>& bytes, size_t from) {
  for (size_t i = from; i < bytes.size(); ++i) {
    if (bytes[i] != ' ' && bytes[i] != '\n' && bytes[i] != '\r' && bytes[i] != '\t') return false;
  }
  return true;
}

}  // namespace

TEST(CheckpointRobustness, EveryTruncationOfEveryValidSample) {
  auto index = std::make_unique<CheckpointIndex>();
  size_t cases = 0;
  for (const auto& sample : validSamples()) {
    ASSERT_FALSE(sample.empty());
    for (size_t length = 0; length < sample.size(); ++length) {
      const std::vector<uint8_t> prefix(sample.begin(), sample.begin() + static_cast<std::ptrdiff_t>(length));
      const OpenResult result = openAndExercise(*index, prefix);
      ++cases;
      if (result.verdict == Verdict::Valid) {
        EXPECT_TRUE(onlyWhitespace(sample, length)) << "prefix of " << length << " bytes accepted";
      } else {
        EXPECT_EQ(result.code, IssueCode::Parse) << "prefix of " << length << " bytes";
      }
    }
  }
  printf("truncations checked: %zu\n", cases);
}

TEST(CheckpointRobustness, RandomByteMutationsWithFixedSeed) {
  auto index = std::make_unique<CheckpointIndex>();
  std::mt19937 rng(0xC1A);
  const char interesting[] = "{}[]\",:\\u0123456789eE.-+tfnul \n\x80\xC3\xED\xF0\xFF";
  size_t cases = 0;
  size_t counts[4] = {0, 0, 0, 0};  // valid, invalid, unsupported, limit
  for (const auto& sample : validSamples()) {
    for (int round = 0; round < 1500; ++round) {
      std::vector<uint8_t> bytes = sample;
      const int edits = 1 + static_cast<int>(rng() % 3);
      for (int e = 0; e < edits && !bytes.empty(); ++e) {
        const size_t at = rng() % bytes.size();
        switch (rng() % 5) {
          case 0:
            bytes[at] = static_cast<uint8_t>(rng());
            break;
          case 1:
            bytes[at] ^= static_cast<uint8_t>(1u << (rng() % 8));
            break;
          case 2:
            bytes[at] = static_cast<uint8_t>(interesting[rng() % (sizeof(interesting) - 1)]);
            break;
          case 3:
            bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                         static_cast<uint8_t>(interesting[rng() % (sizeof(interesting) - 1)]));
            break;
          default:
            bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(at));
            break;
        }
      }
      const OpenResult result = openAndExercise(*index, bytes);
      ++cases;
      if (result.verdict == Verdict::Valid) {
        ++counts[0];
      } else if (result.verdict == Verdict::UnsupportedVersion) {
        ++counts[2];
      } else if (result.code == IssueCode::Limit) {
        ++counts[3];
      } else {
        ++counts[1];
      }
    }
  }
  printf("mutations: %zu (valid %zu, invalid %zu, unsupported-version %zu, limit %zu)\n", cases, counts[0], counts[1],
         counts[2], counts[3]);
}

TEST(CheckpointRobustness, DeepNestingIsBoundedNotRecursive) {
  auto index = std::make_unique<CheckpointIndex>();
  for (const char* open : {"[", "{\"a\":"}) {
    std::string text;
    for (int i = 0; i < 200000; ++i) text += open;
    const OpenResult result = openAndExercise(*index, bytesOf(text));
    EXPECT_EQ(result.code, IssueCode::Limit);
  }
  // Closed correctly but too deep, inside an unknown member of a valid file.
  const std::vector<uint8_t> base = checkpoint_test::readFile(checkpoint_test::fixturePath("valid/minimal.json"));
  std::string text(base.begin(), base.end());
  const size_t brace = text.rfind('}');
  text.insert(brace, ",\"x_deep\":" + std::string(40, '[') + std::string(40, ']'));
  EXPECT_EQ(openAndExercise(*index, bytesOf(text)).code, IssueCode::Limit);
}

TEST(CheckpointRobustness, VeryLongStrings) {
  auto index = std::make_unique<CheckpointIndex>();
  const std::vector<uint8_t> base = checkpoint_test::readFile(checkpoint_test::fixturePath("valid/minimal.json"));
  const std::string minimal(base.begin(), base.end());
  const size_t brace = minimal.rfind('}');
  auto withMember = [&](const std::string& member) {
    std::string text = minimal;
    text.insert(brace, "," + member);
    return text;
  };

  // A long unknown value and a long unknown member name are fine.
  EXPECT_EQ(openAndExercise(*index, bytesOf(withMember("\"x_blob\":\"" + std::string(900000, 'z') + "\""))).verdict,
            Verdict::Valid);
  const std::string longKey(200000, 'k');
  EXPECT_EQ(openAndExercise(*index, bytesOf(withMember("\"" + longKey + "\":1"))).verdict, Verdict::Valid);
  // A path under a long name is cut, not overrun.
  const OpenResult surrogate = openAndExercise(*index, bytesOf(withMember("\"" + longKey + "\":[\"\\ud800\"]")));
  EXPECT_EQ(surrogate.code, IssueCode::Parse);
  EXPECT_EQ(strlen(surrogate.path), checkpoint::PATH_BUFFER_BYTES - 1);
  EXPECT_EQ(std::string(surrogate.path).substr(checkpoint::PATH_BUFFER_BYTES - 4), "...");
  // Escape-heavy strings.
  std::string escapes;
  for (int i = 0; i < 50000; ++i) escapes += "\\ud83d\\ude00\\n\\u0041";
  EXPECT_EQ(openAndExercise(*index, bytesOf(withMember("\"x\":\"" + escapes + "\""))).verdict, Verdict::Valid);
  // Unterminated long string.
  EXPECT_EQ(openAndExercise(*index, bytesOf("{\"x\":\"" + std::string(500000, 'a'))).code, IssueCode::Parse);
  // Over the file size limit.
  EXPECT_EQ(
      openAndExercise(*index, bytesOf(withMember("\"x\":\"" + std::string(checkpoint::MAX_FILE_BYTES, 'a') + "\"")))
          .code,
      IssueCode::Limit);
}

TEST(CheckpointRobustness, HugeNumbers) {
  auto index = std::make_unique<CheckpointIndex>();
  const std::vector<uint8_t> base = checkpoint_test::readFile(checkpoint_test::fixturePath("valid/minimal.json"));
  const std::string minimal(base.begin(), base.end());
  const size_t brace = minimal.rfind('}');
  auto withNumber = [&](const std::string& number) {
    std::string text = minimal;
    text.insert(brace, ",\"x_n\":" + number);
    return openAndExercise(*index, bytesOf(text));
  };
  EXPECT_EQ(withNumber("1e400").verdict, Verdict::Valid);
  EXPECT_EQ(withNumber("-1E-400").verdict, Verdict::Valid);
  EXPECT_EQ(withNumber("123456789012345678901234567890").verdict, Verdict::Valid);
  EXPECT_EQ(withNumber("0.000000000000000000000000000001").verdict, Verdict::Valid);
  EXPECT_EQ(withNumber(std::string(100000, '7')).code, IssueCode::Limit);
  EXPECT_EQ(withNumber("1." + std::string(100000, '0')).code, IssueCode::Limit);
  EXPECT_EQ(withNumber("1e" + std::string(100000, '9')).code, IssueCode::Limit);
  EXPECT_EQ(withNumber("1e+").code, IssueCode::Parse);
}

TEST(CheckpointRobustness, ArbitraryBytes) {
  auto index = std::make_unique<CheckpointIndex>();
  std::mt19937 rng(24);
  for (int round = 0; round < 3000; ++round) {
    std::vector<uint8_t> bytes(rng() % 300);
    for (auto& b : bytes) b = static_cast<uint8_t>(rng());
    if (!bytes.empty() && round % 2 == 0) bytes[0] = '{';
    const OpenResult result = openAndExercise(*index, bytes);
    EXPECT_NE(result.verdict, Verdict::Valid);
  }
}
