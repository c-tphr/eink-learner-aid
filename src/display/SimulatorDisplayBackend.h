#pragma once

// The simulator backend: the real refresh through the renderer (so the simulator window and its
// screenshots show the frame), then a sleep for the rest of the profile's modelled time, so the
// simulator is as slow as the panel. Configuration, in increasing priority (lib/DisplayProfileCore
// README, "Timing model"):
//
//   built-in defaults < docs/device/picco-timings.json (compiled in by the build)
//                     < CROSSPOINT_SIM_UPDATE_MS / CROSSPOINT_SIM_CLEAR_MS (0 = no added delay)
//
// Only DisplayProfile.cpp uses this header; callers go through firmwareBackend().

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED && defined(SIMULATOR)

#include <DisplayBackend.h>
#include <DisplayTiming.h>

#include "DisplayProfile.h"

class GfxRenderer;

namespace displayprofile {

namespace sim_detail {

// Base-from-member: these must exist before ModelledBackend stores references to them.
struct Parts {
  class RendererPresenter final : public Presenter {
   public:
    explicit RendererPresenter(GfxRenderer& renderer) : renderer(renderer) {}
    void present(RefreshKind kind) override;

   private:
    GfxRenderer& renderer;
  };

  class DelaySleeper final : public Sleeper {
   public:
    void sleepMs(uint32_t ms) override;  // delay(): a thread sleep in the simulator, never a busy wait
  };

  explicit Parts(GfxRenderer& renderer) : simPresenter(renderer) {}
  RendererPresenter simPresenter;
  MillisClock simClock;
  DelaySleeper simSleeper;
};

}  // namespace sim_detail

class SimulatorDisplayBackend final : private sim_detail::Parts, public ModelledBackend {
 public:
  // Resolves the timing model and logs it. A malformed environment override stops the program.
  explicit SimulatorDisplayBackend(GfxRenderer& renderer);

 protected:
  uint32_t presentUpdate() override;
  uint32_t presentClear() override;
};

// The resolved model: defaults, the compiled-in timings file, the environment. Logs one line per
// profile; logs an error and aborts on a malformed override (nothing is half-applied).
TimingModel loadSimulatorTimingModel();

}  // namespace displayprofile

#endif  // CROSSPOINT_RSVP_ENABLED && defined(SIMULATOR)
