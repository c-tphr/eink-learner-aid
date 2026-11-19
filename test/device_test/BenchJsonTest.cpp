#include <gtest/gtest.h>

#include <string>

#include "SampleRuns.h"

using namespace devicetest;
using namespace devicetest_samples;

namespace {

// Structural check: balanced braces and brackets outside strings, no raw control characters in
// strings, one document.
bool wellFormed(const std::string& s) {
  int depth = 0;
  bool inString = false;
  for (size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (inString) {
      if (c == '\\') {
        ++i;
      } else if (c == '"') {
        inString = false;
      } else if (static_cast<unsigned char>(c) < 0x20) {
        return false;
      }
      continue;
    }
    if (c == '"') inString = true;
    if (c == '{' || c == '[') ++depth;
    if (c == '}' || c == ']') {
      if (--depth < 0) return false;
      if (depth == 0 && s.find_first_not_of(" \n", i + 1) != std::string::npos) return false;
    }
  }
  return depth == 0 && !inString;
}

size_t count(const std::string& s, const std::string& what) {
  size_t n = 0;
  for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + 1)) ++n;
  return n;
}

}  // namespace

TEST(BenchJson, ASkippedRunRecordsEveryProfileAndEveryStep) {
  const std::string json = skippedRunJson();
  ASSERT_TRUE(wellFormed(json)) << json;
  EXPECT_EQ(json.rfind("{\"format\":\"rsvp-bench\",\"version\":2,\"kind\":\"benchmark\",\"run\":1,", 0), 0u);
  EXPECT_NE(json.find("\"file\":\"0001-bench.json\""), std::string::npos);
  EXPECT_NE(json.find("\"status\":\"ended-by-owner\""), std::string::npos);
  EXPECT_NE(json.find("\"room_temperature_c\":21"), std::string::npos);
  EXPECT_NE(json.find("\"simulator\":{\"time_scale\":50}"), std::string::npos);
  EXPECT_EQ(count(json, "\"step\":"), 5u * STEP_COUNT) << "every profile lists every step";
  EXPECT_NE(json.find("\"key\":\"trial-a\",\"name\":\"Trial A\",\"status\":\"skipped-part-way\",\"reason\":null,"
                      "\"skipped_from\":\"playback-240-1\""),
            std::string::npos);
  EXPECT_NE(json.find("\"key\":\"trial-b\",\"name\":\"Trial B\",\"status\":\"skipped-part-way\",\"reason\":null,"
                      "\"skipped_from\":\"word-band\""),
            std::string::npos);
  EXPECT_NE(json.find("\"key\":\"trial-c\",\"name\":\"Trial C\",\"status\":\"not-run\",\"reason\":\"ended-by-owner\""),
            std::string::npos);
  EXPECT_NE(json.find("\"status\":\"stopped\",\"attempts\":1,\"requested\":100"), std::string::npos)
      << "Trial B's word band, stopped after 30 updates";
  EXPECT_NE(json.find("\"updates\":{\"count\":30,"), std::string::npos);
  EXPECT_NE(json.find("\"attempts\":2"), std::string::npos) << "the repeated step";
  EXPECT_NE(json.find("\"rating\":\"unreadable\""), std::string::npos);
  EXPECT_NE(json.find("\"residue\":\"no\""), std::string::npos);
  EXPECT_NE(json.find("\"order\":{\"in_order\":true,\"words_drawn\":150,\"segments\":1"), std::string::npos);
  EXPECT_NE(json.find("\"order\":{\"in_order\":true,\"words_drawn\":300,"), std::string::npos) << "endurance";
  EXPECT_NE(json.find("\"trial_setting\":{\"before\":0,\"after\":15,\"unlocked_before\":[],"
                      "\"unlocked_after\":[\"trial-a\",\"trial-b\",\"trial-c\",\"trial-d\"],"
                      "\"chosen_before\":\"stock\",\"chosen_after\":\"stock\"}"),
            std::string::npos);
}

TEST(BenchJson, ACompleteSe0400RunSaysWhyTrialsAToCDidNotRun) {
  const std::string json = se0400RunJson();
  ASSERT_TRUE(wellFormed(json)) << json;
  EXPECT_NE(json.find("\"status\":\"complete\""), std::string::npos);
  EXPECT_NE(json.find("\"panel\":\"SE0400NQW47\""), std::string::npos);
  EXPECT_NE(json.find("\"controller\":\"SSD2677\""), std::string::npos);
  EXPECT_NE(json.find("\"room_temperature_c\":null"), std::string::npos) << "unknown temperature";
  EXPECT_NE(json.find("\"clock\":\"2026-12-01T14:05:00\""), std::string::npos);
  EXPECT_NE(json.find("\"simulator\":null"), std::string::npos);
  EXPECT_EQ(count(json, "\"status\":\"not-allowed\",\"reason\":\"panel-no-tables\""), 3u);
  EXPECT_NE(json.find("\"key\":\"trial-d\",\"name\":\"Trial D\",\"status\":\"done\""), std::string::npos);
  EXPECT_NE(json.find("\"unlocked_after\":[\"trial-d\"]"), std::string::npos);
  EXPECT_EQ(count(json, "\"residue\":\"yes\""), 2u);
}

TEST(BenchJson, ControlsCheckFile) {
  const std::string json = controlsJson();
  ASSERT_TRUE(wellFormed(json)) << json;
  EXPECT_EQ(json.rfind("{\"format\":\"rsvp-bench\",\"version\":2,\"kind\":\"controls\",\"run\":2,", 0), 0u);
  EXPECT_NE(json.find("\"settings\":{\"side_button_layout\":\"prev-next\",\"side_button_layout_value\":0,"
                      "\"short_power_button\":\"ignore\",\"short_power_button_value\":0,\"prev_key\":\"PageBack\","
                      "\"next_key\":\"PageForward\",\"confirm_selects\":true,\"hold_ms\":700,\"exit_hold_ms\":3000}"),
            std::string::npos);
  EXPECT_NE(json.find("{\"n\":1,\"t_ms\":1000,\"source\":\"key\",\"kind\":\"short\",\"raw\":4,\"raw_name\":\"BTN_UP\","
                      "\"logical\":[\"Up\",\"PageBack\",\"NavPrevious\"],\"role\":\"prev\",\"held_ms\":110,"
                      "\"x\":null,\"y\":null,\"x2\":null,\"y2\":null}"),
            std::string::npos);
  EXPECT_NE(json.find("\"kind\":\"swipe-left\",\"raw\":null,\"raw_name\":null,\"logical\":[],\"role\":\"none\","
                      "\"held_ms\":null,\"x\":240,\"y\":400,\"x2\":60,\"y2\":404"),
            std::string::npos);
  EXPECT_NE(json.find("\"summary\":{\"up_pressed\":true,\"up_held\":false,\"down_pressed\":true,\"down_held\":true,"
                      "\"confirm_pressed\":false,\"power_clicked\":true,\"touched\":true}"),
            std::string::npos);
  EXPECT_NE(json.find("\"events_dropped\":0}"), std::string::npos);
}

TEST(BenchJson, StringsAreEscaped) {
  ControlsRecord r;
  snprintf(r.header.firmware.version, sizeof(r.header.firmware.version), "%s", "a\"b\\c\nd");
  StringSink sink;
  writeControls(r, sink);
  EXPECT_NE(sink.text.find("\"version\":\"a\\\"b\\\\c\\u000ad\""), std::string::npos);
  EXPECT_TRUE(wellFormed(sink.text));
}
