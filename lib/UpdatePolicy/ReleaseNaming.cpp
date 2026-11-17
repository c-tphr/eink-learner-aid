#include "ReleaseNaming.h"

#include <cstring>

namespace update_release {

namespace {

bool isTagChar(const char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
         c == '+' || c == '-';
}

bool isDigit(const char c) { return c >= '0' && c <= '9'; }

// Reads 1 to 9 digits at *p into value and advances p.
bool readNumber(const char*& p, unsigned long& value) {
  value = 0;
  size_t digits = 0;
  while (isDigit(*p)) {
    if (++digits > 9) return false;
    value = value * 10 + static_cast<unsigned long>(*p - '0');
    p++;
  }
  return digits > 0;
}

}  // namespace

bool formatAssetName(char* out, const size_t capacity, const char* tagName) {
  if (out == nullptr || capacity == 0) return false;
  out[0] = '\0';
  if (tagName == nullptr) return false;
  const size_t tagLen = strlen(tagName);
  if (tagLen == 0 || tagLen > MAX_TAG_LEN) return false;
  for (size_t i = 0; i < tagLen; i++) {
    if (!isTagChar(tagName[i])) return false;
  }
  const size_t prefixLen = sizeof(ASSET_PREFIX) - 1;
  const size_t suffixLen = sizeof(ASSET_SUFFIX) - 1;
  if (prefixLen + tagLen + suffixLen + 1 > capacity) return false;
  memcpy(out, ASSET_PREFIX, prefixLen);
  memcpy(out + prefixLen, tagName, tagLen);
  memcpy(out + prefixLen + tagLen, ASSET_SUFFIX, suffixLen + 1);
  return true;
}

bool parseVersion(const char* text, Version& out) {
  if (text == nullptr) return false;
  const char* p = text;
  if (*p == 'v') p++;
  Version v;
  if (!readNumber(p, v.major) || *p++ != '.') return false;
  if (!readNumber(p, v.minor) || *p++ != '.') return false;
  if (!readNumber(p, v.patch)) return false;
  if (*p == '.') return false;
  out = v;
  return true;
}

bool isNewerRelease(const char* latestTag, const char* currentVersion) {
  Version latest;
  Version current;
  if (!parseVersion(latestTag, latest) || !parseVersion(currentVersion, current)) return false;
  if (latest.major != current.major) return latest.major > current.major;
  if (latest.minor != current.minor) return latest.minor > current.minor;
  if (latest.patch != current.patch) return latest.patch > current.patch;
  return strstr(currentVersion, "-rc") != nullptr;
}

}  // namespace update_release
