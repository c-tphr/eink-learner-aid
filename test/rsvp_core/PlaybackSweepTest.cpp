// Acceptance sweep for the scheduler and the delivered-WPM meter (issue #19, PRD criterion 5 under a
// fake clock). One stream: the committed prose sample (5,793 words) as one chapter, then the
// fixture book's four documents (1,281 words, with an image, a table, a code block and two
// checkpoints). Played for every refresh delay, target WPM and words per update below, resolving
// each stop after 30 s, it checks:
//   - every word index is drawn exactly once and in order, by the scheduler's audit and by the
//     fake display's own log (indexes and texts against the source tokens);
//   - delivered WPM matches the arithmetic expectation:
//       expected = 60,000 * W / (sum over frames of max(holdMs, R + D) + K * C)
//     where W is the words shown, holdMs each frame's ideal hold (from an independent FrameBuilder
//     run), R the refresh delay, D the 60 ms dwell, K the clearing refreshes in the display log
//     and C the clearing time (here equal to R);
//   - at R = 0 delivered equals the target within 1% (prose sample and whole stream);
//   - no frame starts before the previous frame's refresh end plus the dwell, and no frame
//     interval is shorter than the previous frame's ideal hold.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "FixtureBook.h"
#include "PlaybackFakes.h"
#include "TestSupport.h"
#include "lib/RsvpCore/FrameBuilder.h"
#include "lib/RsvpCore/Scheduler.h"

using rsvp::Frame;
using rsvp::StepEvent;
using rsvp::StepResult;
using rsvptest::DisplayCall;
using rsvptest::OwnedToken;

namespace {

constexpr uint32_t DWELL = rsvp::DEFAULT_MIN_DWELL_MS;
constexpr uint32_t STOP_WAIT_MS = 30000;  // time spent at each checkpoint or non-prose item

struct Width {
  size_t maxBytes;
};
Width width{24};
bool fits(void* context, std::string_view text, uint16_t) {
  return text.size() <= static_cast<Width*>(context)->maxBytes;
}

struct Chapter {
  std::string name;
  std::vector<OwnedToken> tokens;
};

const std::vector<Chapter>& stream() {
  static const std::vector<Chapter> chapters = [] {
    std::vector<Chapter> out;
    out.push_back(
        {"prose sample", rsvptest::proseTokens(rsvptest::readFile(RSVP_TEST_DIR "/prose_sample/prose_sample.txt"))});
    for (const char* href :
         {"text/title.xhtml", "text/chapter-1.xhtml", "text/chapter-2.xhtml", "text/chapter-3.xhtml"}) {
      out.push_back({href, rsvptest::chapterTokens(rsvptest::fixtureDocument(href))});
    }
    return out;
  }();
  return chapters;
}

size_t streamWords() {
  size_t n = 0;
  for (const auto& c : stream()) n += c.tokens.size();
  return n;
}

struct Case {
  uint32_t refreshMs;
  uint16_t wpm;
  uint8_t wordsPerFrame;
};

struct PlayRun {
  std::vector<DisplayCall> log;
  std::vector<size_t> chapterLogStart;  // index into log where each chapter starts
  rsvp::MeterStats stats;
  rsvp::MeterStats proseStats;  // after the prose sample chapter
  uint32_t deliveredTenths = 0;
  bool auditInOrder = false;
  uint32_t auditWords = 0;
  uint32_t auditSegments = 0;
  uint32_t auditSkipped = 0;
  std::vector<uint8_t> stops;  // in the order reported
  bool consumedNothingPastStops = true;
};

PlayRun play(const Case& c, uint32_t startMs = 5000, size_t stallAtFrame = SIZE_MAX, uint32_t stallMs = 0) {
  PlayRun run;
  rsvptest::VectorSource source(stream()[0].tokens);
  rsvptest::FakeClock clock(startMs);
  rsvptest::FakeDisplay display(clock, c.refreshMs, c.refreshMs);
  display.stallAtFrame = stallAtFrame;
  display.stallMs = stallMs;
  rsvp::Scheduler scheduler(source, clock, display, fits, &width);
  scheduler.setWpm(c.wpm);
  scheduler.setWordsPerFrame(c.wordsPerFrame);

  for (size_t ch = 0; ch < stream().size(); ++ch) {
    if (ch > 0) {
      source.tokens = stream()[ch].tokens;
      source.seek(0);
      scheduler.reposition();
    }
    run.chapterLogStart.push_back(display.log.size());
    scheduler.play();
    while (true) {
      const StepResult r = scheduler.step();
      if (r.event == StepEvent::STOPPED) {
        run.stops.push_back(r.stops);
        // Nothing past the frame on screen has been consumed from the source.
        if (source.consumed() != scheduler.lastFrame().lastWordIndex + 1) run.consumedNothingPastStops = false;
        if (r.stops & rsvp::STOP_CHAPTER_END) break;
        clock.advance(STOP_WAIT_MS);
        scheduler.play();
        continue;
      }
      if (!r.wake) {
        ADD_FAILURE() << "playback halted without a stop";
        break;
      }
      clock.advanceTo(r.wakeAtMs);
    }
    if (ch == 0) run.proseStats = scheduler.meter().stats();
  }
  run.log = display.log;
  run.stats = scheduler.meter().stats();
  run.deliveredTenths = scheduler.meter().deliveredWpmTenths();
  run.auditInOrder = scheduler.audit().inOrder();
  run.auditWords = scheduler.audit().wordsDrawn();
  run.auditSegments = scheduler.audit().segments();
  run.auditSkipped = scheduler.audit().skippedWords();
  return run;
}

// The frames an independent FrameBuilder makes of one chapter with the same settings.
std::vector<Frame> referenceFrames(const Chapter& chapter, const Case& c) {
  rsvptest::VectorSource source(chapter.tokens);
  rsvp::FrameBuilder builder(source, fits, &width);
  builder.setWpm(c.wpm);
  builder.setWordsPerFrame(c.wordsPerFrame);
  return rsvptest::allFrames(builder);
}

// Arithmetic expectation of active time for chapters [first, last).
uint64_t expectedActiveMs(const PlayRun& run, const Case& c, size_t first, size_t last) {
  uint64_t total = 0;
  for (size_t ch = first; ch < last; ++ch) {
    for (const Frame& f : referenceFrames(stream()[ch], c)) total += std::max(f.holdMs, c.refreshMs + DWELL);
    const size_t end = ch + 1 < run.chapterLogStart.size() ? run.chapterLogStart[ch + 1] : run.log.size();
    for (size_t i = run.chapterLogStart[ch]; i < end; ++i) {
      if (run.log[i].kind == DisplayCall::CLEAR) total += c.refreshMs;  // C = R
    }
  }
  return total;
}

uint64_t idealMs(const Case& c, size_t first, size_t last) {
  uint64_t total = 0;
  for (size_t ch = first; ch < last; ++ch) {
    for (const Frame& f : referenceFrames(stream()[ch], c)) total += f.holdMs;
  }
  return total;
}

double wpmOf(uint64_t words, uint64_t ms) { return ms == 0 ? 0.0 : 60000.0 * static_cast<double>(words) / ms; }

std::vector<Case> sweepCases() {
  std::vector<Case> cases;
  for (const uint32_t delay : {0u, 160u, 540u, 2000u}) {
    for (const uint16_t wpm : {uint16_t{240}, uint16_t{600}}) {
      for (const uint8_t n : {uint8_t{1}, uint8_t{2}, uint8_t{3}}) cases.push_back({delay, wpm, n});
    }
  }
  return cases;
}

// Every chapter's frames in the display log cover its tokens exactly once, in order, with the
// source's own texts.
void expectDisplayLogCoversStream(const PlayRun& run, const Case& c) {
  for (size_t ch = 0; ch < stream().size(); ++ch) {
    const auto& tokens = stream()[ch].tokens;
    const size_t end = ch + 1 < run.chapterLogStart.size() ? run.chapterLogStart[ch + 1] : run.log.size();
    size_t next = 0;
    const std::vector<Frame> reference = referenceFrames(stream()[ch], c);
    size_t frameNo = 0;
    for (size_t i = run.chapterLogStart[ch]; i < end; ++i) {
      const DisplayCall& call = run.log[i];
      if (call.kind != DisplayCall::FRAME) continue;
      ASSERT_EQ(call.firstWordIndex, next) << stream()[ch].name;
      ASSERT_EQ(call.lastWordIndex + 1 - call.firstWordIndex, call.wordCount);
      std::string joined;
      for (uint32_t w = call.firstWordIndex; w <= call.lastWordIndex; ++w) {
        if (!joined.empty()) joined += ' ';
        joined += tokens[w].text;
      }
      ASSERT_EQ(call.text, joined);
      ASSERT_LT(frameNo, reference.size());
      EXPECT_EQ(call.holdMs, reference[frameNo].holdMs);
      ++frameNo;
      next = call.lastWordIndex + 1;
    }
    EXPECT_EQ(next, tokens.size()) << stream()[ch].name;
    EXPECT_EQ(frameNo, reference.size());
  }
}

// No frame starts before the previous frame's refresh end plus the dwell, and no interval between
// frame starts is shorter than the previous frame's ideal hold.
void expectTiming(const PlayRun& run) {
  const DisplayCall* previous = nullptr;
  for (size_t ch = 0; ch < run.chapterLogStart.size(); ++ch) {
    previous = nullptr;  // a new chapter starts after an explicit play
    const size_t end = ch + 1 < run.chapterLogStart.size() ? run.chapterLogStart[ch + 1] : run.log.size();
    for (size_t i = run.chapterLogStart[ch]; i < end; ++i) {
      const DisplayCall& call = run.log[i];
      if (call.kind != DisplayCall::FRAME) continue;
      if (previous != nullptr) {
        ASSERT_GE(call.startMs - previous->endMs, DWELL) << "frame at " << call.firstWordIndex;
        ASSERT_GE(call.startMs - previous->startMs, previous->holdMs) << "frame at " << call.firstWordIndex;
      }
      previous = &call;
    }
  }
}

}  // namespace

TEST(PlaybackSweep, StreamIsLongEnough) {
  EXPECT_GE(stream()[0].tokens.size(), 2000u);
  EXPECT_EQ(streamWords(), 5793u + 20 + 417 + 550 + 294);
}

TEST(PlaybackSweep, EveryWordOnceInOrderAndDeliveredWpmAsExpected) {
  std::string table =
      "| Refresh ms | Target WPM | Words/update | Updates | Clears | Delivered WPM | Expected WPM | Delivered, prose "
      "sample only |\n| --- | --- | --- | --- | --- | --- | --- | --- |\n";
  const size_t words = streamWords();
  for (const Case& c : sweepCases()) {
    SCOPED_TRACE(testing::Message() << "refresh " << c.refreshMs << " ms, " << c.wpm
                                    << " WPM, N=" << int(c.wordsPerFrame));
    const PlayRun run = play(c);

    // Order: the audit, and independently the display's own log.
    EXPECT_TRUE(run.auditInOrder);
    EXPECT_EQ(run.auditWords, words);
    EXPECT_EQ(run.auditSegments, stream().size());
    EXPECT_EQ(run.auditSkipped, 0u);
    expectDisplayLogCoversStream(run, c);
    EXPECT_EQ(run.stats.wordsShown, words);
    EXPECT_TRUE(run.consumedNothingPastStops);
    // Stops: prose sample end; title end; ch1 image, checkpoint, end; ch2 table, end (with
    // checkpoint); ch3 code, end.
    const std::vector<uint8_t> expectedStops = {rsvp::STOP_CHAPTER_END,
                                                rsvp::STOP_CHAPTER_END,
                                                rsvp::STOP_NON_PROSE,
                                                rsvp::STOP_CHECKPOINT,
                                                rsvp::STOP_CHAPTER_END,
                                                rsvp::STOP_NON_PROSE,
                                                rsvp::STOP_CHECKPOINT | rsvp::STOP_CHAPTER_END,
                                                rsvp::STOP_NON_PROSE,
                                                rsvp::STOP_CHAPTER_END};
    EXPECT_EQ(run.stops, expectedStops);

    // Timing.
    expectTiming(run);

    // Delivered WPM against the arithmetic expectation (exact active time, so well within 1%).
    const uint64_t expectedMs = expectedActiveMs(run, c, 0, stream().size());
    EXPECT_EQ(run.stats.activeMs, expectedMs);
    const double delivered = run.deliveredTenths / 10.0;
    const double expected = wpmOf(words, expectedMs);
    EXPECT_NEAR(delivered, expected, expected * 0.01);
    const double proseDelivered = wpmOf(run.proseStats.wordsShown, run.proseStats.activeMs);
    const double proseExpected = wpmOf(stream()[0].tokens.size(), expectedActiveMs(run, c, 0, 1));
    EXPECT_NEAR(proseDelivered, proseExpected, proseExpected * 0.01);

    if (c.refreshMs == 0) {
      EXPECT_NEAR(delivered, c.wpm, c.wpm * 0.01);
      EXPECT_NEAR(proseDelivered, c.wpm, c.wpm * 0.01);
      EXPECT_EQ(run.stats.activeMs, idealMs(c, 0, stream().size()));
    } else {
      EXPECT_LT(delivered, c.wpm);
    }
    if (c.refreshMs == 540 && c.wpm == 240 && c.wordsPerFrame == 1) {
      // 60,000 / (540 + 60) = 100 WPM; holds longer than 600 ms and clears take it a little lower.
      EXPECT_NEAR(delivered, 100.0, 2.5);
    }

    char row[200];
    std::snprintf(row, sizeof(row), "| %u | %u | %u | %u | %u | %.1f | %.2f | %.1f |\n", c.refreshMs, c.wpm,
                  c.wordsPerFrame, run.stats.updates, run.stats.clearingRefreshes, delivered, expected, proseDelivered);
    table += row;
  }
  std::printf("%s", table.c_str());
}

TEST(PlaybackSweep, NoCatchUpAfterAFiveSecondStall) {
  const Case c{160, 240, 1};
  const PlayRun run = play(c, 5000, 200, 5000);
  ASSERT_TRUE(run.auditInOrder);
  std::vector<DisplayCall> frames;
  for (const auto& call : run.log) {
    if (call.kind == DisplayCall::FRAME) frames.push_back(call);
  }
  ASSERT_EQ(frames[200].endMs - frames[200].startMs, 5000u);
  EXPECT_EQ(frames[201].startMs - frames[200].startMs, std::max(frames[200].holdMs, 5000u + DWELL));
  // Up to the next stop, every later interval is exactly the frame's normal slot: never shorter
  // than its hold, and nothing is made up.
  const auto clearsBetween = [&](const DisplayCall& a, const DisplayCall& b) {
    uint32_t ms = 0;
    for (const auto& call : run.log) {
      if (call.kind == DisplayCall::CLEAR && call.startMs >= a.endMs && call.endMs <= b.startMs)
        ms += call.endMs - call.startMs;
    }
    return ms;
  };
  for (size_t i = 202; i < 1000; ++i) {
    const uint32_t interval = frames[i].startMs - frames[i - 1].startMs;
    EXPECT_GE(interval, frames[i - 1].holdMs) << i;
    EXPECT_EQ(interval, std::max(frames[i - 1].holdMs, 160u + DWELL) + clearsBetween(frames[i - 1], frames[i])) << i;
  }
  // The stall adds its excess once to the active time and changes nothing else.
  const PlayRun plain = play(c);
  EXPECT_EQ(run.stats.activeMs - plain.stats.activeMs,
            std::max(frames[200].holdMs, 5000u + DWELL) - std::max(frames[200].holdMs, 160u + DWELL));
  EXPECT_EQ(run.stats.updateMsMax, 5000u);
}

TEST(PlaybackSweep, ClockWrapMidStream) {
  const Case c{540, 240, 2};
  const PlayRun plain = play(c, 5000);
  const PlayRun wrapping = play(c, 0xFFFFFFFFu - 600000);  // wraps after about ten minutes of playback
  ASSERT_EQ(wrapping.log.size(), plain.log.size());
  for (size_t i = 0; i < plain.log.size(); ++i) {
    ASSERT_EQ(wrapping.log[i].startMs - (0xFFFFFFFFu - 600000), plain.log[i].startMs - 5000u) << i;
  }
  EXPECT_EQ(wrapping.stats.activeMs, plain.stats.activeMs);
  EXPECT_EQ(wrapping.deliveredTenths, plain.deliveredTenths);
  EXPECT_TRUE(wrapping.auditInOrder);
}
