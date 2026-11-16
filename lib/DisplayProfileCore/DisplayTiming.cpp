#include "DisplayTiming.h"

namespace displayprofile {

const char* timingSourceName(const TimingSource source) {
  switch (source) {
    case TimingSource::TIMINGS_FILE:
      return "file";
    case TimingSource::ENVIRONMENT:
      return "env";
    case TimingSource::DEFAULTS:
    default:
      return "defaults";
  }
}

bool parseOverrideMs(const char* text, uint32_t& out) {
  if (text == nullptr || *text == '\0') return false;
  uint32_t value = 0;
  size_t digits = 0;
  for (const char* p = text; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') return false;
    if (++digits > 5) return false;
    value = value * 10 + static_cast<uint32_t>(*p - '0');
  }
  if (value > MAX_OVERRIDE_MS) return false;
  out = value;
  return true;
}

OverrideError parseOverrides(const char* updateText, const char* clearText, TimingOverrides& out) {
  TimingOverrides parsed;
  if (updateText != nullptr && *updateText != '\0') {
    if (!parseOverrideMs(updateText, parsed.updateMs)) return OverrideError::BAD_UPDATE;
    parsed.hasUpdate = true;
  }
  if (clearText != nullptr && *clearText != '\0') {
    if (!parseOverrideMs(clearText, parsed.clearMs)) return OverrideError::BAD_CLEAR;
    parsed.hasClear = true;
  }
  out = parsed;
  return OverrideError::NONE;
}

FileError checkTimingFile(const TimingFile& file) {
  bool seen[PROFILE_COUNT] = {};
  for (size_t i = 0; i < file.count; ++i) {
    const FileProfileTiming& entry = file.profiles[i];
    const auto index = static_cast<size_t>(entry.id);
    if (index >= PROFILE_COUNT) return FileError::UNKNOWN_PROFILE;
    if (seen[index]) return FileError::DUPLICATE_PROFILE;
    seen[index] = true;
    if (entry.updateMedianMs == 0 || entry.updateMedianMs > MAX_FILE_UPDATE_MS) return FileError::UPDATE_OUT_OF_RANGE;
    if (entry.clearMedianMs == 0 || entry.clearMedianMs > MAX_FILE_CLEAR_MS) return FileError::CLEAR_OUT_OF_RANGE;
  }
  if (file.count > 0 && !seen[static_cast<size_t>(ProfileId::STOCK)]) return FileError::NO_STOCK;
  return FileError::NONE;
}

const char* fileErrorText(const FileError error) {
  switch (error) {
    case FileError::NONE:
      return "ok";
    case FileError::NO_STOCK:
      return "no stock profile";
    case FileError::DUPLICATE_PROFILE:
      return "a profile is listed twice";
    case FileError::UNKNOWN_PROFILE:
      return "unknown profile";
    case FileError::UPDATE_OUT_OF_RANGE:
      return "word-update median out of range";
    case FileError::CLEAR_OUT_OF_RANGE:
    default:
      return "clear median out of range";
  }
}

TimingModel::TimingModel() {
  for (size_t i = 0; i < PROFILE_COUNT; ++i) entries[i].timing = PROFILES[i].simulatorDefault;
}

bool TimingModel::resolve(const TimingFile& file, const TimingOverrides& overrides) {
  *this = TimingModel();
  if (checkTimingFile(file) != FileError::NONE) return false;

  const FileProfileTiming* fileEntry[PROFILE_COUNT] = {};
  for (size_t i = 0; i < file.count; ++i) fileEntry[static_cast<size_t>(file.profiles[i].id)] = &file.profiles[i];
  const FileProfileTiming* fileStock = fileEntry[static_cast<size_t>(ProfileId::STOCK)];

  for (size_t i = 0; i < PROFILE_COUNT; ++i) {
    ResolvedTiming& entry = entries[i];
    if (overrides.hasUpdate) {
      entry.timing.updateMs = overrides.updateMs;
      entry.updateSource = TimingSource::ENVIRONMENT;
    } else if (fileEntry[i] != nullptr) {
      entry.timing.updateMs = fileEntry[i]->updateMedianMs;
      entry.updateSource = TimingSource::TIMINGS_FILE;
    }

    if (overrides.hasClear) {
      entry.timing.clearMs = overrides.clearMs;
      entry.clearSource = TimingSource::ENVIRONMENT;
    } else if (fileEntry[i] != nullptr) {
      entry.timing.clearMs = fileEntry[i]->clearMedianMs;
      entry.clearSource = TimingSource::TIMINGS_FILE;
    } else if (fileStock != nullptr) {
      entry.timing.clearMs = fileStock->clearMedianMs;
      entry.clearSource = TimingSource::TIMINGS_FILE;
    }
  }
  return true;
}

const ResolvedTiming& TimingModel::forProfile(const ProfileId id) const {
  const auto index = static_cast<size_t>(id);
  return entries[index < PROFILE_COUNT ? index : 0];
}

}  // namespace displayprofile
