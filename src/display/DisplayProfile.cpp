#include "DisplayProfile.h"

#if CROSSPOINT_RSVP_ENABLED

#include <Arduino.h>
#include <Logging.h>

#include <cstdio>

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

namespace {

// What the last call ran, appended to the per-call line only when it differs from what was asked
// (a request run as a clearing refresh, or a word update that ran another profile's waveform than
// the selected one). Stock-only runs print exactly the lines of tasks E3 and D1.
void appendDecision(const DisplayBackend& backend, char* out, const size_t size) {
  out[0] = '\0';
  const RefreshDecision& d = backend.lastDecision();
  if (d.promotion != Promotion::NONE) {
    snprintf(out, size, " for=%s promoted=%s", d.request == RefreshRequest::WORD ? "word" : "screen",
             promotionName(d.promotion));
  } else if (d.action != RefreshAction::CLEAR && d.waveform != backend.profile()) {
    snprintf(out, size, " ran=%s", profileInfo(d.waveform).key);
  }
}

}  // namespace

// The count printed is the call's own number: stats are updated after present*() returns.
void logRefresh(const DisplayBackend& backend, const RefreshKind kind, const uint32_t ms) {
  char extra[48];
  appendDecision(backend, extra, sizeof(extra));
  LOG_DBG("DSP", "%s n=%lu profile=%s ms=%lu%s", kindName(kind), static_cast<unsigned long>(countOf(backend, kind) + 1),
          backend.info().key, static_cast<unsigned long>(ms), extra);
}

void logModelledRefresh(const DisplayBackend& backend, const ModelledCall& call) {
  char extra[48];
  appendDecision(backend, extra, sizeof(extra));
  LOG_DBG("DSP", "%s n=%lu profile=%s ms=%lu model=%lu real=%lu slept=%lu%s", kindName(call.kind),
          static_cast<unsigned long>(countOf(backend, call.kind) + 1), backend.info().key,
          static_cast<unsigned long>(call.totalMs), static_cast<unsigned long>(call.modelMs),
          static_cast<unsigned long>(call.realMs), static_cast<unsigned long>(call.sleptMs), extra);
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
