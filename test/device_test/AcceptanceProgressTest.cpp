#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "SampleRuns.h"
#include "activities/devicetest/AcceptanceProgress.h"

using namespace devicetest;

namespace {

std::vector<std::string> lines(const std::string& text) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start < text.size()) {
    const size_t end = text.find('\n', start);
    out.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return out;
}

bool decode(const std::string& text, AcceptanceRecord& record, Walkthrough& walk) {
  ProgressDecoder d;
  for (const std::string& l : lines(text)) d.line(l.c_str());
  return d.finish(record, walk);
}

std::string replaceLine(const std::string& text, const std::string& start, const std::string& with) {
  std::string out;
  for (const std::string& l : lines(text)) {
    out += l.rfind(start, 0) == 0 ? with : l;
    out += '\n';
  }
  return out;
}

}  // namespace

TEST(AcceptanceProgress, ARoundTripKeepsEverythingTheResultsFileNeeds) {
  std::string progress;
  const std::string json = devicetest_samples::acceptancePartWayJson(&progress);
  for (const std::string& l : lines(progress)) EXPECT_LT(l.size() + 2, PROGRESS_LINE_BYTES) << l;

  AcceptanceRecord record;
  devicetest_samples::fillHeader(record.header, 1, ResultKind::BENCHMARK, PanelId::SIMULATED);
  record.header.simulatorTimeScale = 50;
  Walkthrough walk;
  ASSERT_TRUE(decode(progress, record, walk));
  EXPECT_EQ(record.header.run, 5);
  EXPECT_STREQ(record.header.file, "0005-acceptance.json");
  EXPECT_EQ(record.profile, ProfileId::TRIAL_B);
  EXPECT_EQ(record.header.conditions.batteryPercent, 87);
  EXPECT_EQ(record.header.conditions.uptimeMs, 123456u);
  EXPECT_EQ(walk.stepIndex(), 3);
  EXPECT_TRUE(walk.partB.away);
  EXPECT_EQ(walk.resume(), Walk::ASK_RESULT);
  EXPECT_EQ(walk.partB.steps[1].result, StepResult::FAIL);
  EXPECT_EQ(walk.partB.steps[1].reason, FailReason::WRONG_MODE);
  EXPECT_EQ(walk.partA.runs[0].attempts, 2);

  // The results file written from the decoded state is the same file.
  devicetest_samples::StringSink again;
  writeAcceptance(record, walk, again);
  EXPECT_EQ(again.text, json);
}

TEST(AcceptanceProgress, AnIncompleteOrDamagedFileIsRefusedAsAWhole) {
  std::string progress;
  devicetest_samples::acceptancePartWayJson(&progress);
  AcceptanceRecord record;
  Walkthrough walk;
  ASSERT_TRUE(decode(progress, record, walk));

  // Cut off before "end".
  EXPECT_FALSE(decode(progress.substr(0, progress.rfind("end")), record, walk));
  // Another version, or not our file.
  EXPECT_FALSE(decode(replaceLine(progress, "rsvp-acceptance-progress", "rsvp-acceptance-progress 2"), record, walk));
  EXPECT_FALSE(decode("{\"format\":\"rsvp-bench\"}\n", record, walk));
  EXPECT_FALSE(decode("", record, walk));
  // A value out of range, a missing number, an unknown line, something after "end".
  EXPECT_FALSE(decode(replaceLine(progress, "part_b", "part_b 13 0"), record, walk));
  EXPECT_FALSE(decode(replaceLine(progress, "step 2", "step 2 9 0 1"), record, walk));
  EXPECT_FALSE(decode(replaceLine(progress, "part_a", "part_a 2"), record, walk));
  EXPECT_FALSE(decode(replaceLine(progress, "run 5", "run x"), record, walk));
  EXPECT_FALSE(decode(replaceLine(progress, "file", "file notours.json"), record, walk));
  EXPECT_FALSE(decode(progress + "extra 1\n", record, walk));
  EXPECT_FALSE(decode(replaceLine(progress, "part_b", "colour 3"), record, walk));
}

TEST(AcceptanceProgress, AFreshWalkThroughWithoutAClockRoundTrips) {
  AcceptanceRecord record;
  devicetest_samples::fillHeader(record.header, 9, ResultKind::ACCEPTANCE, PanelId::DEPG0397);
  Walkthrough walk;
  walk.reset();
  devicetest_samples::StringSink sink;
  encodeProgress(record, walk, sink);
  EXPECT_NE(sink.text.find("conditions 87 1 123456 -\n"), std::string::npos);
  AcceptanceRecord back;
  Walkthrough w2;
  ASSERT_TRUE(decode(sink.text, back, w2));
  EXPECT_EQ(back.header.conditions.clock[0], '\0');
  EXPECT_EQ(w2.resume(), Walk::START);
  EXPECT_EQ(w2.answered(), 0);
}
