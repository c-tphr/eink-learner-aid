#pragma once

// Runs one benchmark step (BenchPlan.h, STEPS) on the selected display profile, one display call at a
// time, and reports what it measured. Portable: the caller supplies the backend, the clock and a
// painter; DeviceTestActivity calls tick() on its render task, and test/device_test drives it with
// displayprofile::FakeDisplayBackend and a fake clock through every step of every profile.
//
// - begin(): paints the step screen, runs the clearing refresh the step starts from (so a trial run
//   starts from a clear, exposure rule 2, and the driver's own promotions are absorbed), then resets
//   the backend's statistics: the figures cover exactly this series.
// - Word band, playback and endurance run through rsvp::Scheduler over the passage
//   (PassageSource) with the profile's clearing limits, so clearing refreshes run when the
//   profile's policy says (counted under `clears`, never under `updates`). The word band plays the
//   passage's first 100 words at WORD_BAND_WPM: updates follow each other after the 60 ms dwell.
// - Full screen: a whole page of the passage, alternating between two pages, presented as a word
//   update (UpdateKind::WORD, so a trial runs its own waveform); clearing: the same pages, each with
//   a clearing refresh.
// - requestStop() (safe from any task) ends the series before its next display call.

#include <Clock.h>
#include <DisplayBackend.h>
#include <ProfileDisplaySink.h>
#include <Scheduler.h>

#include <atomic>
#include <cstdint>

#include "BenchPlan.h"
#include "Passage.h"

namespace devicetest {

// What the activity draws; it only draws (the runner refreshes).
class SeriesPainter {
 public:
  virtual ~SeriesPainter() = default;
  // The whole screen a step starts from (headings, the band's guides). Shown by a clearing refresh.
  virtual void paintStepScreen(const StepSpec& step) = 0;
  // The word band with `frame` (nullptr: empty, guides kept); `shown` frames so far, this one included.
  virtual void paintBand(const rsvp::Frame* frame, uint32_t shown) = 0;
  // Full-screen and clearing series: page `n` (0-based), alternating between two pages of text.
  virtual void paintPage(uint32_t n) = 0;
};

class SeriesRunner {
 public:
  SeriesRunner(displayprofile::DisplayBackend& backend, rsvp::Clock& clock, SeriesPainter& painter,
               rsvp::FitsFn fits = nullptr, void* fitsContext = nullptr);

  void begin(const StepSpec& step);

  struct Tick {
    bool done = false;  // the series has ended (complete or stopped); figures() is final
    bool wake = false;  // call tick() again at wakeAtMs (clock time); otherwise at once
    uint32_t wakeAtMs = 0;
  };
  // At most one display call.
  Tick tick();

  void requestStop();
  bool done() const { return finished; }
  bool stopped() const { return finished && wasStopped; }
  // Display operations of the series so far: frames, full-screen updates or clearing refreshes.
  uint32_t progress() const { return operations; }
  const StepSpec& step() const { return *spec; }
  // What the series measured (so far, until done()).
  StepFigures figures() const;
  const rsvp::Scheduler& playback() const { return scheduler; }

 private:
  class BandAdapter final : public displayprofile::FramePainter {
   public:
    explicit BandAdapter(SeriesRunner& runner) : owner(runner) {}
    void paintFrame(const rsvp::Frame& frame) override;
    void paintClearedBand() override;

   private:
    SeriesRunner& owner;
  };

  void finish(bool byStop);
  StepFigures live() const;

  displayprofile::DisplayBackend& backend;
  rsvp::Clock& clock;
  SeriesPainter& painter;
  PassageSource source;
  BandAdapter band{*this};
  displayprofile::ProfileDisplaySink sink;
  rsvp::Scheduler scheduler;

  const StepSpec* spec = &STEPS[0];
  std::atomic<bool> stopRequested{false};
  bool finished = true;
  bool wasStopped = false;
  uint32_t operations = 0;
  uint32_t framesPainted = 0;
  uint32_t promotionsAtStart = 0;
  uint32_t startMs = 0;
  uint32_t endMs = 0;
  StepFigures snapshot;
};

}  // namespace devicetest
