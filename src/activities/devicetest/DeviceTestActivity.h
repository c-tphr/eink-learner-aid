#pragma once

// The Device test menu, the Controls check and the display benchmark (task D3a; docs/PLAN.md section
// 10, steps 7 and 9; docs/spikes/S4-control-map.md rows 34 and section 7). Opened from Settings >
// Display > Device test (DeviceTestLaunch.h). README.md in this directory describes the screens, the
// sweep, the log lines and the results files.
//
// Threads: as in RsvpActivity. The render task draws and makes every display call (one series step
// per render(), SeriesRunner::tick()); the main task reads input in loop() and changes state under the
// RenderLock. Every refresh goes through the display backend (src/display/DisplayProfile.h).

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <Clock.h>

#include <atomic>
#include <cstdint>

#include "BenchJson.h"
#include "BenchPlan.h"
#include "ChoiceList.h"
#include "ControlsCheck.h"
#include "SeriesRunner.h"
#include "activities/Activity.h"
#include "activities/rsvp/WordBand.h"
#include "controls/ControlInput.h"
#include "display/DisplayProfile.h"

class DeviceTestActivity final : public Activity, private devicetest::SeriesPainter {
 public:
  DeviceTestActivity(GfxRenderer& renderer, MappedInputManager& input);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

  // A series must not be cut by auto-sleep, nor the last frame of a rated step before the owner looks.
  bool preventAutoSleep() override;
  bool handleHomeGesture() override;
  bool handleForcedRefresh() override;
  void prepareForSleep() override;

 private:
  enum class Screen : uint8_t {
    Menu,
    Controls,
    Temperature,
    Overview,
    Step,  // a series runs
    Look,  // a rated series has ended; the last frame stays until a key or touch
    Rating,
    Residue,
    Stopped,
    ProfileSummary,
    RunSummary,
  };

  // The menu's rows, in order. Task D3b adds "Guided acceptance" and "Choose a profile" before Leave:
  // one entry here, one label, one case in chooseMenu().
  enum class MenuRow : uint8_t { ControlsCheck, Benchmark, ClearingRefresh, Leave };

  // rsvp::Clock for the series; in the simulator CROSSPOINT_SIM_BENCH_TIME_SCALE speeds it up so the
  // pacing of a scenario's playback steps takes a fraction of the real time (README.md).
  class BenchClock final : public rsvp::Clock {
   public:
    void start(uint32_t scale);
    uint32_t nowMs() override;
    uint32_t scale() const { return factor; }

   private:
    uint32_t factor = 1;
    uint32_t origin = 0;
  };

  // --- SeriesPainter (render task) ---
  void paintStepScreen(const devicetest::StepSpec& step) override;
  void paintBand(const rsvp::Frame* frame, uint32_t shown) override;
  void paintPage(uint32_t n) override;

  // --- Render task ---
  void drawScreen(bool clearing);
  void paintList(const char* title, const char* subtitle, const char* const* labels, uint8_t lineCount);
  void paintControls();
  // Draws `text` from `y`, wrapped to the screen width, not below `bottom`; returns the next line's y.
  int drawWrapped(const char* text, int y, int bottom);
  void runClearingRefresh();
  void stepSeries();
  static int measure(void* context, uint8_t font, std::string_view text);
  static bool fits(void* context, std::string_view text, uint16_t focusOffset);

  // --- Main task ---
  void loopList();
  void loopControls();
  void loopStep();
  void loopLook();
  void choose(uint8_t row);
  void back();
  void show(Screen next, uint8_t rows, uint8_t repeatMask, bool clearing);
  void recordEvent(const devicetest::ControlEvent& event);
  void finishControls(devicetest::RunStatus status);
  void seriesEnded();

  // --- Either task, holding the RenderLock ---
  void startControls();
  void planBenchmark();
  void startBenchmark();
  void go(devicetest::Next next);
  void endBenchmark();
  bool allocateRun(devicetest::ResultKind kind, devicetest::RecordHeader& header);
  void fillHeader(devicetest::RecordHeader& header);
  bool saveBenchmark();
  bool saveControls();
  void logResult(const devicetest::StepFigures& f, bool stopped);
  void restoreProfile();
  const char* screenName() const;

  displayprofile::DisplayBackend& backend;
  BenchClock clock;
  devicetest::SeriesRunner runner;
  controls::ControlInput controlInput;
  rsvpui::BandGeometry band;
  GfxRenderer::Orientation previousOrientation = GfxRenderer::Orientation::Portrait;

  // Shared between the tasks: written by the main task under the RenderLock.
  std::atomic<Screen> screen{Screen::Menu};
  devicetest::ChoiceList list;
  bool needClear = true;
  bool pendingClearing = false;  // the menu's Clearing refresh row
  uint32_t lastClearingMs = 0;
  bool stepPending = false;
  std::atomic<bool> seriesActive{false};
  std::atomic<bool> seriesDone{false};
  std::atomic<bool> wakeValid{false};
  std::atomic<uint32_t> wakeAtMs{0};
  std::atomic<uint32_t> lastLoopMs{0};
  std::atomic<bool> guardRestartPending{false};

  // The benchmark.
  bool temperatureKnown = false;
  int8_t temperatureC = 20;
  devicetest::Sweep sweep;
  devicetest::BenchmarkRecord bench;
  bool benchSaved = true;  // the last write of the results file succeeded

  // The Controls check.
  bool controlsActive = false;
  devicetest::KeyTracker keys;
  devicetest::ControlsRecord controlsRecord;
  bool controlsDirty = false;
  bool controlsSaved = true;
  uint32_t lastEventMs = 0;

  // Render task only: the text of the list screen being drawn.
  static constexpr int INFO_LINES = 14;
  static constexpr int LINE_BYTES = 112;
  char labelText[controls::CHOICE_ROWS_MAX][LINE_BYTES] = {};
  char info[INFO_LINES][LINE_BYTES] = {};
};

#endif  // CROSSPOINT_RSVP_ENABLED
