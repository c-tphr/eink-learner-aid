#include "SlotPolicy.h"

namespace update_policy {

namespace {

bool isOtaApp(const Partition& p) {
  return p.type == TYPE_APP && p.subtype >= SUBTYPE_APP_OTA_0 && p.subtype <= SUBTYPE_APP_OTA_15;
}

const Partition* findOtaApp(const Layout& layout, const uint32_t offset) {
  for (size_t i = 0; i < layout.count; i++) {
    const Partition& p = layout.partitions[i];
    if (isOtaApp(p) && p.offset == offset) return &p;
  }
  return nullptr;
}

}  // namespace

Verdict decide(const Layout& layout) {
  if (!layout.complete || (layout.count > 0 && layout.partitions == nullptr)) return Verdict::TableIncomplete;

  size_t otaApps = 0;
  size_t otherApps = 0;
  size_t otadata = 0;
  bool otadataLargeEnough = true;
  for (size_t i = 0; i < layout.count; i++) {
    const Partition& p = layout.partitions[i];
    if (p.type == TYPE_APP) {
      if (isOtaApp(p)) {
        otaApps++;
      } else {
        otherApps++;
      }
    } else if (p.type == TYPE_DATA && p.subtype == SUBTYPE_DATA_OTA) {
      otadata++;
      if (p.size < OTADATA_MIN_SIZE) otadataLargeEnough = false;
    }
  }
  if (otherApps != 0) return Verdict::NonOtaApp;
  if (otaApps != 2) return Verdict::NotTwoSlots;
  if (otadata != 1 || !otadataLargeEnough) return Verdict::NoOtadata;

  const Partition* running = layout.hasRunning ? findOtaApp(layout, layout.runningOffset) : nullptr;
  if (running == nullptr) return Verdict::NotRunningFromOta;
  const Partition* next = layout.hasNext ? findOtaApp(layout, layout.nextOffset) : nullptr;
  if (next == nullptr) return Verdict::NoNextSlot;
  if (next == running) return Verdict::NextIsRunning;
  if (next->size < running->size) return Verdict::SlotTooSmall;
  return Verdict::Allowed;
}

Verdict checkWrite(const Layout& layout, const uint32_t targetOffset) {
  const Verdict verdict = decide(layout);
  if (verdict != Verdict::Allowed) return verdict;
  if (targetOffset == layout.runningOffset) return Verdict::TargetIsRunning;
  if (targetOffset != layout.nextOffset) return Verdict::TargetNotNext;
  return Verdict::Allowed;
}

const char* verdictName(const Verdict verdict) {
  switch (verdict) {
    case Verdict::Allowed:
      return "Allowed";
    case Verdict::TableIncomplete:
      return "TableIncomplete";
    case Verdict::NonOtaApp:
      return "NonOtaApp";
    case Verdict::NotTwoSlots:
      return "NotTwoSlots";
    case Verdict::NoOtadata:
      return "NoOtadata";
    case Verdict::NotRunningFromOta:
      return "NotRunningFromOta";
    case Verdict::NoNextSlot:
      return "NoNextSlot";
    case Verdict::NextIsRunning:
      return "NextIsRunning";
    case Verdict::SlotTooSmall:
      return "SlotTooSmall";
    case Verdict::TargetIsRunning:
      return "TargetIsRunning";
    case Verdict::TargetNotNext:
      return "TargetNotNext";
  }
  return "Unknown";
}

}  // namespace update_policy
