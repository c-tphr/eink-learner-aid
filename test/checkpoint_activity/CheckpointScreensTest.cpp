// The checkpoint screens' pure part (src/activities/checkpoint/CheckpointScreens.h) and their rows of
// the binding table (src/controls/ControlMap.h): which screen each flow state shows, focus order,
// zones and defaults from docs/spikes/S4-control-map.md (section 2 and rows 17-29), the key paths of
// S4 section 9, the items offered per state, answer paging and how a checkpoint ends. Also the
// gesture reader's vertical swipe (S4 row 20).

#include <gtest/gtest.h>

#include <vector>

#include "activities/checkpoint/CheckpointScreens.h"
#include "controls/ControlGesture.h"
#include "controls/ControlMap.h"

using checkpoint::FlowEvent;
using checkpoint::FlowState;
using checkpoint::ReadingMode;
using checkpointui::End;
using checkpointui::PageMove;
using checkpointui::Screen;
using controls::Item;

namespace {

const FlowState ALL_STATES[] = {FlowState::Idle,        FlowState::PromptHidden,
                                FlowState::AnswerShown, FlowState::IncorrectChoice,
                                FlowState::Rereading,   FlowState::Finished};

std::vector<Item> order(const controls::Screen& s) {
  std::vector<Item> items;
  for (int i = 0; i < s.count; ++i) items.push_back(s.zones[i].item);
  return items;
}

// The item reached from the default by `downs` short presses of Down (S4 "Down xN").
Item afterDowns(const Screen screen, const ReadingMode mode, const int downs) {
  const controls::Screen& layout = checkpointui::layoutOf(screen);
  int f = checkpointui::defaultFocus(screen, mode);
  for (int i = 0; i < downs; ++i) f = controls::focusNext(layout, f);
  return layout.zones[f].item;
}

Item afterUps(const Screen screen, const ReadingMode mode, const int ups) {
  const controls::Screen& layout = checkpointui::layoutOf(screen);
  int f = checkpointui::defaultFocus(screen, mode);
  for (int i = 0; i < ups; ++i) f = controls::focusPrev(f);
  return layout.zones[f].item;
}

}  // namespace

TEST(CheckpointScreens, EachFlowStateHasItsScreen) {
  EXPECT_EQ(checkpointui::screenFor(FlowState::PromptHidden), Screen::Prompt);
  EXPECT_EQ(checkpointui::screenFor(FlowState::AnswerShown), Screen::Answer);
  EXPECT_EQ(checkpointui::screenFor(FlowState::IncorrectChoice), Screen::AfterIncorrect);
  // The activity ends in these: nothing of its own is shown.
  EXPECT_EQ(checkpointui::screenFor(FlowState::Idle), Screen::None);
  EXPECT_EQ(checkpointui::screenFor(FlowState::Rereading), Screen::None);
  EXPECT_EQ(checkpointui::screenFor(FlowState::Finished), Screen::None);
  EXPECT_STREQ(checkpointui::screenName(Screen::Prompt), "Prompt");
  EXPECT_STREQ(checkpointui::screenName(Screen::Answer), "Answer");
  EXPECT_STREQ(checkpointui::screenName(Screen::AfterIncorrect), "Incorrect");
}

TEST(CheckpointScreens, FocusOrderPutsSkipCheckpointLast) {
  // S4 section 2: "Focus order: REVEAL, Skip question, Skip checkpoint", "ANSWER BOX, Correct,
  // Incorrect, Skip question, Skip checkpoint", "Reread RSVP, Reread page, Continue, Skip question,
  // Skip checkpoint".
  EXPECT_EQ(order(checkpointui::layoutOf(Screen::Prompt)),
            (std::vector<Item>{Item::Reveal, Item::SkipPrompt, Item::SkipCheckpoint}));
  EXPECT_EQ(
      order(checkpointui::layoutOf(Screen::Answer)),
      (std::vector<Item>{Item::AnswerBox, Item::Correct, Item::Incorrect, Item::SkipPrompt, Item::SkipCheckpoint}));
  EXPECT_EQ(order(checkpointui::layoutOf(Screen::AfterIncorrect)),
            (std::vector<Item>{Item::RereadRsvp, Item::RereadPage, Item::ContinueOn, Item::SkipPrompt,
                               Item::SkipCheckpoint}));
}

TEST(CheckpointScreens, ZonesMatchS4) {
  struct Expected {
    Item item;
    int x, y, w, h;
  };
  const Expected prompt[] = {{Item::Reveal, 16, 416, 448, 120},
                             {Item::SkipPrompt, 264, 680, 200, 64},
                             {Item::SkipCheckpoint, 16, 680, 200, 64}};
  const Expected answer[] = {{Item::AnswerBox, 16, 120, 448, 440},
                             {Item::Correct, 16, 576, 208, 88},
                             {Item::Incorrect, 256, 576, 208, 88},
                             {Item::SkipPrompt, 264, 680, 200, 64},
                             {Item::SkipCheckpoint, 16, 680, 200, 64}};
  const Expected incorrect[] = {{Item::RereadRsvp, 16, 216, 448, 80},
                                {Item::RereadPage, 16, 308, 448, 80},
                                {Item::ContinueOn, 16, 400, 448, 80},
                                {Item::SkipPrompt, 264, 680, 200, 64},
                                {Item::SkipCheckpoint, 16, 680, 200, 64}};
  const auto check = [](const controls::Screen& s, const Expected* e, const int n) {
    ASSERT_EQ(s.count, n);
    for (int i = 0; i < n; ++i) {
      const controls::Zone& z = s.zones[i];
      EXPECT_EQ(z.item, e[i].item) << i;
      EXPECT_EQ(z.x, e[i].x) << controls::itemName(z.item);
      EXPECT_EQ(z.y, e[i].y) << controls::itemName(z.item);
      EXPECT_EQ(z.w, e[i].w) << controls::itemName(z.item);
      EXPECT_EQ(z.h, e[i].h) << controls::itemName(z.item);
      EXPECT_FALSE(z.repeats) << "no checkpoint item repeats while held";
      // S4 "Target sizes": at least 64 px high and 96 px wide; the default primary action 120 high.
      EXPECT_GE(z.h, 64);
      EXPECT_GE(z.w, 96);
      // Inside the screen and above the key hint row (y 752).
      EXPECT_GE(z.x, 0);
      EXPECT_LE(z.x + z.w, 480);
      EXPECT_LE(z.y + z.h, 752);
    }
  };
  check(controls::CHECKPOINT_PROMPT, prompt, 3);
  check(controls::CHECKPOINT_ANSWER, answer, 5);
  check(controls::CHECKPOINT_INCORRECT, incorrect, 5);
}

TEST(CheckpointScreens, ZonesDoNotOverlapAndTapsFindThem) {
  for (const Screen screen : {Screen::Prompt, Screen::Answer, Screen::AfterIncorrect}) {
    const controls::Screen& s = checkpointui::layoutOf(screen);
    for (int i = 0; i < s.count; ++i) {
      const controls::Zone& a = s.zones[i];
      for (int j = i + 1; j < s.count; ++j) {
        const controls::Zone& b = s.zones[j];
        const bool apart = a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y;
        EXPECT_TRUE(apart) << controls::itemName(a.item) << " / " << controls::itemName(b.item);
      }
      const controls::Zone* hit = controls::hit(s, a.x + a.w / 2, a.y + a.h / 2);
      ASSERT_NE(hit, nullptr);
      EXPECT_EQ(hit->item, a.item);
    }
  }
  // S4 section 9 touch tokens.
  EXPECT_EQ(controls::hit(controls::CHECKPOINT_PROMPT, 240, 476)->item, Item::Reveal);
  EXPECT_EQ(controls::hit(controls::CHECKPOINT_PROMPT, 364, 712)->item, Item::SkipPrompt);
  EXPECT_EQ(controls::hit(controls::CHECKPOINT_PROMPT, 116, 712)->item, Item::SkipCheckpoint);
  EXPECT_EQ(controls::hit(controls::CHECKPOINT_ANSWER, 240, 340)->item, Item::AnswerBox);
  EXPECT_EQ(controls::hit(controls::CHECKPOINT_ANSWER, 120, 620)->item, Item::Correct);
  EXPECT_EQ(controls::hit(controls::CHECKPOINT_ANSWER, 360, 620)->item, Item::Incorrect);
  EXPECT_EQ(controls::hit(controls::CHECKPOINT_INCORRECT, 240, 256)->item, Item::RereadRsvp);
  EXPECT_EQ(controls::hit(controls::CHECKPOINT_INCORRECT, 240, 348)->item, Item::RereadPage);
  EXPECT_EQ(controls::hit(controls::CHECKPOINT_INCORRECT, 240, 440)->item, Item::ContinueOn);
  // S4 section 6: a second tap where Reveal was lands in the answer box, never on a grade; a double
  // tap on a grade lands on nothing after Incorrect.
  EXPECT_EQ(controls::hit(controls::CHECKPOINT_ANSWER, 240, 476)->item, Item::AnswerBox);
  EXPECT_EQ(controls::hit(controls::CHECKPOINT_INCORRECT, 120, 620), nullptr);
  EXPECT_EQ(controls::hit(controls::CHECKPOINT_INCORRECT, 360, 620), nullptr);
}

TEST(CheckpointScreens, DefaultsAreTheSafeActions) {
  for (const ReadingMode mode : {ReadingMode::Rsvp, ReadingMode::Page}) {
    EXPECT_EQ(afterDowns(Screen::Prompt, mode, 0), Item::Reveal);
    EXPECT_EQ(afterDowns(Screen::Answer, mode, 0), Item::AnswerBox) << "never a grade";
  }
  // After Incorrect: the reread in the mode in use when the checkpoint appeared.
  EXPECT_EQ(afterDowns(Screen::AfterIncorrect, ReadingMode::Rsvp, 0), Item::RereadRsvp);
  EXPECT_EQ(afterDowns(Screen::AfterIncorrect, ReadingMode::Page, 0), Item::RereadPage);
}

TEST(CheckpointScreens, KeyPathsOfS4) {
  // Rows 17-19.
  EXPECT_EQ(afterDowns(Screen::Prompt, ReadingMode::Page, 1), Item::SkipPrompt);
  EXPECT_EQ(afterDowns(Screen::Prompt, ReadingMode::Page, 2), Item::SkipCheckpoint);
  EXPECT_EQ(afterDowns(Screen::Prompt, ReadingMode::Page, 3), Item::SkipCheckpoint) << "no wrap";
  EXPECT_EQ(afterUps(Screen::Prompt, ReadingMode::Page, 1), Item::Reveal) << "no wrap";
  // Rows 20-24.
  EXPECT_EQ(afterDowns(Screen::Answer, ReadingMode::Page, 1), Item::Correct);
  EXPECT_EQ(afterDowns(Screen::Answer, ReadingMode::Page, 2), Item::Incorrect);
  EXPECT_EQ(afterDowns(Screen::Answer, ReadingMode::Page, 3), Item::SkipPrompt);
  EXPECT_EQ(afterDowns(Screen::Answer, ReadingMode::Page, 4), Item::SkipCheckpoint);
  // Rows 25-29, from either default.
  EXPECT_EQ(afterUps(Screen::AfterIncorrect, ReadingMode::Page, 1), Item::RereadRsvp);
  EXPECT_EQ(afterDowns(Screen::AfterIncorrect, ReadingMode::Rsvp, 1), Item::RereadPage);
  EXPECT_EQ(afterDowns(Screen::AfterIncorrect, ReadingMode::Page, 1), Item::ContinueOn);
  EXPECT_EQ(afterDowns(Screen::AfterIncorrect, ReadingMode::Rsvp, 2), Item::ContinueOn);
  EXPECT_EQ(afterDowns(Screen::AfterIncorrect, ReadingMode::Page, 2), Item::SkipPrompt);
  EXPECT_EQ(afterDowns(Screen::AfterIncorrect, ReadingMode::Rsvp, 3), Item::SkipPrompt);
  EXPECT_EQ(afterDowns(Screen::AfterIncorrect, ReadingMode::Page, 3), Item::SkipCheckpoint);
  EXPECT_EQ(afterDowns(Screen::AfterIncorrect, ReadingMode::Rsvp, 4), Item::SkipCheckpoint);
}

TEST(CheckpointScreens, ItemsSendTheirFlowEvents) {
  const std::pair<Item, FlowEvent> pairs[] = {
      {Item::Reveal, FlowEvent::Reveal},
      {Item::SkipPrompt, FlowEvent::SkipPrompt},
      {Item::SkipCheckpoint, FlowEvent::SkipCheckpoint},
      {Item::Correct, FlowEvent::Correct},
      {Item::Incorrect, FlowEvent::Incorrect},
      {Item::RereadRsvp, FlowEvent::RereadRsvp},
      {Item::RereadPage, FlowEvent::RereadPage},
      {Item::ContinueOn, FlowEvent::Continue},
  };
  for (const auto& [item, expected] : pairs) {
    FlowEvent event = FlowEvent::ReturnEarly;
    ASSERT_TRUE(checkpointui::flowEventFor(item, event)) << controls::itemName(item);
    EXPECT_EQ(event, expected) << controls::itemName(item);
  }
  FlowEvent event = FlowEvent::Reveal;
  EXPECT_FALSE(checkpointui::flowEventFor(Item::AnswerBox, event)) << "the answer box pages";
  EXPECT_FALSE(checkpointui::flowEventFor(Item::Play, event)) << "a pause-screen item";
}

TEST(CheckpointScreens, EveryItemOnAScreenIsOfferedInItsStateAndNowhereElse) {
  for (const FlowState state : ALL_STATES) {
    const Screen screen = checkpointui::screenFor(state);
    for (const Screen other : {Screen::Prompt, Screen::Answer, Screen::AfterIncorrect}) {
      const controls::Screen& layout = checkpointui::layoutOf(other);
      for (int i = 0; i < layout.count; ++i) {
        const Item item = layout.zones[i].item;
        FlowEvent event = FlowEvent::Reveal;
        const bool accepted = item == Item::AnswerBox ? state == FlowState::AnswerShown
                                                      : checkpointui::flowEventFor(item, event) &&
                                                            checkpoint::CheckpointFlow::accepts(state, event);
        EXPECT_EQ(checkpointui::offered(item, state), accepted)
            << checkpoint::flowStateName(state) << " " << controls::itemName(item);
        // Everything a screen shows is accepted in the state it shows.
        if (other == screen) {
          EXPECT_TRUE(checkpointui::offered(item, state)) << controls::itemName(item);
        }
      }
    }
  }
}

TEST(CheckpointScreens, AnswerPartsAreWholeLines) {
  EXPECT_EQ(checkpointui::partCount(0, 8), 1);
  EXPECT_EQ(checkpointui::partCount(7, 8), 1);
  EXPECT_EQ(checkpointui::partCount(8, 8), 1);
  EXPECT_EQ(checkpointui::partCount(9, 8), 2);
  EXPECT_EQ(checkpointui::partCount(24, 8), 3);
  EXPECT_EQ(checkpointui::partCount(25, 8), 4);
  EXPECT_EQ(checkpointui::partCount(5, 0), 1) << "no room for a line: one part, never a division by zero";
  EXPECT_EQ(checkpointui::partFirstLine(0, 8), 0u);
  EXPECT_EQ(checkpointui::partFirstLine(2, 8), 16u);
  EXPECT_EQ(checkpointui::partLines(19, 0, 8), 8u);
  EXPECT_EQ(checkpointui::partLines(19, 1, 8), 8u);
  EXPECT_EQ(checkpointui::partLines(19, 2, 8), 3u) << "the last part holds the rest";
  EXPECT_EQ(checkpointui::partLines(19, 3, 8), 0u);
  // Every line is shown exactly once over the parts.
  for (uint32_t lines = 0; lines <= 40; ++lines) {
    for (uint16_t per = 1; per <= 10; ++per) {
      uint32_t shown = 0;
      const uint16_t parts = checkpointui::partCount(lines, per);
      for (uint16_t p = 0; p < parts; ++p) {
        EXPECT_EQ(checkpointui::partFirstLine(p, per), shown);
        shown += checkpointui::partLines(lines, p, per);
        if (lines > 0) {
          EXPECT_GT(checkpointui::partLines(lines, p, per), 0u) << "no empty part";
        }
      }
      EXPECT_EQ(shown, lines);
    }
  }
}

TEST(CheckpointScreens, PagingMoves) {
  // A tap or a hold cycles; a swipe up shows the next part and stops at the last; a swipe down the
  // previous and stops at the first (S4 row 20).
  EXPECT_EQ(checkpointui::movePart(0, 3, PageMove::Cycle), 1);
  EXPECT_EQ(checkpointui::movePart(1, 3, PageMove::Cycle), 2);
  EXPECT_EQ(checkpointui::movePart(2, 3, PageMove::Cycle), 0);
  EXPECT_EQ(checkpointui::movePart(0, 3, PageMove::Next), 1);
  EXPECT_EQ(checkpointui::movePart(2, 3, PageMove::Next), 2);
  EXPECT_EQ(checkpointui::movePart(2, 3, PageMove::Previous), 1);
  EXPECT_EQ(checkpointui::movePart(0, 3, PageMove::Previous), 0);
  // A one-part answer: a hold does nothing.
  for (const PageMove m : {PageMove::Cycle, PageMove::Next, PageMove::Previous}) {
    EXPECT_EQ(checkpointui::movePart(0, 1, m), 0);
    EXPECT_EQ(checkpointui::movePart(0, 0, m), 0);
  }
  EXPECT_EQ(checkpointui::movePart(7, 3, PageMove::Previous), 1) << "a part past the end counts as the last";
}

TEST(CheckpointScreens, HowACheckpointEnds) {
  checkpoint::Transition t;
  EXPECT_EQ(checkpointui::endFor(FlowEvent::SkipCheckpoint, t), End::None) << "refused: nothing ends";
  t.accepted = true;
  t.state = FlowState::PromptHidden;
  EXPECT_EQ(checkpointui::endFor(FlowEvent::Correct, t), End::None) << "the next prompt";
  t.state = FlowState::Finished;
  t.finished = true;
  EXPECT_EQ(checkpointui::endFor(FlowEvent::SkipCheckpoint, t), End::Skipped);
  EXPECT_EQ(checkpointui::endFor(FlowEvent::Correct, t), End::Finished);
  EXPECT_EQ(checkpointui::endFor(FlowEvent::SkipPrompt, t), End::Finished) << "skipping the last prompt";
  EXPECT_EQ(checkpointui::endFor(FlowEvent::Continue, t), End::Finished);
  t.finished = false;
  t.state = FlowState::Rereading;
  EXPECT_EQ(checkpointui::endFor(FlowEvent::RereadRsvp, t), End::RereadRsvp);
  EXPECT_EQ(checkpointui::endFor(FlowEvent::RereadPage, t), End::RereadPage);
  EXPECT_STREQ(checkpointui::endName(End::RereadPage), "reread-page");
  EXPECT_STREQ(checkpointui::endName(End::Skipped), "skipped");
}

// --- The vertical swipe of the gesture reader (src/controls/ControlGesture.h).

TEST(CheckpointSwipe, VerticalSwipesCarryTheirStart) {
  controls::GestureReader reader;
  controls::InputFrame f;
  f.nowMs = 1000;
  f.swipeUp = true;
  f.x = 240;
  f.y = 480;
  controls::Event e = reader.pollFocus(f);
  EXPECT_EQ(e.ev, controls::Ev::SwipeUp);
  EXPECT_EQ(e.x, 240);
  EXPECT_EQ(e.y, 480);
  EXPECT_EQ(controls::resolve(controls::Ctx::FocusScreen, e.ev), controls::Act::SwipeNext);

  f = controls::InputFrame{};
  f.nowMs = 1100;
  f.swipeDown = true;
  f.x = 240;
  f.y = 200;
  e = reader.pollFocus(f);
  EXPECT_EQ(e.ev, controls::Ev::SwipeDown);
  EXPECT_EQ(controls::resolve(controls::Ctx::FocusScreen, e.ev), controls::Act::SwipePrev);
  // Playback ignores them (any touch-down already paused).
  EXPECT_EQ(controls::resolve(controls::Ctx::RsvpPlaying, controls::Ev::SwipeUp), controls::Act::None);
}

TEST(CheckpointSwipe, ANewScreenIgnoresSwipesUntilArmed) {
  controls::GestureReader reader;
  reader.restartGuard(1000);
  controls::InputFrame f;
  f.nowMs = 1100;
  f.swipeUp = true;
  EXPECT_EQ(reader.pollFocus(f).ev, controls::Ev::None) << "within 300 ms";
  f = controls::InputFrame{};
  f.nowMs = 1300;
  EXPECT_EQ(reader.pollFocus(f).ev, controls::Ev::None) << "the frame that arms reports nothing";
  EXPECT_TRUE(reader.armed());
  f.swipeUp = true;
  f.nowMs = 1310;
  EXPECT_EQ(reader.pollFocus(f).ev, controls::Ev::SwipeUp);
}
