#pragma once

// Results records built the way the activity builds them, for the encoder tests and the schema
// check: the series run through SeriesRunner on the fake display backend, the owner's answers through
// Sweep.

#include <FakeDisplayBackend.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "activities/devicetest/BenchJson.h"
#include "activities/devicetest/SeriesRunner.h"

namespace devicetest_samples {

using namespace devicetest;

class NullPainter final : public SeriesPainter {
 public:
  void paintStepScreen(const StepSpec&) override {}
  void paintBand(const rsvp::Frame*, uint32_t) override {}
  void paintPage(uint32_t) override {}
};

class StringSink final : public JsonSink {
 public:
  void write(const char* data, const size_t length) override { text.append(data, length); }
  std::string text;
};

inline void fillHeader(RecordHeader& h, const uint16_t run, const ResultKind kind, const PanelId panel) {
  h.run = run;
  resultFileName(run, kind, h.file, sizeof(h.file));
  snprintf(h.firmware.version, sizeof(h.firmware.version), "%s", "2.0.0-dev-picco2");
  snprintf(h.firmware.elfSha256, sizeof(h.firmware.elfSha256), "%s",
           "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
  snprintf(h.device.board, sizeof(h.device.board), "%s", "BOOX Picco");
  h.device.panel = panel;
  h.device.panelVariant = static_cast<uint8_t>(panel == PanelId::SIMULATED ? 0 : static_cast<uint8_t>(panel));
  snprintf(h.device.driver, sizeof(h.device.driver), "%s",
           displayprofile::controllerName(displayprofile::panelRecord(panel).controller));
  h.device.simulated = panel == PanelId::SIMULATED;
  h.conditions.batteryPercent = 87;
  h.conditions.usbConnected = true;
  h.conditions.uptimeMs = 123456;
}

// Runs the current step of `sweep` through a runner, as the activity does, and hands the result on.
inline Next runCurrentStep(Sweep& sweep, SeriesRunner& runner, displayprofile::FakeClock& clock,
                           const int stopAfter = -1) {
  runner.begin(sweep.stepSpec());
  while (!runner.done()) {
    if (stopAfter >= 0 && runner.progress() >= static_cast<uint32_t>(stopAfter)) runner.requestStop();
    const SeriesRunner::Tick t = runner.tick();
    if (t.wake) clock.advanceTo(t.wakeAtMs);
  }
  if (runner.stopped()) {
    sweep.seriesStopped(runner.figures());
    return Next::RUN_STEP;  // the caller answers the stop prompt
  }
  return sweep.seriesDone(runner.figures());
}

// A benchmark on the simulator's panel: Stock complete (one step repeated, a mix of ratings), Trial A
// skipped part-way after an Unreadable playback step, Trial B stopped during its word band and the
// benchmark ended there.
inline std::string skippedRunJson() {
  displayprofile::FakeClock clock(1000);
  displayprofile::FakeDisplayBackend backend(clock);
  NullPainter painter;
  SeriesRunner runner(backend, clock, painter);
  Sweep sweep;
  const displayprofile::TrialMask runnable = sweep.plan(PanelId::SIMULATED, false);
  BenchmarkRecord record;
  fillHeader(record.header, 1, ResultKind::BENCHMARK, PanelId::SIMULATED);
  record.header.conditions.hasTemperature = true;
  record.header.conditions.temperatureKnown = true;
  record.header.conditions.temperatureC = 21;
  record.header.simulatorTimeScale = 50;
  record.trialSettingBefore = 0;
  record.trialSettingAfter = displayprofile::encodeTrialSetting(unlockForBenchmark({}, runnable));
  backend.setUnlockedTrials(runnable);

  sweep.start();
  backend.select(sweep.profile().id);
  int rated = 0;
  while (sweep.active()) {
    const ProfileId id = sweep.profile().id;
    if (id == ProfileId::TRIAL_B) {
      runCurrentStep(sweep, runner, clock, 30);
      sweep.stopped(StopChoice::END_RUN);
      break;
    }
    Next n = runCurrentStep(sweep, runner, clock);
    if (n == Next::ASK_RATING) {
      const bool skip = id == ProfileId::TRIAL_A && sweep.stepIndex() == 4;
      const bool repeat = id == ProfileId::STOCK && sweep.stepIndex() == 5 && sweep.stepRecord().attempts == 1;
      const RatingChoice choice = skip     ? RatingChoice::UNREADABLE_SKIP
                                  : repeat ? RatingChoice::REPEAT
                                           : (rated++ % 3 == 0 ? RatingChoice::TOLERABLE : RatingChoice::CLEAN);
      n = sweep.rate(choice);
      if (n == Next::ASK_RESIDUE) n = sweep.residue(ResidueChoice::NO);
    }
    if (n == Next::PROFILE_SUMMARY) {
      n = sweep.summary(SummaryChoice::NEXT);
      if (n == Next::RUN_STEP) backend.select(sweep.profile().id);
    }
  }
  StringSink sink;
  writeBenchmark(record, sweep, sink);
  return sink.text;
}

// A complete benchmark on SE0400NQW47 (Stock and Trial D), temperature unknown, on the device.
inline std::string se0400RunJson() {
  displayprofile::FakeClock clock(1000);
  displayprofile::FakeDisplayBackend backend(clock, displayprofile::TimingModel(), PanelId::SE0400NQW47);
  NullPainter painter;
  SeriesRunner runner(backend, clock, painter);
  Sweep sweep;
  const displayprofile::TrialMask runnable = sweep.plan(PanelId::SE0400NQW47, false);
  BenchmarkRecord record;
  fillHeader(record.header, 12, ResultKind::BENCHMARK, PanelId::SE0400NQW47);
  record.header.conditions.hasTemperature = true;
  record.header.conditions.temperatureKnown = false;
  snprintf(record.header.conditions.clock, sizeof(record.header.conditions.clock), "%s", "2026-12-01T14:05:00");
  record.trialSettingAfter = displayprofile::encodeTrialSetting(unlockForBenchmark({}, runnable));
  backend.setUnlockedTrials(runnable);
  sweep.start();
  backend.select(sweep.profile().id);
  while (sweep.active()) {
    Next n = runCurrentStep(sweep, runner, clock);
    if (n == Next::ASK_RATING) n = sweep.rate(RatingChoice::CLEAN);
    if (n == Next::ASK_RESIDUE) n = sweep.residue(ResidueChoice::YES);
    if (n == Next::PROFILE_SUMMARY) {
      n = sweep.summary(SummaryChoice::NEXT);
      if (n == Next::RUN_STEP) backend.select(sweep.profile().id);
    }
  }
  StringSink sink;
  writeBenchmark(record, sweep, sink);
  return sink.text;
}

// A Controls check: Up short, Down held, a power click, a tap, a swipe and the Back gesture, then a
// 3 s hold to leave.
inline std::string controlsJson() {
  ControlsRecord record;
  fillHeader(record.header, 2, ResultKind::CONTROLS, PanelId::SIMULATED);
  record.header.simulatorTimeScale = 0;
  record.status = RunStatus::COMPLETE;
  ControlEvent e;
  e.atMs = 1000;
  e.raw = RAW_UP;
  e.logical = logicalBit(LOGICAL_UP) | logicalBit(LOGICAL_PAGE_BACK) | logicalBit(10);
  e.role = Role::PREV;
  e.heldMs = 110;
  record.events[record.count++] = e;
  e.atMs = 3000;
  e.raw = RAW_DOWN;
  e.kind = EventKind::HOLD;
  e.logical = logicalBit(LOGICAL_DOWN) | logicalBit(LOGICAL_PAGE_FORWARD) | logicalBit(9);
  e.role = Role::NEXT;
  e.heldMs = 950;
  record.events[record.count++] = e;
  e = ControlEvent{};
  e.atMs = 4000;
  e.raw = RAW_POWER;
  e.logical = logicalBit(6);
  e.heldMs = 80;
  record.events[record.count++] = e;
  e = ControlEvent{};
  e.source = EventSource::TOUCH;
  e.kind = EventKind::TAP;
  e.atMs = 5000;
  e.x = 240;
  e.y = 400;
  record.events[record.count++] = e;
  e.kind = EventKind::SWIPE_LEFT;
  e.x2 = 60;
  e.y2 = 404;
  record.events[record.count++] = e;
  e = ControlEvent{};
  e.source = EventSource::TOUCH;
  e.kind = EventKind::BACK_GESTURE;
  e.logical = logicalBit(0);
  record.events[record.count++] = e;
  e = ControlEvent{};
  e.kind = EventKind::EXIT_HOLD;
  e.raw = RAW_DOWN;
  e.logical = logicalBit(LOGICAL_DOWN);
  e.role = Role::NEXT;
  e.heldMs = EXIT_HOLD_MS;
  record.events[record.count++] = e;
  StringSink sink;
  writeControls(record, sink);
  return sink.text;
}

}  // namespace devicetest_samples
