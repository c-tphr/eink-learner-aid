// State file encoding (docs/PLAN.md section 7, "State file"): the plan's
// example, round trips of random states, truncations and random edits under
// ASan/UBSan, forward compatibility, versions and the book hash.

#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

#include "StateTestSupport.h"

using namespace checkpoint;
using checkpoint_test::decodeString;
using checkpoint_test::encodeToString;
using checkpoint_test::readFile;

namespace {

const std::string SHA = "b07c3ea9857b00644d284c0eae083bc504bfc895525d0a864c183b916e17c82c";
const std::string OTHER_SHA = "1e874b76a5df7dff6e3ea423c297d1a8565e2ebf5c98c9e67ac248598f597fdd";

// The ```json block under "### State file" in docs/PLAN.md, with its
// placeholder hash replaced by a real one (book_sha256 must be 64 hex digits).
std::string planExample() {
  const std::vector<uint8_t> bytes = readFile(CHECKPOINT_PLAN_PATH);
  const std::string plan(bytes.begin(), bytes.end());
  const size_t heading = plan.find("### State file");
  const size_t start = plan.find("```json\n", heading);
  const size_t end = plan.find("```", start + 8);
  if (heading == std::string::npos || start == std::string::npos || end == std::string::npos) return "";
  std::string example = plan.substr(start + 8, end - start - 8);
  const std::string placeholder = "<the checkpoint file's book.source_sha256>";
  const size_t at = example.find(placeholder);
  if (at == std::string::npos) return "";
  return example.replace(at, placeholder.size(), SHA);
}

bool isFresh(const BookState& s) {
  auto fresh = std::make_unique<BookState>();
  return s.sameAs(*fresh);
}

std::string randomId(std::mt19937& rng, size_t maxLength = MAX_ID_CHARS) {
  static const char FIRST[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  static const char REST[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";
  const size_t length = 1 + rng() % maxLength;
  std::string id(1, FIRST[rng() % (sizeof(FIRST) - 1)]);
  while (id.size() < length) id += REST[rng() % (sizeof(REST) - 1)];
  return id;
}

Place randomPlace(std::mt19937& rng) {
  Place p;
  p.position.chapter = static_cast<uint16_t>(rng() % 3 == 0 ? NO_CHAPTER - 1 : rng() % 300);
  p.position.word = rng() % 4 == 0 ? 0xFFFFFFFFu : rng() % 100000;
  p.mode = rng() % 2 ? ReadingMode::Rsvp : ReadingMode::Page;
  return p;
}

void randomState(std::mt19937& rng, BookState& s, int maxGrades = GRADE_CAPACITY) {
  s.reset();
  if (rng() % 4 != 0) {
    for (size_t i = 0; i < SHA256_HEX_CHARS; ++i) s.bookSha256[i] = "0123456789abcdef"[rng() % 16];
    s.bookSha256[SHA256_HEX_CHARS] = '\0';
  }
  if (rng() % 2) {
    s.overrides.hasWpm = true;
    s.overrides.wpm = static_cast<uint16_t>(1 + rng() % 65535);
  }
  if (rng() % 2) {
    s.overrides.hasWordsPerUpdate = true;
    s.overrides.wordsPerUpdate = static_cast<uint8_t>(1 + rng() % 255);
  }
  if (rng() % 5 != 0) {
    s.hasPosition = true;
    s.position = randomPlace(rng);
  }
  const int grades = static_cast<int>(rng() % 10 == 0 ? rng() % static_cast<unsigned>(maxGrades) : rng() % 8u);
  for (int i = 0; i < grades; ++i) {
    const std::string id = randomId(rng);
    const Timestamp at = rng() % 5 == 0 ? NO_TIMESTAMP : static_cast<Timestamp>(rng() % 4291747200u);
    s.grades.recordFirst(id.c_str(), id.size(), rng() % 2 ? Grade::Correct : Grade::Incorrect, at);
  }
  if (rng() % 2) {
    s.hasActive = true;
    const std::string id = randomId(rng);
    memcpy(s.active.id, id.c_str(), id.size() + 1);
    s.active.promptIndex = static_cast<uint16_t>(rng() % MAX_PROMPTS_PER_CHECKPOINT);
    s.active.resume = randomPlace(rng);
    if (rng() % 2) {
      s.active.rereading = true;
      s.active.reread = randomPlace(rng);
    }
  }
}

// Decoding `text` never crashes; a success holds the invariants and
// re-encodes stably; a failure leaves a fresh state. Returns the status.
struct SafetyCheck {
  std::unique_ptr<BookState> out = std::make_unique<BookState>();
  std::unique_ptr<BookState> second = std::make_unique<BookState>();
  std::unique_ptr<BookState> fresh = std::make_unique<BookState>();

  DecodeStatus run(const std::string& text) {
    const DecodeResult r = decodeString(text, *out, SHA.c_str());
    if (r.status == DecodeStatus::Ok) {
      EXPECT_TRUE(stateInvariantsHold(*out)) << text;
      const std::string again = encodeToString(*out);
      EXPECT_EQ(decodeString(again, *second).status, DecodeStatus::Ok) << text;
      EXPECT_TRUE(second->sameAs(*out)) << text;
    } else {
      EXPECT_TRUE(out->sameAs(*fresh)) << decodeStatusName(r.status);
    }
    return r.status;
  }
};

}  // namespace

TEST(StateCodec, PlanExampleDecodesAndReencodesToTheSameDocument) {
  const std::string example = planExample();
  ASSERT_FALSE(example.empty());
  auto state = std::make_unique<BookState>();
  const DecodeResult r = decodeString(example, *state, SHA.c_str());
  ASSERT_EQ(r.status, DecodeStatus::Ok) << r.detail << " at " << r.offset;
  EXPECT_FALSE(r.shaMismatch);
  EXPECT_EQ(state->overrides.wpm, 240);
  EXPECT_EQ(state->overrides.wordsPerUpdate, 1);
  EXPECT_EQ(state->position.position, (Position{3, 1542}));
  EXPECT_EQ(state->position.mode, ReadingMode::Rsvp);
  EXPECT_EQ(state->grades.count(), 2);
  EXPECT_EQ(state->grades.find("c0007-p1"), Grade::Correct);
  EXPECT_EQ(state->grades.find("c0007-p2"), Grade::Incorrect);
  ASSERT_TRUE(state->hasActive);
  EXPECT_STREQ(state->active.id, "c0007");
  EXPECT_EQ(state->active.promptIndex, 1);
  EXPECT_EQ(state->active.resume.position, (Position{3, 1543}));
  EXPECT_EQ(state->active.resume.mode, ReadingMode::Rsvp);
  ASSERT_TRUE(state->active.rereading);
  EXPECT_EQ(state->active.reread.position, (Position{3, 910}));
  EXPECT_EQ(state->active.reread.mode, ReadingMode::Page);
  // Byte for byte, member order and layout included.
  EXPECT_EQ(encodeToString(*state), example);
  // The example as written has a placeholder hash, which is refused.
  auto raw = std::make_unique<BookState>();
  std::string placeholder = example;
  placeholder.replace(placeholder.find(SHA), SHA.size(), "<hash of the prepared file's source>");
  EXPECT_EQ(decodeString(placeholder, *raw).status, DecodeStatus::Schema);
}

TEST(StateCodec, RandomStatesRoundTrip) {
  std::mt19937 rng(20261117);
  auto state = std::make_unique<BookState>();
  auto decoded = std::make_unique<BookState>();
  for (int i = 0; i < 1500; ++i) {
    randomState(rng, *state);
    ASSERT_TRUE(stateInvariantsHold(*state));
    const std::string text = encodeToString(*state);
    ASSERT_FALSE(text.empty());
    ASSERT_LE(text.size(), STATE_ENCODED_MAX_BYTES);
    const DecodeResult r = decodeString(text, *decoded, state->bookSha256);
    ASSERT_EQ(r.status, DecodeStatus::Ok) << r.detail << " at " << r.offset << "\n" << text;
    ASSERT_FALSE(r.shaMismatch);
    ASSERT_TRUE(decoded->sameAs(*state)) << text;
    ASSERT_EQ(encodeToString(*decoded), text);
  }
}

TEST(StateCodec, WorstCaseStateEncodesToExactlyTheMaximumSize) {
  auto s = std::make_unique<BookState>();
  memcpy(s->bookSha256, SHA.c_str(), SHA.size() + 1);
  s->overrides = Overrides{true, 65535, true, 255};
  s->hasPosition = true;
  s->position = Place{Position{NO_CHAPTER - 1, 0xFFFFFFFFu}, ReadingMode::Rsvp};
  char id[MAX_ID_CHARS + 1];
  for (uint16_t i = 0; i < GRADE_CAPACITY; ++i) {
    snprintf(id, sizeof(id), "%064u", static_cast<unsigned>(i));
    s->grades.recordFirst(id, Grade::Incorrect, "2026-12-01T19:05:10");
  }
  s->hasActive = true;
  memset(s->active.id, 'c', MAX_ID_CHARS);
  s->active.promptIndex = MAX_PROMPTS_PER_CHECKPOINT - 1;
  s->active.resume = s->position;
  s->active.rereading = true;
  s->active.reread = s->position;
  const std::string text = encodeToString(*s);
  EXPECT_EQ(text.size(), STATE_ENCODED_MAX_BYTES);
  std::vector<char> small(STATE_ENCODED_MAX_BYTES - 1);
  BufferStateSink sink(small.data(), small.size());
  EXPECT_FALSE(encodeState(*s, sink));
  EXPECT_TRUE(sink.overflowed());
  auto back = std::make_unique<BookState>();
  ASSERT_EQ(decodeString(text, *back).status, DecodeStatus::Ok);
  EXPECT_TRUE(back->sameAs(*s));
}

TEST(StateCodec, EveryTruncationIsRejectedCleanly) {
  std::mt19937 rng(7);
  std::vector<std::string> samples = {planExample()};
  auto s = std::make_unique<BookState>();
  for (int i = 0; i < 8; ++i) {
    randomState(rng, *s, 40);
    samples.push_back(encodeToString(*s));
  }
  SafetyCheck check;
  for (const std::string& text : samples) {
    const size_t closing = text.rfind('}');
    for (size_t n = 0; n < text.size(); ++n) {
      const DecodeStatus status = check.run(text.substr(0, n));
      if (n <= closing) {
        ASSERT_NE(status, DecodeStatus::Ok) << n;
      } else {
        ASSERT_EQ(status, DecodeStatus::Ok);  // only the final newline is missing
      }
      if (HasFailure()) return;
    }
  }
}

TEST(StateCodec, RandomEditsNeverCrashOrBreakTheInvariants) {
  std::mt19937 rng(25);
  std::vector<std::string> samples = {planExample()};
  auto s = std::make_unique<BookState>();
  for (int i = 0; i < 8; ++i) {
    randomState(rng, *s, 40);
    samples.push_back(encodeToString(*s));
  }
  static const char INTERESTING[] = "{}[]\",:0123456789-+.eE \\u\"truefalsenull";
  SafetyCheck check;
  int accepted = 0;
  for (int i = 0; i < 30000; ++i) {
    std::string text = samples[rng() % samples.size()];
    const int edits = 1 + static_cast<int>(rng() % 4);
    for (int e = 0; e < edits && !text.empty(); ++e) {
      const size_t at = rng() % text.size();
      const char c = rng() % 2 ? INTERESTING[rng() % (sizeof(INTERESTING) - 1)] : static_cast<char>(rng() % 256);
      switch (rng() % 3) {
        case 0:
          text[at] = c;
          break;
        case 1:
          text.insert(text.begin() + static_cast<std::ptrdiff_t>(at), c);
          break;
        default:
          text.erase(at, 1 + rng() % 8);
      }
    }
    accepted += check.run(text) == DecodeStatus::Ok ? 1 : 0;
    if (HasFailure()) return;
  }
  std::printf("random edits: 30000 decoded, %d still valid\n", accepted);
  // Random bytes.
  for (int i = 0; i < 2000; ++i) {
    std::string text(rng() % 200, '\0');
    for (char& c : text) c = static_cast<char>(rng() % 256);
    check.run(text);
    if (HasFailure()) return;
  }
}

TEST(StateCodec, UnknownMembersAreIgnoredAtEveryLevel) {
  const std::string text =
      "{\"future\": {\"a\": [1, {\"b\": null}, -2.5e3]}, \"version\": 1, \"book_sha256\": \"" + SHA +
      "\", \"overrides\": {\"wpm\": 300, \"display_profile\": \"trial-a\"},"
      " \"position\": {\"x\": true, \"chapter\": 2, \"word\": 7, \"mode\": \"page\", \"offset\": 1234},"
      " \"grades\": {\"p1\": {\"first\": \"correct\", \"source\": \"device\"}},"
      " \"active_checkpoint\": {\"id\": \"c1\", \"prompt_index\": 0, \"note\": [],"
      " \"resume\": {\"chapter\": 2, \"word\": 8, \"mode\": \"rsvp\", \"y\": {}},"
      " \"reread\": {\"mode\": \"rsvp\", \"chapter\": 2, \"word\": 3, \"z\": \"\\u00e9\"}}, \"zz\": false}";
  auto s = std::make_unique<BookState>();
  const DecodeResult r = decodeString(text, *s, SHA.c_str());
  ASSERT_EQ(r.status, DecodeStatus::Ok) << r.detail << " at " << r.offset;
  EXPECT_EQ(s->overrides.wpm, 300);
  EXPECT_FALSE(s->overrides.hasWordsPerUpdate);
  EXPECT_EQ(s->position.position, (Position{2, 7}));
  EXPECT_EQ(s->grades.find("p1"), Grade::Correct);
  EXPECT_TRUE(s->active.rereading);
  EXPECT_EQ(s->active.reread.position, (Position{2, 3}));
}

TEST(StateCodec, AHigherVersionIsRejectedWhateverItsStructure) {
  auto s = std::make_unique<BookState>();
  EXPECT_EQ(decodeString("{\"version\": 2}", *s).status, DecodeStatus::UnsupportedVersion);
  EXPECT_EQ(decodeString("{\"position\": [1, 2], \"version\": 17, \"grades\": 3}", *s).status,
            DecodeStatus::UnsupportedVersion);
  // Not an integer above 1: a schema problem.
  for (const char* v : {"0", "1.0", "1e0", "-1", "\"1\"", "null", "2.5"}) {
    const std::string text = std::string("{\"version\": ") + v +
                             ", \"position\": {\"chapter\": 0, \"word\": 0, "
                             "\"mode\": \"page\"}}";
    EXPECT_EQ(decodeString(text, *s).status, DecodeStatus::Schema) << v;
  }
  EXPECT_EQ(decodeString("{}", *s).status, DecodeStatus::Schema);
  // Syntax is checked first.
  EXPECT_EQ(decodeString("{\"version\": 2,}", *s).status, DecodeStatus::Syntax);
}

TEST(StateCodec, ADifferentBookHashIsReported) {
  const std::string text = "{\"version\": 1, \"book_sha256\": \"" + SHA + "\", \"overrides\": {}}";
  auto s = std::make_unique<BookState>();
  DecodeResult r = decodeString(text, *s, OTHER_SHA.c_str());
  EXPECT_EQ(r.status, DecodeStatus::Ok);
  EXPECT_TRUE(r.shaMismatch);
  EXPECT_STREQ(s->bookSha256, SHA.c_str());
  EXPECT_FALSE(decodeString(text, *s, SHA.c_str()).shaMismatch);
  EXPECT_FALSE(decodeString(text, *s, "").shaMismatch);
  EXPECT_FALSE(decodeString(text, *s, nullptr).shaMismatch);
  EXPECT_FALSE(decodeString("{\"version\": 1}", *s, SHA.c_str()).shaMismatch);
}

TEST(StateCodec, ABookWithoutCheckpointDataHoldsOnlyOverridesAndPosition) {
  auto s = std::make_unique<BookState>();
  s->overrides.hasWpm = true;
  s->overrides.wpm = 320;
  s->hasPosition = true;
  s->position = Place{Position{4, 17}, ReadingMode::Page};
  EXPECT_EQ(encodeToString(*s),
            "{\n  \"version\": 1,\n  \"overrides\": { \"wpm\": 320 },\n"
            "  \"position\": { \"chapter\": 4, \"word\": 17, \"mode\": \"page\" }\n}\n");
  s->reset();
  EXPECT_EQ(encodeToString(*s), "{\n  \"version\": 1,\n  \"overrides\": {}\n}\n");
  // A prepared book with no grades yet writes its hash and an empty grade table.
  memcpy(s->bookSha256, SHA.c_str(), SHA.size() + 1);
  EXPECT_EQ(encodeToString(*s),
            "{\n  \"version\": 1,\n  \"book_sha256\": \"" + SHA + "\",\n  \"overrides\": {},\n  \"grades\": {}\n}\n");
}

TEST(StateCodec, BadInputIsRejectedWithItsReason) {
  const std::string pos = "\"position\": {\"chapter\": 0, \"word\": 0, \"mode\": \"page\"}";
  struct Case {
    std::string text;
    DecodeStatus status;
  };
  std::string deep = "{\"version\": 1, \"x\": ";
  for (int i = 0; i < 40; ++i) deep += "[";
  for (int i = 0; i < 40; ++i) deep += "]";
  deep += "}";
  std::string manyGrades = "{\"version\": 1, \"grades\": {";
  for (int i = 0; i <= GRADE_CAPACITY; ++i) {
    manyGrades += (i ? ",\"g" : "\"g") + std::to_string(i) + "\": {\"first\": \"correct\"}";
  }
  manyGrades += "}}";
  const Case cases[] = {
      {"", DecodeStatus::Syntax},
      {"\xEF\xBB\xBF{\"version\": 1}", DecodeStatus::Syntax},  // byte order mark
      {"{\"version\": 1} {}", DecodeStatus::Syntax},
      {"{\"version\": 1, \"x\": \"\\ud800\"}", DecodeStatus::Syntax},  // lone surrogate
      {"{\"version\": 1, \"x\": \"a\tb\"}", DecodeStatus::Syntax},     // raw control character
      {"{\"version\": 1, \"x\": 01}", DecodeStatus::Syntax},
      {"{\"version\": 1, \"x\": \"\xC0\xAF\"}", DecodeStatus::Syntax},  // overlong UTF-8
      {"[1]", DecodeStatus::Schema},
      {deep, DecodeStatus::Limit},
      {manyGrades, DecodeStatus::Limit},
      {"{\"version\": 1, \"version\": 1}", DecodeStatus::Schema},
      {"{\"version\": 1, " + pos + ", " + pos + "}", DecodeStatus::Schema},
      {"{\"version\": 1, \"position\": {\"chapter\": 0, \"word\": 0}}", DecodeStatus::Schema},
      {"{\"version\": 1, \"position\": {\"chapter\": 65535, \"word\": 0, \"mode\": \"page\"}}", DecodeStatus::Schema},
      {"{\"version\": 1, \"position\": {\"chapter\": 0, \"word\": 4294967296, \"mode\": \"page\"}}",
       DecodeStatus::Schema},
      {"{\"version\": 1, \"position\": {\"chapter\": 0, \"word\": 0, \"mode\": \"scroll\"}}", DecodeStatus::Schema},
      {"{\"version\": 1, \"book_sha256\": \"" + SHA.substr(1) + "\"}", DecodeStatus::Schema},
      {"{\"version\": 1, \"book_sha256\": \"" + std::string(64, 'A') + "\"}", DecodeStatus::Schema},
      {"{\"version\": 1, \"overrides\": {\"wpm\": 0}}", DecodeStatus::Schema},
      {"{\"version\": 1, \"overrides\": {\"words_per_update\": 256}}", DecodeStatus::Schema},
      {"{\"version\": 1, \"grades\": {\"-bad\": {\"first\": \"correct\"}}}", DecodeStatus::Schema},
      {"{\"version\": 1, \"grades\": {\"p\": {\"first\": \"partly\"}}}", DecodeStatus::Schema},
      {"{\"version\": 1, \"grades\": {\"p\": {\"at\": \"2026-12-01T19:04:00\"}}}", DecodeStatus::Schema},
      {"{\"version\": 1, \"grades\": {\"p\": {\"first\": \"correct\", \"at\": \"yesterday\"}}}", DecodeStatus::Schema},
      {"{\"version\": 1, \"grades\": {\"p\": {\"first\": \"correct\"}, \"p\": {\"first\": \"correct\"}}}",
       DecodeStatus::Schema},
      {"{\"version\": 1, \"active_checkpoint\": {\"id\": \"c1\", \"prompt_index\": 32, \"resume\": "
       "{\"chapter\": 0, \"word\": 0, \"mode\": \"page\"}}}",
       DecodeStatus::Schema},
      {"{\"version\": 1, \"active_checkpoint\": {\"id\": \"c1\", \"prompt_index\": 0}}", DecodeStatus::Schema},
      {"{\"version\": 1, \"active_checkpoint\": null}", DecodeStatus::Schema},
  };
  for (const Case& c : cases) {
    auto s = std::make_unique<BookState>();
    const DecodeResult r = decodeString(c.text, *s);
    EXPECT_EQ(r.status, c.status) << c.text.substr(0, 120) << " -> " << decodeStatusName(r.status) << ": " << r.detail;
    EXPECT_TRUE(isFresh(*s));
  }
  auto s = std::make_unique<BookState>();
  std::vector<uint8_t> huge(STATE_DECODE_MAX_BYTES + 1, ' ');
  EXPECT_EQ(decodeState(huge.data(), huge.size(), "", *s).status, DecodeStatus::TooLarge);
  EXPECT_EQ(decodeState(nullptr, 10, "", *s).status, DecodeStatus::Syntax);
}
