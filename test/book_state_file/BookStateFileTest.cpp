// Task C5: the per-book state file service (src/reading/BookStateFile) on an SD card in memory whose
// power is cut after any number of changes (stubs/HalStorage.h). Whatever the moment of the cut, the
// file reads back as the state before the write or the state after it, never as missing or damaged
// (docs/PLAN.md section 7: "written to a temporary file and renamed so a power cut cannot leave it
// half-written"). The simulator's kill cannot land inside one write, so this is where that is proven.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "lib/CheckpointCore/BookState.h"
#include "lib/CheckpointCore/StateCodec.h"
#include "reading/BookStateFile.h"
#include "reading/BookStateRules.h"
#include "stubs/HalStorage.h"

namespace {

const std::string BOOK = "/books/fixture.prepared.epub";
const std::string PATH = BOOK + ".rsvp-state.json";
const std::string TMP = PATH + ".tmp";

// The state before the write: c0002 in progress at question 1, its first prompt graded.
const char* const BEFORE = R"({
  "version": 1,
  "overrides": { "wpm": 1000 },
  "position": { "chapter": 2, "word": 490, "mode": "page" },
  "grades": {
    "c0002-p1": { "first": "correct", "at": "2026-12-01T19:04:00" }
  },
  "active_checkpoint": {
    "id": "c0002",
    "prompt_index": 0,
    "resume": { "chapter": 3, "word": 0, "mode": "page" }
  }
}
)";

class StringSink final : public checkpoint::StateSink {
 public:
  bool write(const char* data, size_t length) override {
    text.append(data, length);
    return true;
  }
  std::string text;
};

std::string encoded(const checkpoint::BookState& state) {
  StringSink sink;
  EXPECT_TRUE(checkpoint::encodeState(state, sink));
  return sink.text;
}

std::unique_ptr<checkpoint::BookState> decoded(const std::string& text) {
  auto state = std::make_unique<checkpoint::BookState>();
  const reading::StateLoad load = reading::decodeForUse(reinterpret_cast<const uint8_t*>(text.data()), text.size(),
                                                        reading::NO_BOOK_SHA256, *state);
  EXPECT_EQ(load.source, reading::StateSource::Read);
  return state;
}

// The write under test: question 2 Incorrect, the checkpoint moves on to question 2 (as a checkpoint
// transition writes through CheckpointActivity's edit).
void nextQuestion(checkpoint::BookState& state, const void*) {
  state.grades.recordFirst("c0002-p2", checkpoint::Grade::Incorrect, nullptr);
  state.active.promptIndex = 1;
}

// What the card holds after a reboot, as an encoding ("missing" or the load's source when not Read).
std::string afterReboot() {
  fakesd::cut = false;
  fakesd::opsLeft = -1;
  auto state = std::make_unique<checkpoint::BookState>();
  const reading::StateLoad load = reading::loadBookState(BOOK, reading::BookIdentity{}, *state);
  if (load.source != reading::StateSource::Read) return reading::stateSourceName(load.source);
  return encoded(*state);
}

// Changes the card asks for in one uncut edit from `files`.
int changesOfOneEdit(const std::map<std::string, std::string>& files) {
  fakesd::reset();
  fakesd::files = files;
  EXPECT_TRUE(reading::editBookState(BOOK, reading::BookIdentity{}, &nextQuestion, nullptr));
  return fakesd::ops;
}

}  // namespace

TEST(BookStateFile, APowerCutAtAnyStepLeavesTheOldOrTheNewState) {
  const std::string before = encoded(*decoded(BEFORE));
  auto next = decoded(BEFORE);
  nextQuestion(*next, nullptr);
  const std::string after = encoded(*next);
  ASSERT_NE(before, after);

  // With and without a .tmp left by an earlier cut.
  for (const bool staleTmp : {false, true}) {
    std::map<std::string, std::string> files{{PATH, BEFORE}};
    if (staleTmp) files[TMP] = std::string(BEFORE).substr(0, 40);
    const int changes = changesOfOneEdit(files);
    ASSERT_GE(changes, 4) << "open, writes, remove, rename";
    int sawOld = 0;
    int sawNew = 0;
    for (int cutAfter = 0; cutAfter <= changes; ++cutAfter) {
      fakesd::reset();
      fakesd::files = files;
      fakesd::opsLeft = cutAfter;
      const bool ok = reading::editBookState(BOOK, reading::BookIdentity{}, &nextQuestion, nullptr);
      EXPECT_EQ(ok, cutAfter == changes) << "cut after " << cutAfter << " of " << changes;
      const std::string onCard = afterReboot();
      EXPECT_TRUE(onCard == before || onCard == after)
          << "cut after " << cutAfter << " of " << changes << " changes (stale tmp " << staleTmp << "): " << onCard;
      sawOld += onCard == before ? 1 : 0;
      sawNew += onCard == after ? 1 : 0;
      // The next write after the reboot completes and leaves one file, whatever the cut left.
      ASSERT_TRUE(reading::editBookState(BOOK, reading::BookIdentity{}, &nextQuestion, nullptr));
      EXPECT_EQ(fakesd::files.count(TMP), 0u) << "cut after " << cutAfter;
      EXPECT_EQ(afterReboot(), after) << "cut after " << cutAfter;
    }
    EXPECT_GT(sawOld, 0);
    EXPECT_EQ(sawNew, 2) << "the cut on the rename (only the complete .tmp is left) and no cut";
  }
}

TEST(BookStateFile, ACompleteTmpBesideAMissingFileIsReadAndRenamedByTheNextWrite) {
  fakesd::reset();
  fakesd::files[TMP] = BEFORE;
  ASSERT_TRUE(reading::bookStateExists(BOOK));
  auto state = std::make_unique<checkpoint::BookState>();
  const reading::StateLoad load = reading::loadBookState(BOOK, reading::BookIdentity{}, *state);
  EXPECT_EQ(load.source, reading::StateSource::Read);
  EXPECT_TRUE(state->hasActive);
  EXPECT_EQ(state->grades.find("c0002-p1"), checkpoint::Grade::Correct) << "no grade is lost";
  ASSERT_TRUE(reading::editBookState(BOOK, reading::BookIdentity{}, &nextQuestion, nullptr));
  EXPECT_EQ(fakesd::files.count(TMP), 0u);
  ASSERT_EQ(fakesd::files.count(PATH), 1u);
  auto next = decoded(fakesd::files[PATH]);
  EXPECT_EQ(next->grades.find("c0002-p1"), checkpoint::Grade::Correct);
  EXPECT_EQ(next->grades.find("c0002-p2"), checkpoint::Grade::Incorrect);
}

TEST(BookStateFile, ATmpCutShortWithNoFileReadsAsMissing) {
  // A first write cut short: nothing was saved before it, and nothing half-written is read.
  for (size_t length : {size_t{0}, size_t{1}, size_t{50}, std::string(BEFORE).size() - 3}) {
    fakesd::reset();
    fakesd::files[TMP] = std::string(BEFORE).substr(0, length);
    auto state = std::make_unique<checkpoint::BookState>();
    EXPECT_EQ(reading::loadBookState(BOOK, reading::BookIdentity{}, *state).source, reading::StateSource::Missing)
        << length;
    EXPECT_FALSE(state->hasActive);
  }
}

TEST(BookStateFile, WriteBookStateWritesTheWholeStateAtomically) {
  fakesd::reset();
  fakesd::files[PATH] = "{}";
  auto state = decoded(BEFORE);
  ASSERT_TRUE(reading::writeBookState(BOOK, *state));
  EXPECT_EQ(fakesd::files.count(TMP), 0u);
  EXPECT_EQ(fakesd::files[PATH], encoded(*state));
  // Cut at every step: the old file or the new one.
  const int changes = fakesd::ops;
  for (int cutAfter = 0; cutAfter < changes; ++cutAfter) {
    fakesd::reset();
    fakesd::files[PATH] = BEFORE;
    fakesd::opsLeft = cutAfter;
    auto next = decoded(BEFORE);
    nextQuestion(*next, nullptr);
    EXPECT_FALSE(reading::writeBookState(BOOK, *next));
    const std::string onCard = afterReboot();
    EXPECT_TRUE(onCard == encoded(*state) || onCard == encoded(*next)) << cutAfter << ": " << onCard;
  }
}

TEST(BookStateFile, NoStateFileMeansNothingToRead) {
  fakesd::reset();
  EXPECT_FALSE(reading::bookStateExists(BOOK));
  auto state = std::make_unique<checkpoint::BookState>();
  EXPECT_EQ(reading::loadBookState(BOOK, reading::BookIdentity{}, *state).source, reading::StateSource::Missing);
}
