// The checkpoint data loader's decisions (src/reading/CheckpointBookRules.h) on the shared samples of
// fixtures/checkpoints: every sample the manifest calls valid opens the checkpoint screens; every
// other one leaves an ordinary book (UnsupportedVersion for a newer version, Invalid for the rest).
// Also the size limit checked before the file is copied, and the cached copy's name.

#include <CheckpointIndex.h>
#include <CheckpointLimits.h>
#include <CheckpointSource.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <regex>
#include <string>

#include "reading/CheckpointBookRules.h"

using reading::CheckpointData;

namespace {

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// A source whose reads fail after `limit` bytes, like a card that fails mid-file.
class FailingSource final : public checkpoint::CheckpointSource {
 public:
  FailingSource(const std::string& bytes, const uint32_t limit) : bytes(bytes), limit(limit) {}
  uint32_t size() const override { return static_cast<uint32_t>(bytes.size()); }
  size_t read(const uint32_t offset, uint8_t* out, const size_t length) override {
    if (offset >= limit) return 0;
    const size_t n = std::min(length, static_cast<size_t>(limit - offset));
    std::memcpy(out, bytes.data() + offset, n);
    return n;
  }

 private:
  const std::string& bytes;
  uint32_t limit;
};

}  // namespace

TEST(CheckpointBookRules, EverySharedSampleGivesTheExpectedBook) {
  const std::string manifest = readFile(std::string(CHECKPOINT_FIXTURES_DIR) + "/manifest.json");
  ASSERT_FALSE(manifest.empty());
  const std::regex entry(R"re("file":\s*"([^"]+)",\s*"verdict":\s*"([^"]+)")re");
  auto index = std::make_unique<checkpoint::CheckpointIndex>();
  int samples = 0;
  int valid = 0;
  for (auto it = std::sregex_iterator(manifest.begin(), manifest.end(), entry); it != std::sregex_iterator(); ++it) {
    const std::string file = (*it)[1];
    const std::string verdict = (*it)[2];
    const std::string bytes = readFile(std::string(CHECKPOINT_FIXTURES_DIR) + "/" + file);
    checkpoint::MemoryCheckpointSource source(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    const CheckpointData data = reading::checkpointDataFor(index->open(source));
    const CheckpointData expected = verdict == "valid"                 ? CheckpointData::Valid
                                    : verdict == "unsupported-version" ? CheckpointData::UnsupportedVersion
                                                                       : CheckpointData::Invalid;
    EXPECT_EQ(data, expected) << file << ": " << reading::checkpointDataName(data);
    EXPECT_TRUE(reading::checkpointFileSizeAllowed(bytes.size())) << file;
    ++samples;
    valid += data == CheckpointData::Valid ? 1 : 0;
  }
  EXPECT_GE(samples, 100) << "the manifest lists every sample";
  EXPECT_GE(valid, 10);
}

TEST(CheckpointBookRules, AReadErrorIsNotAnInvalidFile) {
  const std::string bytes = readFile(std::string(CHECKPOINT_FIXTURES_DIR) + "/valid/multi-prompt.json");
  ASSERT_GT(bytes.size(), 100u);
  FailingSource source(bytes, 100);
  auto index = std::make_unique<checkpoint::CheckpointIndex>();
  const checkpoint::OpenResult r = index->open(source);
  EXPECT_EQ(r.code, checkpoint::IssueCode::Io);
  EXPECT_EQ(reading::checkpointDataFor(r), CheckpointData::ReadFailed);
}

TEST(CheckpointBookRules, SizeLimitAndCopyName) {
  EXPECT_TRUE(reading::checkpointFileSizeAllowed(0));
  EXPECT_TRUE(reading::checkpointFileSizeAllowed(checkpoint::MAX_FILE_BYTES));
  EXPECT_FALSE(reading::checkpointFileSizeAllowed(checkpoint::MAX_FILE_BYTES + 1));
  EXPECT_EQ(reading::checkpointCopyName(0x0123abcdu, 2805), "checkpoints-0123abcd-2805.json");
  EXPECT_EQ(reading::checkpointCopyName(0, 91475), "checkpoints-00000000-91475.json");
  EXPECT_NE(reading::checkpointCopyName(1, 2805), reading::checkpointCopyName(2, 2805)) << "another entry, another copy";
  EXPECT_STREQ(reading::checkpointDataName(CheckpointData::TooLarge), "too-large");
  EXPECT_STREQ(reading::checkpointDataName(CheckpointData::None), "none");
}
