#include "SpeedReadSetting.h"

#if CROSSPOINT_RSVP_ENABLED

#include <BoardConfig.h>
#include <DisplayProfiles.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <PanelRecords.h>
#include <TrialUnlock.h>

#include "CrossPointSettings.h"
#include "ProfileChoice.h"

namespace speedreadsetting {

namespace {

// The panel the display backend resolves (src/display/DeviceDisplayBackend.cpp; the simulator's own in
// the simulator), without creating the backend from Settings.
displayprofile::PanelId panel() {
#if defined(SIMULATOR)
  return displayprofile::PanelId::SIMULATED;
#else
  return displayprofile::resolvePanel(BoardConfig::ACTIVE.displayControllerVariant, display.controllerName());
#endif
}

}  // namespace

bool visible() { return devicetest::speedReadRowVisible(SETTINGS.rsvpDisplayProfile, SETTINGS.rsvpProfilesKept); }

Options options() {
  const devicetest::SpeedReadOptions o =
      devicetest::speedReadOptions(SETTINGS.rsvpDisplayProfile, SETTINGS.rsvpProfilesKept, panel());
  Options out;
  for (uint8_t i = 0; i < o.list.count && out.count < Options::MAX; ++i) {
    out.names[out.count] = displayprofile::profileInfo(o.list.ids[i]).name;
    out.ids[out.count] = static_cast<uint8_t>(o.list.ids[i]);
    ++out.count;
  }
  out.current = o.current;
  return out;
}

const char* currentName() {
  const Options o = options();
  return o.count > 0 ? o.names[o.current] : "";
}

void select(const Options& options, const int index) {
  if (index < 0 || index >= options.count) return;
  const auto id = static_cast<displayprofile::ProfileId>(options.ids[index]);
  const uint8_t before = SETTINGS.rsvpDisplayProfile;
  SETTINGS.rsvpDisplayProfile = devicetest::selectSpeedReadProfile(before, SETTINGS.rsvpProfilesKept, panel(), id);
  const bool ok = SETTINGS.rsvpDisplayProfile == before || SETTINGS.saveToFile();
  LOG_INF("DTS", "speed read display chosen=%s setting_before=%u setting_after=%u saved=%d",
          displayprofile::profileInfo(id).key, static_cast<unsigned>(before),
          static_cast<unsigned>(SETTINGS.rsvpDisplayProfile), ok ? 1 : 0);
}

}  // namespace speedreadsetting

#endif  // CROSSPOINT_RSVP_ENABLED
