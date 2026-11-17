#include "UpdateGate.h"

#if CROSSPOINT_RSVP_ENABLED

#include <Logging.h>
#include <ReleaseNaming.h>
#include <SlotPolicy.h>

#ifndef SIMULATOR
#include <esp_ota_ops.h>
#include <esp_partition.h>
#endif

namespace update_gate {

#ifndef SIMULATOR

namespace {

// More than any table this firmware is built for (the two-slot table has 5 rows); a longer table
// is reported as incomplete, which the policy refuses.
constexpr size_t MAX_PARTITIONS = 32;

struct TableSnapshot {
  update_policy::Partition partitions[MAX_PARTITIONS];
  update_policy::Layout layout;
};

// Reads the running table and slots from ESP-IDF (its in-RAM copy of the table; no flash write).
void readTable(TableSnapshot& snap) {
  size_t count = 0;
  bool complete = true;
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  while (it != nullptr) {
    if (count == MAX_PARTITIONS) {
      complete = false;
      esp_partition_iterator_release(it);
      break;
    }
    const esp_partition_t* p = esp_partition_get(it);
    snap.partitions[count++] = {static_cast<uint8_t>(p->type), static_cast<uint8_t>(p->subtype), p->address, p->size};
    it = esp_partition_next(it);  // releases the iterator after the last partition
  }

  update_policy::Layout& layout = snap.layout;
  layout.partitions = snap.partitions;
  layout.count = count;
  layout.complete = complete;
  const esp_partition_t* running = esp_ota_get_running_partition();
  layout.hasRunning = running != nullptr;
  layout.runningOffset = running ? running->address : 0;
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  layout.hasNext = next != nullptr;
  layout.nextOffset = next ? next->address : 0;
}

}  // namespace

bool selfUpdateAllowed() {
  TableSnapshot snap;
  readTable(snap);
  const update_policy::Verdict verdict = update_policy::decide(snap.layout);
  LOG_INF("UPD", "self-update %s: %s (%u partitions, running 0x%x, next 0x%x)",
          verdict == update_policy::Verdict::Allowed ? "allowed" : "not allowed", update_policy::verdictName(verdict),
          static_cast<unsigned>(snap.layout.count), static_cast<unsigned>(snap.layout.runningOffset),
          static_cast<unsigned>(snap.layout.nextOffset));
  return verdict == update_policy::Verdict::Allowed;
}

bool mayWriteUpdateSlot(const uint32_t targetAddress, const char* what) {
  TableSnapshot snap;
  readTable(snap);
  const update_policy::Verdict verdict = update_policy::checkWrite(snap.layout, targetAddress);
  if (verdict != update_policy::Verdict::Allowed) {
    LOG_ERR("UPD", "refusing to write update slot 0x%x for %s: %s (running 0x%x, next 0x%x)",
            static_cast<unsigned>(targetAddress), what ? what : "?", update_policy::verdictName(verdict),
            static_cast<unsigned>(snap.layout.runningOffset), static_cast<unsigned>(snap.layout.nextOffset));
    return false;
  }
  return true;
}

#else  // SIMULATOR: no partition table, so updates are never offered and never written.

bool selfUpdateAllowed() { return false; }

bool mayWriteUpdateSlot(uint32_t /*targetAddress*/, const char* /*what*/) { return false; }

#endif

const char* releaseUrl(const char* /*upstreamUrl*/) { return update_release::LATEST_RELEASE_URL; }

bool formatAssetName(char* out, const size_t capacity, const char* tagName) {
  if (!update_release::formatAssetName(out, capacity, tagName)) {
    LOG_ERR("UPD", "release tag unusable for an asset name: '%s'", tagName ? tagName : "");
    // ReleaseJsonParser would match "" against an asset without a name; GitHub asset names never
    // contain '/', so this matches nothing and the updater reports no update.
    if (capacity >= 2) {
      out[0] = '/';
      out[1] = '\0';
    }
  }
  return true;
}

bool isNewerRelease(const char* latestTag, const char* currentVersion) {
  return update_release::isNewerRelease(latestTag, currentVersion);
}

}  // namespace update_gate

#endif  // CROSSPOINT_RSVP_ENABLED
