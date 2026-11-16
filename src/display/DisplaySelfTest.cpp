#include "DisplaySelfTest.h"

#if CROSSPOINT_RSVP_ENABLED && defined(SIMULATOR)

#include <Arduino.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include <cstdio>
#include <cstdlib>

#include "DisplayProfile.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "fontIds.h"

namespace {

constexpr uint32_t HOME_SETTLE_MS = 500;  // Home has drawn every phase (an empty SD card has one)
constexpr int MAX_UPDATES = 100;

// The band: logical portrait coordinates, below Home's menu.
constexpr int BAND_X = 40;
constexpr int BAND_Y = 600;
constexpr int BAND_W = 400;
constexpr int BAND_H = 120;

enum class State { UNCHECKED, WAITING, DONE };

// A frame of the band: a counter and a bar with `step` of `total` cells filled.
void drawBand(const GfxRenderer& renderer, const int step, const int total, const char* profileKey) {
  renderer.fillRect(BAND_X, BAND_Y, BAND_W, BAND_H, false);
  renderer.drawRect(BAND_X, BAND_Y, BAND_W, BAND_H, 2, true);
  char line[64];
  std::snprintf(line, sizeof(line), "Display self-test: %s", profileKey);
  renderer.drawCenteredText(UI_10_FONT_ID, BAND_Y + 14, line);
  std::snprintf(line, sizeof(line), "Update %d of %d", step, total);
  renderer.drawCenteredText(UI_12_FONT_ID, BAND_Y + 44, line);
  constexpr int barX = BAND_X + 20;
  constexpr int barY = BAND_Y + 86;
  constexpr int barW = BAND_W - 40;
  constexpr int barH = 18;
  renderer.drawRect(barX, barY, barW, barH, true);
  renderer.fillRect(barX + 2, barY + 2, (barW - 4) * step / total, barH - 4, true);
}

void runSelfTest(GfxRenderer& renderer, const int total) {
  // The render task waits while the band is drawn and presented; Home redraws on its next update.
  RenderLock lock;
  displayprofile::DisplayBackend& backend = displayprofile::firmwareBackend(renderer);
  backend.resetStats();
  LOG_DBG("DSP", "Self-test: %d updates and 1 clear, profile %s", total, backend.info().key);
  for (int step = 1; step <= total; ++step) {
    drawBand(renderer, step, total, backend.info().key);
    backend.update();
  }
  backend.clear();  // leaves the last band on screen
  displayprofile::logStats(backend);
  LOG_DBG("DSP", "Self-test done");
}

}  // namespace

void runDisplaySelfTestIfRequested(GfxRenderer& renderer) {
  static State state = State::UNCHECKED;
  static int total = 0;
  static uint32_t homeSince = 0;
  if (state == State::DONE) return;

  if (state == State::UNCHECKED) {
    const char* text = std::getenv("CROSSPOINT_SIM_DISPLAY_SELFTEST");
    if (text == nullptr || *text == '\0') {
      state = State::DONE;
      return;
    }
    char* end = nullptr;
    const long n = std::strtol(text, &end, 10);
    if (*end != '\0' || n < 1 || n > MAX_UPDATES) {
      LOG_ERR("DSP", "CROSSPOINT_SIM_DISPLAY_SELFTEST='%s' must be a count from 1 to %d", text, MAX_UPDATES);
      std::abort();
    }
    total = static_cast<int>(n);
    state = State::WAITING;
  }

  if (!activityManager.isForegroundHomeShown()) {
    homeSince = 0;
    return;
  }
  const uint32_t now = static_cast<uint32_t>(millis());
  if (homeSince == 0) homeSince = now == 0 ? 1 : now;
  if (now - homeSince < HOME_SETTLE_MS) return;

  state = State::DONE;
  runSelfTest(renderer, total);
}

#endif  // CROSSPOINT_RSVP_ENABLED && defined(SIMULATOR)
