// What the checkpoint screens write to the state file (src/activities/checkpoint/CheckpointSave.h),
// driven by the real CheckpointFlow over a shared sample (fixtures/checkpoints/valid/multi-prompt.json:
// one checkpoint, three prompts). The "file" is the encoded state: each save decodes it, applies the
// change, and encodes it again, as reading::editBookState() does on the device. Plan section 7 grade
// rules: the first grade is written once and never overwritten, skipping writes nothing.

#include <BookState.h>
#include <CheckpointFlow.h>
#include <CheckpointIndex.h>
#include <CheckpointSource.h>
#include <CheckpointTrigger.h>
#include <StateCodec.h>
#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "activities/checkpoint/CheckpointSave.h"

using checkpoint::FlowEvent;
using checkpoint::FlowState;
using checkpoint::Grade;
using checkpoint::ReadingMode;

namespace {

constexpr const char* SAMPLE_SHA = "1e874b76a5df7dff6e3ea423c297d1a8565e2ebf5c98c9e67ac248598f597fdd";
constexpr const char* OTHER_SHA = "0000000000000000000000000000000000000000000000000000000000000001";

std::string readSample(const char* name) {
  std::ifstream in(std::string(CHECKPOINT_FIXTURES_DIR) + "/valid/" + name, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The state file: bytes, edited like reading::editBookState() (decode, change, encode).
struct StateFile {
  std::string bytes;

  void edit(const checkpointui::StateChange& change, const char* sha) {
    auto state = std::make_unique<checkpoint::BookState>();
    if (!bytes.empty()) {
      const checkpoint::DecodeResult r =
          checkpoint::decodeState(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), sha, *state);
      ASSERT_EQ(r.status, checkpoint::DecodeStatus::Ok) << bytes;
    }
    checkpointui::applyChange(*state, change, sha);
    write(*state);
  }

  void write(const checkpoint::BookState& state) {
    std::vector<char> out(checkpoint::STATE_ENCODED_MAX_BYTES);
    checkpoint::BufferStateSink sink(out.data(), out.size());
    ASSERT_TRUE(checkpoint::encodeState(state, sink));
    bytes.assign(out.data(), sink.length());
  }

  std::unique_ptr<checkpoint::BookState> read(const char* sha) const {
    auto state = std::make_unique<checkpoint::BookState>();
    if (!bytes.empty()) {
      checkpoint::decodeState(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), sha, *state);
    }
    return state;
  }

  Grade grade(const char* id) const { return read(SAMPLE_SHA)->grades.find(id); }
};

// One run of the screens over the sample: the session state loaded from the file, the flow, and
// the save after every Save::Now transition (the activity's apply()).
class Session {
 public:
  Session(const checkpoint::CheckpointIndex& index, StateFile& file, const ReadingMode mode = ReadingMode::Page)
      : state(file.read(SAMPLE_SHA)), flow(*state, index), index(index), file(file) {
    // The section is words [10, 130) of chapter 0; reading past word 129 fires.
    sections->reset(0, 200, checkpoint::NO_CHAPTER);
    EXPECT_EQ(sections->add(0, 10, 130, 0), checkpoint::ChapterSections::AddResult::Added);
    const checkpoint::Step fire = checkpoint::rsvpStep(*sections, 129);
    EXPECT_EQ(fire.kind, checkpoint::StepKind::Fire);
    last = flow.start(fire, mode);
    EXPECT_TRUE(last.accepted);
    save(nullptr);
  }

  checkpoint::Transition send(const FlowEvent event, const char* at = "2026-12-01T19:04:00") {
    char id[checkpoint::ID_BUFFER_BYTES];
    EXPECT_TRUE(index.loadPromptId(flow.checkpoint(), flow.promptIndex(), id, sizeof(id)));
    last = flow.handle(event, at);
    EXPECT_TRUE(last.accepted) << checkpoint::flowEventName(event);
    if (last.save == checkpoint::Save::Now) save(id);
    return last;
  }

  int saves = 0;
  std::unique_ptr<checkpoint::BookState> state;
  checkpoint::CheckpointFlow flow;

 private:
  void save(const char* id) {
    file.edit(checkpointui::changeFor(*state, last, id), SAMPLE_SHA);
    ++saves;
  }

  const checkpoint::CheckpointIndex& index;
  StateFile& file;
  std::unique_ptr<checkpoint::ChapterSections> sections = std::make_unique<checkpoint::ChapterSections>();
  checkpoint::Transition last;
};

class CheckpointSave : public ::testing::Test {
 protected:
  void SetUp() override {
    json = readSample("multi-prompt.json");
    ASSERT_FALSE(json.empty());
    source = std::make_unique<checkpoint::MemoryCheckpointSource>(reinterpret_cast<const uint8_t*>(json.data()),
                                                                  json.size());
    index = std::make_unique<checkpoint::CheckpointIndex>();
    ASSERT_EQ(index->open(*source).verdict, checkpoint::Verdict::Valid);
  }

  std::string json;
  std::unique_ptr<checkpoint::MemoryCheckpointSource> source;
  std::unique_ptr<checkpoint::CheckpointIndex> index;
  StateFile file;
};

}  // namespace

TEST_F(CheckpointSave, StartWritesTheCheckpointInProgress) {
  Session s(*index, file);
  const auto state = file.read(SAMPLE_SHA);
  EXPECT_STREQ(state->bookSha256, SAMPLE_SHA) << "a new file takes the book's book_sha256";
  ASSERT_TRUE(state->hasActive);
  EXPECT_STREQ(state->active.id, "c0001");
  EXPECT_EQ(state->active.promptIndex, 0);
  EXPECT_EQ(state->active.resume.position.word, 130u) << "the first word after the break";
  EXPECT_EQ(state->active.resume.mode, ReadingMode::Page);
  EXPECT_FALSE(state->hasPosition) << "the reading position moves only when the checkpoint ends";
  EXPECT_EQ(state->grades.count(), 0);
}

TEST_F(CheckpointSave, RevealWritesNothing) {
  Session s(*index, file);
  const std::string before = file.bytes;
  const checkpoint::Transition t = s.send(FlowEvent::Reveal);
  EXPECT_EQ(t.save, checkpoint::Save::No);
  EXPECT_EQ(s.saves, 1);
  EXPECT_EQ(file.bytes, before);
}

TEST_F(CheckpointSave, EachGradeIsWrittenWithItsTransition) {
  Session s(*index, file);
  s.send(FlowEvent::Reveal);
  checkpoint::Transition t = s.send(FlowEvent::Incorrect);
  EXPECT_TRUE(t.gradeRecorded);
  EXPECT_EQ(t.save, checkpoint::Save::Now);
  EXPECT_EQ(file.grade("c0001-p1"), Grade::Incorrect) << "written before the after-Incorrect screen";
  checkpoint::Timestamp at = checkpoint::NO_TIMESTAMP;
  file.read(SAMPLE_SHA)->grades.find("c0001-p1", &at);
  char text[checkpoint::TIMESTAMP_CHARS + 1];
  ASSERT_TRUE(checkpoint::formatTimestamp(at, text));
  EXPECT_STREQ(text, "2026-12-01T19:04:00");

  s.send(FlowEvent::Continue);
  EXPECT_EQ(file.read(SAMPLE_SHA)->active.promptIndex, 1) << "the next prompt is in the file before it shows";
  s.send(FlowEvent::Reveal);
  t = s.send(FlowEvent::Correct);
  EXPECT_EQ(file.grade("c0001-p2"), Grade::Correct);
  EXPECT_EQ(file.read(SAMPLE_SHA)->active.promptIndex, 2);
}

TEST_F(CheckpointSave, SkipsWriteNoGradeAndAFinishMovesThePosition) {
  Session s(*index, file);
  s.send(FlowEvent::SkipPrompt);
  s.send(FlowEvent::Reveal);
  s.send(FlowEvent::SkipPrompt);
  EXPECT_EQ(file.read(SAMPLE_SHA)->grades.count(), 0);
  EXPECT_EQ(file.read(SAMPLE_SHA)->active.promptIndex, 2);
  const checkpoint::Transition t = s.send(FlowEvent::SkipCheckpoint);
  EXPECT_TRUE(t.finished);
  const auto state = file.read(SAMPLE_SHA);
  EXPECT_EQ(state->grades.count(), 0) << "skipped prompts stay ungraded";
  EXPECT_FALSE(state->hasActive);
  ASSERT_TRUE(state->hasPosition);
  EXPECT_EQ(state->position.position.word, 130u);
  EXPECT_EQ(state->position.mode, ReadingMode::Page);
}

TEST_F(CheckpointSave, TheFirstGradeSurvivesALaterPassWithAnotherAnswer) {
  {
    Session first(*index, file);
    first.send(FlowEvent::Reveal);
    first.send(FlowEvent::Incorrect);  // p1: incorrect
    first.send(FlowEvent::Continue);
    first.send(FlowEvent::SkipPrompt);  // p2: skipped
    first.send(FlowEvent::Reveal);
    first.send(FlowEvent::Correct);  // p3: correct; finished
    EXPECT_EQ(first.flow.state(), FlowState::Finished);
  }
  EXPECT_EQ(file.grade("c0001-p1"), Grade::Incorrect);
  EXPECT_EQ(file.grade("c0001-p2"), Grade::Ungraded);
  EXPECT_EQ(file.grade("c0001-p3"), Grade::Correct);

  Session second(*index, file, ReadingMode::Rsvp);
  EXPECT_EQ(second.flow.storedGrade(), Grade::Incorrect) << "shown as already recorded";
  second.send(FlowEvent::Reveal);
  checkpoint::Transition t = second.send(FlowEvent::Correct, "2027-01-01T00:00:00");
  EXPECT_FALSE(t.gradeRecorded);
  EXPECT_EQ(file.grade("c0001-p1"), Grade::Incorrect) << "the first grade is never overwritten";
  EXPECT_EQ(second.flow.storedGrade(), Grade::Ungraded) << "p2 was skipped before";
  second.send(FlowEvent::Reveal);
  t = second.send(FlowEvent::Incorrect);
  EXPECT_TRUE(t.gradeRecorded) << "a skipped prompt takes its first grade on a later pass";
  EXPECT_EQ(file.grade("c0001-p2"), Grade::Incorrect);
  second.send(FlowEvent::Continue);
  second.send(FlowEvent::Reveal);
  t = second.send(FlowEvent::Incorrect);
  EXPECT_FALSE(t.gradeRecorded);
  EXPECT_EQ(t.save, checkpoint::Save::No) << "nothing new to write";
  EXPECT_EQ(file.grade("c0001-p3"), Grade::Correct);
}

TEST_F(CheckpointSave, AGradeAlreadyInTheFileIsKept) {
  // Another writer stored a grade for p1 after this session read the file.
  Session s(*index, file);
  {
    auto other = file.read(SAMPLE_SHA);
    ASSERT_EQ(other->grades.recordFirst("c0001-p1", Grade::Correct, "2026-11-30T08:00:00"),
              checkpoint::GradeTable::RecordResult::Recorded);
    file.write(*other);
  }
  s.send(FlowEvent::Reveal);
  const checkpoint::Transition t = s.send(FlowEvent::Incorrect);
  EXPECT_TRUE(t.gradeRecorded) << "first in this session's state";
  EXPECT_EQ(file.grade("c0001-p1"), Grade::Correct) << "but the file's first grade stays";
}

TEST_F(CheckpointSave, ARereadRequestWritesTheRereadPlace) {
  Session s(*index, file, ReadingMode::Rsvp);
  s.send(FlowEvent::Reveal);
  s.send(FlowEvent::Incorrect);
  const checkpoint::Transition t = s.send(FlowEvent::RereadPage);
  EXPECT_EQ(t.state, FlowState::Rereading);
  const auto state = file.read(SAMPLE_SHA);
  ASSERT_TRUE(state->hasActive);
  EXPECT_TRUE(state->active.rereading);
  EXPECT_EQ(state->active.reread.position.word, 10u) << "the section's first word";
  EXPECT_EQ(state->active.reread.mode, ReadingMode::Page);
  EXPECT_EQ(state->active.resume.mode, ReadingMode::Rsvp) << "resume keeps the mode the checkpoint came from";
  EXPECT_EQ(state->active.promptIndex, 0);
}

TEST_F(CheckpointSave, OtherMembersOfTheFileAreKept) {
  {
    auto seeded = std::make_unique<checkpoint::BookState>();
    seeded->overrides.hasWpm = true;
    seeded->overrides.wpm = 300;
    seeded->hasPosition = true;
    seeded->position.position = {0, 55};
    seeded->position.mode = ReadingMode::Rsvp;
    ASSERT_EQ(seeded->grades.recordFirst("other-p1", Grade::Correct, nullptr),
              checkpoint::GradeTable::RecordResult::Recorded);
    std::memcpy(seeded->bookSha256, OTHER_SHA, 65);
    file.write(*seeded);
  }
  checkpointui::StateChange change;
  change.hasActive = false;
  // An edit for the open book (another sha in the file was already handled by the decoder's rule).
  auto state = file.read(OTHER_SHA);
  checkpointui::applyChange(*state, change, SAMPLE_SHA);
  EXPECT_STREQ(state->bookSha256, OTHER_SHA) << "a set book_sha256 is never replaced here";
  EXPECT_TRUE(state->overrides.hasWpm);
  EXPECT_EQ(state->overrides.wpm, 300);
  EXPECT_TRUE(state->hasPosition) << "no finish: the position stays";
  EXPECT_EQ(state->position.position.word, 55u);
  EXPECT_EQ(state->grades.find("other-p1"), Grade::Correct);
}

TEST(CheckpointGradeTime, FormatsASetClockOnly) {
  char out[checkpoint::TIMESTAMP_CHARS + 1];
  // struct tm: years since 1900, months from 0.
  ASSERT_TRUE(checkpointui::formatGradeTime(126, 11, 1, 19, 4, 0, out));
  EXPECT_STREQ(out, "2026-12-01T19:04:00");
  ASSERT_TRUE(checkpointui::formatGradeTime(124, 1, 29, 0, 0, 60, out));
  EXPECT_STREQ(out, "2024-02-29T00:00:59") << "a leap second is written as :59";
  // An unset clock (the epoch, or a cold boot near it) and years the state file cannot hold.
  EXPECT_FALSE(checkpointui::formatGradeTime(70, 0, 1, 0, 0, 0, out));
  EXPECT_STREQ(out, "");
  EXPECT_FALSE(checkpointui::formatGradeTime(119, 11, 31, 23, 59, 59, out));
  EXPECT_FALSE(checkpointui::formatGradeTime(236, 0, 1, 0, 0, 0, out));
  EXPECT_TRUE(checkpointui::formatGradeTime(235, 0, 1, 0, 0, 0, out));
  // Impossible dates and fields.
  EXPECT_FALSE(checkpointui::formatGradeTime(126, 3, 31, 0, 0, 0, out));
  EXPECT_FALSE(checkpointui::formatGradeTime(125, 1, 29, 0, 0, 0, out));
  EXPECT_FALSE(checkpointui::formatGradeTime(126, 12, 1, 0, 0, 0, out));
  EXPECT_FALSE(checkpointui::formatGradeTime(126, 0, 1, 24, 0, 0, out));
}

TEST(CheckpointGradeTime, AGradeWithoutATimeIsStillWritten) {
  checkpoint::BookState session;
  ASSERT_EQ(session.grades.recordFirst("c0001-p1", Grade::Correct, nullptr),
            checkpoint::GradeTable::RecordResult::Recorded);
  checkpoint::Transition t;
  t.accepted = true;
  t.gradeRecorded = true;
  const checkpointui::StateChange change = checkpointui::changeFor(session, t, "c0001-p1");
  EXPECT_TRUE(change.gradeRecorded);
  EXPECT_EQ(change.at, checkpoint::NO_TIMESTAMP);
  checkpoint::BookState fileState;
  checkpointui::applyChange(fileState, change, SAMPLE_SHA);
  EXPECT_EQ(fileState.grades.find("c0001-p1"), Grade::Correct);
}
