// Task R4b: the pause screen's steppers (S4 rows 6-9), how the global default is stored, which values
// RSVP opens with (src/reading/RsvpValues), and what the reading code does with a state file's bytes
// (src/reading/BookStateRules): unusable files, another edition, and keeping the members RSVP does
// not own when it writes the position or the overrides.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "lib/CheckpointCore/BookState.h"
#include "lib/CheckpointCore/StateCodec.h"
#include "reading/BookStateRules.h"
#include "reading/RsvpValues.h"

using reading::RsvpValues;

namespace {

std::string encode(const checkpoint::BookState& state) {
  std::string out(checkpoint::STATE_ENCODED_MAX_BYTES, '\0');
  checkpoint::BufferStateSink sink(out.data(), out.size());
  EXPECT_TRUE(checkpoint::encodeState(state, sink));
  out.resize(sink.length());
  return out;
}

reading::StateLoad decode(const std::string& text, const char* sha, checkpoint::BookState& out) {
  return reading::decodeForUse(reinterpret_cast<const uint8_t*>(text.data()), text.size(), sha, out);
}

const char* const SHA_A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* const SHA_B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

// A state a checkpoint task could have written: hash, overrides, position, grades, active checkpoint.
const char* const FULL_STATE = R"({
  "version": 1,
  "book_sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "overrides": { "wpm": 300 },
  "position": { "chapter": 3, "word": 1542, "mode": "page" },
  "grades": {
    "c0007-p1": { "first": "correct", "at": "2026-12-01T19:04:00" },
    "c0007-p2": { "first": "incorrect" }
  },
  "active_checkpoint": {
    "id": "c0007",
    "prompt_index": 1,
    "resume": { "chapter": 3, "word": 1543, "mode": "rsvp" },
    "reread": { "mode": "page", "chapter": 3, "word": 910 }
  }
}
)";

}  // namespace

// --- Steppers ---

TEST(RsvpValues, WpmStepsBy20Within60To1000) {
  EXPECT_EQ(reading::stepWpm(240, true), 260);
  EXPECT_EQ(reading::stepWpm(240, false), 220);
  EXPECT_EQ(reading::stepWpm(80, false), 60);
  EXPECT_EQ(reading::stepWpm(60, false), 60) << "a press at the lower limit changes nothing";
  EXPECT_EQ(reading::stepWpm(980, true), 1000);
  EXPECT_EQ(reading::stepWpm(1000, true), 1000) << "a press at the upper limit changes nothing";
  // From any value the steps stay in range, and every stepper value from 60 is reached.
  uint16_t wpm = 60;
  int presses = 0;
  while (wpm < 1000) {
    const uint16_t next = reading::stepWpm(wpm, true);
    EXPECT_EQ(next, wpm + 20);
    wpm = next;
    ++presses;
  }
  EXPECT_EQ(presses, 47);
  // Values a hand-edited file may hold: clamped first, then stepped.
  EXPECT_EQ(reading::stepWpm(0, false), 60);
  EXPECT_EQ(reading::stepWpm(10, true), 80);
  EXPECT_EQ(reading::stepWpm(65535, false), 980);
  EXPECT_EQ(reading::stepWpm(250, true), 270);
}

TEST(RsvpValues, WordsPerUpdateStepsWithin1To4) {
  EXPECT_EQ(reading::RSVP_WORDS_MAX, 4) << "FrameBuilder's limit";
  EXPECT_EQ(reading::stepWordsPerUpdate(1, true), 2);
  EXPECT_EQ(reading::stepWordsPerUpdate(3, true), 4);
  EXPECT_EQ(reading::stepWordsPerUpdate(4, true), 4);
  EXPECT_EQ(reading::stepWordsPerUpdate(2, false), 1);
  EXPECT_EQ(reading::stepWordsPerUpdate(1, false), 1);
  EXPECT_EQ(reading::stepWordsPerUpdate(0, false), 1);
  EXPECT_EQ(reading::stepWordsPerUpdate(200, false), 3);
}

TEST(RsvpValues, TheSettingsFileKeepsWpmInTensWithoutLoss) {
  EXPECT_EQ(reading::RSVP_WPM_TENS_DEFAULT, 24);
  for (uint16_t wpm = reading::RSVP_WPM_MIN; wpm <= reading::RSVP_WPM_MAX; wpm += reading::RSVP_WPM_STEP) {
    EXPECT_EQ(reading::wpmFromTens(reading::wpmToTens(wpm)), wpm);
  }
  EXPECT_EQ(reading::wpmToTens(1000), 100);
  EXPECT_EQ(reading::wpmToTens(60), 6);
  // A value outside the range (hand-edited, or from another build) reads clamped.
  EXPECT_EQ(reading::wpmFromTens(0), 60);
  EXPECT_EQ(reading::wpmFromTens(255), 1000);
  EXPECT_EQ(reading::wpmToTens(5000), 100);
}

// --- Precedence ---

TEST(RsvpValues, TheBooksOverrideBeatsTheGlobalDefault) {
  const RsvpValues global{400, 3};
  checkpoint::Overrides book;
  EXPECT_EQ(reading::resolveValues(&book, global), (RsvpValues{400, 3})) << "no override: the default";
  EXPECT_EQ(reading::resolveValues(nullptr, global), (RsvpValues{400, 3})) << "no state file read";
  book.hasWpm = true;
  book.wpm = 320;
  EXPECT_EQ(reading::resolveValues(&book, global), (RsvpValues{320, 3})) << "each value is chosen on its own";
  book.hasWordsPerUpdate = true;
  book.wordsPerUpdate = 2;
  EXPECT_EQ(reading::resolveValues(&book, global), (RsvpValues{320, 2}));
  // Nothing saved anywhere: the settings file's own default, 240 and 1.
  EXPECT_EQ(reading::resolveValues(nullptr, RsvpValues{reading::wpmFromTens(reading::RSVP_WPM_TENS_DEFAULT), 1}),
            reading::RSVP_DEFAULTS);
  EXPECT_EQ(reading::RSVP_DEFAULTS, (RsvpValues{240, 1}));
  // The state file allows 1..65535 and 1..255; RSVP clamps.
  book.wpm = 5;
  book.wordsPerUpdate = 9;
  EXPECT_EQ(reading::resolveValues(&book, global), (RsvpValues{60, 4}));
  book.wpm = 65535;
  EXPECT_EQ(reading::resolveValues(&book, global).wpm, 1000);
}

TEST(RsvpValues, OverridesForWritesBothValues) {
  const checkpoint::Overrides o = reading::overridesFor(RsvpValues{520, 2});
  EXPECT_TRUE(o.hasWpm);
  EXPECT_EQ(o.wpm, 520);
  EXPECT_TRUE(o.hasWordsPerUpdate);
  EXPECT_EQ(o.wordsPerUpdate, 2);
}

// --- State file bytes ---

TEST(BookStateRules, UnusableFilesGiveAFreshState) {
  auto state = std::make_unique<checkpoint::BookState>();
  const std::string cases[] = {
      "",                                                      // empty (cut off before any byte)
      "{\"version\": 1, \"overrides\": {\"wpm\": 300}",        // truncated
      "not json at all",                                       // garbage
      "{\"version\": 2, \"overrides\": {}}\n",                 // a newer version
      "{\"version\": 1, \"overrides\": {\"wpm\": \"fast\"}}",  // wrong type
      "{\"overrides\": {}}",                                   // no version
  };
  for (const std::string& text : cases) {
    state->overrides.hasWpm = true;  // must be cleared
    state->hasPosition = true;
    const reading::StateLoad load = decode(text, "", *state);
    EXPECT_EQ(load.source, reading::StateSource::Invalid) << text;
    EXPECT_FALSE(state->overrides.hasWpm) << text;
    EXPECT_FALSE(state->hasPosition) << text;
    EXPECT_EQ(state->grades.count(), 0u) << text;
  }
  EXPECT_EQ(decode("{\"version\": 2}", "", *state).status, checkpoint::DecodeStatus::UnsupportedVersion);
  EXPECT_STREQ(reading::stateSourceName(reading::StateSource::Invalid), "invalid");
}

TEST(BookStateRules, AFileForThisBookIsUsedAsItIs) {
  auto state = std::make_unique<checkpoint::BookState>();
  // R4b opens every book as one without checkpoint data (sha ""), so a hash in the file is kept.
  for (const char* sha : {"", SHA_A}) {
    const reading::StateLoad load = decode(FULL_STATE, sha, *state);
    EXPECT_EQ(load.source, reading::StateSource::Read);
    EXPECT_FALSE(load.shaMismatch);
    EXPECT_STREQ(state->bookSha256, SHA_A);
    EXPECT_TRUE(state->hasPosition);
    EXPECT_EQ(state->position.position.word, 1542u);
    EXPECT_TRUE(state->overrides.hasWpm);
    EXPECT_FALSE(state->overrides.hasWordsPerUpdate);
    EXPECT_EQ(state->grades.count(), 2u);
    EXPECT_TRUE(state->hasActive);
  }
}

TEST(BookStateRules, AFileForAnotherEditionKeepsOverridesAndGradesButNotPositions) {
  auto state = std::make_unique<checkpoint::BookState>();
  const reading::StateLoad load = decode(FULL_STATE, SHA_B, *state);
  EXPECT_EQ(load.source, reading::StateSource::Read);
  EXPECT_TRUE(load.shaMismatch);
  EXPECT_STREQ(state->bookSha256, SHA_B) << "the state now belongs to the open book";
  EXPECT_FALSE(state->hasPosition) << "word positions may differ in another edition";
  EXPECT_FALSE(state->hasActive);
  EXPECT_TRUE(state->overrides.hasWpm);
  EXPECT_EQ(state->overrides.wpm, 300);
  EXPECT_EQ(state->grades.count(), 2u);
}

TEST(BookStateRules, WritingThePositionOrOverridesKeepsWhatRsvpDoesNotOwn) {
  auto before = std::make_unique<checkpoint::BookState>();
  ASSERT_EQ(decode(FULL_STATE, "", *before).source, reading::StateSource::Read);

  // What editBookState() does for a pause: decode, set the position, encode.
  auto state = std::make_unique<checkpoint::BookState>();
  ASSERT_EQ(decode(FULL_STATE, "", *state).source, reading::StateSource::Read);
  state->hasPosition = true;
  state->position.position.chapter = 1;
  state->position.position.word = 42;
  state->position.mode = checkpoint::ReadingMode::Rsvp;
  // ...and "Save for this book".
  state->overrides = reading::overridesFor(RsvpValues{500, 2});
  const std::string written = encode(*state);

  auto after = std::make_unique<checkpoint::BookState>();
  ASSERT_EQ(decode(written, "", *after).source, reading::StateSource::Read);
  EXPECT_STREQ(after->bookSha256, before->bookSha256);
  EXPECT_TRUE(after->grades.sameAs(before->grades));
  ASSERT_TRUE(after->hasActive);
  EXPECT_STREQ(after->active.id, "c0007");
  EXPECT_EQ(after->active.promptIndex, 1);
  EXPECT_EQ(after->active.resume, before->active.resume);
  EXPECT_TRUE(after->active.rereading);
  EXPECT_EQ(after->active.reread, before->active.reread);
  EXPECT_EQ(after->position.position.word, 42u);
  EXPECT_EQ(after->position.mode, checkpoint::ReadingMode::Rsvp);
  EXPECT_EQ(after->overrides.wpm, 500);
  EXPECT_EQ(after->overrides.wordsPerUpdate, 2);
}

TEST(BookStateRules, ABookWithoutCheckpointDataWritesOnlyOverridesAndPosition) {
  // The file RSVP writes for a plain EPUB, from a fresh state (no file yet): the plan's shape.
  auto state = std::make_unique<checkpoint::BookState>();
  state->overrides = reading::overridesFor(RsvpValues{300, 2});
  state->hasPosition = true;
  state->position.position.chapter = 1;
  state->position.position.word = 7;
  state->position.mode = checkpoint::ReadingMode::Page;
  EXPECT_EQ(encode(*state),
            "{\n"
            "  \"version\": 1,\n"
            "  \"overrides\": { \"wpm\": 300, \"words_per_update\": 2 },\n"
            "  \"position\": { \"chapter\": 1, \"word\": 7, \"mode\": \"page\" }\n"
            "}\n");
}
