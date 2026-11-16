// API, rule and limit tests for CheckpointIndex beyond the shared samples.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

#include "CheckpointTestSupport.h"

using checkpoint::CheckpointIndex;
using checkpoint::IssueCode;
using checkpoint::MemoryCheckpointSource;
using checkpoint::OpenResult;
using checkpoint::Verdict;
using checkpoint_test::bytesOf;

namespace {

const char* SHA = "1e874b76a5df7dff6e3ea423c297d1a8565e2ebf5c98c9e67ac248598f597fdd";

struct Prompt {
  std::string id;
  std::string prompt;
  std::string answer;
};

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

std::string sectionJson(const std::string& id, const std::string& href, const std::string& label = "Label",
                        const std::string& extra = "") {
  return "{\"id\":" + quoted(id) + ",\"chapter_href\":" + quoted(href) + ",\"start_anchor\":\"rsvp-" + id +
         "-start\",\"end_anchor\":\"rsvp-" + id + "-end\",\"label\":" + quoted(label) + ",\"word_count\":120" + extra +
         "}";
}

std::string checkpointJson(const std::string& id, const std::string& section, const std::vector<Prompt>& prompts) {
  std::string out = "{\"id\":" + quoted(id) + ",\"section\":" + quoted(section) + ",\"prompts\":[";
  for (size_t i = 0; i < prompts.size(); ++i) {
    if (i > 0) out += ",";
    out += "{\"id\":" + quoted(prompts[i].id) + ",\"prompt\":" + quoted(prompts[i].prompt) +
           ",\"answer\":" + quoted(prompts[i].answer) + "}";
  }
  return out + "]}";
}

std::string documentJson(const std::string& sections, const std::string& checkpoints, const std::string& book = "",
                         const std::string& extra = "") {
  const std::string bookJson = book.empty() ? "{\"title\":\"T\",\"language\":\"en\",\"source_sha256\":\"" +
                                                  std::string(SHA) + "\",\"instructions_version\":\"1.0\"}"
                                            : book;
  return "{\"format\":\"rsvp-checkpoints\",\"version\":1,\"book\":" + bookJson + ",\"sections\":[" + sections +
         "],\"checkpoints\":[" + checkpoints + "]" + extra + "}";
}

// One section and one checkpoint with one prompt; text parts replaceable.
std::string oneCheckpoint(const std::string& prompt, const std::string& answer, const std::string& promptId = "p1") {
  return documentJson(sectionJson("s1", "c.xhtml"), checkpointJson("c1", "s1", {{promptId, prompt, answer}}));
}

std::string repeat(const std::string& s, size_t n) {
  std::string out;
  out.reserve(s.size() * n);
  for (size_t i = 0; i < n; ++i) out += s;
  return out;
}

// A book shaped like the RAM design point: `sections` sections, ten per
// chapter, each with a checkpoint of `promptsEach` prompts of realistic length.
std::string bigBook(int sections, int promptsEach, size_t promptChars = 150, size_t answerChars = 400) {
  std::string sectionList;
  std::string checkpointList;
  char id[32];
  char href[48];
  for (int i = 1; i <= sections; ++i) {
    snprintf(id, sizeof(id), "s%04d", i);
    snprintf(href, sizeof(href), "text/chapter-%d.xhtml", (i - 1) / 10 + 1);
    if (i > 1) sectionList += ",";
    sectionList += sectionJson(id, href, std::string("Section label ") + id);
    if (promptsEach > 0) {
      std::vector<Prompt> prompts;
      char cid[32];
      snprintf(cid, sizeof(cid), "c%04d", i);
      for (int p = 1; p <= promptsEach; ++p) {
        prompts.push_back({std::string(cid) + "-p" + std::to_string(p), repeat("q", promptChars - 1) + "?",
                           repeat("a", answerChars)});
      }
      if (i > 1) checkpointList += ",";
      checkpointList += checkpointJson(cid, id, prompts);
    }
  }
  return documentJson(sectionList, checkpointList);
}

OpenResult openText(const std::string& text) {
  auto index = std::make_unique<CheckpointIndex>();
  return checkpoint_test::openBytes(*index, bytesOf(text));
}

std::string codeOf(const std::string& text) { return checkpoint::issueCodeName(openText(text).code); }

}  // namespace

TEST(CheckpointIndex, PlanExampleFields) {
  auto index = std::make_unique<CheckpointIndex>();
  checkpoint_test::FileSource source(checkpoint_test::fixturePath("valid/plan-example.json"));
  const OpenResult result = index->open(source);
  ASSERT_EQ(result.verdict, Verdict::Valid) << result.detail;
  EXPECT_EQ(result.code, IssueCode::None);
  EXPECT_TRUE(index->isOpen());

  char title[checkpoint::TITLE_BUFFER_BYTES];
  ASSERT_TRUE(index->loadTitle(title, sizeof(title)));
  EXPECT_STREQ(title, "Example Title");
  EXPECT_STREQ(index->language(), "en");
  EXPECT_STREQ(index->instructionsVersion(), "1.0");
  EXPECT_EQ(strlen(index->sourceSha256()), 64u);

  ASSERT_EQ(index->sectionCount(), 1);
  EXPECT_STREQ(index->sectionId(0), "s0007");
  EXPECT_STREQ(index->sectionChapterHref(0), "text/chapter-3.xhtml");
  EXPECT_EQ(index->sectionWordCount(0), 1180u);
  char label[checkpoint::LABEL_BUFFER_BYTES];
  ASSERT_TRUE(index->loadSectionLabel(0, label, sizeof(label)));
  EXPECT_STREQ(label, "Why spacing works");

  EXPECT_EQ(index->findSection("s0007"), 0);
  EXPECT_EQ(index->findSection("s0008"), -1);
  EXPECT_EQ(index->findSection("S0007"), -1);
  EXPECT_EQ(index->findSectionByEndAnchor("rsvp-s0007-end"), 0);
  EXPECT_EQ(index->findSectionByStartAnchor("rsvp-s0007-start"), 0);
  EXPECT_EQ(index->findSectionByEndAnchor("rsvp-s0007-start"), -1);
  EXPECT_EQ(index->findSectionByEndAnchor("s0007-end"), -1);
  EXPECT_EQ(index->findSectionByEndAnchor("rsvp--end"), -1);
  EXPECT_EQ(index->findSectionByEndAnchor(nullptr), -1);

  EXPECT_EQ(index->chapterCount(), 1);
  EXPECT_EQ(index->findChapter("text/chapter-3.xhtml"), 0);
  EXPECT_EQ(index->findChapter("text/chapter-4.xhtml"), -1);
  EXPECT_EQ(index->nextSectionInChapter(0, -1), 0);
  EXPECT_EQ(index->nextSectionInChapter(0, 0), -1);

  ASSERT_EQ(index->checkpointCount(), 1);
  EXPECT_EQ(index->sectionCheckpoint(0), 0);
  EXPECT_STREQ(index->checkpointId(0), "c0007");
  EXPECT_EQ(index->findCheckpoint("c0007"), 0);
  EXPECT_EQ(index->checkpointSection(0), 0);
  ASSERT_EQ(index->promptCount(0), 1);

  char text[checkpoint::ANSWER_BUFFER_BYTES];
  ASSERT_TRUE(index->loadPromptId(0, 0, text, sizeof(text)));
  EXPECT_STREQ(text, "c0007-p1");
  ASSERT_TRUE(index->loadPromptText(0, 0, text, sizeof(text)));
  EXPECT_STREQ(text, "What two effects does the author say spacing has?");
  ASSERT_TRUE(index->loadAnswerText(0, 0, text, sizeof(text)));
  EXPECT_STREQ(text, "It forces effortful recall, and it exposes what was forgotten.");
}

TEST(CheckpointIndex, EscapesAreDecodedOnLoad) {
  auto index = std::make_unique<CheckpointIndex>();
  checkpoint_test::FileSource source(checkpoint_test::fixturePath("valid/escaped-strings.json"));
  ASSERT_EQ(index->open(source).verdict, Verdict::Valid);
  char text[checkpoint::ANSWER_BUFFER_BYTES];
  ASSERT_TRUE(index->loadTitle(text, sizeof(text)));
  EXPECT_STREQ(text, "Escaped \xC3\xA9 \"quotes\" \\ and / slash");
  EXPECT_STREQ(index->sectionChapterHref(0), "text/ch1.xhtml");
  EXPECT_EQ(index->findSectionByEndAnchor("rsvp-s0001-end"), 0);
  ASSERT_TRUE(index->loadSectionLabel(0, text, sizeof(text)));
  EXPECT_STREQ(text, "Caf\xC3\xA9");
  ASSERT_TRUE(index->loadPromptText(0, 0, text, sizeof(text)));
  EXPECT_STREQ(text, "Line one\nLine two \xF0\x9F\x98\x80");
  ASSERT_TRUE(index->loadAnswerText(0, 0, text, sizeof(text)));
  EXPECT_STREQ(text, "Escaped pair \xF0\x9F\xA7\xA0 and \xE6\x97\xA5");
}

TEST(CheckpointIndex, LoadFailsCleanlyOnSmallBufferOrBadIndex) {
  auto index = std::make_unique<CheckpointIndex>();
  const std::vector<uint8_t> bytes = bytesOf(oneCheckpoint("Why?", "Because it is so."));
  MemoryCheckpointSource source(bytes.data(), bytes.size());
  ASSERT_EQ(index->open(source).verdict, Verdict::Valid);
  char text[8] = "xxxxxxx";
  EXPECT_FALSE(index->loadAnswerText(0, 0, text, sizeof(text)));
  EXPECT_STREQ(text, "");
  EXPECT_TRUE(index->loadPromptText(0, 0, text, 5));  // "Why?" plus NUL
  EXPECT_STREQ(text, "Why?");
  EXPECT_FALSE(index->loadPromptText(0, 0, text, 4));
  EXPECT_FALSE(index->loadPromptText(0, 1, text, sizeof(text)));
  EXPECT_FALSE(index->loadPromptText(1, 0, text, sizeof(text)));
  EXPECT_FALSE(index->loadPromptText(0, 0, nullptr, 0));
  EXPECT_FALSE(index->loadSectionLabel(3, text, sizeof(text)));
  EXPECT_EQ(index->sectionId(1), nullptr);
  EXPECT_EQ(index->sectionChapter(1), checkpoint::NO_INDEX);
  EXPECT_EQ(index->sectionCheckpoint(1), -1);
  EXPECT_EQ(index->checkpointId(1), nullptr);
  EXPECT_EQ(index->checkpointSection(1), checkpoint::NO_INDEX);
  EXPECT_EQ(index->promptCount(1), 0);
  EXPECT_EQ(index->chapterHref(1), nullptr);
  EXPECT_EQ(index->nextSectionInChapter(5, -1), -1);
}

TEST(CheckpointIndex, ClosedAfterRejection) {
  auto index = std::make_unique<CheckpointIndex>();
  const std::vector<uint8_t> good = bytesOf(oneCheckpoint("Why?", "Because."));
  MemoryCheckpointSource goodSource(good.data(), good.size());
  ASSERT_EQ(index->open(goodSource).verdict, Verdict::Valid);
  const std::vector<uint8_t> bad = bytesOf(oneCheckpoint("Why?", "<b>Because.</b>"));
  MemoryCheckpointSource badSource(bad.data(), bad.size());
  const OpenResult result = index->open(badSource);
  EXPECT_EQ(result.verdict, Verdict::Invalid);
  EXPECT_EQ(result.code, IssueCode::NotPlainText);
  EXPECT_STREQ(result.path, "/checkpoints/0/prompts/0/answer");
  EXPECT_FALSE(index->isOpen());
  EXPECT_EQ(index->sectionCount(), 0);
  EXPECT_EQ(index->checkpointCount(), 0);
  EXPECT_STREQ(index->language(), "");
  char text[16];
  EXPECT_FALSE(index->loadTitle(text, sizeof(text)));
}

TEST(CheckpointIndex, ChapterQueriesFollowFileOrder) {
  const std::string text = documentJson(
      sectionJson("a", "ch1.xhtml") + "," + sectionJson("b", "ch2.xhtml") + "," + sectionJson("c", "ch1.xhtml"),
      checkpointJson("k", "c", {{"k1", "Q?", "A."}}));
  auto index = std::make_unique<CheckpointIndex>();
  const std::vector<uint8_t> bytes = bytesOf(text);
  MemoryCheckpointSource source(bytes.data(), bytes.size());
  ASSERT_EQ(index->open(source).verdict, Verdict::Valid);
  EXPECT_EQ(index->chapterCount(), 2);
  const int ch1 = index->findChapter("ch1.xhtml");
  ASSERT_EQ(ch1, 0);
  EXPECT_EQ(index->nextSectionInChapter(0, -1), 0);
  EXPECT_EQ(index->nextSectionInChapter(0, 0), 2);
  EXPECT_EQ(index->nextSectionInChapter(0, 2), -1);
  EXPECT_EQ(index->nextSectionInChapter(1, -1), 1);
  EXPECT_EQ(index->sectionChapter(2), 0);
  EXPECT_EQ(index->sectionCheckpoint(0), -1);
  EXPECT_EQ(index->sectionCheckpoint(2), 0);
  EXPECT_EQ(index->checkpointSection(0), 2);
}

TEST(CheckpointIndex, CheckpointsOutOfSectionOrderLinkCorrectly) {
  auto index = std::make_unique<CheckpointIndex>();
  checkpoint_test::FileSource source(checkpoint_test::fixturePath("valid/checkpoints-out-of-section-order.json"));
  ASSERT_EQ(index->open(source).verdict, Verdict::Valid);
  for (uint16_t c = 0; c < index->checkpointCount(); ++c) {
    const uint16_t s = index->checkpointSection(c);
    ASSERT_NE(s, checkpoint::NO_INDEX);
    EXPECT_EQ(index->sectionCheckpoint(s), c);
  }
}

TEST(CheckpointIndex, CheckpointsBeforeSectionsInFile) {
  const std::string text = "{\"checkpoints\":[" + checkpointJson("c1", "s2", {{"p1", "Q?", "A."}}) +
                           "],\"sections\":[" + sectionJson("s1", "a.xhtml") + "," + sectionJson("s2", "a.xhtml") +
                           "],\"book\":{\"title\":\"T\",\"language\":\"en\",\"source_sha256\":\"" + SHA +
                           "\",\"instructions_version\":\"1\"},\"version\":1,\"format\":\"rsvp-checkpoints\"}";
  auto index = std::make_unique<CheckpointIndex>();
  const std::vector<uint8_t> bytes = bytesOf(text);
  MemoryCheckpointSource source(bytes.data(), bytes.size());
  ASSERT_EQ(index->open(source).verdict, Verdict::Valid);
  EXPECT_EQ(index->sectionCheckpoint(1), 0);
  EXPECT_EQ(index->sectionCheckpoint(0), -1);
}

// "costarring" and "liquid" have the same FNV-1a 32-bit hash, so these cases
// prove every hash match is confirmed against the text.
TEST(CheckpointIndex, HashCollisionsAreNotDuplicates) {
  EXPECT_EQ(codeOf(documentJson(sectionJson("s1", "a.xhtml", "L", ",\"x\":{\"costarring\":1,\"liquid\":2}"), "")), "");
  const std::string sections = sectionJson("costarring", "a.xhtml") + "," + sectionJson("liquid", "a.xhtml");
  const std::string text =
      documentJson(sections, checkpointJson("liquid", "liquid", {{"costarring", "Q?", "A."}, {"liquid", "Q?", "A."}}) +
                                 "," + checkpointJson("costarring", "costarring", {{"panc4v", "Q?", "A."}}) + "," +
                                 checkpointJson("x", "liquid", {{"pbha0a", "Q?", "A."}}));
  const OpenResult result = openText(text);
  EXPECT_EQ(result.code, IssueCode::MultipleCheckpointsForSection);
  EXPECT_STREQ(result.path, "/checkpoints/2/section");

  auto index = std::make_unique<CheckpointIndex>();
  const std::string valid = documentJson(
      sections, checkpointJson("liquid", "liquid", {{"costarring", "Q?", "A."}, {"liquid", "Q?", "A."}}) + "," +
                    checkpointJson("costarring", "costarring", {{"panc4v", "Q?", "A."}, {"pbha0a", "Q?", "A."}}));
  const std::vector<uint8_t> bytes = bytesOf(valid);
  MemoryCheckpointSource source(bytes.data(), bytes.size());
  ASSERT_EQ(index->open(source).verdict, Verdict::Valid);
  EXPECT_EQ(index->findSection("liquid"), 1);
  EXPECT_EQ(index->findSection("costarring"), 0);
  EXPECT_EQ(index->checkpointSection(0), 1);
  EXPECT_EQ(index->checkpointSection(1), 0);
  EXPECT_EQ(index->findCheckpoint("costarring"), 1);

  const OpenResult duplicate = openText(documentJson(
      sections,
      checkpointJson("c1", "liquid", {{"costarring", "Q?", "A."}, {"liquid", "Q?", "A."}, {"liquid", "Q?", "A."}})));
  EXPECT_EQ(duplicate.code, IssueCode::DuplicateId);
  EXPECT_STREQ(duplicate.path, "/checkpoints/0/prompts/2/id");
}

TEST(CheckpointIndex, DuplicateMemberNames) {
  // Escapes are resolved before names are compared.
  EXPECT_EQ(codeOf(R"({"format":"rsvp-checkpoints","format":"x"})"), "duplicate-key");
  // Inside unknown members too, at any depth.
  EXPECT_EQ(codeOf(documentJson(sectionJson("s1", "a.xhtml"), "", "", R"(,"x_tool":{"a":[{"b":1,"b":2}]})")),
            "duplicate-key");
  // Same name in sibling objects is fine.
  EXPECT_EQ(codeOf(documentJson(sectionJson("s1", "a.xhtml"), "", "", R"(,"x":[{"b":1},{"b":2}],"y":{"b":3})")), "");
}

TEST(CheckpointIndex, ParseLayerPrecedenceMatchesReference) {
  const std::string valid = documentJson(sectionJson("s1", "a.xhtml"), "");
  // Python decodes the whole file first: a syntax error after a duplicate key wins.
  EXPECT_EQ(codeOf(R"({"a":1,"a":2,)"), "parse");
  EXPECT_EQ(codeOf(R"({"a":1,"a":2} x)"), "parse");
  // Duplicate keys are found before lone surrogates, wherever they are.
  EXPECT_EQ(codeOf(R"({"x":"\ud800","a":1,"a":2})"), "duplicate-key");
  // A lone surrogate is reported at its string, or at the object for a name.
  OpenResult r = openText(R"({"x":["\udc00"]})");
  EXPECT_EQ(r.code, IssueCode::Parse);
  EXPECT_STREQ(r.path, "/x/0");
  r = openText(R"({"x":{"a\ud800":1}})");
  EXPECT_EQ(r.code, IssueCode::Parse);
  EXPECT_STREQ(r.path, "/x");
  // A high surrogate followed by a non-low escape stays lone.
  EXPECT_EQ(codeOf(R"({"x":"\ud83dA"})"), "parse");
  EXPECT_EQ(codeOf(R"({"x":"\ud83d\n"})"), "parse");
  // Parse problems beat the version layer.
  EXPECT_EQ(codeOf(R"({"format":"rsvp-checkpoints","version":2,"a":1,"a":1})"), "duplicate-key");
  // JSON Pointer escaping of member names.
  r = openText(R"({"a/b~c":"\udfff"})");
  EXPECT_STREQ(r.path, "/a~1b~0c");
  EXPECT_EQ(codeOf(valid), "");
}

TEST(CheckpointIndex, StrictJsonSyntax) {
  const std::string valid = documentJson(sectionJson("s1", "a.xhtml"), "");
  EXPECT_EQ(codeOf(valid + "\n \t\r\n"), "");
  EXPECT_EQ(codeOf(" " + valid), "");
  EXPECT_EQ(codeOf(""), "parse");
  EXPECT_EQ(codeOf("   "), "parse");
  EXPECT_EQ(codeOf(valid + "x"), "parse");
  EXPECT_EQ(codeOf(valid + valid), "parse");
  EXPECT_EQ(codeOf("\xEF\xBB\xBF" + valid), "parse");
  EXPECT_EQ(codeOf(std::string("{\"x\":\"a\0b\"}", 11)), "parse");  // raw control character
  EXPECT_EQ(codeOf("{\"x\":\"\xC0\xAF\"}"), "parse");               // overlong
  EXPECT_EQ(codeOf("{\"x\":\"\xED\xA0\x80\"}"), "parse");           // encoded surrogate
  EXPECT_EQ(codeOf("{\"x\":\"\xF4\x90\x80\x80\"}"), "parse");       // above U+10FFFF
  EXPECT_EQ(codeOf("{\"x\":\"\xE6\x97\"}"), "parse");               // truncated sequence
  EXPECT_EQ(codeOf("{\"x\":\"\x80\"}"), "parse");                   // stray continuation
  EXPECT_EQ(codeOf("{\"\xFF\":1}"), "parse");
  EXPECT_EQ(codeOf(R"({"x":NaN})"), "parse");
  EXPECT_EQ(codeOf(R"({"x":Infinity})"), "parse");
  EXPECT_EQ(codeOf(R"({"x":-Infinity})"), "parse");
  EXPECT_EQ(codeOf(R"({"x":[1,]})"), "parse");
  EXPECT_EQ(codeOf(R"({"x":1,})"), "parse");
  EXPECT_EQ(codeOf(R"({"x":01})"), "parse");
  EXPECT_EQ(codeOf(R"({"x":1.})"), "parse");
  EXPECT_EQ(codeOf(R"({"x":.5})"), "parse");
  EXPECT_EQ(codeOf(R"({"x":1e})"), "parse");
  EXPECT_EQ(codeOf(R"({"x":+1})"), "parse");
  EXPECT_EQ(codeOf(R"({"x":-})"), "parse");
  EXPECT_EQ(codeOf(R"({"x":tru})"), "parse");
  EXPECT_EQ(codeOf(R"({"x":"\x"})"), "parse");
  EXPECT_EQ(codeOf(R"({"x":"\u12g4"})"), "parse");
  EXPECT_EQ(codeOf(R"({'x':1})"), "parse");
  EXPECT_EQ(codeOf(R"({"x" 1})"), "parse");
  EXPECT_EQ(codeOf(R"({"x":1 "y":2})"), "parse");
  EXPECT_EQ(codeOf(R"([1] )"), "schema");
  EXPECT_EQ(codeOf(R"("text")"), "schema");
  EXPECT_EQ(codeOf(R"(null)"), "schema");
  EXPECT_EQ(codeOf(R"({"x":1e400,"y":-0.0e-400,"z":-0})"), "schema");  // valid JSON, missing members
}

TEST(CheckpointIndex, VersionLayer) {
  auto withVersion = [](const std::string& version, const std::string& format = "rsvp-checkpoints") {
    return "{\"format\":\"" + format + "\",\"version\":" + version + "}";
  };
  OpenResult r = openText(withVersion("2"));
  EXPECT_EQ(r.verdict, Verdict::UnsupportedVersion);
  EXPECT_EQ(r.code, IssueCode::UnsupportedVersion);
  EXPECT_STREQ(r.path, "/version");
  EXPECT_EQ(openText(withVersion("99999999999999999999999")).verdict, Verdict::UnsupportedVersion);
  EXPECT_EQ(openText(withVersion("10")).verdict, Verdict::UnsupportedVersion);
  EXPECT_EQ(openText(R"({"version":2,"format":"rsvp-checkpoints","sections":{"x":[]}})").verdict,
            Verdict::UnsupportedVersion);
  EXPECT_EQ(codeOf(withVersion("2", "rsvp-checkpoint")), "schema");
  EXPECT_EQ(codeOf(withVersion("-2")), "schema");
  EXPECT_EQ(codeOf(withVersion("-0")), "schema");
  EXPECT_EQ(codeOf(withVersion("2.0")), "schema");
  EXPECT_EQ(codeOf(withVersion("2e0")), "schema");
  EXPECT_EQ(codeOf(withVersion("\"2\"")), "schema");
  EXPECT_EQ(codeOf(withVersion("true")), "schema");
  EXPECT_EQ(codeOf(withVersion("[2]")), "schema");
  EXPECT_EQ(codeOf(R"([{"format":"rsvp-checkpoints","version":2}])"), "schema");
  // A firmware limit does not hide a newer version.
  EXPECT_EQ(openText(withVersion("2") + "").verdict, Verdict::UnsupportedVersion);
  const std::string manySections = "{\"format\":\"rsvp-checkpoints\",\"version\":3,\"sections\":[" +
                                   repeat(sectionJson("s1", "a.xhtml") + ",", 600) + sectionJson("s1", "a.xhtml") +
                                   "]}";
  EXPECT_EQ(openText(manySections).verdict, Verdict::UnsupportedVersion);
}

TEST(CheckpointIndex, SchemaRules) {
  const std::string section = sectionJson("s1", "a.xhtml");
  auto wordCount = [&](const std::string& value) {
    return documentJson(
        "{\"id\":\"s1\",\"chapter_href\":\"a\",\"start_anchor\":\"rsvp-s1-start\",\"end_anchor\":\"rsvp-s1-end\","
        "\"label\":\"L\",\"word_count\":" +
            value + "}",
        "");
  };
  EXPECT_EQ(codeOf(wordCount("1")), "");
  EXPECT_EQ(codeOf(wordCount("2147483647")), "");
  EXPECT_EQ(codeOf(wordCount("2147483648")), "schema");
  EXPECT_EQ(codeOf(wordCount("99999999999999999999")), "schema");
  EXPECT_EQ(codeOf(wordCount("0")), "schema");
  EXPECT_EQ(codeOf(wordCount("-1")), "schema");
  EXPECT_EQ(codeOf(wordCount("1.0")), "schema");
  EXPECT_EQ(codeOf(wordCount("1e2")), "schema");
  EXPECT_EQ(codeOf(wordCount("true")), "schema");
  EXPECT_EQ(codeOf(wordCount("null")), "schema");

  auto sectionId = [](const std::string& id) {
    return documentJson("{\"id\":\"" + id + "\",\"chapter_href\":\"a\",\"start_anchor\":\"rsvp-" + id +
                            "-start\",\"end_anchor\":\"rsvp-" + id + "-end\",\"label\":\"L\",\"word_count\":1}",
                        "");
  };
  EXPECT_EQ(codeOf(sectionId("a_-9")), "");
  EXPECT_EQ(codeOf(sectionId("9")), "");
  EXPECT_EQ(codeOf(sectionId(repeat("x", 64))), "");
  EXPECT_EQ(codeOf(sectionId(repeat("x", 65))), "schema");
  EXPECT_EQ(codeOf(sectionId("_a")), "schema");
  EXPECT_EQ(codeOf(sectionId("a.b")), "schema");
  EXPECT_EQ(codeOf(sectionId("a\\n")), "schema");

  // Lengths are code points after unescaping.
  const std::string fourByte = "\xF0\x9F\x98\x80";
  EXPECT_EQ(codeOf(oneCheckpoint(repeat(fourByte, 300), repeat(fourByte, 600))), "");
  EXPECT_EQ(codeOf(oneCheckpoint(repeat("\\ud83d\\ude00", 300), "A.")), "");
  EXPECT_EQ(codeOf(oneCheckpoint(repeat("\\ud83d\\ude00", 301), "A.")), "schema");
  EXPECT_EQ(codeOf(oneCheckpoint("Q", repeat("\\u00e9", 601))), "schema");
  EXPECT_EQ(codeOf(oneCheckpoint("", "A.")), "schema");
  EXPECT_EQ(codeOf(oneCheckpoint("Q", "")), "schema");

  // Schema issues are reported at the first path in sorted order, as tools/prep sorts them.
  OpenResult r = openText(R"({"version":"1","book":7})");
  EXPECT_EQ(r.code, IssueCode::Schema);
  EXPECT_STREQ(r.path, "");
  r = openText(documentJson(
      section, "", "{\"title\":\"\",\"language\":\"\",\"source_sha256\":\"x\",\"instructions_version\":\"1\"}"));
  EXPECT_STREQ(r.path, "/book/language");
  EXPECT_EQ(codeOf(documentJson("", "")), "schema");
  EXPECT_EQ(codeOf(documentJson(section, "{\"id\":\"c\",\"section\":\"s1\",\"prompts\":[]}")), "schema");
  EXPECT_EQ(codeOf(documentJson(section, "7")), "schema");
  EXPECT_EQ(codeOf(documentJson(section + ",[]", "")), "schema");
}

TEST(CheckpointIndex, PlainAndBlankTextRules) {
  auto check = [](const std::string& prompt) { return codeOf(oneCheckpoint(prompt, "Answer.")); };
  EXPECT_EQ(check("a < b and x<3 and a <= b"), "");
  EXPECT_EQ(check("x<y"), "not-plain-text");
  EXPECT_EQ(check("<<a"), "not-plain-text");
  EXPECT_EQ(check("a </"), "not-plain-text");
  EXPECT_EQ(check("<!--"), "not-plain-text");
  EXPECT_EQ(check("<?"), "not-plain-text");
  EXPECT_EQ(check("Q&A, R & D, &amp, &#; &#x; &#xg; &1;"), "");
  EXPECT_EQ(check("&amp;"), "not-plain-text");
  EXPECT_EQ(check("&a1;"), "not-plain-text");
  EXPECT_EQ(check("&&amp;"), "not-plain-text");
  EXPECT_EQ(check("&#12;"), "not-plain-text");
  EXPECT_EQ(check("&#X1f;"), "not-plain-text");
  EXPECT_EQ(check("&#x1g;"), "");
  EXPECT_EQ(check("line\\nline"), "");
  EXPECT_EQ(check("tab\\t"), "not-plain-text");
  EXPECT_EQ(check("\\u007f"), "not-plain-text");
  EXPECT_EQ(check("\xC2\x85"), "not-plain-text");  // U+0085
  EXPECT_EQ(check("\xC2\x9F"), "not-plain-text");  // U+009F
  EXPECT_EQ(check("\xC2\xA0x"), "");               // U+00A0 is allowed
  EXPECT_EQ(check("\xC2\xA0"), "blank-text");
  EXPECT_EQ(check("\xE3\x80\x80 \\n"), "blank-text");  // U+3000, space, LF
  EXPECT_EQ(check("\xE2\x80\x8B"), "");                // U+200B is not white space
  EXPECT_EQ(check("\\t"), "not-plain-text");           // not-plain-text is reported before blank-text
  // The prompt is checked before the answer, whatever the member order.
  const std::string text = documentJson(
      sectionJson("s1", "a"), R"({"id":"c","section":"s1","prompts":[{"answer":"\t","id":"p","prompt":" "}]})");
  const OpenResult r = openText(text);
  EXPECT_EQ(r.code, IssueCode::BlankText);
  EXPECT_STREQ(r.path, "/checkpoints/0/prompts/0/prompt");
}

TEST(CheckpointIndex, CrossReferenceOrderMatchesReference) {
  // Section rules come before checkpoint rules, whatever the file order.
  const std::string text = "{\"checkpoints\":[" + checkpointJson("c1", "nope", {{"p1", "Q?", "A."}}) +
                           "],\"sections\":[" + sectionJson("s1", "a") + "," +
                           "{\"id\":\"s2\",\"chapter_href\":\"a\",\"start_anchor\":\"rsvp-s2-start\",\"end_anchor\":"
                           "\"rsvp-s1-end\",\"label\":\"L\",\"word_count\":1}" +
                           "],\"book\":{\"title\":\"T\",\"language\":\"en\",\"source_sha256\":\"" + SHA +
                           "\",\"instructions_version\":\"1\"},\"version\":1,\"format\":\"rsvp-checkpoints\"}";
  const OpenResult r = openText(text);
  EXPECT_EQ(r.code, IssueCode::AnchorName);
  EXPECT_STREQ(r.path, "/sections/1/end_anchor");
  // A duplicate section id is reported before that section's anchors.
  const OpenResult d = openText(documentJson(sectionJson("s1", "a") + "," +
                                                 "{\"id\":\"s1\",\"chapter_href\":\"a\",\"start_anchor\":\"x\",\"end_"
                                                 "anchor\":\"y\",\"label\":\"L\",\"word_count\":1}",
                                             ""));
  EXPECT_EQ(d.code, IssueCode::DuplicateId);
  EXPECT_STREQ(d.path, "/sections/1/id");
}

TEST(CheckpointIndex, FirmwareLimits) {
  // Entry counts.
  EXPECT_EQ(codeOf(bigBook(checkpoint::MAX_SECTIONS, 1, 10, 10)), "");
  EXPECT_EQ(codeOf(bigBook(checkpoint::MAX_SECTIONS + 1, 0)), "limit");
  std::vector<Prompt> prompts;
  for (int i = 0; i < checkpoint::MAX_PROMPTS_PER_CHECKPOINT; ++i)
    prompts.push_back({"p" + std::to_string(i), "Q", "A"});
  EXPECT_EQ(codeOf(documentJson(sectionJson("s1", "a"), checkpointJson("c1", "s1", prompts))), "");
  prompts.push_back({"extra", "Q", "A"});
  EXPECT_EQ(codeOf(documentJson(sectionJson("s1", "a"), checkpointJson("c1", "s1", prompts))), "limit");
  EXPECT_EQ(codeOf(bigBook(checkpoint::MAX_PROMPTS / 4, 4, 5, 5)), "");
  EXPECT_EQ(codeOf(bigBook(checkpoint::MAX_PROMPTS / 4 + 1, 4, 5, 5)), "limit");

  // Text lengths, in code points.
  const std::string e = "\xC3\xA9";
  EXPECT_EQ(codeOf(documentJson(sectionJson("s1", "a", repeat(e, checkpoint::MAX_LABEL_CHARS)), "")), "");
  EXPECT_EQ(codeOf(documentJson(sectionJson("s1", "a", repeat(e, checkpoint::MAX_LABEL_CHARS + 1)), "")), "limit");
  EXPECT_EQ(codeOf(documentJson(sectionJson("s1", repeat(e, checkpoint::MAX_HREF_CHARS)), "")), "");
  EXPECT_EQ(codeOf(documentJson(sectionJson("s1", repeat("h", checkpoint::MAX_HREF_CHARS + 1)), "")), "limit");
  auto book = [](const std::string& title, const std::string& language, const std::string& iv) {
    return "{\"title\":\"" + title + "\",\"language\":\"" + language + "\",\"source_sha256\":\"" + SHA +
           "\",\"instructions_version\":\"" + iv + "\"}";
  };
  const std::string s = sectionJson("s1", "a");
  EXPECT_EQ(codeOf(documentJson(s, "", book(repeat(e, 300), repeat(e, 35), repeat(e, 32)))), "");
  EXPECT_EQ(codeOf(documentJson(s, "", book(repeat(e, 301), "en", "1"))), "limit");
  EXPECT_EQ(codeOf(documentJson(s, "", book("T", repeat(e, 36), "1"))), "limit");
  EXPECT_EQ(codeOf(documentJson(s, "", book("T", "en", repeat(e, 33)))), "limit");

  // The string pool: long ids on many sections.
  std::string sections;
  for (int i = 0; i < 300; ++i) {
    char id[80];
    snprintf(id, sizeof(id), "%s%04d", repeat("x", 60).c_str(), i);
    if (i > 0) sections += ",";
    sections += sectionJson(id, "a");
  }
  OpenResult r = openText(documentJson(sections, ""));
  EXPECT_EQ(r.code, IssueCode::Limit);
  EXPECT_NE(std::string(r.detail).find("STRING_POOL_BYTES"), std::string::npos);

  // Structure.
  EXPECT_EQ(codeOf("{\"x\":" + repeat("[", checkpoint::MAX_DEPTH - 1) + repeat("]", checkpoint::MAX_DEPTH - 1) + "}"),
            "schema");
  EXPECT_EQ(codeOf("{\"x\":" + repeat("[", checkpoint::MAX_DEPTH) + repeat("]", checkpoint::MAX_DEPTH) + "}"), "limit");
  std::string members;
  for (int i = 0; i < 100; ++i) members += ",\"m" + std::to_string(i) + "\":" + std::to_string(i);
  EXPECT_EQ(codeOf(documentJson(s, "", "", ",\"x\":{\"first\":0" + members + "}")), "");
  for (int i = 100; i < 200; ++i) members += ",\"m" + std::to_string(i) + "\":" + std::to_string(i);
  EXPECT_EQ(codeOf(documentJson(s, "", "", ",\"x\":{\"first\":0" + members + "}")), "limit");
  EXPECT_EQ(codeOf(documentJson(s, "", "", ",\"x\":" + repeat("9", checkpoint::MAX_NUMBER_CHARS))), "");
  EXPECT_EQ(codeOf(documentJson(s, "", "", ",\"x\":" + repeat("9", checkpoint::MAX_NUMBER_CHARS + 1))), "limit");

  // File size.
  auto index = std::make_unique<CheckpointIndex>();
  checkpoint_test::HugeSource huge(checkpoint::MAX_FILE_BYTES + 1);
  r = index->open(huge);
  EXPECT_EQ(r.code, IssueCode::Limit);
  checkpoint_test::HugeSource whitespace(checkpoint::MAX_FILE_BYTES);
  EXPECT_EQ(index->open(whitespace).code, IssueCode::Parse);
}

TEST(CheckpointIndex, ReadErrorsAreReported) {
  const std::vector<uint8_t> bytes = bytesOf(oneCheckpoint("Why?", "Because."));
  auto index = std::make_unique<CheckpointIndex>();
  for (uint32_t good : {0u, 1u, 100u, static_cast<uint32_t>(bytes.size() - 1)}) {
    checkpoint_test::FailingSource source(bytes, good);
    EXPECT_EQ(index->open(source).code, IssueCode::Io) << good;
  }
}

// The RAM design point: 300 sections, 600 prompts, a file of a few hundred KB.
TEST(CheckpointIndex, DesignPointBook) {
  const std::string text = bigBook(300, 2);
  auto index = std::make_unique<CheckpointIndex>();
  const std::vector<uint8_t> bytes = bytesOf(text);
  MemoryCheckpointSource source(bytes.data(), bytes.size());
  const auto start = std::chrono::steady_clock::now();
  const OpenResult result = index->open(source);
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  ASSERT_EQ(result.verdict, Verdict::Valid) << result.detail << " at " << result.offset;
  printf("design point: file %zu bytes, sizeof(CheckpointIndex) %zu bytes, open %lld us (host, sanitizers on)\n",
         bytes.size(), sizeof(CheckpointIndex), static_cast<long long>(elapsed));
  EXPECT_EQ(index->sectionCount(), 300);
  EXPECT_EQ(index->checkpointCount(), 300);
  EXPECT_EQ(index->chapterCount(), 30);
  EXPECT_EQ(index->findSectionByEndAnchor("rsvp-s0299-end"), 298);
  EXPECT_EQ(index->findChapter("text/chapter-30.xhtml"), 29);
  int inChapter = 0;
  for (int s = index->nextSectionInChapter(29, -1); s >= 0; s = index->nextSectionInChapter(29, s)) ++inChapter;
  EXPECT_EQ(inChapter, 10);
  char text2[checkpoint::ANSWER_BUFFER_BYTES];
  ASSERT_TRUE(index->loadPromptId(299, 1, text2, sizeof(text2)));
  EXPECT_STREQ(text2, "c0300-p2");
  ASSERT_TRUE(index->loadAnswerText(299, 1, text2, sizeof(text2)));
  EXPECT_EQ(strlen(text2), 400u);
}
