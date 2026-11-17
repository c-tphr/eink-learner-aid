#include "DisplayProfile.h"

#if CROSSPOINT_RSVP_ENABLED

#include <Arduino.h>
#include <Logging.h>

#if defined(SIMULATOR)
#include "SimulatorDisplayBackend.h"
#else
#include "DeviceDisplayBackend.h"
#endif

namespace displayprofile {

namespace {

const char* kindName(const RefreshKind kind) { return kind == RefreshKind::UPDATE ? "update" : "clear"; }

uint32_t countOf(const DisplayBackend& backend, const RefreshKind kind) {
  return kind == RefreshKind::UPDATE ? backend.stats().updates.count : backend.stats().clears.count;
}

}  // namespace

uint32_t MillisClock::nowMs() { return static_cast<uint32_t>(millis()); }

DisplayBackend& firmwareBackend(GfxRenderer& renderer) {
#if defined(SIMULATOR)
  static SimulatorDisplayBackend backend(renderer);
#else
  static DeviceDisplayBackend backend(renderer);
#endif
  return backend;
}

// The count printed is the call's own number: stats are updated after present*() returns.
void logRefresh(const DisplayBackend& backend, const RefreshKind kind, const uint32_t ms) {
  LOG_DBG("DSP", "%s n=%lu profile=%s ms=%lu", kindName(kind), static_cast<unsigned long>(countOf(backend, kind) + 1),
          backend.info().key, static_cast<unsigned long>(ms));
}

void logModelledRefresh(const DisplayBackend& backend, const ModelledCall& call) {
  LOG_DBG("DSP", "%s n=%lu profile=%s ms=%lu model=%lu real=%lu slept=%lu", kindName(call.kind),
          static_cast<unsigned long>(countOf(backend, call.kind) + 1), backend.info().key,
          static_cast<unsigned long>(call.totalMs), static_cast<unsigned long>(call.modelMs),
          static_cast<unsigned long>(call.realMs), static_cast<unsigned long>(call.sleptMs));
}

namespace {

// Link-check parts: a backend whose "refresh" does nothing and whose clock moves 7 ms per read.
class NullPresenter final : public Presenter {
 public:
  void present(RefreshKind) override {}
};
class StepClock final : public rsvp::Clock {
 public:
  uint32_t nowMs() override { return now += 7; }

 private:
  uint32_t now = 0;
};
class NullSleeper final : public Sleeper {
 public:
  void sleepMs(uint32_t) override {}
};
class NullPainter final : public FramePainter {
 public:
  void paintFrame(const rsvp::Frame&) override {}
  void paintClearedBand() override {}
};

}  // namespace

void runDisplayProfileLinkCheck() {
  NullPresenter presenter;
  StepClock clock;
  NullSleeper sleeper;
  NullPainter painter;
  ModelledBackend backend(presenter, clock, sleeper, TimingModel());
  ProfileDisplaySink sink(backend, painter);
  const bool selected = backend.select(ProfileId::TRIAL_C);
  sink.showFrame(rsvp::Frame{});
  sink.clearingRefresh();
  DisplayBackend& (*volatile build)(GfxRenderer&) = &firmwareBackend;
  LOG_DBG("DSP", "Link check: %u profiles, %s selected %d, updates %lu (%lu ms), clears %lu, build backend %s",
          static_cast<unsigned>(PROFILE_COUNT), backend.info().key, selected ? 1 : 0,
          static_cast<unsigned long>(backend.stats().updates.count),
          static_cast<unsigned long>(backend.stats().updates.lastMs),
          static_cast<unsigned long>(backend.stats().clears.count), build != nullptr ? "linked" : "missing");
}

void logStats(const DisplayBackend& backend) {
  const RefreshStats& u = backend.stats().updates;
  const RefreshStats& c = backend.stats().clears;
  // Fields after the first "last=" group were added by D1; scenarios match the line's start, so
  // new fields only ever go at the end.
  LOG_DBG("DSP",
          "stats profile=%s panel=%s updates=%lu min=%lu max=%lu mean=%lu last=%lu clears=%lu min=%lu max=%lu "
          "mean=%lu last=%lu update_median=%lu update_p95=%lu update_timeouts=%lu clear_median=%lu clear_p95=%lu "
          "clear_timeouts=%lu",
          backend.info().key, panelName(backend.panel()), static_cast<unsigned long>(u.count),
          static_cast<unsigned long>(u.minMs), static_cast<unsigned long>(u.maxMs),
          static_cast<unsigned long>(u.meanMs()), static_cast<unsigned long>(u.lastMs),
          static_cast<unsigned long>(c.count), static_cast<unsigned long>(c.minMs), static_cast<unsigned long>(c.maxMs),
          static_cast<unsigned long>(c.meanMs()), static_cast<unsigned long>(c.lastMs),
          static_cast<unsigned long>(u.medianMs()), static_cast<unsigned long>(u.percentileMs(95)),
          static_cast<unsigned long>(u.timeouts), static_cast<unsigned long>(c.medianMs()),
          static_cast<unsigned long>(c.percentileMs(95)), static_cast<unsigned long>(c.timeouts));
}

}  // namespace displayprofile

#endif  // CROSSPOINT_RSVP_ENABLED
