#pragma once

// The checkpoint screens (docs/PLAN.md section 7, "Per-prompt flow"; layouts and controls:
// docs/spikes/S4-control-map.md rows 17-29): the prompt with its answer hidden, the revealed answer
// with the grades, and the choice after Incorrect. Driven by checkpoint::CheckpointFlow; README.md in
// this directory has the screens, the log lines and the memory. Created through CheckpointLaunch.h.

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <BookState.h>
#include <CheckpointFlow.h>
#include <CheckpointLimits.h>

#include <atomic>
#include <memory>
#include <string>

#include "CheckpointLaunch.h"
#include "CheckpointScreens.h"
#include "activities/Activity.h"
#include "controls/ControlInput.h"
#include "display/DisplayProfile.h"
#include "reading/BookStateFile.h"

namespace reading {
class CheckpointBook;
}

class CheckpointActivity final : public Activity {
 public:
  CheckpointActivity(GfxRenderer& renderer, MappedInputManager& input, std::string bookPath,
                     std::shared_ptr<reading::CheckpointBook> book, const checkpointentry::Request& request,
                     std::shared_ptr<checkpointentry::Outcome> outcome);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool handleForcedRefresh() override;

 private:
  // --- Main task. The flow, the index and the screen fields are shared with the render task: every
  // change takes the RenderLock.
  bool start();
  void activate(controls::Item item);
  void apply(checkpoint::FlowEvent event);
  void page(checkpointui::PageMove move, const char* how);
  // `place`, when given, is also written as the reading position.
  void persist(const checkpoint::Transition& t, const char* promptId, const char* reason,
               const checkpoint::Place* place = nullptr);
  reading::BookIdentity identity() const;
  // `prompt`: the prompt the ending event was given on.
  void end(checkpointui::End how, const checkpoint::Transition& t, uint16_t prompt);
  // The screen for the flow's state, with the prompt's text loaded when the prompt changed.
  void enterScreen();
  static const char* gradeTime(char* out);

  // --- Render task.
  void paint();
  void paintPrompt();
  void paintAnswer();
  void paintAfterIncorrect();
  void paintButton(int index);
  void paintHeader(bool capital, const char* suffix);
  int paintWrapped(int fontId, const char* text, int y, int maxLines);

  const std::string bookPath;
  std::shared_ptr<reading::CheckpointBook> book;
  const checkpointentry::Request request;
  std::shared_ptr<checkpointentry::Outcome> outcome;

  displayprofile::DisplayBackend& backend;
  controls::ControlInput controlInput;
  // The flow and the state it changes, shared with reading::ReadingSession (CheckpointLaunch.h).
  std::shared_ptr<checkpointentry::Progress> progress;

  std::atomic<bool> guardRestartPending{false};
  bool startFailed = false;
  bool ended = false;

  // Screen fields (RenderLock).
  checkpointui::Screen screen = checkpointui::Screen::None;
  int focus = 0;
  bool needClear = true;
  uint16_t loadedPrompt = checkpoint::NO_INDEX;
  checkpoint::Grade recorded = checkpoint::Grade::Ungraded;  // the prompt's stored first grade
  uint16_t answerPart = 0;
  uint16_t answerParts = 1;  // set by paintAnswer()
  uint32_t answerLines = 0;  // set by paintAnswer()
  char promptId[checkpoint::ID_BUFFER_BYTES] = {};
  char promptText[checkpoint::PROMPT_BUFFER_BYTES] = {};
  char answerText[checkpoint::ANSWER_BUFFER_BYTES] = {};
};

#endif  // CROSSPOINT_RSVP_ENABLED
