// Scheduler behaviour against a fake clock and a fake display: pacing, dwell, no catch-up, stops,
// pause, settings changes, seeking, clearing refreshes and clock wrap.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "FixtureBook.h"
#include "PlaybackFakes.h"
#include "TestSupport.h"
#include "lib/RsvpCore/Scheduler.h"

using rsvp::PlaybackState;
using rsvp::Scheduler;
using rsvp::StepEvent;
using rsvp::StepResult;
using rsvptest::DisplayCall;
using rsvptest::FakeClock;
using rsvptest::FakeDisplay;
using rsvptest::OwnedToken;
using rsvptest::VectorSource;

namespace {

// `count` plain words; every `paragraph`-th word (1-based) ends a paragraph (0: none).
std::vector<OwnedToken> plainWords(uint32_t count, uint32_t paragraph = 0) {
  std::vector<OwnedToken> tokens;
  for (uint32_t i = 0; i < count; ++i) {
    uint8_t flags = 0;
    if (paragraph != 0 && (i + 1) % paragraph == 0) flags |= rsvp::TOKEN_PARAGRAPH_END;
    tokens.push_back({"word" + std::to_string(i), i, flags});
  }
  return tokens;
}

struct Rig {
  explicit Rig(std::vector<OwnedToken> tokens, uint32_t refreshMs = 0, uint32_t clearMs = 0, uint32_t startMs = 1000)
      : source(std::move(tokens)),
        clock(startMs),
        display(clock, refreshMs, clearMs),
        scheduler(source, clock, display) {}

  VectorSource source;
  FakeClock clock;
  FakeDisplay display;
  Scheduler scheduler;
};

uint32_t slotOf(const DisplayCall& frame, uint32_t refreshMs, uint32_t dwellMs = rsvp::DEFAULT_MIN_DWELL_MS) {
  return std::max(frame.holdMs, refreshMs + dwellMs);
}

}  // namespace

TEST(Scheduler, StartsPausedAndDrawsNothingUntilPlay) {
  Rig rig(plainWords(10));
  EXPECT_EQ(rig.scheduler.state(), PlaybackState::PAUSED);
  const StepResult result = rig.scheduler.step();
  EXPECT_EQ(result.event, StepEvent::NONE);
  EXPECT_FALSE(result.wake);
  EXPECT_EQ(rig.display.frames, 0u);
  EXPECT_FALSE(rig.scheduler.hasFrame());
  EXPECT_TRUE(rig.scheduler.play());
  EXPECT_FALSE(rig.scheduler.play());  // already playing
}

TEST(Scheduler, IdealDisplayHoldsEachFrameForItsPacedInterval) {
  Rig rig(plainWords(40));
  rig.scheduler.play();
  const StepResult first = rig.scheduler.step();
  ASSERT_EQ(first.event, StepEvent::SHOWED_FRAME);
  EXPECT_EQ(rig.display.log[0].startMs, 1000u);  // no wait before the first frame
  EXPECT_EQ(first.wakeAtMs, 1000u + rig.display.log[0].holdMs);
  rsvptest::runUntilHalt(rig.scheduler, rig.clock);
  const auto frames = rig.display.frameCalls();
  ASSERT_EQ(frames.size(), 40u);
  for (size_t i = 1; i < frames.size(); ++i) {
    EXPECT_EQ(frames[i].startMs - frames[i - 1].startMs, frames[i - 1].holdMs) << i;
    EXPECT_GE(frames[i - 1].holdMs, 223u);  // 240 WPM, plain word: 60,000 / 240 * 1000 / 1117
  }
  EXPECT_EQ(rig.scheduler.state(), PlaybackState::STOPPED);
  EXPECT_EQ(rig.scheduler.stopReasons(), rsvp::STOP_CHAPTER_END);
}

TEST(Scheduler, SlowDisplayHoldsRefreshPlusDwell) {
  for (const uint16_t dwell : {uint16_t{60}, uint16_t{0}, uint16_t{150}}) {
    Rig rig(plainWords(30), 540);
    rig.scheduler.setMinDwellMs(dwell);
    rig.scheduler.play();
    rsvptest::runUntilHalt(rig.scheduler, rig.clock);
    const auto frames = rig.display.frameCalls();
    ASSERT_EQ(frames.size(), 30u);
    for (size_t i = 1; i < frames.size(); ++i) {
      EXPECT_EQ(frames[i].endMs - frames[i].startMs, 540u);
      EXPECT_EQ(frames[i].startMs - frames[i - 1].startMs, std::max<uint32_t>(frames[i - 1].holdMs, 540u + dwell))
          << "dwell " << dwell << " frame " << i;
      EXPECT_GE(frames[i].startMs - frames[i - 1].endMs, dwell);
    }
  }
}

TEST(Scheduler, LateCallsAndStallsAreNeverRepaid) {
  Rig rig(plainWords(60), 160);
  rig.display.stallAtFrame = 10;
  rig.display.stallMs = 5000;
  rig.scheduler.play();
  // The caller is also 700 ms late for frame 30 (after 30 frames were drawn).
  for (int i = 0; i < 1000 && rig.scheduler.state() == PlaybackState::PLAYING; ++i) {
    const StepResult r = rig.scheduler.step();
    if (!r.wake) break;
    rig.clock.advanceTo(r.wakeAtMs + (rig.display.frames == 30 ? 700 : 0));
  }
  const auto frames = rig.display.frameCalls();
  ASSERT_EQ(frames.size(), 60u);
  EXPECT_EQ(frames[11].startMs - frames[10].startMs, 5060u);
  EXPECT_EQ(frames[30].startMs - frames[29].startMs, frames[29].holdMs + 700);
  for (size_t i = 1; i < frames.size(); ++i) {
    EXPECT_GE(frames[i].startMs - frames[i - 1].startMs, frames[i - 1].holdMs) << i;
    if (i != 11 && i != 30) {
      EXPECT_EQ(frames[i].startMs - frames[i - 1].startMs, slotOf(frames[i - 1], 160)) << i;
    }
  }
}

TEST(Scheduler, StopsAfterTheCheckpointWordWithoutConsumingPastIt) {
  auto tokens = plainWords(50, 10);
  tokens[24].flags |= rsvp::TOKEN_SECTION_END | rsvp::TOKEN_CHECKPOINT;
  Rig rig(tokens, 200);
  rig.scheduler.setWordsPerFrame(2);
  rig.scheduler.play();
  const StepResult stop = rsvptest::runUntilHalt(rig.scheduler, rig.clock);
  ASSERT_EQ(stop.event, StepEvent::STOPPED);
  EXPECT_EQ(stop.stops, rsvp::STOP_CHECKPOINT);
  EXPECT_FALSE(stop.wake);
  EXPECT_EQ(rig.scheduler.state(), PlaybackState::STOPPED);
  EXPECT_EQ(rig.scheduler.stopReasons(), rsvp::STOP_CHECKPOINT);

  const DisplayCall last = rig.display.log.back();
  EXPECT_EQ(last.lastWordIndex, 24u);
  EXPECT_EQ(rig.scheduler.lastFrame().lastWordIndex, 24u);
  EXPECT_EQ(rig.source.consumed(), 25u);  // word 25 was only peeked, never consumed
  // The stop is reported once the section's last word has had its full slot.
  EXPECT_EQ(rig.clock.now, last.startMs + slotOf(last, 200));

  // Time at the checkpoint is not active time.
  const uint64_t activeAtStop = rig.scheduler.meter().stats().activeMs;
  rig.clock.advance(10 * 60 * 1000);
  EXPECT_EQ(rig.scheduler.step().event, StepEvent::NONE);
  EXPECT_EQ(rig.display.frames, last.firstWordIndex / 2 + 1);

  rig.scheduler.resolveStop();
  EXPECT_EQ(rig.scheduler.state(), PlaybackState::PAUSED);
  EXPECT_EQ(rig.scheduler.step().event, StepEvent::NONE);  // waits for an explicit play
  EXPECT_EQ(rig.scheduler.meter().stats().activeMs, activeAtStop);
  rig.scheduler.play();
  const StepResult next = rig.scheduler.step();
  ASSERT_EQ(next.event, StepEvent::SHOWED_FRAME);
  EXPECT_EQ(rig.display.log.back().firstWordIndex, 25u);
  rsvptest::runUntilHalt(rig.scheduler, rig.clock);
  EXPECT_TRUE(rig.scheduler.audit().inOrder());
  EXPECT_EQ(rig.scheduler.audit().wordsDrawn(), 50u);
}

TEST(Scheduler, SectionEndWithoutCheckpointDoesNotStop) {
  auto tokens = plainWords(20);
  tokens[9].flags |= rsvp::TOKEN_SECTION_END;
  Rig rig(tokens);
  rig.scheduler.play();
  const StepResult r = rsvptest::runUntilHalt(rig.scheduler, rig.clock);
  EXPECT_EQ(r.stops, rsvp::STOP_CHAPTER_END);
  EXPECT_EQ(rig.display.frames, 20u);
}

TEST(Scheduler, StopsAtANonProseMarkerAndAtTheChapterEnd) {
  const auto& document = rsvptest::fixtureDocument("text/chapter-1.xhtml");
  const auto tokens = rsvptest::chapterTokens(document);
  // Where chapter 1 stops, from the expected file: the figure's IMAGE marker, the end of s0001
  // (which has a checkpoint) and the last word handed out (the footnote body after it is skipped).
  size_t image = 0;
  for (const auto& m : rsvptest::fixtureMarkers(document)) {
    if (m.kind == "IMAGE") image = m.start;
  }
  ASSERT_GT(image, 0u);
  const auto s0001End = static_cast<size_t>(rsvptest::requireIndex(rsvptest::fixtureSection("s0001"), "end", "s0001"));
  ASSERT_GT(s0001End, image);
  Rig rig(tokens, 540);
  rig.scheduler.play();

  StepResult r = rsvptest::runUntilHalt(rig.scheduler, rig.clock);
  ASSERT_EQ(r.event, StepEvent::STOPPED);
  EXPECT_EQ(r.stops, rsvp::STOP_NON_PROSE);  // the figure: the word before its IMAGE marker
  EXPECT_EQ(rig.display.log.back().lastWordIndex, image - 1);
  EXPECT_EQ(rig.source.consumed(), rsvptest::tokenPosition(tokens, image - 1) + 1);
  rig.clock.advance(45000);  // viewing the image
  rig.scheduler.play();

  r = rsvptest::runUntilHalt(rig.scheduler, rig.clock);
  ASSERT_EQ(r.event, StepEvent::STOPPED);
  EXPECT_EQ(r.stops, rsvp::STOP_CHECKPOINT);  // end of s0001
  EXPECT_EQ(rig.display.log.back().lastWordIndex, s0001End - 1);
  EXPECT_EQ(rig.source.consumed(), rsvptest::tokenPosition(tokens, s0001End - 1) + 1);
  rig.scheduler.play();

  r = rsvptest::runUntilHalt(rig.scheduler, rig.clock);
  ASSERT_EQ(r.event, StepEvent::STOPPED);
  EXPECT_EQ(r.stops, rsvp::STOP_CHAPTER_END);
  EXPECT_EQ(rig.display.log.back().lastWordIndex, tokens.back().wordIndex);
  const size_t frames = rig.display.frames;

  // Playing again at the chapter end stops again without drawing or clearing.
  rig.scheduler.play();
  r = rig.scheduler.step();
  EXPECT_EQ(r.event, StepEvent::STOPPED);
  EXPECT_EQ(r.stops, rsvp::STOP_CHAPTER_END);
  EXPECT_EQ(rig.display.log.size(), frames + rig.display.clears);
  EXPECT_EQ(rig.display.frames, frames);

  EXPECT_TRUE(rig.scheduler.audit().inOrder());
  EXPECT_EQ(rig.scheduler.audit().wordsDrawn(), tokens.size());
  // The caption follows the image directly and the footnote body comes after the last word drawn,
  // so playback passes over no index.
  EXPECT_EQ(rig.scheduler.audit().skippedWords(), 0u);
}

TEST(Scheduler, ResumingAfterNonProseAcceptsSkippedIndexes) {
  // A source whose non-prose words keep their indexes (a table's cells, words 6 to 9).
  auto tokens = plainWords(6);
  tokens[5].flags |= rsvp::TOKEN_NON_PROSE_FOLLOWS;
  for (uint32_t i = 10; i < 15; ++i) tokens.push_back({"after" + std::to_string(i), i, 0});
  Rig rig(tokens);
  rig.scheduler.play();
  EXPECT_EQ(rsvptest::runUntilHalt(rig.scheduler, rig.clock).stops, rsvp::STOP_NON_PROSE);
  rig.scheduler.play();
  EXPECT_EQ(rsvptest::runUntilHalt(rig.scheduler, rig.clock).stops, rsvp::STOP_CHAPTER_END);
  EXPECT_TRUE(rig.scheduler.audit().inOrder());
  EXPECT_EQ(rig.scheduler.audit().skippedWords(), 4u);
  EXPECT_EQ(rig.scheduler.audit().wordsDrawn(), 11u);
}

TEST(Scheduler, AContinuedSessionMatchesPlayingOnAtTheNonProseStop) {
  // Task R5: RSVP closes at a non-prose stop to show the item in page view, and a new scheduler
  // carries the session on after it. Meter and audit must end exactly as if play() had been called
  // at the stop, and the time spent viewing the item is not active time.
  auto tokens = plainWords(6);
  tokens[5].flags |= rsvp::TOKEN_NON_PROSE_FOLLOWS;
  for (uint32_t i = 10; i < 15; ++i) tokens.push_back({"after" + std::to_string(i), i, 0});

  Rig reference(tokens, 120);
  reference.scheduler.play();
  ASSERT_EQ(rsvptest::runUntilHalt(reference.scheduler, reference.clock).stops, rsvp::STOP_NON_PROSE);
  reference.clock.advance(45000);  // viewing the item
  reference.scheduler.play();
  ASSERT_EQ(rsvptest::runUntilHalt(reference.scheduler, reference.clock).stops, rsvp::STOP_CHAPTER_END);

  Rig first(tokens, 120);
  first.scheduler.play();
  ASSERT_EQ(rsvptest::runUntilHalt(first.scheduler, first.clock).stops, rsvp::STOP_NON_PROSE);
  ASSERT_EQ(first.source.consumed(), 6u);
  const rsvp::DeliveredWpmMeter meterAtStop = first.scheduler.meter();
  const rsvp::WordOrderAudit auditAtStop = first.scheduler.audit();

  Rig second(tokens, 120, 0, first.clock.now + 45000);
  second.source.seek(6);  // the first word after the item
  second.scheduler.reposition();
  second.scheduler.continueSession(meterAtStop, auditAtStop, true);
  second.scheduler.play();
  ASSERT_EQ(rsvptest::runUntilHalt(second.scheduler, second.clock).stops, rsvp::STOP_CHAPTER_END);

  const rsvp::WordOrderAudit& a = second.scheduler.audit();
  EXPECT_TRUE(a.inOrder());
  EXPECT_EQ(a.wordsDrawn(), 11u);
  EXPECT_EQ(a.segments(), 1u) << "one run across the hand-over";
  EXPECT_EQ(a.skippedWords(), 4u) << "the item's own indexes, 6 to 9";
  EXPECT_EQ(a.wordsDrawn(), reference.scheduler.audit().wordsDrawn());
  EXPECT_EQ(a.skippedWords(), reference.scheduler.audit().skippedWords());

  const rsvp::MeterStats& m = second.scheduler.meter().stats();
  const rsvp::MeterStats& r = reference.scheduler.meter().stats();
  EXPECT_EQ(m.wordsShown, r.wordsShown);
  EXPECT_EQ(m.updates, r.updates);
  // The new scheduler's pacing starts without the old one's sub-millisecond remainder, so a hold
  // after the item can round 1 ms differently; the 45 s on the item are not active time either way.
  EXPECT_NEAR(static_cast<double>(m.activeMs), static_cast<double>(r.activeMs), 1.0);
  EXPECT_LT(m.activeMs, 45000u);
  EXPECT_NEAR(second.scheduler.meter().deliveredWpmTenths(), reference.scheduler.meter().deliveredWpmTenths(), 1);
}

TEST(Scheduler, AContinuedSessionWithoutTheNonProseFlagReportsTheJumpAsAGap) {
  auto tokens = plainWords(6);
  tokens[5].flags |= rsvp::TOKEN_NON_PROSE_FOLLOWS;
  for (uint32_t i = 10; i < 15; ++i) tokens.push_back({"after" + std::to_string(i), i, 0});
  Rig first(tokens);
  first.scheduler.play();
  ASSERT_EQ(rsvptest::runUntilHalt(first.scheduler, first.clock).stops, rsvp::STOP_NON_PROSE);

  Rig second(tokens);
  second.source.seek(6);
  second.scheduler.reposition();
  second.scheduler.continueSession(first.scheduler.meter(), first.scheduler.audit(), false);
  second.scheduler.play();
  rsvptest::runUntilHalt(second.scheduler, second.clock);
  EXPECT_FALSE(second.scheduler.audit().inOrder());
  EXPECT_EQ(second.scheduler.audit().gaps(), 1u);
}

TEST(Scheduler, PauseRequestedDuringARefreshTakesEffectBeforeTheNextFrame) {
  Rig rig(plainWords(100), 540);
  rig.display.duringShow = [&](size_t frame) {
    if (frame == 5) rig.scheduler.requestPause();  // arrives from another task mid-refresh
  };
  rig.scheduler.play();
  const StepResult r = rsvptest::runUntilHalt(rig.scheduler, rig.clock);
  EXPECT_EQ(r.event, StepEvent::PAUSED);
  EXPECT_FALSE(r.wake);
  EXPECT_EQ(rig.scheduler.state(), PlaybackState::PAUSED);
  EXPECT_EQ(rig.display.frames, 6u);  // the frame being refreshed completes; nothing after it
  EXPECT_EQ(rig.source.consumed(), 6u);
  // Paused at the refresh end: the last frame's active time runs to there.
  const auto frames = rig.display.frameCalls();
  EXPECT_EQ(rig.scheduler.meter().stats().activeMs, frames[5].endMs - frames[0].startMs);
  EXPECT_EQ(rig.scheduler.meter().stats().wordsShown, 6u);

  rig.clock.advance(60000);
  EXPECT_EQ(rig.scheduler.step().event, StepEvent::NONE);
  EXPECT_EQ(rig.display.frames, 6u);
  rig.display.duringShow = nullptr;
  rig.scheduler.play();
  rsvptest::runUntilHalt(rig.scheduler, rig.clock);
  EXPECT_EQ(rig.display.frameCalls()[6].firstWordIndex, 6u);
  EXPECT_TRUE(rig.scheduler.audit().inOrder());
  EXPECT_EQ(rig.scheduler.audit().wordsDrawn(), 100u);
}

TEST(Scheduler, PauseMidHoldCountsTheLastFrameUntilThePause) {
  Rig rig(plainWords(100), 100);
  rig.scheduler.play();
  rsvptest::runFrames(rig.scheduler, rig.clock, rig.display, 10);
  rig.clock.advance(50);  // refresh ended at +100, so this is 150 ms into the last frame
  rig.scheduler.requestPause();
  EXPECT_EQ(rig.scheduler.step().event, StepEvent::PAUSED);
  const auto frames = rig.display.frameCalls();
  EXPECT_EQ(rig.scheduler.meter().stats().activeMs, frames[9].startMs + 150 - frames[0].startMs);
}

TEST(Scheduler, PauseLongAfterTheSlotCountsOnlyUpToTheSlotEnd) {
  Rig rig(plainWords(100), 100);
  rig.scheduler.play();
  rsvptest::runFrames(rig.scheduler, rig.clock, rig.display, 10);
  rig.clock.advance(5000);  // the caller did not step again for 5 s, then paused
  rig.scheduler.requestPause();
  EXPECT_EQ(rig.scheduler.step().event, StepEvent::PAUSED);
  const auto frames = rig.display.frameCalls();
  EXPECT_EQ(rig.scheduler.meter().stats().activeMs, frames[9].startMs + slotOf(frames[9], 100) - frames[0].startMs);
}

TEST(Scheduler, PauseWhileTheStopFrameIsShownReportsTheStop) {
  auto tokens = plainWords(20);
  tokens[7].flags |= rsvp::TOKEN_SECTION_END | rsvp::TOKEN_CHECKPOINT;
  Rig rig(tokens, 100);
  rig.scheduler.play();
  rsvptest::runFrames(rig.scheduler, rig.clock, rig.display, 8);
  rig.scheduler.requestPause();
  const StepResult r = rig.scheduler.step();
  EXPECT_EQ(r.event, StepEvent::STOPPED);
  EXPECT_EQ(r.stops, rsvp::STOP_CHECKPOINT);
  EXPECT_EQ(rig.source.consumed(), 8u);
}

TEST(Scheduler, PausedAndCheckpointTimeAreExcludedFromTheMeter) {
  // The same chapter played straight through, and with a one-hour pause at a slot end plus ten
  // minutes at each stop: identical words and active time.
  const auto tokens = rsvptest::chapterTokens(rsvptest::fixtureDocument("text/chapter-2.xhtml"));
  auto run = [&](bool interruptions) {
    Rig rig(tokens, 160, 900);
    rig.scheduler.play();
    while (true) {
      const StepResult r = rig.scheduler.step();
      if (r.event == StepEvent::STOPPED) {
        if (r.stops & rsvp::STOP_CHAPTER_END) break;
        if (interruptions) rig.clock.advance(10 * 60 * 1000);
        rig.scheduler.play();
        continue;
      }
      rig.clock.advanceTo(r.wakeAtMs);
      if (interruptions && rig.display.frames == 100 && r.event == StepEvent::SHOWED_FRAME) {
        rig.scheduler.requestPause();
        EXPECT_EQ(rig.scheduler.step().event, StepEvent::PAUSED);
        rig.clock.advance(3600 * 1000);
        rig.scheduler.play();
      }
    }
    EXPECT_TRUE(rig.scheduler.audit().inOrder());
    return rig.scheduler.meter().stats();
  };
  const rsvp::MeterStats straight = run(false);
  const rsvp::MeterStats interrupted = run(true);
  EXPECT_EQ(straight.wordsShown, tokens.size());  // chapter 2's prose words (the table and notes are not shown)
  EXPECT_EQ(interrupted.wordsShown, straight.wordsShown);
  EXPECT_EQ(interrupted.activeMs, straight.activeMs);
  EXPECT_EQ(interrupted.updates, straight.updates);
  EXPECT_LT(interrupted.activeMs, 3600u * 1000);
}

TEST(Scheduler, TimeBeforeTheFirstPlayIsNotActive) {
  Rig rig(plainWords(10));
  rig.clock.advance(100000);
  rig.scheduler.play();
  rig.clock.advance(30);  // the activity draws its guides first
  rsvptest::runUntilHalt(rig.scheduler, rig.clock);
  uint64_t slots = 0;
  for (const auto& f : rig.display.frameCalls()) slots += slotOf(f, 0);
  EXPECT_EQ(rig.scheduler.meter().stats().activeMs, slots);
}

TEST(Scheduler, WpmAndGroupSizeChangeOnlyOnResumeAndKeepThePosition) {
  Rig rig(plainWords(200), 0);
  rig.scheduler.play();
  rsvptest::runFrames(rig.scheduler, rig.clock, rig.display, 20);
  rig.scheduler.setWpm(600);  // while playing: waits for the next play()
  rig.scheduler.setWordsPerFrame(3);
  EXPECT_EQ(rig.scheduler.wpm(), 600);
  rsvptest::runFrames(rig.scheduler, rig.clock, rig.display, 10);
  auto frames = rig.display.frameCalls();
  for (size_t i = 20; i < 30; ++i) {
    EXPECT_EQ(frames[i].wordCount, 1u);
    EXPECT_GE(frames[i].holdMs, 223u);
  }
  rig.scheduler.requestPause();
  ASSERT_EQ(rig.scheduler.step().event, StepEvent::PAUSED);
  rig.clock.advance(20000);
  rig.scheduler.play();
  rsvptest::runFrames(rig.scheduler, rig.clock, rig.display, 10);
  frames = rig.display.frameCalls();
  EXPECT_EQ(frames[30].firstWordIndex, 30u);
  for (size_t i = 30; i < 40; ++i) {
    EXPECT_EQ(frames[i].wordCount, 3u);
    // Three plain words at 600 WPM: 3 * 60,000 / 600 * 1000 / 1117 = 268.6 ms.
    EXPECT_GE(frames[i].holdMs, 268u);
    EXPECT_LE(frames[i].holdMs, 269u);
  }
  rsvptest::runUntilHalt(rig.scheduler, rig.clock);
  EXPECT_TRUE(rig.scheduler.audit().inOrder());
  EXPECT_EQ(rig.scheduler.audit().wordsDrawn(), 200u);
}

TEST(Scheduler, RepositionResetsPacingAndOpensAnAuditSegment) {
  Rig rig(plainWords(100), 300);
  rig.scheduler.play();
  rsvptest::runFrames(rig.scheduler, rig.clock, rig.display, 30);
  // A sentence step back while playing: reposition pauses.
  rig.source.seek(12);
  rig.scheduler.reposition();
  EXPECT_EQ(rig.scheduler.state(), PlaybackState::PAUSED);
  EXPECT_FALSE(rig.scheduler.hasFrame());
  rig.clock.advance(5000);
  rig.scheduler.play();
  const uint32_t playedAt = rig.clock.now;
  ASSERT_EQ(rig.scheduler.step().event, StepEvent::SHOWED_FRAME);
  EXPECT_EQ(rig.display.log.back().startMs, playedAt);  // no stale deadline
  EXPECT_EQ(rig.display.log.back().firstWordIndex, 12u);
  // The pacing remainder restarted: the first hold equals a fresh pacer's.
  rsvp::Pacer fresh(240);
  EXPECT_EQ(rig.display.log.back().holdMs, fresh.intervalMs(rsvp::PACING.normal));
  rsvptest::runUntilHalt(rig.scheduler, rig.clock);
  const rsvp::WordOrderAudit& audit = rig.scheduler.audit();
  EXPECT_TRUE(audit.inOrder());
  EXPECT_EQ(audit.segments(), 2u);
  EXPECT_EQ(audit.wordsDrawn(), 30u + 88u);

  // A seek forward while paused, then a seek without reposition() is caught by the audit.
  Rig other(plainWords(100));
  other.scheduler.play();
  rsvptest::runFrames(other.scheduler, other.clock, other.display, 5);
  other.scheduler.requestPause();
  other.scheduler.step();
  other.source.seek(50);
  other.scheduler.reposition();
  other.scheduler.play();
  rsvptest::runFrames(other.scheduler, other.clock, other.display, 5);
  EXPECT_TRUE(other.scheduler.audit().inOrder());
  other.source.seek(20);  // not announced
  rsvptest::runFrames(other.scheduler, other.clock, other.display, 1);
  EXPECT_FALSE(other.scheduler.audit().inOrder());
  EXPECT_EQ(other.scheduler.audit().firstViolationExpected(), 55u);
  EXPECT_EQ(other.scheduler.audit().firstViolationDrawn(), 20u);
}

TEST(Scheduler, ResumingRightAfterARefreshStillHonoursTheDwell) {
  Rig rig(plainWords(20), 200);
  rig.scheduler.play();
  rsvptest::runFrames(rig.scheduler, rig.clock, rig.display, 3);
  rig.scheduler.requestPause();
  ASSERT_EQ(rig.scheduler.step().event, StepEvent::PAUSED);
  rig.clock.advance(10);
  rig.scheduler.play();
  const StepResult wait = rig.scheduler.step();
  EXPECT_EQ(wait.event, StepEvent::NONE);
  EXPECT_EQ(wait.wakeAtMs, rig.display.log.back().endMs + 60);
  rig.clock.advanceTo(wait.wakeAtMs);
  EXPECT_EQ(rig.scheduler.step().event, StepEvent::SHOWED_FRAME);
}

TEST(Scheduler, ClearsAtTheFirstParagraphEndAfterTheSoftLimit) {
  // Paragraphs of 7 words: the first paragraph end at or after update 60 is update 63.
  Rig rig(plainWords(300, 7), 100, 1500);
  rig.scheduler.play();
  rsvptest::runUntilHalt(rig.scheduler, rig.clock);
  std::vector<size_t> clearAfter;  // frames drawn before each clear
  size_t frames = 0;
  for (const auto& call : rig.display.log) {
    if (call.kind == DisplayCall::CLEAR) {
      clearAfter.push_back(frames);
    } else {
      ++frames;
    }
  }
  EXPECT_EQ(clearAfter, (std::vector<size_t>{63, 126, 189, 252}));
  EXPECT_EQ(rig.scheduler.meter().stats().clearingRefreshes, 4u);
  EXPECT_EQ(rig.scheduler.meter().stats().clearMsTotal, 6000u);
}

TEST(Scheduler, HardLimitClearsMidParagraph) {
  Rig rig(plainWords(300), 100, 1500);
  rig.scheduler.play();
  rsvptest::runUntilHalt(rig.scheduler, rig.clock);
  std::vector<size_t> clearAfter;
  size_t frames = 0;
  for (const auto& call : rig.display.log) {
    if (call.kind == DisplayCall::CLEAR) {
      clearAfter.push_back(frames);
    } else {
      ++frames;
    }
  }
  EXPECT_EQ(clearAfter, (std::vector<size_t>{120, 240}));
}

TEST(Scheduler, LoweredLimitsForTrialProfiles) {
  struct Case {
    rsvp::ClearingLimits limits;
    uint32_t paragraph;
    std::vector<size_t> expected;
  };
  const Case cases[] = {
      {{45, 90}, 7, {49, 98, 147, 196, 245, 294}},
      {{30, 60}, 7, {35, 70, 105, 140, 175, 210, 245, 280}},
      {{45, 90}, 0, {90, 180, 270}},
      {{30, 60}, 0, {60, 120, 180, 240}},
  };
  for (const Case& c : cases) {
    Rig rig(plainWords(300, c.paragraph), 100, 800);
    rig.scheduler.setClearingLimits(c.limits);
    rig.scheduler.play();
    rsvptest::runUntilHalt(rig.scheduler, rig.clock);
    std::vector<size_t> clearAfter;
    size_t frames = 0;
    for (const auto& call : rig.display.log) {
      if (call.kind == DisplayCall::CLEAR) {
        clearAfter.push_back(frames);
      } else {
        ++frames;
      }
    }
    EXPECT_EQ(clearAfter, c.expected) << c.limits.softLimit << "/" << c.limits.hardLimit;
  }
}

TEST(Scheduler, ClearingTimeIsActiveDisplayDelay) {
  Rig rig(plainWords(300, 7), 100, 1500);
  rig.scheduler.play();
  rsvptest::runUntilHalt(rig.scheduler, rig.clock);
  const auto& log = rig.display.log;
  uint64_t expected = 0;
  for (size_t i = 0; i < log.size(); ++i) {
    if (log[i].kind == DisplayCall::CLEAR) {
      expected += log[i].endMs - log[i].startMs;
      // The paragraph-end frame keeps its full slot; the clear starts when the next frame was
      // due, and the next frame follows the clear at once.
      ASSERT_GT(i, 0u);
      EXPECT_EQ(log[i].startMs, log[i - 1].startMs + slotOf(log[i - 1], 100));
      ASSERT_LT(i + 1, log.size());
      EXPECT_EQ(log[i + 1].startMs, log[i].endMs);
    } else {
      expected += slotOf(log[i], 100);
    }
  }
  const rsvp::MeterStats& stats = rig.scheduler.meter().stats();
  EXPECT_EQ(stats.activeMs, expected);
  EXPECT_EQ(stats.clearingRefreshes, 4u);
  // Delivered WPM includes the clears: 300 words over the slots plus 4 x 1.5 s.
  EXPECT_EQ(rig.scheduler.meter().deliveredWpmTenths(), (300ull * 600000 + expected / 2) / expected);
}

TEST(Scheduler, AClearRunByTheCallerRestartsTheCount) {
  Rig rig(plainWords(300, 7), 100, 1500);
  rig.scheduler.play();
  rsvptest::runFrames(rig.scheduler, rig.clock, rig.display, 50);
  rig.scheduler.requestPause();
  ASSERT_EQ(rig.scheduler.step().event, StepEvent::PAUSED);
  rig.scheduler.noteClearingRefresh();  // the activity cleared on pause
  EXPECT_EQ(rig.scheduler.clearingPolicy().updatesSinceClear(), 0u);
  rig.scheduler.play();
  rsvptest::runUntilHalt(rig.scheduler, rig.clock);
  size_t frames = 0;
  size_t firstClear = 0;
  for (const auto& call : rig.display.log) {
    if (call.kind == DisplayCall::CLEAR) {
      firstClear = frames;
      break;
    }
    ++frames;
  }
  EXPECT_EQ(firstClear, 112u);  // 50 + the first multiple of 7 at or after 50 + 60
}

TEST(Scheduler, ClockWrapDoesNotChangeTheSchedule) {
  auto run = [](uint32_t startMs) {
    Rig rig(plainWords(400, 9), 540, 1960, startMs);
    rig.scheduler.play();
    rsvptest::runUntilHalt(rig.scheduler, rig.clock);
    std::vector<uint32_t> offsets;
    for (const auto& call : rig.display.log) offsets.push_back(call.startMs - startMs);
    return std::make_pair(offsets, rig.scheduler.meter().stats());
  };
  const auto plain = run(1000);
  const auto wrapping = run(0xFFFFFFFFu - 30000);  // wraps about 50 updates in
  EXPECT_EQ(wrapping.first, plain.first);
  EXPECT_EQ(wrapping.second.activeMs, plain.second.activeMs);
  EXPECT_EQ(wrapping.second.wordsShown, 400u);
  EXPECT_EQ(wrapping.second.updateMsMax, 540u);
  EXPECT_EQ(wrapping.second.clearMsMax, 1960u);
}

TEST(Scheduler, ResetSessionStartsANewMeterAndAudit) {
  Rig rig(plainWords(30), 100);
  rig.scheduler.play();
  rsvptest::runFrames(rig.scheduler, rig.clock, rig.display, 10);
  rig.scheduler.requestPause();
  rig.scheduler.step();
  rig.scheduler.resetSession();
  EXPECT_EQ(rig.scheduler.meter().stats().wordsShown, 0u);
  EXPECT_EQ(rig.scheduler.meter().stats().activeMs, 0u);
  EXPECT_EQ(rig.scheduler.audit().wordsDrawn(), 0u);
  rig.scheduler.play();
  rsvptest::runUntilHalt(rig.scheduler, rig.clock);
  EXPECT_EQ(rig.scheduler.meter().stats().wordsShown, 20u);
  EXPECT_TRUE(rig.scheduler.audit().inOrder());
}
