#include "PanelRecords.h"

#include <cstddef>
#include <cstring>

namespace displayprofile {

namespace {

constexpr size_t RECORD_COUNT = sizeof(PANEL_RECORDS) / sizeof(PANEL_RECORDS[0]);

// The table is indexed by search, but keep it in PanelId order and complete: one record per panel
// the SDK probe can report (0..4) plus the simulator.
static_assert(RECORD_COUNT == 6, "one record per PanelId");
static_assert(PANEL_RECORDS[0].panel == PanelId::UNKNOWN && !PANEL_RECORDS[0].recognised,
              "record 0 is the unrecognised fallback");
static_assert(PANEL_RECORDS[4].panel == PanelId::SE0400NQW47 && PANEL_RECORDS[4].controller == Controller::SSD2677,
              "SE0400NQW47 runs on the SSD2677 driver");
static_assert(!PANEL_RECORDS[4].asyncSplit, "the SSD2677 driver has no deferred refresh");

}  // namespace

const char* controllerName(const Controller controller) {
  switch (controller) {
    case Controller::SSD1677:
      return "SSD1677";
    case Controller::SSD2677:
      return "SSD2677";
    case Controller::NONE:
    default:
      return "none";
  }
}

Controller controllerFromName(const char* name) {
  if (name == nullptr) return Controller::NONE;
  if (std::strcmp(name, "SSD1677") == 0) return Controller::SSD1677;
  if (std::strcmp(name, "SSD2677") == 0) return Controller::SSD2677;
  return Controller::NONE;
}

const PanelRecord& panelRecord(const PanelId panel) {
  for (const PanelRecord& record : PANEL_RECORDS) {
    if (record.panel == panel) return record;
  }
  return PANEL_RECORDS[0];
}

PanelId resolvePanel(const uint8_t variant, const char* driverName) {
  const PanelId panel = panelFromVariant(variant);
  if (panel == PanelId::UNKNOWN) return PanelId::UNKNOWN;
  return controllerFromName(driverName) == panelRecord(panel).controller ? panel : PanelId::UNKNOWN;
}

}  // namespace displayprofile
