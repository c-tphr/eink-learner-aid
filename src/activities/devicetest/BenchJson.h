#pragma once

// The results files under /rsvp-bench/ (docs/PLAN.md sections 4 and 10): what they record and their
// JSON encoding. Format "rsvp-bench", version 2 (task D3b added the acceptance and choice files; the
// benchmark and controls layouts are those of version 1); the JSON Schema is
// tools/device/schema/rsvp-bench.schema.json and every member is described in
// tools/device/schema/rsvp-bench.md. Portable: the encoder writes through a JsonSink (a file on the
// device, a string in the host tests) and allocates nothing. Host-tested in test/device_test, where
// the encoded files are also checked against the schema.

#include <cstddef>
#include <cstdint>

#include "Acceptance.h"
#include "BenchPlan.h"
#include "ControlsCheck.h"
#include "ProfileChoice.h"

namespace devicetest {

inline constexpr const char RESULTS_FORMAT[] = "rsvp-bench";
inline constexpr uint32_t RESULTS_VERSION = 2;

struct FirmwareRecord {
  char version[64] = {};    // CROSSPOINT_VERSION, as the About screen shows it
  char elfSha256[72] = {};  // the app image's ELF SHA-256 (ESP-IDF); "simulator" in the simulator
};

struct DeviceRecord {
  char board[32] = {};  // BoardConfig::ACTIVE.name
  PanelId panel = PanelId::UNKNOWN;
  uint8_t panelVariant = 0;  // BoardConfig::ACTIVE.displayControllerVariant (the SDK probe)
  char driver[24] = {};      // HalDisplay::controllerName(), "none" when the build has no SDK driver
  bool simulated = false;
  bool fadingFix = false;  // the "fading fix" setting in force (the renderer's turnOffScreen)
  bool inverted = false;   // the screen was inverted (night mode) when the run started
};

struct ConditionsRecord {
  bool hasTemperature = false;  // benchmark files only
  bool temperatureKnown = false;
  int8_t temperatureC = 0;
  uint8_t batteryPercent = 0;
  bool usbConnected = false;
  char clock[32] = {};  // local time "YYYY-MM-DDTHH:MM:SS" when the device has a clock, "" otherwise
  uint32_t uptimeMs = 0;
};

struct RecordHeader {
  uint16_t run = 0;
  char file[24] = {};
  FirmwareRecord firmware;
  DeviceRecord device;
  ConditionsRecord conditions;
  uint16_t simulatorTimeScale = 0;  // 0 on the device (README.md, "Simulator time scale")
};

// Ten steps' worth of results lives in the Sweep; the stored unlock value before and after.
struct BenchmarkRecord {
  RecordHeader header;
  uint8_t trialSettingBefore = 0;
  uint8_t trialSettingAfter = 0;
};

inline constexpr size_t MAX_CONTROL_EVENTS = 64;

struct ControlsRecord {
  RecordHeader header;
  RunStatus status = RunStatus::RUNNING;
  uint8_t sideButtonLayout = 0;  // CrossPointSettings::sideButtonLayout
  uint8_t shortPowerButton = 0;  // CrossPointSettings::shortPwrBtn
  controls::SideKeys keys{controls::LogicalButton::PageBack, controls::LogicalButton::PageForward};
  bool confirmSelects = true;
  ControlEvent events[MAX_CONTROL_EVENTS];
  size_t count = 0;
  uint32_t dropped = 0;  // events after the first MAX_CONTROL_EVENTS (all are in the serial log)
};

// The guided acceptance (Acceptance.h): the walk-through's answers live in the Walkthrough.
struct AcceptanceRecord {
  RecordHeader header;
  ProfileId profile = ProfileId::STOCK;  // the profile Part A plays with: the chosen one, as RSVP resolves it
  uint8_t trialSetting = 0;              // the stored byte when the walk-through started
};

// One "Choose a profile" (ProfileChoice.h).
struct ChoiceRecord {
  RecordHeader header;
  ProfileId chosen = ProfileId::STOCK;
  ProfileList candidates;  // what the screen offered
  uint8_t settingBefore = 0;
  uint8_t settingAfter = 0;
  TrialMask keptBefore = 0;
  TrialMask keptAfter = 0;
  TrialMask locked = 0;
  bool saved = false;  // the settings file was written
  BenchRatings bench;  // the ratings the choice was based on
};

class JsonSink {
 public:
  virtual ~JsonSink() = default;
  virtual void write(const char* data, size_t length) = 0;
};

void writeBenchmark(const BenchmarkRecord& record, const Sweep& sweep, JsonSink& sink);
void writeControls(const ControlsRecord& record, JsonSink& sink);
void writeAcceptance(const AcceptanceRecord& record, const Walkthrough& walk, JsonSink& sink);
void writeChoice(const ChoiceRecord& record, JsonSink& sink);

// Names used in the files.
const char* sideLayoutName(uint8_t layout);  // "prev-next", "next-prev", "disabled", "next-next", "prev-prev"
const char* shortPowerName(uint8_t action);  // "ignore", "sleep", "page-turn", ... "unknown"
const char* logicalButtonName(controls::LogicalButton button);  // "PageBack", "PageForward", "Up", ...

}  // namespace devicetest
