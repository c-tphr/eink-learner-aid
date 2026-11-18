#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include "SeriesRunner.h"

namespace devicetest {

namespace {

SeriesFigures seriesFrom(const displayprofile::RefreshStats& s) {
  SeriesFigures f;
  f.count = s.count;
  f.medianMs = s.medianMs();
  f.p95Ms = s.percentileMs(95);
  f.maxMs = s.maxMs;
  f.minMs = s.minMs;
  f.meanMs = s.meanMs();
  f.timeouts = s.timeouts;
  return f;
}

}  // namespace

SeriesRunner::SeriesRunner(displayprofile::DisplayBackend& display, rsvp::Clock& time, SeriesPainter& screen,
                           const rsvp::FitsFn fits, void* fitsContext)
    : backend(display),
      clock(time),
      painter(screen),
      sink(display, band),
      scheduler(source, time, sink, fits, fitsContext) {}

void SeriesRunner::BandAdapter::paintFrame(const rsvp::Frame& frame) {
  ++owner.framesPainted;
  owner.painter.paintBand(&frame, owner.framesPainted);
}

void SeriesRunner::BandAdapter::paintClearedBand() { owner.painter.paintBand(nullptr, owner.framesPainted); }

void SeriesRunner::begin(const StepSpec& step) {
  spec = &step;
  stopRequested.store(false);
  finished = false;
  wasStopped = false;
  operations = 0;
  framesPainted = 0;

  painter.paintStepScreen(step);
  backend.clear();
  backend.resetStats();
  promotionsAtStart = backend.exposure().promotions();
  startMs = clock.nowMs();
  endMs = startMs;

  if (usesScheduler(step.kind)) {
    source.reset(step.count);
    scheduler.reposition();
    scheduler.resetSession();
    scheduler.setWpm(step.wpm);
    scheduler.setWordsPerFrame(step.wordsPerUpdate);
    scheduler.setClearingLimits(backend.info().clearing);
    scheduler.noteClearingRefresh();
    scheduler.play();
  }
}

void SeriesRunner::requestStop() {
  stopRequested.store(true);
  scheduler.requestPause();  // atomic; ignored unless playing
}

SeriesRunner::Tick SeriesRunner::tick() {
  Tick t;
  if (finished) {
    t.done = true;
    return t;
  }

  if (usesScheduler(spec->kind)) {
    if (stopRequested.load()) scheduler.requestPause();
    const rsvp::StepResult r = scheduler.step();
    switch (r.event) {
      case rsvp::StepEvent::SHOWED_FRAME:
        ++operations;
        endMs = clock.nowMs();
        break;
      case rsvp::StepEvent::CLEARED:
        endMs = clock.nowMs();
        break;
      case rsvp::StepEvent::PAUSED:
        finish(true);
        t.done = true;
        return t;
      case rsvp::StepEvent::STOPPED:
        finish(false);
        t.done = true;
        return t;
      case rsvp::StepEvent::NONE:
        break;
    }
    t.wake = r.wake;
    t.wakeAtMs = r.wakeAtMs;
    return t;
  }

  if (stopRequested.load()) {
    finish(true);
    t.done = true;
    return t;
  }
  painter.paintPage(operations);
  if (spec->kind == StepKind::FULL_SCREEN) {
    backend.update(displayprofile::UpdateKind::WORD);
  } else {
    backend.clear();
  }
  ++operations;
  endMs = clock.nowMs();
  if (operations >= spec->count) finish(false);
  t.done = finished;
  return t;
}

void SeriesRunner::finish(const bool byStop) {
  // Snapshot first: anything drawn after the series (a prompt) adds to the backend's statistics.
  snapshot = live();
  finished = true;
  wasStopped = byStop;
}

StepFigures SeriesRunner::figures() const { return finished ? snapshot : live(); }

StepFigures SeriesRunner::live() const {
  StepFigures f;
  f.updates = seriesFrom(backend.stats().updates);
  f.clears = seriesFrom(backend.stats().clears);
  f.promotions = backend.exposure().promotions() - promotionsAtStart;
  f.durationMs = endMs - startMs;
  if (usesScheduler(spec->kind)) {
    f.hasPlayback = true;
    const rsvp::MeterStats& m = scheduler.meter().stats();
    const rsvp::WordOrderAudit& a = scheduler.audit();
    PlaybackFigures& p = f.playback;
    p.deliveredWpmTenths = scheduler.meter().deliveredWpmTenths();
    p.wordsShown = m.wordsShown;
    p.frames = m.updates;
    p.clearingRefreshes = m.clearingRefreshes;
    p.activeMs = m.activeMs;
    p.inOrder = a.inOrder();
    p.wordsDrawn = a.wordsDrawn();
    p.segments = a.segments();
    p.violations = a.violations();
    p.gaps = a.gaps();
    p.repeats = a.repeats();
  }
  return f;
}

}  // namespace devicetest

#endif  // CROSSPOINT_RSVP_ENABLED
