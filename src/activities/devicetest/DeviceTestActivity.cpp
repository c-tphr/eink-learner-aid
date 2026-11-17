#include "DeviceTestActivity.h"

#if CROSSPOINT_RSVP_ENABLED

#include <Arduino.h>
#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <TrialUnlock.h>
#include <TrialWaveforms.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#if !defined(SIMULATOR)
#include <esp_app_desc.h>
#endif

#include "CrossPointSettings.h"
#include "DeviceTestLaunch.h"
#include "MappedInputManager.h"
#include "fontIds.h"

using namespace devicetest;
using displayprofile::ProfileId;
using displayprofile::profileInfo;

namespace {

using Btn = MappedInputManager::Button;

// The word band, as on the RSVP playback screen (src/activities/rsvp/RsvpActivity.cpp).
constexpr int BAND_FONTS[rsvpui::BAND_FONT_COUNT] = {NOTOSANS_18_FONT_ID, NOTOSANS_16_FONT_ID, NOTOSANS_14_FONT_ID,
                                                     NOTOSANS_12_FONT_ID};
constexpr EpdFontFamily::Style BAND_STYLE = EpdFontFamily::BOLD;
constexpr int16_t MARGIN = 16;
constexpr int16_t BAND_TOP = 352;
constexpr int16_t BAND_HEIGHT = 96;
constexpr int16_t TICK_LENGTH = 20;
constexpr int16_t TICK_GAP = 4;
constexpr int16_t TICK_WIDTH = 3;
constexpr int16_t COUNTER_Y = 600;
constexpr int16_t COUNTER_H = 40;

// List screens (S4 row 34): title and subtitle above the rows, notes below them, the key hint last.
constexpr int16_t TITLE_Y = 10;
constexpr int16_t SUBTITLE_Y = 42;
constexpr int16_t HINT_Y = 752;
constexpr int16_t HINT_H = 48;
constexpr int16_t BORDER = 2;
constexpr int16_t LINE_GAP = 6;

// Controls check: the newest events, oldest first, above the Finish button.
constexpr int16_t EVENTS_Y = 200;
constexpr int CONTROLS_EVENT_LINES = 16;
constexpr uint32_t CONTROLS_FLUSH_IDLE_MS = 2000;

// The render task has not seen the main loop for this long: another screen (the light panel) covers
// this one, so a running series stops rather than run unseen (as RsvpActivity does).
constexpr uint32_t COVERED_MS = 1500;

constexpr int8_t TEMPERATURE_MIN = -10;
constexpr int8_t TEMPERATURE_MAX = 45;
constexpr int8_t TEMPERATURE_START = 20;

constexpr const char* MENU_LOG_NAMES[] = {"controls-check", "benchmark", "clearing-refresh", "leave"};
constexpr uint8_t MENU_ROWS = 4;

bool reached(const uint32_t now, const uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

static_assert(static_cast<int>(Btn::Back) == 0 && static_cast<int>(Btn::Confirm) == 1 &&
                  static_cast<int>(Btn::Up) == LOGICAL_UP && static_cast<int>(Btn::Down) == LOGICAL_DOWN &&
                  static_cast<int>(Btn::Power) == 6 && static_cast<int>(Btn::PageBack) == LOGICAL_PAGE_BACK &&
                  static_cast<int>(Btn::PageForward) == LOGICAL_PAGE_FORWARD && static_cast<int>(Btn::NavNext) == 9 &&
                  static_cast<int>(Btn::NavPrevious) == 10,
              "ControlsCheck.h's logical bits follow MappedInputManager::Button");
static_assert(HalGPIO::BTN_CONFIRM == RAW_CONFIRM && HalGPIO::BTN_UP == RAW_UP && HalGPIO::BTN_DOWN == RAW_DOWN &&
                  HalGPIO::BTN_POWER == RAW_POWER,
              "ControlsCheck.h's raw indices are HalGPIO's");

bool sideLayoutUsable() {
  return SETTINGS.sideButtonLayout == CrossPointSettings::PREV_NEXT ||
         SETTINGS.sideButtonLayout == CrossPointSettings::NEXT_PREV;
}

const char* reasonText(const NotRunReason reason) {
  switch (reason) {
    case NotRunReason::PANEL_UNRECOGNISED:
      return tr(STR_DT_REASON_PANEL_UNRECOGNISED);
    case NotRunReason::PANEL_NO_TABLES:
      return tr(STR_DT_REASON_NO_TABLES);
    case NotRunReason::SCREEN_INVERTED:
      return tr(STR_DT_REASON_INVERTED);
    case NotRunReason::REFUSED:
      return tr(STR_DT_REASON_REFUSED);
    case NotRunReason::ENDED_BY_OWNER:
    case NotRunReason::INTERRUPTED:
    case NotRunReason::NONE:
      break;
  }
  return "";
}

const char* ratingText(const Rating rating) {
  switch (rating) {
    case Rating::CLEAN:
      return tr(STR_DT_CLEAN);
    case Rating::TOLERABLE:
      return tr(STR_DT_TOLERABLE);
    case Rating::UNREADABLE:
      return tr(STR_DT_UNREADABLE);
    case Rating::NONE:
      break;
  }
  return tr(STR_DT_NOT_RATED);
}

// "120 WPM, 1 word per update"
void paceText(const StepSpec& s, char* out, const size_t size) {
  snprintf(out, size, "%u %s, %u %s", s.wpm, tr(STR_RSVP_WPM), s.wordsPerUpdate,
           s.wordsPerUpdate == 1 ? tr(STR_RSVP_WORD_PER_UPDATE) : tr(STR_RSVP_WORDS_PER_UPDATE));
}

void stepText(const StepSpec& s, char* out, const size_t size) {
  switch (s.kind) {
    case StepKind::WORD_BAND:
      snprintf(out, size, "%s", tr(STR_DT_WORD_BAND_DESC));
      return;
    case StepKind::FULL_SCREEN:
      snprintf(out, size, "%s", tr(STR_DT_FULL_SCREEN_DESC));
      return;
    case StepKind::CLEARING:
      snprintf(out, size, "%s", tr(STR_DT_CLEARING_DESC));
      return;
    case StepKind::ENDURANCE:
      snprintf(out, size, "%s", tr(STR_DT_ENDURANCE_DESC));
      return;
    case StepKind::PLAYBACK: {
      char pace[64];
      paceText(s, pace, sizeof(pace));
      snprintf(out, size, "%s %s", tr(STR_DT_PASSAGE_AT), pace);
      return;
    }
  }
  out[0] = '\0';
}

// The time of one word update and one clearing refresh used for the overview's estimate: the
// panel's computable prediction plus the frame transfer, else the Xteink X4 figures the simulator
// models (lib/DisplayProfileCore README, "Profiles").
void estimateTimes(const displayprofile::PanelId panel, uint32_t& updateMs, uint32_t& clearMs) {
  const displayprofile::PanelRecord& r = displayprofile::panelRecord(panel);
  const displayprofile::ProfileTiming x4 = profileInfo(ProfileId::STOCK).simulatorDefault;
  updateMs = r.predictedUpdateMs > 0 ? r.predictedUpdateMs + displayprofile::FRAME_TRANSFER_MS : x4.updateMs;
  clearMs = r.predictedClearMs > 0 ? r.predictedClearMs + displayprofile::FRAME_TRANSFER_MS : x4.clearMs;
}

// A JsonSink over an open file, through a small buffer.
class FileSink final : public JsonSink {
 public:
  explicit FileSink(HalFile& target) : file(target) {}
  void write(const char* data, size_t length) override {
    while (length > 0) {
      const size_t n = length < sizeof(buffer) - used ? length : sizeof(buffer) - used;
      memcpy(buffer + used, data, n);
      used += n;
      data += n;
      length -= n;
      if (used == sizeof(buffer)) flush();
    }
  }
  bool finish() {
    flush();
    return ok;
  }
  size_t bytes() const { return total; }

 private:
  void flush() {
    if (used == 0) return;
    if (file.write(buffer, used) != used) ok = false;
    total += used;
    used = 0;
  }
  HalFile& file;
  char buffer[256] = {};
  size_t used = 0;
  size_t total = 0;
  bool ok = true;
};

// Writes `path` through a temporary file and a rename, as src/reading/BookStateFile.cpp does, so a
// cut-off write never replaces the last good file.
template <typename Encode>
bool writeAtomically(const char* path, Encode encode, size_t& bytes) {
  char tmp[64];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  {
    HalFile file;
    if (!Storage.openFileForWrite("DTS", tmp, file)) {
      LOG_ERR("DTS", "results %s: cannot open for write", tmp);
      return false;
    }
    FileSink sink(file);
    encode(sink);
    const bool ok = sink.finish();
    bytes = sink.bytes();
    file.close();  // SdFat does not rename a file that is still open
    if (!ok) {
      LOG_ERR("DTS", "results %s: short write", tmp);
      Storage.remove(tmp);
      return false;
    }
  }
  // SdFat's rename does not replace, so the old file goes first.
  Storage.remove(path);
  if (!Storage.rename(tmp, path)) {
    LOG_ERR("DTS", "results %s: rename failed", path);
    return false;
  }
  return true;
}

}  // namespace

namespace devicetest {

std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& input) {
  // One screen-lifetime object: the sweep's records for five profiles of ten steps, the Controls
  // check's 64 events, the scheduler with its frame, and the text of the screen being drawn.
  auto activity = makeUniqueNoThrow<DeviceTestActivity>(renderer, input);
  if (!activity) LOG_ERR("DTS", "OOM: device test (%u bytes)", static_cast<unsigned>(sizeof(DeviceTestActivity)));
  return activity;
}

}  // namespace devicetest

// ---------------------------------------------------------------------------------------------
// Clock

void DeviceTestActivity::BenchClock::start(const uint32_t scale) {
  factor = scale == 0 ? 1 : scale;
  origin = static_cast<uint32_t>(millis());
}

uint32_t DeviceTestActivity::BenchClock::nowMs() {
  const uint32_t now = static_cast<uint32_t>(millis());
  return factor <= 1 ? now : origin + (now - origin) * factor;
}

// ---------------------------------------------------------------------------------------------
// Lifecycle

DeviceTestActivity::DeviceTestActivity(GfxRenderer& renderer, MappedInputManager& input)
    : Activity("DeviceTest", renderer, input),
      backend(displayprofile::firmwareBackend(renderer)),
      runner(backend, clock, *this, &DeviceTestActivity::fits, this),
      controlInput(input) {}

void DeviceTestActivity::onEnter() {
  Activity::onEnter();
  // The layout and the touch zones are portrait (S4 section 1); onExit() restores the UI orientation.
  previousOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  const int width = renderer.getScreenWidth();
  band.left = MARGIN;
  band.right = static_cast<int16_t>(width - MARGIN);
  band.focusX = static_cast<int16_t>(width * 2 / 5);
  band.top = BAND_TOP;
  band.height = BAND_HEIGHT;
  for (uint8_t i = 0; i < rsvpui::BAND_FONT_COUNT; ++i) {
    band.lineHeight[i] = static_cast<int16_t>(renderer.getLineHeight(BAND_FONTS[i]));
  }

  uint32_t scale = 1;
#if defined(SIMULATOR)
  if (const char* text = getenv("CROSSPOINT_SIM_BENCH_TIME_SCALE")) {
    const long v = strtol(text, nullptr, 10);
    if (v >= 1 && v <= 1000) scale = static_cast<uint32_t>(v);
    LOG_DBG("DTS", "simulator time scale %lu", static_cast<unsigned long>(scale));
  }
#endif
  clock.start(scale);

  const displayprofile::TrialSetting setting = displayprofile::decodeTrialSetting(SETTINGS.rsvpDisplayProfile);
  backend.setUnlockedTrials(setting.unlocked);
  backend.select(ProfileId::STOCK);  // menus and prompts: stock refreshes only
  lastLoopMs.store(static_cast<uint32_t>(millis()));
  LOG_DBG("DTS", "open panel=%s setting=%u activity_bytes=%u", displayprofile::panelName(backend.panel()),
          static_cast<unsigned>(SETTINGS.rsvpDisplayProfile), static_cast<unsigned>(sizeof(DeviceTestActivity)));
  RenderLock lock;
  show(Screen::Menu, MENU_ROWS, 0, true);
}

void DeviceTestActivity::onExit() {
  // The caller holds the RenderLock.
  if (sweep.active()) {
    if (seriesActive.load()) sweep.seriesStopped(runner.figures());
    sweep.abandon(RunStatus::INTERRUPTED);
    saveBenchmark();
    LOG_INF("DTS", "bench end run=%u status=%s", bench.header.run, runStatusName(sweep.status()));
  }
  if (controlsActive) {
    controlsRecord.status = RunStatus::COMPLETE;
    saveControls();
    controlsActive = false;
  }
  seriesActive.store(false);
  restoreProfile();
  Activity::onExit();
  renderer.setOrientation(previousOrientation);
}

void DeviceTestActivity::prepareForSleep() {
  // Under the RenderLock, no series step is running. Deep sleep restarts the device on wake, so the
  // files are written now with what was measured.
  if (sweep.active()) {
    if (seriesActive.load()) {
      sweep.seriesStopped(runner.figures());
      seriesActive.store(false);
    }
    sweep.abandon(RunStatus::INTERRUPTED);
    saveBenchmark();
    LOG_INF("DTS", "bench end run=%u status=%s", bench.header.run, runStatusName(sweep.status()));
  }
  if (controlsActive) {
    controlsRecord.status = RunStatus::INTERRUPTED;
    saveControls();
    controlsActive = false;
  }
}

bool DeviceTestActivity::preventAutoSleep() {
  const Screen s = screen.load();
  return s == Screen::Step || s == Screen::Look;
}

bool DeviceTestActivity::handleHomeGesture() {
  const Screen s = screen.load();
  if (s == Screen::Step) {
    runner.requestStop();
    LOG_DBG("DTS", "step stop requested by=home");
    return true;
  }
  if (s == Screen::Controls) {
    ControlEvent e;
    e.atMs = static_cast<uint32_t>(millis());
    e.source = EventSource::TOUCH;
    e.kind = EventKind::HOME_GESTURE;
    recordEvent(e);
  }
  return false;  // Home; onExit() writes the files
}

bool DeviceTestActivity::handleForcedRefresh() {
  const Screen s = screen.load();
  if (s != Screen::Step && s != Screen::Look) {
    RenderLock lock;
    needClear = true;
    requestUpdate();
  }
  return true;  // every refresh here goes through the display backend
}

void DeviceTestActivity::restoreProfile() {
  // RSVP selects its own profile when it opens (RsvpActivity::onEnter); this leaves the shared
  // backend as RSVP would find it with the stored value.
  const displayprofile::TrialSetting setting = displayprofile::decodeTrialSetting(SETTINGS.rsvpDisplayProfile);
  backend.setUnlockedTrials(setting.unlocked);
  backend.select(setting.chosen);
}

// ---------------------------------------------------------------------------------------------
// Render task

void DeviceTestActivity::render(RenderLock&&) {
  switch (screen.load()) {
    case Screen::Step:
      stepSeries();
      return;
    case Screen::Look:
      return;  // the series' last frame stays on screen until the owner has looked
    case Screen::Menu:
      if (pendingClearing) {
        runClearingRefresh();
        return;
      }
      drawScreen(needClear);
      return;
    default:
      drawScreen(needClear);
      return;
  }
}

void DeviceTestActivity::stepSeries() {
  if (stepPending) {
    stepPending = false;
    seriesDone.store(false);
    seriesActive.store(true);
    LOG_DBG("DTS", "step start profile=%s step=%s attempt=%u", profileInfo(sweep.profile().id).key, sweep.stepSpec().key,
            static_cast<unsigned>(sweep.stepRecord().attempts + 1));
    runner.begin(sweep.stepSpec());
    wakeAtMs.store(clock.nowMs());
    wakeValid.store(true);
    return;
  }
  if (!seriesActive.load() || seriesDone.load()) return;
  if (reached(static_cast<uint32_t>(millis()), lastLoopMs.load() + COVERED_MS)) runner.requestStop();
  const SeriesRunner::Tick t = runner.tick();
  if (t.done) {
    wakeValid.store(false);
    seriesDone.store(true);
    return;
  }
  wakeAtMs.store(t.wake ? t.wakeAtMs : clock.nowMs());
  wakeValid.store(true);
}

void DeviceTestActivity::runClearingRefresh() {
  // One clearing refresh of the menu as it is (the plan's remedy for residue), then the menu again
  // with the refresh's time.
  pendingClearing = false;
  drawScreen(true);
  lastClearingMs = backend.stats().clears.lastMs;
  LOG_INF("DTS", "clearing refresh ms=%lu", static_cast<unsigned long>(lastClearingMs));
  drawScreen(false);
}

void DeviceTestActivity::paintList(const char* title, const char* subtitle, const char* const* labels,
                                   const uint8_t lineCount) {
  renderer.clearScreen();
  renderer.drawText(UI_12_FONT_ID, MARGIN, TITLE_Y, title, true, EpdFontFamily::BOLD);
  if (subtitle != nullptr && subtitle[0] != '\0') renderer.drawText(UI_10_FONT_ID, MARGIN, SUBTITLE_Y, subtitle);

  const controls::Screen s = list.screen();
  const int labelHeight = renderer.getLineHeight(UI_12_FONT_ID);
  for (uint8_t i = 0; i < s.count; ++i) {
    const controls::Zone& z = s.zones[i];
    const bool focused = i == list.focus();
    if (focused) {
      renderer.fillRect(z.x, z.y, z.w, z.h, true);
    } else {
      renderer.drawRect(z.x, z.y, z.w, z.h, BORDER, true);
    }
    renderer.drawText(UI_12_FONT_ID, z.x + MARGIN, z.y + (z.h - labelHeight) / 2, labels[i], !focused);
  }

  const int small = renderer.getLineHeight(UI_10_FONT_ID);
  int y = s.zones[s.count - 1].y + s.zones[s.count - 1].h + 12;
  for (uint8_t i = 0; i < lineCount && y + small <= HINT_Y; ++i) {
    renderer.drawText(UI_10_FONT_ID, MARGIN, y, info[i]);
    y += small + LINE_GAP;
  }
  renderer.drawCenteredText(UI_10_FONT_ID, HINT_Y + (HINT_H - small) / 2, tr(STR_RSVP_KEY_HINT));
}

void DeviceTestActivity::drawScreen(const bool clearing) {
  const Screen s = screen.load();
  const char* labels[controls::CHOICE_ROWS_MAX] = {};
  uint8_t lines = 0;
  const char* title = "";
  char subtitle[LINE_BYTES] = {};
  const auto line = [&](const char* fmt, auto... args) {
    if (lines < INFO_LINES) snprintf(info[lines++], LINE_BYTES, fmt, args...);
  };

  switch (s) {
    case Screen::Menu: {
      title = tr(STR_DT_DEVICE_TEST);
      labels[0] = tr(STR_DT_CONTROLS_CHECK);
      labels[1] = tr(STR_DT_BENCHMARK);
      labels[2] = tr(STR_DT_CLEARING_REFRESH);
      labels[3] = tr(STR_DT_LEAVE);
      const displayprofile::PanelRecord& r = displayprofile::panelRecord(backend.panel());
      snprintf(subtitle, sizeof(subtitle), "%s: %s (%s)", tr(STR_DT_PANEL), displayprofile::panelName(backend.panel()),
               displayprofile::controllerName(r.controller));
      if (lastClearingMs > 0) line("%s: %lu ms", tr(STR_DT_CLEARING_DONE), static_cast<unsigned long>(lastClearingMs));
      break;
    }
    case Screen::Controls:
      paintControls();
      break;
    case Screen::Temperature: {
      title = tr(STR_DT_TEMPERATURE);
      if (temperatureKnown) {
        snprintf(subtitle, sizeof(subtitle), "%s: %d °C", tr(STR_DT_TEMPERATURE), temperatureC);
      } else {
        snprintf(subtitle, sizeof(subtitle), "%s: %s", tr(STR_DT_TEMPERATURE), tr(STR_DT_UNKNOWN));
      }
      labels[0] = tr(STR_DT_CONTINUE);
      labels[1] = tr(STR_DT_WARMER);
      labels[2] = tr(STR_DT_COOLER);
      labels[3] = tr(STR_DT_SET_UNKNOWN);
      labels[4] = tr(STR_DT_BACK);
      line("%s", tr(STR_DT_TEMP_HELP));
      break;
    }
    case Screen::Overview: {
      title = tr(STR_DT_BENCHMARK);
      labels[0] = tr(STR_DT_START);
      labels[1] = tr(STR_DT_BACK);
      const displayprofile::PanelRecord& r = displayprofile::panelRecord(backend.panel());
      snprintf(subtitle, sizeof(subtitle), "%s: %s (%s)", tr(STR_DT_PANEL), displayprofile::panelName(backend.panel()),
               displayprofile::controllerName(r.controller));
      if (temperatureKnown) {
        line("%s: %d °C", tr(STR_DT_TEMPERATURE), temperatureC);
      } else {
        line("%s: %s", tr(STR_DT_TEMPERATURE), tr(STR_DT_UNKNOWN));
      }
      line("%s: %u%%", tr(STR_DT_BATTERY), static_cast<unsigned>(powerManager.getBatteryPercentage()));
      char names[LINE_BYTES] = {};
      size_t used = 0;
      for (const ProfileRecord& p : sweep.profiles) {
        if (p.status == ProfileStatus::NOT_ALLOWED) continue;
        const int w = snprintf(names + used, sizeof(names) - used, "%s%s", used ? ", " : "", profileInfo(p.id).name);
        if (w > 0) used += static_cast<size_t>(w) < sizeof(names) - used ? static_cast<size_t>(w) : 0;
      }
      line("%s: %s", tr(STR_DT_TO_RUN), names);
      bool anyNotRun = false;
      for (const ProfileRecord& p : sweep.profiles) {
        if (p.status != ProfileStatus::NOT_ALLOWED) continue;
        line("%s: %s, %s", tr(STR_DT_NOT_RUN), profileInfo(p.id).name, reasonText(p.reason));
        anyNotRun = true;
      }
      if (!anyNotRun) line("%s: %s", tr(STR_DT_NOT_RUN), tr(STR_DT_NONE));
      uint32_t updateMs = 0;
      uint32_t clearMs = 0;
      estimateTimes(backend.panel(), updateMs, clearMs);
      const uint32_t seconds =
          estimateProfileSeconds(updateMs, clearMs, profileInfo(ProfileId::STOCK).clearing.softLimit,
                                 OWNER_SECONDS_PER_RATING);
      line("%s %lu %s", tr(STR_DT_PER_PROFILE), static_cast<unsigned long>((seconds + 30) / 60), tr(STR_DT_MINUTES));
      break;
    }
    case Screen::Rating:
    case Screen::Residue:
    case Screen::Stopped: {
      const StepSpec& spec = sweep.stepSpec();
      char what[LINE_BYTES - 16];
      stepText(spec, what, sizeof(what));
      snprintf(subtitle, sizeof(subtitle), "%s · %s", profileInfo(sweep.profile().id).name, what);
      if (s == Screen::Rating) {
        title = tr(STR_DT_RATE_TITLE);
        labels[0] = tr(STR_DT_REPEAT_STEP);
        labels[1] = tr(STR_DT_CLEAN);
        labels[2] = tr(STR_DT_TOLERABLE);
        labels[3] = tr(STR_DT_UNREADABLE);
        labels[4] = tr(STR_DT_UNREADABLE_SKIP);
      } else if (s == Screen::Residue) {
        title = tr(STR_DT_RESIDUE_TITLE);
        labels[0] = tr(STR_DT_REPEAT_STEP);
        labels[1] = tr(STR_DT_RESIDUE_NO);
        labels[2] = tr(STR_DT_RESIDUE_YES);
      } else {
        title = tr(STR_DT_STOPPED_TITLE);
        labels[0] = tr(STR_DT_REPEAT_STEP);
        labels[1] = tr(STR_DT_SKIP_PROFILE);
        labels[2] = tr(STR_DT_END_RUN);
      }
      const StepRecord& r = sweep.stepRecord();
      if (s != Screen::Stopped && r.hasFigures && r.figures.hasPlayback) {
        const PlaybackFigures& p = r.figures.playback;
        line("%s %lu.%lu WPM · %lu / %u · %s", tr(STR_DT_DELIVERED),
             static_cast<unsigned long>(p.deliveredWpmTenths / 10), static_cast<unsigned long>(p.deliveredWpmTenths % 10),
             static_cast<unsigned long>(p.wordsDrawn), spec.count, p.inOrder ? "order ok" : "ORDER VIOLATED");
        line("%s %lu ms · %s %lu", tr(STR_DT_MEDIAN), static_cast<unsigned long>(r.figures.updates.medianMs),
             tr(STR_DT_CLEARING), static_cast<unsigned long>(r.figures.clears.count));
      }
      break;
    }
    case Screen::ProfileSummary: {
      const ProfileRecord& p = sweep.profile();
      title = profileInfo(p.id).name;
      snprintf(subtitle, sizeof(subtitle), "%s · %s", profileInfo(p.id).name,
               p.status == ProfileStatus::DONE ? tr(STR_DT_PROFILE_DONE) : tr(STR_DT_PROFILE_SKIPPED));
      const ProfileRecord* next = sweep.nextProfile();
      if (next != nullptr) {
        snprintf(labelText[0], LINE_BYTES, "%s: %s", tr(STR_DT_NEXT_PROFILE), profileInfo(next->id).name);
        labels[0] = labelText[0];
        labels[1] = tr(STR_DT_END_RUN);
      } else {
        labels[0] = tr(STR_DT_FINISH);
      }
      for (size_t i = 0; i < STEP_COUNT; ++i) {
        const StepSpec& spec = STEPS[i];
        const StepRecord& r = p.steps[i];
        const char* name = spec.kind == StepKind::WORD_BAND     ? tr(STR_DT_WORD_BAND)
                           : spec.kind == StepKind::FULL_SCREEN ? tr(STR_DT_FULL_SCREEN)
                           : spec.kind == StepKind::CLEARING    ? tr(STR_DT_CLEARING)
                           : spec.kind == StepKind::ENDURANCE   ? tr(STR_DT_ENDURANCE)
                                                                : nullptr;
        char label[48];
        if (name != nullptr) {
          snprintf(label, sizeof(label), "%s", name);
        } else {
          snprintf(label, sizeof(label), "%u WPM x%u", spec.wpm, spec.wordsPerUpdate);
        }
        if (r.status == StepStatus::SKIPPED || r.status == StepStatus::PENDING || !r.hasFigures) {
          line("%s: %s", label, tr(STR_DT_SKIPPED));
          continue;
        }
        const StepFigures& f = r.figures;
        const char* stopped = r.status == StepStatus::STOPPED ? tr(STR_DT_STOPPED) : "";
        switch (spec.kind) {
          case StepKind::WORD_BAND:
            line("%s: %s %lu, p95 %lu, max %lu ms %s", label, tr(STR_DT_MEDIAN),
                 static_cast<unsigned long>(f.updates.medianMs), static_cast<unsigned long>(f.updates.p95Ms),
                 static_cast<unsigned long>(f.updates.maxMs), stopped);
            break;
          case StepKind::FULL_SCREEN:
            line("%s: %s %lu ms %s", label, tr(STR_DT_MEDIAN), static_cast<unsigned long>(f.updates.medianMs), stopped);
            break;
          case StepKind::CLEARING:
            line("%s: %s %lu ms %s", label, tr(STR_DT_MEDIAN), static_cast<unsigned long>(f.clears.medianMs), stopped);
            break;
          case StepKind::PLAYBACK:
            line("%s: %lu.%lu %s, %s %s", label, static_cast<unsigned long>(f.playback.deliveredWpmTenths / 10),
                 static_cast<unsigned long>(f.playback.deliveredWpmTenths % 10), tr(STR_DT_DELIVERED),
                 ratingText(r.rating), stopped);
            break;
          case StepKind::ENDURANCE:
            line("%s: %s, %s %s %s", label, ratingText(r.rating), tr(STR_DT_RESIDUE), residueName(r.residue),
                 stopped);
            break;
        }
      }
      break;
    }
    case Screen::RunSummary: {
      title = sweep.status() == RunStatus::COMPLETE ? tr(STR_DT_RUN_FINISHED) : tr(STR_DT_RUN_ENDED);
      labels[0] = tr(STR_DT_BACK_TO_MENU);
      if (benchSaved) {
        snprintf(subtitle, sizeof(subtitle), "%s %s/%s", tr(STR_DT_SAVED_AS), RESULTS_DIR, bench.header.file);
      } else {
        snprintf(subtitle, sizeof(subtitle), "%s", tr(STR_DT_NOT_SAVED));
      }
      for (const ProfileRecord& p : sweep.profiles) {
        if (p.status == ProfileStatus::NOT_ALLOWED) {
          line("%s: %s, %s", profileInfo(p.id).name, tr(STR_DT_NOT_RUN), reasonText(p.reason));
        } else if (p.status == ProfileStatus::DONE) {
          line("%s: %s", profileInfo(p.id).name, tr(STR_DT_PROFILE_DONE));
        } else if (p.status == ProfileStatus::SKIPPED_PART_WAY) {
          line("%s: %s", profileInfo(p.id).name, tr(STR_DT_PROFILE_SKIPPED));
        } else {
          line("%s: %s", profileInfo(p.id).name, tr(STR_DT_NOT_RUN));
        }
      }
      break;
    }
    case Screen::Step:
    case Screen::Look:
      return;
  }

  if (s != Screen::Controls) paintList(title, subtitle, labels, lines);
  if (clearing) {
    backend.clear();
  } else {
    backend.update();
  }
  needClear = false;
  LOG_DBG("DTS", "screen %s focus=%u refresh=%s", screenName(), static_cast<unsigned>(list.focus()),
          clearing ? "clear" : "update");
}

void DeviceTestActivity::paintControls() {
  renderer.clearScreen();
  renderer.drawText(UI_12_FONT_ID, MARGIN, TITLE_Y, tr(STR_DT_CONTROLS_CHECK), true, EpdFontFamily::BOLD);
  const int small = renderer.getLineHeight(UI_10_FONT_ID);
  int y = SUBTITLE_Y;
  for (const char* help : {tr(STR_DT_CONTROLS_HELP_1), tr(STR_DT_CONTROLS_HELP_2), tr(STR_DT_CONTROLS_HELP_3)}) {
    renderer.drawText(UI_10_FONT_ID, MARGIN, y, help);
    y += small + LINE_GAP;
  }

  const size_t count = controlsRecord.count;
  const ControlsSummary sum = summarise(controlsRecord.events, count);
  const struct {
    bool done;
    const char* name;
  } items[] = {{sum.upPressed, tr(STR_DT_ITEM_UP)},          {sum.upHeld, tr(STR_DT_ITEM_UP_HELD)},
               {sum.downPressed, tr(STR_DT_ITEM_DOWN)},      {sum.downHeld, tr(STR_DT_ITEM_DOWN_HELD)},
               {sum.confirmPressed, tr(STR_DT_ITEM_CONFIRM)}, {sum.powerClicked, tr(STR_DT_ITEM_POWER)},
               {sum.touched, tr(STR_DT_ITEM_TOUCH)}};
  for (const bool want : {true, false}) {
    char text[LINE_BYTES];
    size_t used = static_cast<size_t>(
        snprintf(text, sizeof(text), "%s:", want ? tr(STR_DT_TRIED) : tr(STR_DT_NOT_YET)));
    for (const auto& item : items) {
      if (item.done != want || used >= sizeof(text)) continue;
      const int w = snprintf(text + used, sizeof(text) - used, " %s,", item.name);
      if (w > 0) used += static_cast<size_t>(w);
    }
    if (used < sizeof(text) && text[used - 1] == ',') text[used - 1] = '\0';
    y += LINE_GAP;
    renderer.drawText(UI_10_FONT_ID, MARGIN, y, text);
    y += small;
  }

  y = EVENTS_Y;
  if (count == 0) renderer.drawText(UI_10_FONT_ID, MARGIN, y, tr(STR_DT_NO_EVENTS));
  const size_t first = count > CONTROLS_EVENT_LINES ? count - CONTROLS_EVENT_LINES : 0;
  for (size_t i = first; i < count; ++i) {
    char described[LINE_BYTES - 8];
    describeEvent(controlsRecord.events[i], described, sizeof(described));
    char text[LINE_BYTES];
    snprintf(text, sizeof(text), "%u. %s", static_cast<unsigned>(i + 1), described);
    renderer.drawText(UI_10_FONT_ID, MARGIN, y, text);
    y += small + 4;
  }

  const controls::Zone& z = controls::CONTROLS_CHECK_ZONES[0];
  renderer.drawRect(z.x, z.y, z.w, z.h, BORDER, true);
  renderer.drawCenteredText(UI_12_FONT_ID, z.y + (z.h - renderer.getLineHeight(UI_12_FONT_ID)) / 2,
                            tr(STR_DT_CONTROLS_FINISH));
}

void DeviceTestActivity::paintStepScreen(const StepSpec& step) {
  renderer.clearScreen();
  char text[LINE_BYTES];
  snprintf(text, sizeof(text), "%s · %s %u / %u", profileInfo(sweep.profile().id).name, tr(STR_DT_STEP),
           static_cast<unsigned>(sweep.stepIndex() + 1), static_cast<unsigned>(STEP_COUNT));
  renderer.drawText(UI_12_FONT_ID, MARGIN, TITLE_Y, text, true, EpdFontFamily::BOLD);
  const int small = renderer.getLineHeight(UI_10_FONT_ID);
  int y = SUBTITLE_Y;
  stepText(step, text, sizeof(text));
  renderer.drawText(UI_10_FONT_ID, MARGIN, y, text);
  y += small + LINE_GAP;
  renderer.drawText(UI_10_FONT_ID, MARGIN, y, tr(STR_DT_STOP_HINT));
  if (step.rated) {
    y += small + LINE_GAP;
    renderer.drawText(UI_10_FONT_ID, MARGIN, y, tr(STR_DT_LOOK_HINT));
  }
  if (usesScheduler(step.kind)) {
    const int x = band.focusX - TICK_WIDTH / 2;
    renderer.fillRect(x, band.top - TICK_GAP - TICK_LENGTH, TICK_WIDTH, TICK_LENGTH, true);
    renderer.fillRect(x, band.top + band.height + TICK_GAP, TICK_WIDTH, TICK_LENGTH, true);
  }
}

void DeviceTestActivity::paintBand(const rsvp::Frame* frame, const uint32_t shown) {
  const StepSpec& step = runner.step();
  renderer.fillRect(0, band.top, renderer.getScreenWidth(), band.height, false);
  if (step.kind != StepKind::PLAYBACK) {
    // The timing series and the endurance run show how far they are; the rated passages do not, so
    // that only the band changes, as in reading.
    char text[48];
    snprintf(text, sizeof(text), "%s %lu / %u", tr(STR_DT_UPDATE), static_cast<unsigned long>(shown), step.count);
    renderer.fillRect(0, COUNTER_Y, renderer.getScreenWidth(), COUNTER_H, false);
    renderer.drawCenteredText(UI_10_FONT_ID, COUNTER_Y + 8, text);
  }
  if (frame == nullptr || frame->textLength == 0) return;
  const std::string_view text = frame->view();
  const rsvpui::BandLayout layout = rsvpui::layoutBand(band, &DeviceTestActivity::measure, this, text,
                                                       frame->focusByteOffset, frame->focusByteLength);
  char line[rsvp::FRAME_TEXT_CAPACITY + 1];
  for (uint8_t i = 0; i < layout.lineCount; ++i) {
    const rsvpui::BandLine& l = layout.lines[i];
    memcpy(line, text.data() + l.offset, l.length);
    line[l.length] = '\0';
    renderer.drawText(BAND_FONTS[layout.font], l.x, l.y, line, true, BAND_STYLE);
  }
}

void DeviceTestActivity::paintPage(const uint32_t n) {
  // Two pages of the passage that differ on almost every line, so each update changes most of the
  // screen.
  renderer.clearScreen();
  const StepSpec& step = runner.step();
  char text[LINE_BYTES];
  snprintf(text, sizeof(text), "%s · %s %lu / %u", profileInfo(sweep.profile().id).name, tr(STR_DT_UPDATE),
           static_cast<unsigned long>(n + 1), step.count);
  renderer.drawText(UI_12_FONT_ID, MARGIN, TITLE_Y, text, true, EpdFontFamily::BOLD);

  PassageSource words;
  words.reset(4 * PASSAGE_WORDS);
  rsvp::Token t;
  for (uint32_t skip = (n % 2) * (PASSAGE_WORDS / 2); skip > 0 && words.peek(t); --skip) words.advance();
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int maxWidth = renderer.getScreenWidth() - 2 * MARGIN;
  for (int y = SUBTITLE_Y + 8; y + lineHeight <= HINT_Y + HINT_H - 8; y += lineHeight) {
    char line[LINE_BYTES] = {};
    size_t used = 0;
    while (words.peek(t)) {
      const size_t add = t.text.size() + (used > 0 ? 1 : 0);
      if (used + add >= sizeof(line)) break;
      char candidate[LINE_BYTES];
      memcpy(candidate, line, used);
      size_t c = used;
      if (c > 0) candidate[c++] = ' ';
      memcpy(candidate + c, t.text.data(), t.text.size());
      candidate[c + t.text.size()] = '\0';
      if (used > 0 && renderer.getTextWidth(UI_12_FONT_ID, candidate) > maxWidth) break;
      memcpy(line, candidate, c + t.text.size() + 1);
      used = c + t.text.size();
      words.advance();
    }
    renderer.drawText(UI_12_FONT_ID, MARGIN, y, line);
  }
}

int DeviceTestActivity::measure(void* context, const uint8_t font, const std::string_view text) {
  auto* self = static_cast<DeviceTestActivity*>(context);
  if (text.empty()) return 0;
  char buffer[rsvp::FRAME_TEXT_CAPACITY + 1];
  const size_t length = text.size() < rsvp::FRAME_TEXT_CAPACITY ? text.size() : rsvp::FRAME_TEXT_CAPACITY;
  memcpy(buffer, text.data(), length);
  buffer[length] = '\0';
  return self->renderer.getTextWidth(BAND_FONTS[font], buffer, BAND_STYLE);
}

bool DeviceTestActivity::fits(void* context, const std::string_view text, const uint16_t focusOffset) {
  auto* self = static_cast<DeviceTestActivity*>(context);
  return rsvpui::fitsOnColumn(self->band, &DeviceTestActivity::measure, context, 0, text, focusOffset,
                              rsvpui::codePointBytes(text, focusOffset));
}

// ---------------------------------------------------------------------------------------------
// Main task

void DeviceTestActivity::loop() {
  lastLoopMs.store(static_cast<uint32_t>(millis()));
  switch (screen.load()) {
    case Screen::Controls:
      loopControls();
      return;
    case Screen::Step:
      loopStep();
      return;
    case Screen::Look:
      loopLook();
      return;
    default:
      loopList();
      return;
  }
}

void DeviceTestActivity::show(const Screen next, const uint8_t rows, const uint8_t repeatMask, const bool clearing) {
  // The caller holds the RenderLock.
  screen.store(next);
  list.reset(rows, 0, repeatMask);
  needClear = clearing;
  guardRestartPending.store(true);
  requestUpdate();
}

void DeviceTestActivity::loopList() {
  if (guardRestartPending.exchange(false)) controlInput.restartGuard();
  const bool wasArmed = controlInput.armed();
  const controls::Event e = controlInput.pollFocus();
  if (!wasArmed && controlInput.armed()) LOG_DBG("CTL", "DeviceTest armed screen=%s", screenName());
  ChoiceList::Outcome o;
  {
    RenderLock lock;
    o = list.handle(e);
  }
  switch (o.kind) {
    case ChoiceList::Kind::MOVED:
      LOG_DBG("CTL", "DeviceTest focus=%s:%u", screenName(), static_cast<unsigned>(o.row));
      requestUpdate();
      return;
    case ChoiceList::Kind::CHOSEN:
    case ChoiceList::Kind::REPEATED:
      LOG_DBG("CTL", "DeviceTest select=%s:%u", screenName(), static_cast<unsigned>(o.row));
      choose(o.row);
      return;
    case ChoiceList::Kind::BACK:
      back();
      return;
    case ChoiceList::Kind::NONE:
      return;
  }
}

void DeviceTestActivity::choose(const uint8_t row) {
  RenderLock lock;
  switch (screen.load()) {
    case Screen::Menu:
      LOG_DBG("DTS", "menu %s", row < MENU_ROWS ? MENU_LOG_NAMES[row] : "?");
      switch (static_cast<MenuRow>(row)) {
        case MenuRow::ControlsCheck:
          startControls();
          return;
        case MenuRow::Benchmark:
          show(Screen::Temperature, 5, 0b0110, false);
          return;
        case MenuRow::ClearingRefresh:
          pendingClearing = true;
          requestUpdate();
          return;
        case MenuRow::Leave:
          lock.unlock();
          finish();
          return;
      }
      return;
    case Screen::Temperature:
      if (row == 0) {
        planBenchmark();
        show(Screen::Overview, 2, 0, false);
      } else if (row == 1 || row == 2) {
        if (!temperatureKnown) {
          temperatureKnown = true;
          temperatureC = TEMPERATURE_START;
        } else if (row == 1 && temperatureC < TEMPERATURE_MAX) {
          ++temperatureC;
        } else if (row == 2 && temperatureC > TEMPERATURE_MIN) {
          --temperatureC;
        }
        LOG_DBG("DTS", "temperature %d known=1", temperatureC);
        requestUpdate();
      } else if (row == 3) {
        temperatureKnown = false;
        LOG_DBG("DTS", "temperature unknown");
        requestUpdate();
      } else {
        show(Screen::Menu, MENU_ROWS, 0, false);
      }
      return;
    case Screen::Overview:
      if (row == 0) {
        startBenchmark();
      } else {
        show(Screen::Menu, MENU_ROWS, 0, false);
      }
      return;
    case Screen::Rating: {
      const auto choice = static_cast<RatingChoice>(row);
      static constexpr const char* NAMES[RATING_CHOICES] = {"repeat", "clean", "tolerable", "unreadable",
                                                            "unreadable-skip"};
      LOG_INF("DTS", "rating profile=%s step=%s choice=%s attempt=%u", profileInfo(sweep.profile().id).key,
              sweep.stepSpec().key, NAMES[row < RATING_CHOICES ? row : 0],
              static_cast<unsigned>(sweep.stepRecord().attempts));
      go(sweep.rate(choice));
      return;
    }
    case Screen::Residue: {
      static constexpr const char* NAMES[RESIDUE_CHOICES] = {"repeat", "no", "yes"};
      LOG_INF("DTS", "residue profile=%s step=%s choice=%s", profileInfo(sweep.profile().id).key, sweep.stepSpec().key,
              NAMES[row < RESIDUE_CHOICES ? row : 0]);
      go(sweep.residue(static_cast<ResidueChoice>(row)));
      return;
    }
    case Screen::Stopped: {
      static constexpr const char* NAMES[STOP_CHOICES] = {"repeat", "skip-profile", "end-run"};
      LOG_INF("DTS", "stopped profile=%s step=%s choice=%s", profileInfo(sweep.profile().id).key, sweep.stepSpec().key,
              NAMES[row < STOP_CHOICES ? row : 0]);
      go(sweep.stopped(static_cast<StopChoice>(row)));
      return;
    }
    case Screen::ProfileSummary:
      LOG_DBG("DTS", "summary profile=%s choice=%s", profileInfo(sweep.profile().id).key, row == 0 ? "next" : "end-run");
      go(sweep.summary(row == 0 ? SummaryChoice::NEXT : SummaryChoice::END_RUN));
      return;
    case Screen::RunSummary:
      show(Screen::Menu, MENU_ROWS, 0, false);
      return;
    case Screen::Controls:
    case Screen::Step:
    case Screen::Look:
      return;
  }
}

void DeviceTestActivity::back() {
  const Screen s = screen.load();
  if (s == Screen::Menu) {
    finish();
    return;
  }
  if (s == Screen::Temperature || s == Screen::Overview || s == Screen::RunSummary) {
    RenderLock lock;
    show(Screen::Menu, MENU_ROWS, 0, false);
  }
  // Prompts during a benchmark need an answer: the Back gesture does nothing there.
}

// --- Controls check ---------------------------------------------------------------------------

void DeviceTestActivity::startControls() {
  // The caller holds the RenderLock.
  controlsRecord = ControlsRecord{};
  controlsSaved = allocateRun(ResultKind::CONTROLS, controlsRecord.header);
  controlsRecord.status = RunStatus::RUNNING;
  controlsRecord.sideButtonLayout = SETTINGS.sideButtonLayout;
  controlsRecord.shortPowerButton = SETTINGS.shortPwrBtn;
  controlsRecord.keys = controls::sideKeys(controls::KEYS, sideLayoutUsable());
  controlsRecord.confirmSelects = controls::KEYS.confirmSelects;
  controlsActive = true;
  controlsDirty = true;
  keys.restart(static_cast<uint32_t>(millis()));
  LOG_INF("DTS", "controls start run=%u file=%s/%s side_layout=%s short_power=%s", controlsRecord.header.run,
          RESULTS_DIR, controlsRecord.header.file, sideLayoutName(controlsRecord.sideButtonLayout),
          shortPowerName(controlsRecord.shortPowerButton));
  show(Screen::Controls, 1, 0, true);
}

void DeviceTestActivity::recordEvent(const ControlEvent& event) {
  RenderLock lock;
  if (controlsRecord.count < MAX_CONTROL_EVENTS) {
    controlsRecord.events[controlsRecord.count++] = event;
  } else {
    ++controlsRecord.dropped;
  }
  char text[LINE_BYTES];
  describeEvent(event, text, sizeof(text));
  LOG_INF("DTS", "controls event n=%lu %s", static_cast<unsigned long>(controlsRecord.count + controlsRecord.dropped),
          text);
  controlsDirty = true;
  lastEventMs = static_cast<uint32_t>(millis());
  requestUpdate();
}

void DeviceTestActivity::loopControls() {
  const uint32_t now = static_cast<uint32_t>(millis());
  KeyFrame f;
  f.nowMs = now;
  for (uint8_t i = 0; i < RAW_KEY_COUNT; ++i) {
    const uint8_t bit = static_cast<uint8_t>(1u << i);
    if (gpio.wasPressed(i)) f.pressed |= bit;
    if (gpio.wasReleased(i)) f.released |= bit;
    if (gpio.isPressed(i)) f.down |= bit;
  }
  for (uint8_t b = 0; b < LOGICAL_COUNT; ++b) {
    if (mappedInput.wasPressed(static_cast<Btn>(b))) f.logicalPressed |= logicalBit(b);
    if (mappedInput.wasReleased(static_cast<Btn>(b))) f.logicalReleased |= logicalBit(b);
  }
  const bool wasArmed = keys.armed();
  ControlEvent events[RAW_KEY_COUNT];
  const size_t n = keys.feed(f, controlsRecord.keys, controlsRecord.confirmSelects, events, RAW_KEY_COUNT);
  if (!wasArmed && keys.armed()) LOG_DBG("CTL", "DeviceTest armed screen=controls");
  for (size_t i = 0; i < n; ++i) recordEvent(events[i]);
  if (keys.exitRequested()) {
    // The held key's release belongs to this gesture: hand it to MappedInputManager so the menu never
    // sees it (as ControlInput does after a hold).
    for (const Btn b : {Btn::PageBack, Btn::PageForward, Btn::Up, Btn::Down, Btn::Confirm}) mappedInput.wasLongPressed(b, 0);
    finishControls(RunStatus::COMPLETE);
    return;
  }

  if (keys.armed()) {
    ControlEvent e;
    e.atMs = now;
    e.source = EventSource::TOUCH;
    int x = 0;
    int y = 0;
    bool touched = false;
    if (mappedInput.wasBackGesture()) {
      e.kind = EventKind::BACK_GESTURE;
      e.logical = logicalBit(0);
      touched = true;
    } else if (const auto dir = mappedInput.wasSwipe(&x, &y); dir != MappedInputManager::SwipeDir::None) {
      e.kind = dir == MappedInputManager::SwipeDir::Left    ? EventKind::SWIPE_LEFT
               : dir == MappedInputManager::SwipeDir::Right ? EventKind::SWIPE_RIGHT
               : dir == MappedInputManager::SwipeDir::Up    ? EventKind::SWIPE_UP
                                                            : EventKind::SWIPE_DOWN;
      e.x = static_cast<int16_t>(x);
      e.y = static_cast<int16_t>(y);
      touched = true;
    } else if (mappedInput.wasScreenLongPress(x, y)) {
      e.kind = EventKind::LONG_PRESS;
      e.x = static_cast<int16_t>(x);
      e.y = static_cast<int16_t>(y);
      touched = true;
    } else if (mappedInput.wasScreenTapped(x, y)) {
      e.kind = EventKind::TAP;
      e.x = static_cast<int16_t>(x);
      e.y = static_cast<int16_t>(y);
      touched = true;
    }
    if (touched) {
      recordEvent(e);
      if (e.kind == EventKind::TAP && controls::hit(controls::CONTROLS_CHECK, x, y) != nullptr) {
        LOG_DBG("CTL", "DeviceTest select=controls:finish");
        finishControls(RunStatus::COMPLETE);
        return;
      }
    }
  }

  // Written when the owner pauses, so a write never delays reading a quick press.
  if (controlsDirty && controlsRecord.count > 0 && reached(now, lastEventMs + CONTROLS_FLUSH_IDLE_MS)) {
    RenderLock lock;
    saveControls();
  }
}

void DeviceTestActivity::finishControls(const RunStatus status) {
  RenderLock lock;
  controlsRecord.status = status;
  saveControls();
  controlsActive = false;
  LOG_INF("DTS", "controls end run=%u events=%lu status=%s", controlsRecord.header.run,
          static_cast<unsigned long>(controlsRecord.count), runStatusName(status));
  show(Screen::Menu, MENU_ROWS, 0, true);
}

// --- Benchmark --------------------------------------------------------------------------------

void DeviceTestActivity::planBenchmark() {
  // The caller holds the RenderLock. What will run, for the overview; startBenchmark() plans again.
  sweep.plan(backend.panel(), display.isInverted());
}

void DeviceTestActivity::startBenchmark() {
  // The caller holds the RenderLock.
  const displayprofile::TrialMask runnable = sweep.plan(backend.panel(), display.isInverted());
  bench = BenchmarkRecord{};
  benchSaved = allocateRun(ResultKind::BENCHMARK, bench.header);
  bench.header.conditions.hasTemperature = true;
  bench.header.conditions.temperatureKnown = temperatureKnown;
  bench.header.conditions.temperatureC = temperatureC;

  // README.md, "What the benchmark stores": the trials about to run are unlocked; the chosen profile
  // is left as it was.
  bench.trialSettingBefore = SETTINGS.rsvpDisplayProfile;
  const displayprofile::TrialSetting after =
      unlockForBenchmark(displayprofile::decodeTrialSetting(bench.trialSettingBefore), runnable);
  bench.trialSettingAfter = displayprofile::encodeTrialSetting(after);
  if (bench.trialSettingAfter != SETTINGS.rsvpDisplayProfile) {
    SETTINGS.rsvpDisplayProfile = bench.trialSettingAfter;
    const bool ok = SETTINGS.saveToFile();
    LOG_DBG("DTS", "trial setting stored %u ok=%d", static_cast<unsigned>(bench.trialSettingAfter), ok ? 1 : 0);
  }
  backend.setUnlockedTrials(after.unlocked);

  char run[64] = {};
  char notRun[96] = {};
  size_t r = 0;
  size_t nr = 0;
  for (const ProfileRecord& p : sweep.profiles) {
    if (p.status == ProfileStatus::NOT_ALLOWED) {
      const int w = snprintf(notRun + nr, sizeof(notRun) - nr, "%s%s:%s", nr ? "," : "", profileInfo(p.id).key,
                             notRunReasonName(p.reason));
      if (w > 0 && static_cast<size_t>(w) < sizeof(notRun) - nr) nr += static_cast<size_t>(w);
    } else {
      const int w = snprintf(run + r, sizeof(run) - r, "%s%s", r ? "," : "", profileInfo(p.id).key);
      if (w > 0 && static_cast<size_t>(w) < sizeof(run) - r) r += static_cast<size_t>(w);
    }
  }
  LOG_INF("DTS",
          "bench start run=%u file=%s/%s panel=%s controller=%s temperature=%d known=%d battery=%u profiles=%s "
          "not_run=%s setting_before=%u setting_after=%u",
          bench.header.run, RESULTS_DIR, bench.header.file, displayprofile::panelName(backend.panel()),
          displayprofile::controllerName(displayprofile::panelRecord(backend.panel()).controller), temperatureC,
          temperatureKnown ? 1 : 0, static_cast<unsigned>(bench.header.conditions.batteryPercent), run,
          nr ? notRun : "none", static_cast<unsigned>(bench.trialSettingBefore),
          static_cast<unsigned>(bench.trialSettingAfter));
  go(sweep.start());
}

void DeviceTestActivity::go(Next next) {
  // The caller holds the RenderLock. Every answer and every finished series is written at once.
  while (next == Next::RUN_STEP && !backend.select(sweep.profile().id)) {
    LOG_ERR("DTS", "profile %s refused by the backend", profileInfo(sweep.profile().id).key);
    next = sweep.refuse();
  }
  benchSaved = saveBenchmark() && benchSaved;
  switch (next) {
    case Next::RUN_STEP:
      screen.store(Screen::Step);
      stepPending = true;
      seriesDone.store(false);
      wakeValid.store(false);
      requestUpdate();
      return;
    case Next::ASK_RATING:
      screen.store(Screen::Look);
      return;
    case Next::ASK_RESIDUE:
      show(Screen::Residue, RESIDUE_CHOICES, 0, false);
      return;
    case Next::PROFILE_SUMMARY:
      // Drawn with a clearing refresh: after a trial, and after a skipped profile, it is the clearing
      // refresh the plan's stop conditions ask for.
      show(Screen::ProfileSummary, sweep.nextProfile() != nullptr ? 2 : 1, 0, true);
      return;
    case Next::RUN_SUMMARY:
      endBenchmark();
      return;
  }
}

void DeviceTestActivity::endBenchmark() {
  // The caller holds the RenderLock.
  backend.select(ProfileId::STOCK);
  LOG_INF("DTS", "bench end run=%u status=%s saved=%d", bench.header.run, runStatusName(sweep.status()),
          benchSaved ? 1 : 0);
  show(Screen::RunSummary, 1, 0, true);
}

void DeviceTestActivity::loopStep() {
  const controls::Event e = controlInput.pollPlaying();
  if (e.ev != controls::Ev::None && seriesActive.load()) {
    runner.requestStop();
    LOG_DBG("DTS", "step stop requested by=%s",
            e.ev == controls::Ev::TouchDown ? "touch" : (e.ev == controls::Ev::KeyDown ? "key" : "gesture"));
  }
  if (seriesDone.load()) {
    seriesEnded();
    return;
  }
  if (wakeValid.load() && reached(clock.nowMs(), wakeAtMs.load()) && !RenderLock::peek()) requestUpdate();
}

void DeviceTestActivity::seriesEnded() {
  RenderLock lock;
  seriesDone.store(false);
  seriesActive.store(false);
  const StepFigures f = runner.figures();
  const bool stopped = runner.stopped();
  logResult(f, stopped);
  if (stopped) {
    sweep.seriesStopped(f);
    // Drawn with a clearing refresh, which also ends a trial's run.
    show(Screen::Stopped, STOP_CHOICES, 0, true);
    return;
  }
  go(sweep.seriesDone(f));
}

void DeviceTestActivity::loopLook() {
  const controls::Event e = controlInput.pollPlaying();
  if (e.ev == controls::Ev::None) return;
  LOG_DBG("DTS", "look done by=%s", e.ev == controls::Ev::TouchDown ? "touch" : "key");
  RenderLock lock;
  // The clearing refresh that draws the prompt also ends a trial's run.
  show(Screen::Rating, RATING_CHOICES, 0, true);
}

void DeviceTestActivity::logResult(const StepFigures& f, const bool stopped) {
  // The same figures as the results file, at once: a failed SD write does not lose them (plan 10).
  const StepSpec& spec = sweep.stepSpec();
  const char* key = profileInfo(sweep.profile().id).key;
  LOG_INF("DTS",
          "result profile=%s step=%s outcome=%s attempt=%u updates=%lu median=%lu p95=%lu max=%lu min=%lu mean=%lu "
          "timeouts=%lu clears=%lu clear_median=%lu clear_max=%lu clear_timeouts=%lu promotions=%lu duration_ms=%lu",
          key, spec.key, stopped ? "stopped" : "complete", static_cast<unsigned>(sweep.stepRecord().attempts + 1),
          static_cast<unsigned long>(f.updates.count), static_cast<unsigned long>(f.updates.medianMs),
          static_cast<unsigned long>(f.updates.p95Ms), static_cast<unsigned long>(f.updates.maxMs),
          static_cast<unsigned long>(f.updates.minMs), static_cast<unsigned long>(f.updates.meanMs),
          static_cast<unsigned long>(f.updates.timeouts), static_cast<unsigned long>(f.clears.count),
          static_cast<unsigned long>(f.clears.medianMs), static_cast<unsigned long>(f.clears.maxMs),
          static_cast<unsigned long>(f.clears.timeouts), static_cast<unsigned long>(f.promotions),
          static_cast<unsigned long>(f.durationMs));
  if (f.hasPlayback) {
    const PlaybackFigures& p = f.playback;
    LOG_INF("DTS",
            "result profile=%s step=%s target=%u words_per_update=%u delivered=%lu.%lu words=%lu frames=%lu "
            "clearing=%lu active_ms=%lu order=%s drawn=%lu segments=%lu violations=%lu gaps=%lu repeats=%lu",
            key, spec.key, spec.wpm, spec.wordsPerUpdate, static_cast<unsigned long>(p.deliveredWpmTenths / 10),
            static_cast<unsigned long>(p.deliveredWpmTenths % 10), static_cast<unsigned long>(p.wordsShown),
            static_cast<unsigned long>(p.frames), static_cast<unsigned long>(p.clearingRefreshes),
            static_cast<unsigned long>(p.activeMs), p.inOrder ? "ok" : "violated",
            static_cast<unsigned long>(p.wordsDrawn), static_cast<unsigned long>(p.segments),
            static_cast<unsigned long>(p.violations), static_cast<unsigned long>(p.gaps),
            static_cast<unsigned long>(p.repeats));
  }
  displayprofile::logStats(backend);
}

// --- Results files ----------------------------------------------------------------------------

bool DeviceTestActivity::allocateRun(const ResultKind kind, RecordHeader& header) {
  // The largest run number in the folder plus one (BenchPlan.h, resultFileName): names sort by the
  // order of the runs without the clock, and no run overwrites another.
  Storage.mkdir(RESULTS_DIR);
  uint16_t highest = 0;
  HalFile dir = Storage.open(RESULTS_DIR);
  if (dir && dir.isDirectory()) {
    for (HalFile f = dir.openNextFile(); f; f = dir.openNextFile()) {
      char name[48] = {};
      f.getName(name, sizeof(name));
      const uint16_t n = runNumberOf(name);
      if (n > highest) highest = n;
      f.close();
    }
  }
  if (dir) dir.close();
  fillHeader(header);
  char path[64];
  for (uint32_t run = highest + 1u; run <= MAX_RUN_NUMBER; ++run) {
    resultFileName(static_cast<uint16_t>(run), kind, header.file, sizeof(header.file));
    snprintf(path, sizeof(path), "%s/%s", RESULTS_DIR, header.file);
    if (Storage.exists(path)) continue;
    header.run = static_cast<uint16_t>(run);
    return true;
  }
  LOG_ERR("DTS", "no free run number in %s", RESULTS_DIR);
  header.run = MAX_RUN_NUMBER;
  resultFileName(MAX_RUN_NUMBER, kind, header.file, sizeof(header.file));
  return false;
}

void DeviceTestActivity::fillHeader(RecordHeader& h) {
  snprintf(h.firmware.version, sizeof(h.firmware.version), "%s", CROSSPOINT_VERSION);
#if defined(SIMULATOR)
  snprintf(h.firmware.elfSha256, sizeof(h.firmware.elfSha256), "%s", "simulator");
#else
  // The ELF SHA-256 the build embeds in the app image (ESP-IDF): identifies the exact firmware.
  if (esp_app_get_elf_sha256(h.firmware.elfSha256, 65) <= 0) {
    snprintf(h.firmware.elfSha256, sizeof(h.firmware.elfSha256), "%s", "unknown");
  }
#endif
  snprintf(h.device.board, sizeof(h.device.board), "%s", BoardConfig::ACTIVE.name ? BoardConfig::ACTIVE.name : "");
  h.device.panel = backend.panel();
  h.device.panelVariant = BoardConfig::ACTIVE.displayControllerVariant;
  const char* driver = display.controllerName();
  snprintf(h.device.driver, sizeof(h.device.driver), "%s", driver ? driver : "none");
  h.device.simulated = backend.panel() == displayprofile::PanelId::SIMULATED;
  h.device.fadingFix = SETTINGS.fadingFix && CrossPointSettings::fadingFixSupported();
  h.device.inverted = display.isInverted();
  h.conditions.batteryPercent = static_cast<uint8_t>(powerManager.getBatteryPercentage());
  h.conditions.usbConnected = gpio.isUsbConnected();
  h.conditions.uptimeMs = static_cast<uint32_t>(millis());
  tm t{};
  if (halClock.isAvailable() && halClock.localTime(t) && t.tm_year + 1900 >= 2024) {
    snprintf(h.conditions.clock, sizeof(h.conditions.clock), "%04d-%02d-%02dT%02d:%02d:%02d", t.tm_year + 1900,
             t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  }
#if defined(SIMULATOR)
  h.simulatorTimeScale = static_cast<uint16_t>(clock.scale());
#else
  h.simulatorTimeScale = 0;
#endif
}

bool DeviceTestActivity::saveBenchmark() {
  char path[64];
  snprintf(path, sizeof(path), "%s/%s", RESULTS_DIR, bench.header.file);
  size_t bytes = 0;
  const bool ok = writeAtomically(path, [this](JsonSink& sink) { writeBenchmark(bench, sweep, sink); }, bytes);
  LOG_INF("DTS", "saved file=%s bytes=%lu status=%s ok=%d", path, static_cast<unsigned long>(bytes),
          runStatusName(sweep.status()), ok ? 1 : 0);
  return ok;
}

bool DeviceTestActivity::saveControls() {
  char path[64];
  snprintf(path, sizeof(path), "%s/%s", RESULTS_DIR, controlsRecord.header.file);
  size_t bytes = 0;
  const bool ok = writeAtomically(path, [this](JsonSink& sink) { writeControls(controlsRecord, sink); }, bytes);
  controlsDirty = false;
  controlsSaved = ok;
  LOG_INF("DTS", "saved file=%s bytes=%lu events=%lu status=%s ok=%d", path, static_cast<unsigned long>(bytes),
          static_cast<unsigned long>(controlsRecord.count), runStatusName(controlsRecord.status), ok ? 1 : 0);
  return ok;
}

const char* DeviceTestActivity::screenName() const {
  switch (screen.load()) {
    case Screen::Menu:
      return "menu";
    case Screen::Controls:
      return "controls";
    case Screen::Temperature:
      return "temperature";
    case Screen::Overview:
      return "overview";
    case Screen::Step:
      return "step";
    case Screen::Look:
      return "look";
    case Screen::Rating:
      return "rating";
    case Screen::Residue:
      return "residue";
    case Screen::Stopped:
      return "stopped";
    case Screen::ProfileSummary:
      return "profile-summary";
    case Screen::RunSummary:
      return "run-summary";
  }
  return "?";
}

#endif  // CROSSPOINT_RSVP_ENABLED
