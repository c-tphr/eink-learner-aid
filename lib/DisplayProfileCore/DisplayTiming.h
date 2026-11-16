#pragma once

// The simulator's timing model: how long each profile's word update and clearing refresh is made
// to take, resolved from three sources in increasing priority (README.md, "Timing model"):
//
//   1. built-in defaults           PROFILES[].simulatorDefault (DisplayProfiles.h)
//   2. the timings file            docs/device/picco-timings.json, validated at build time by
//                                  scripts/display_timings.py and compiled in as a TimingFile
//   3. environment overrides       CROSSPOINT_SIM_UPDATE_MS, CROSSPOINT_SIM_CLEAR_MS (all profiles)
//
// Also the "sleep the remainder" arithmetic. Portable: the firmware reads the environment and the
// generated header and passes plain values in.

#include <cstddef>
#include <cstdint>

#include "DisplayProfiles.h"

namespace displayprofile {

// Largest accepted environment override: a minute. 0 is accepted and means "no added delay".
inline constexpr uint32_t MAX_OVERRIDE_MS = 60000;
// Accepted medians in the timings file (also the schema's maxima; a host test checks they agree).
inline constexpr uint32_t MAX_FILE_UPDATE_MS = 5000;
inline constexpr uint32_t MAX_FILE_CLEAR_MS = 10000;

enum class TimingSource : uint8_t { DEFAULTS, TIMINGS_FILE, ENVIRONMENT };
// "defaults", "file" or "env".
const char* timingSourceName(TimingSource source);

// One profile of the timings file: the measured medians (ms).
struct FileProfileTiming {
  ProfileId id;
  uint32_t updateMedianMs;
  uint32_t clearMedianMs;
};

// The timings file as compiled in. `count` 0 means there is no file.
struct TimingFile {
  const FileProfileTiming* profiles = nullptr;
  size_t count = 0;
  const char* panel = "";   // panel the numbers were measured on
  const char* source = "";  // where they came from
  const char* date = "";    // YYYY-MM-DD

  bool present() const { return count > 0; }
};

// Environment overrides, applied to every profile.
struct TimingOverrides {
  bool hasUpdate = false;
  uint32_t updateMs = 0;
  bool hasClear = false;
  uint32_t clearMs = 0;
};

// Parses one override value: 1 to 5 decimal digits, 0..MAX_OVERRIDE_MS, nothing else (no sign,
// space, unit or fraction). False when malformed; `out` is then unchanged.
bool parseOverrideMs(const char* text, uint32_t& out);

enum class OverrideError : uint8_t { NONE, BAD_UPDATE, BAD_CLEAR };

// Reads both overrides; a null or empty text means "not set". On an error nothing is applied:
// `out` is left as it was.
OverrideError parseOverrides(const char* updateText, const char* clearText, TimingOverrides& out);

// Why a compiled-in timings file is refused (the build validates it first; this is a second check).
enum class FileError : uint8_t {
  NONE,
  NO_STOCK,
  DUPLICATE_PROFILE,
  UNKNOWN_PROFILE,
  UPDATE_OUT_OF_RANGE,
  CLEAR_OUT_OF_RANGE
};
FileError checkTimingFile(const TimingFile& file);
const char* fileErrorText(FileError error);

struct ResolvedTiming {
  ProfileTiming timing;
  TimingSource updateSource = TimingSource::DEFAULTS;
  TimingSource clearSource = TimingSource::DEFAULTS;
};

class TimingModel {
 public:
  // Built-in defaults only.
  TimingModel();

  // Resolves every profile from the three sources. `file` must pass checkTimingFile() (call it
  // first; resolve() refuses a bad file and leaves the model at the defaults).
  //   updateMs: env, else the profile's file median, else the default.
  //   clearMs:  env, else the profile's file median, else the file's stock clear median (a
  //             clearing refresh is the same HALF refresh for every profile), else the default.
  bool resolve(const TimingFile& file, const TimingOverrides& overrides);

  const ResolvedTiming& forProfile(ProfileId id) const;
  ProfileTiming timing(ProfileId id) const { return forProfile(id).timing; }

 private:
  ResolvedTiming entries[PROFILE_COUNT];
};

// How long to sleep after a real refresh that took `elapsedMs` so the call lasts `modelMs`.
// Never negative: 0 when the real call already took as long as the model or longer.
inline constexpr uint32_t remainderToSleepMs(const uint32_t modelMs, const uint32_t elapsedMs) {
  return elapsedMs >= modelMs ? 0 : modelMs - elapsedMs;
}

}  // namespace displayprofile
