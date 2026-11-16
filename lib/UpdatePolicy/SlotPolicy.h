#pragma once

// Self-update slot policy (task D4; spike S3, section 6). Pure: no ESP-IDF, no Arduino, no I/O.
//
// The firmware may update itself (from the SD card or over Wi-Fi) only when the RUNNING partition
// table has exactly two OTA app slots, no factory app, one otadata partition, and an update would
// go to the other slot. The stock Picco table (partitions_picco.csv: factory `safeboot` plus one
// ota_0 slot) fails; the project's two-slot table (partitions_picco_2slot.csv) passes from either
// slot. src/update/ fills a Layout from the ESP-IDF partition API at run time; the host tests in
// test/update_policy feed it tables written out by hand.

#include <cstddef>
#include <cstdint>

namespace update_policy {

// Type and subtype codes as ESP-IDF stores them in the partition table (esp_partition.h,
// gen_esp32part.py).
inline constexpr uint8_t TYPE_APP = 0x00;
inline constexpr uint8_t TYPE_DATA = 0x01;
inline constexpr uint8_t SUBTYPE_APP_FACTORY = 0x00;
inline constexpr uint8_t SUBTYPE_APP_OTA_0 = 0x10;
inline constexpr uint8_t SUBTYPE_APP_OTA_15 = 0x1F;
inline constexpr uint8_t SUBTYPE_DATA_OTA = 0x00;  // otadata
// otadata holds two 4 KiB boot-selection records (ESP-IDF bootloader, src/network/OtaBootSwitch.cpp).
inline constexpr uint32_t OTADATA_MIN_SIZE = 0x2000;

struct Partition {
  uint8_t type;
  uint8_t subtype;
  uint32_t offset;
  uint32_t size;
};

// The running partition table and the two slots that matter. Partitions are identified by their
// offset, which is unique within a table.
struct Layout {
  const Partition* partitions = nullptr;  // every partition of the running table
  size_t count = 0;
  bool complete = false;  // false when the reader could not list every partition
  bool hasRunning = false;
  uint32_t runningOffset = 0;  // esp_ota_get_running_partition()
  bool hasNext = false;
  uint32_t nextOffset = 0;  // esp_ota_get_next_update_partition(nullptr)
};

enum class Verdict : uint8_t {
  Allowed,
  TableIncomplete,    // the partition list could not be read in full
  NonOtaApp,          // a factory or test app partition exists (the stock Picco's `safeboot`)
  NotTwoSlots,        // not exactly two ota_N app partitions (OtaBootSwitch assumes two)
  NoOtadata,          // no otadata partition, more than one, or smaller than 0x2000
  NotRunningFromOta,  // the running partition is not one of the two OTA slots
  NoNextSlot,         // no next update partition, or it is not one of the two OTA slots
  NextIsRunning,      // the next update slot is the running slot
  SlotTooSmall,       // the next update slot is smaller than the running slot
  TargetIsRunning,    // checkWrite: the partition about to be written is the running one
  TargetNotNext,      // checkWrite: the partition about to be written is not the next update slot
};

// "Is self-update allowed on this table?" Allowed only when every rule above holds.
Verdict decide(const Layout& layout);

// "May this partition be erased and written as the update target?" decide() must say Allowed,
// and the target must be the next update slot and not the running one. Called at the point of
// writing by both update paths.
Verdict checkWrite(const Layout& layout, uint32_t targetOffset);

// Short stable name for logs ("Allowed", "NonOtaApp", ...).
const char* verdictName(Verdict verdict);

}  // namespace update_policy
