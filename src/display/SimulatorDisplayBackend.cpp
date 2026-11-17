#include "SimulatorDisplayBackend.h"

#if CROSSPOINT_RSVP_ENABLED && defined(SIMULATOR)

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>

#include <cstdlib>

// Written by scripts/gen_display_timings.py (a pre-script of [env:simulator_picco]) into the build
// directory: the validated docs/device/picco-timings.json, or "no file".
#include "DisplayTimingsFile.generated.h"

namespace displayprofile {

namespace sim_detail {

void Parts::RendererPresenter::present(const RefreshKind kind) {
  renderer.displayBuffer(kind == RefreshKind::UPDATE ? HalDisplay::FAST_REFRESH : HalDisplay::HALF_REFRESH);
  // The frame is on screen from here until the modelled time has passed: scenarios shoot after this.
  const unsigned long count = kind == RefreshKind::UPDATE ? ++updates : ++clears;
  LOG_DBG("DSP", "presented %s n=%lu", kind == RefreshKind::UPDATE ? "update" : "clear", count);
}

bool Parts::RendererPresenter::inverted() const { return display.isInverted(); }

void Parts::DelaySleeper::sleepMs(const uint32_t ms) { delay(ms); }

}  // namespace sim_detail

TimingModel loadSimulatorTimingModel() {
  const TimingFile& file = generated::TIMINGS_FILE;
  const FileError fileError = checkTimingFile(file);
  if (fileError != FileError::NONE) {
    LOG_ERR("DSP", "Timings file %s refused: %s", generated::TIMINGS_PATH, fileErrorText(fileError));
    std::abort();
  }

  const char* updateText = std::getenv("CROSSPOINT_SIM_UPDATE_MS");
  const char* clearText = std::getenv("CROSSPOINT_SIM_CLEAR_MS");
  TimingOverrides overrides;
  switch (parseOverrides(updateText, clearText, overrides)) {
    case OverrideError::BAD_UPDATE:
      LOG_ERR("DSP", "CROSSPOINT_SIM_UPDATE_MS='%s' is not a whole number of ms from 0 to %lu", updateText,
              static_cast<unsigned long>(MAX_OVERRIDE_MS));
      std::abort();
    case OverrideError::BAD_CLEAR:
      LOG_ERR("DSP", "CROSSPOINT_SIM_CLEAR_MS='%s' is not a whole number of ms from 0 to %lu", clearText,
              static_cast<unsigned long>(MAX_OVERRIDE_MS));
      std::abort();
    case OverrideError::NONE:
      break;
  }

  TimingModel model;
  model.resolve(file, overrides);  // cannot fail: the file was checked above
  if (file.present()) {
    LOG_DBG("DSP", "Timings file %s: panel %s, %s, %s", generated::TIMINGS_PATH, file.panel, file.date, file.source);
  } else {
    LOG_DBG("DSP", "No timings file (%s): built-in defaults", generated::TIMINGS_PATH);
  }
  for (const ProfileInfo& info : PROFILES) {
    const ResolvedTiming& r = model.forProfile(info.id);
    LOG_DBG("DSP", "Simulator model %s: update %lu ms (%s), clear %lu ms (%s)", info.key,
            static_cast<unsigned long>(r.timing.updateMs), timingSourceName(r.updateSource),
            static_cast<unsigned long>(r.timing.clearMs), timingSourceName(r.clearSource));
  }
  return model;
}

SimulatorDisplayBackend::SimulatorDisplayBackend(GfxRenderer& renderer)
    : Parts(renderer),
      ModelledBackend(simPresenter, simClock, simSleeper, loadSimulatorTimingModel(), PanelId::SIMULATED) {}

uint32_t SimulatorDisplayBackend::presentUpdate(const ProfileId waveform) {
  const uint32_t ms = ModelledBackend::presentUpdate(waveform);
  logModelledRefresh(*this, lastCall());
  return ms;
}

uint32_t SimulatorDisplayBackend::presentClear() {
  const uint32_t ms = ModelledBackend::presentClear();
  logModelledRefresh(*this, lastCall());
  return ms;
}

}  // namespace displayprofile

#endif  // CROSSPOINT_RSVP_ENABLED && defined(SIMULATOR)
