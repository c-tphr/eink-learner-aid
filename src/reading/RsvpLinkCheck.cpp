#include "RsvpLinkCheck.h"

#if CROSSPOINT_RSVP_ENABLED

#include <Arduino.h>
#include <CheckpointFlow.h>
#include <CheckpointIndex.h>
#include <CheckpointSource.h>
#include <CheckpointTrigger.h>
#include <Logging.h>
#include <Memory.h>
#include <Scheduler.h>
#include <SentenceStep.h>
#include <StateCodec.h>
#include <StateRestore.h>
#include <WordClassifier.h>

#include <cstddef>
#include <cstdint>

namespace {

constexpr rsvp::Token TOKENS[] = {
    {"Link", 0, 0},
    {"check.", 1, rsvp::TOKEN_PARAGRAPH_END | rsvp::TOKEN_CHAPTER_END},
};
constexpr uint32_t TOKEN_COUNT = sizeof(TOKENS) / sizeof(TOKENS[0]);

// open() rejects this file (no book, sections or checkpoints) after a full validating pass.
constexpr char CHECKPOINT_JSON[] = "{\"version\":1}";

class TokenSource final : public rsvp::WordSource {
 public:
  bool peek(rsvp::Token& out) override {
    if (next >= TOKEN_COUNT) return false;
    out = TOKENS[next];
    return true;
  }
  void advance() override {
    if (next < TOKEN_COUNT) ++next;
  }

 private:
  uint32_t next = 0;
};

// Moves 10 ms on every read, so the scheduler sees time pass without waiting.
class StepClock final : public rsvp::Clock {
 public:
  uint32_t nowMs() override { return now += 10; }

 private:
  uint32_t now = 0;
};

class NullDisplay final : public rsvp::DisplaySink {
 public:
  void showFrame(const rsvp::Frame&) override {}
  void clearingRefresh() override {}
};

class OneChapterLayout final : public checkpoint::BookLayout {
 public:
  uint16_t chapterCount() const override { return 1; }
  bool chapterWordCount(uint16_t, uint32_t& words) override {
    words = TOKEN_COUNT;
    return true;
  }
  bool sectionRange(uint16_t, checkpoint::SectionRange&) override { return false; }
};

// About 160 KB, mostly CheckpointIndex and BookState: allocated for the check and freed on return
// (PSRAM on the Picco). The source is declared before the index, which keeps a pointer to it.
struct Scratch {
  TokenSource words;
  StepClock clock;
  NullDisplay display;
  rsvp::Scheduler scheduler{words, clock, display};
  rsvp::WordClass classes[TOKEN_COUNT];
  OneChapterLayout layout;
  checkpoint::MemoryCheckpointSource checkpointFile{reinterpret_cast<const uint8_t*>(CHECKPOINT_JSON),
                                                    sizeof(CHECKPOINT_JSON) - 1};
  checkpoint::CheckpointIndex index;
  checkpoint::BookState state;
  checkpoint::ChapterSections sections;
  char encoded[512];
};

}  // namespace

void runRsvpLinkCheck() {
  const unsigned long startMs = millis();
  auto scratch = makeUniqueNoThrow<Scratch>();
  if (!scratch) {
    LOG_ERR("RSVP", "Link check: allocation of %u bytes failed", static_cast<unsigned>(sizeof(Scratch)));
    return;
  }
  Scratch& s = *scratch;

  // RsvpCore: show one frame, then find the start of the sentence holding word 1.
  s.scheduler.play();
  const rsvp::StepResult shown = s.scheduler.step();
  rsvp::classifyWords(TOKENS, TOKEN_COUNT, {}, s.classes);
  uint32_t sentence = 0;
  rsvp::sentenceStart({s.classes, TOKEN_COUNT, 0}, 1, sentence);

  // CheckpointCore: parse, trigger rules, flow, restore plan and the state file round trip.
  const checkpoint::OpenResult opened = s.index.open(s.checkpointFile);
  s.sections.reset(0, TOKEN_COUNT, checkpoint::NO_CHAPTER);
  const checkpoint::Step trigger = checkpoint::rsvpStep(s.sections, 0);
  checkpoint::CheckpointFlow flow(s.state, s.index);
  const checkpoint::Transition refused = flow.handle(checkpoint::FlowEvent::Reveal);
  const checkpoint::RestorePlan plan = checkpoint::planRestore(s.state, nullptr, s.layout);
  checkpoint::BufferStateSink sink(s.encoded, sizeof(s.encoded));
  const bool encoded = checkpoint::encodeState(s.state, sink);
  const checkpoint::DecodeResult decoded =
      checkpoint::decodeState(reinterpret_cast<const uint8_t*>(s.encoded), sink.length(), "", s.state);

  LOG_DBG("RSVP",
          "Link check: frame \"%s\" (event %u), sentence %u; checkpoint file %s, trigger %u, flow %s (refusal %u), "
          "restore %s, state %u bytes %s; %lu ms",
          s.scheduler.lastFrame().text, static_cast<unsigned>(shown.event), static_cast<unsigned>(sentence),
          checkpoint::issueCodeName(opened.code), static_cast<unsigned>(trigger.kind),
          checkpoint::flowStateName(refused.state), static_cast<unsigned>(refused.refusal),
          checkpoint::readingModeName(plan.place.mode), encoded ? static_cast<unsigned>(sink.length()) : 0u,
          checkpoint::decodeStatusName(decoded.status), millis() - startMs);
}

#endif
