// Host tests for update_policy::decide and checkWrite (lib/UpdatePolicy/SlotPolicy.h).
// The main cases read the repository's real partition tables, so a change to one of them that
// would change the verdict on the device fails here.

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "SlotPolicy.h"

using update_policy::checkWrite;
using update_policy::decide;
using update_policy::Layout;
using update_policy::Partition;
using update_policy::Verdict;
using update_policy::verdictName;

namespace {

struct NamedPartition {
  std::string name;
  Partition p;
};

std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  const auto e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

uint8_t subtypeCode(const std::string& type, const std::string& subtype) {
  if (type == "app") {
    if (subtype == "factory") return 0x00;
    if (subtype == "test") return 0x20;
    if (subtype.rfind("ota_", 0) == 0) return static_cast<uint8_t>(0x10 + std::stoi(subtype.substr(4)));
  } else {
    if (subtype == "ota") return 0x00;
    if (subtype == "phy") return 0x01;
    if (subtype == "nvs") return 0x02;
    if (subtype == "coredump") return 0x03;
    if (subtype == "spiffs") return 0x82;
  }
  ADD_FAILURE() << "unknown subtype " << type << "/" << subtype;
  return 0xFF;
}

// Reads a partition CSV the way gen_esp32part.py does for the tables in this repository (every row
// has an explicit offset and size).
std::vector<NamedPartition> readCsv(const std::string& relPath) {
  std::ifstream in(std::string(UPDATE_POLICY_REPO_ROOT) + "/" + relPath);
  EXPECT_TRUE(in.good()) << relPath;
  std::vector<NamedPartition> rows;
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line.substr(0, line.find('#')));
    if (line.empty()) continue;
    std::vector<std::string> f;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) f.push_back(trim(cell));
    EXPECT_GE(f.size(), 5u) << line;
    const uint8_t type = f[1] == "app" ? update_policy::TYPE_APP : update_policy::TYPE_DATA;
    rows.push_back({f[0], Partition{type, subtypeCode(f[1], f[2]), static_cast<uint32_t>(std::stoul(f[3], nullptr, 0)),
                                    static_cast<uint32_t>(std::stoul(f[4], nullptr, 0))}});
  }
  return rows;
}

// A table plus running/next slots, named by partition name ("" = none).
struct Case {
  std::vector<Partition> parts;
  Layout layout;

  Case(const std::vector<NamedPartition>& rows, const std::string& running, const std::string& next) {
    for (const auto& r : rows) parts.push_back(r.p);
    layout.partitions = parts.data();
    layout.count = parts.size();
    layout.complete = true;
    for (const auto& r : rows) {
      if (r.name == running) {
        layout.hasRunning = true;
        layout.runningOffset = r.p.offset;
      }
      if (r.name == next) {
        layout.hasNext = true;
        layout.nextOffset = r.p.offset;
      }
    }
    EXPECT_EQ(layout.hasRunning, !running.empty()) << running;
    EXPECT_EQ(layout.hasNext, !next.empty()) << next;
  }
  Case(const Case&) = delete;
};

std::vector<NamedPartition> twoSlot() { return readCsv("partitions_picco_2slot.csv"); }

constexpr uint32_t APP0 = 0x20000;
constexpr uint32_t APP1 = 0x800000;

}  // namespace

// --- The real tables -----------------------------------------------------------------------------

TEST(SlotPolicy, RealTwoSlotTableHasTheExpectedRows) {
  const auto rows = twoSlot();
  ASSERT_EQ(rows.size(), 5u);
  EXPECT_EQ(rows[2].name, "app0");
  EXPECT_EQ(rows[2].p.offset, APP0);
  EXPECT_EQ(rows[2].p.size, 0x7E0000u);
  EXPECT_EQ(rows[3].name, "app1");
  EXPECT_EQ(rows[3].p.offset, APP1);
  EXPECT_EQ(rows[3].p.size, 0x7E0000u);
  EXPECT_EQ(rows[1].name, "otadata");
  EXPECT_EQ(rows[1].p.offset, 0x15000u);
}

TEST(SlotPolicy, ProposedSpikeTableMatchesTheRootTable) {
  const auto root = twoSlot();
  const auto spike = readCsv("docs/spikes/partitions_picco_2slot.proposed.csv");
  ASSERT_EQ(root.size(), spike.size());
  for (size_t i = 0; i < root.size(); i++) {
    EXPECT_EQ(root[i].name, spike[i].name);
    EXPECT_EQ(root[i].p.type, spike[i].p.type);
    EXPECT_EQ(root[i].p.subtype, spike[i].p.subtype);
    EXPECT_EQ(root[i].p.offset, spike[i].p.offset);
    EXPECT_EQ(root[i].p.size, spike[i].p.size);
  }
}

// Stock Picco: factory safeboot plus one ota_0 slot. Running from ota_0, ESP-IDF's
// esp_ota_get_next_update_partition returns ota_0 again (spike S3, section 1).
TEST(SlotPolicy, StockPiccoTableIsRefused) {
  Case c(readCsv("partitions_picco.csv"), "app", "app");
  EXPECT_EQ(decide(c.layout), Verdict::NonOtaApp);
  EXPECT_EQ(checkWrite(c.layout, 0xE0000), Verdict::NonOtaApp);
}

TEST(SlotPolicy, StockPiccoTableRunningSafebootIsRefused) {
  Case c(readCsv("partitions_picco.csv"), "safeboot", "app");
  EXPECT_EQ(decide(c.layout), Verdict::NonOtaApp);
}

TEST(SlotPolicy, TwoSlotTableRunningApp0IsAllowed) {
  Case c(twoSlot(), "app0", "app1");
  EXPECT_EQ(decide(c.layout), Verdict::Allowed);
  EXPECT_EQ(checkWrite(c.layout, APP1), Verdict::Allowed);
}

TEST(SlotPolicy, TwoSlotTableRunningApp1IsAllowed) {
  Case c(twoSlot(), "app1", "app0");
  EXPECT_EQ(decide(c.layout), Verdict::Allowed);
  EXPECT_EQ(checkWrite(c.layout, APP0), Verdict::Allowed);
}

TEST(SlotPolicy, UpstreamTwoSlotTableIsAllowed) {
  Case c(readCsv("partitions.csv"), "app0", "app1");
  EXPECT_EQ(decide(c.layout), Verdict::Allowed);
}

// --- Each failing condition ------------------------------------------------------------------------

TEST(SlotPolicy, NextEqualToRunningIsRefused) {
  Case c(twoSlot(), "app0", "app0");
  EXPECT_EQ(decide(c.layout), Verdict::NextIsRunning);
  EXPECT_EQ(checkWrite(c.layout, APP0), Verdict::NextIsRunning);
}

TEST(SlotPolicy, MissingNextSlotIsRefused) {
  Case c(twoSlot(), "app0", "");
  EXPECT_EQ(decide(c.layout), Verdict::NoNextSlot);
}

TEST(SlotPolicy, NextSlotThatIsNotAnAppIsRefused) {
  Case c(twoSlot(), "app0", "coredump");
  EXPECT_EQ(decide(c.layout), Verdict::NoNextSlot);
}

TEST(SlotPolicy, UnknownRunningPartitionIsRefused) {
  Case c(twoSlot(), "", "app1");
  EXPECT_EQ(decide(c.layout), Verdict::NotRunningFromOta);
}

TEST(SlotPolicy, RunningFromADataOffsetIsRefused) {
  Case c(twoSlot(), "nvs", "app1");
  EXPECT_EQ(decide(c.layout), Verdict::NotRunningFromOta);
}

TEST(SlotPolicy, MissingOtadataIsRefused) {
  auto rows = twoSlot();
  rows.erase(rows.begin() + 1);
  Case c(rows, "app0", "app1");
  EXPECT_EQ(decide(c.layout), Verdict::NoOtadata);
}

TEST(SlotPolicy, SmallOtadataIsRefused) {
  auto rows = twoSlot();
  rows[1].p.size = 0x1000;
  Case c(rows, "app0", "app1");
  EXPECT_EQ(decide(c.layout), Verdict::NoOtadata);
}

TEST(SlotPolicy, TwoOtadataPartitionsAreRefused) {
  auto rows = twoSlot();
  rows.push_back({"otadata2", Partition{update_policy::TYPE_DATA, update_policy::SUBTYPE_DATA_OTA, 0x17000, 0x2000}});
  Case c(rows, "app0", "app1");
  EXPECT_EQ(decide(c.layout), Verdict::NoOtadata);
}

TEST(SlotPolicy, FactoryAppIsRefusedEvenWithTwoOtaSlots) {
  auto rows = twoSlot();
  rows.push_back({"factory", Partition{update_policy::TYPE_APP, update_policy::SUBTYPE_APP_FACTORY, 0xFE0000, 0x10000}});
  Case running0(rows, "app0", "app1");
  EXPECT_EQ(decide(running0.layout), Verdict::NonOtaApp);
  Case runningFactory(rows, "factory", "app0");
  EXPECT_EQ(decide(runningFactory.layout), Verdict::NonOtaApp);
}

TEST(SlotPolicy, TestAppIsRefused) {
  auto rows = twoSlot();
  rows.push_back({"test", Partition{update_policy::TYPE_APP, 0x20, 0xFE0000, 0x10000}});
  Case c(rows, "app0", "app1");
  EXPECT_EQ(decide(c.layout), Verdict::NonOtaApp);
}

TEST(SlotPolicy, SingleOtaSlotWithoutFactoryIsRefused) {
  auto rows = twoSlot();
  rows.erase(rows.begin() + 3);  // app1
  Case c(rows, "app0", "app0");
  EXPECT_EQ(decide(c.layout), Verdict::NotTwoSlots);
}

TEST(SlotPolicy, ThreeOtaSlotsAreRefused) {
  auto rows = twoSlot();
  rows[2].p.size = 0x500000;
  rows[3].p.offset = 0x520000;
  rows[3].p.size = 0x500000;
  rows.push_back({"app2", Partition{update_policy::TYPE_APP, 0x12, 0xA20000, 0x500000}});
  Case c(rows, "app0", "app1");
  EXPECT_EQ(decide(c.layout), Verdict::NotTwoSlots);
}

TEST(SlotPolicy, SmallerNextSlotIsRefused) {
  auto rows = twoSlot();
  rows[3].p.size = 0x700000;
  Case fromApp0(rows, "app0", "app1");
  EXPECT_EQ(decide(fromApp0.layout), Verdict::SlotTooSmall);
  // The other direction (bigger next slot) is fine.
  Case fromApp1(rows, "app1", "app0");
  EXPECT_EQ(decide(fromApp1.layout), Verdict::Allowed);
}

TEST(SlotPolicy, IncompleteTableIsRefused) {
  Case c(twoSlot(), "app0", "app1");
  c.layout.complete = false;
  EXPECT_EQ(decide(c.layout), Verdict::TableIncomplete);
  Layout empty;
  EXPECT_EQ(decide(empty), Verdict::TableIncomplete);
  Layout nullRows;
  nullRows.complete = true;
  nullRows.count = 3;
  EXPECT_EQ(decide(nullRows), Verdict::TableIncomplete);
}

TEST(SlotPolicy, EmptyCompleteTableIsRefused) {
  Layout empty;
  empty.complete = true;
  EXPECT_EQ(decide(empty), Verdict::NotTwoSlots);
}

// --- checkWrite: the running-slot guard at the point of writing -----------------------------------

TEST(SlotPolicy, WriteToRunningSlotIsRefused) {
  Case c(twoSlot(), "app0", "app1");
  EXPECT_EQ(checkWrite(c.layout, APP0), Verdict::TargetIsRunning);
  Case c1(twoSlot(), "app1", "app0");
  EXPECT_EQ(checkWrite(c1.layout, APP1), Verdict::TargetIsRunning);
}

TEST(SlotPolicy, WriteToAnythingButTheNextSlotIsRefused) {
  Case c(twoSlot(), "app0", "app1");
  EXPECT_EQ(checkWrite(c.layout, 0x9000), Verdict::TargetNotNext);     // nvs
  EXPECT_EQ(checkWrite(c.layout, 0xFF0000), Verdict::TargetNotNext);   // coredump
  EXPECT_EQ(checkWrite(c.layout, 0xE0000), Verdict::TargetNotNext);    // the stock app offset
  EXPECT_EQ(checkWrite(c.layout, APP1 + 0x1000), Verdict::TargetNotNext);
}

TEST(SlotPolicy, VerdictNamesAreDistinct) {
  const Verdict all[] = {Verdict::Allowed,       Verdict::TableIncomplete, Verdict::NonOtaApp,
                         Verdict::NotTwoSlots,   Verdict::NoOtadata,       Verdict::NotRunningFromOta,
                         Verdict::NoNextSlot,    Verdict::NextIsRunning,   Verdict::SlotTooSmall,
                         Verdict::TargetIsRunning, Verdict::TargetNotNext};
  std::vector<std::string> names;
  for (const auto v : all) {
    const std::string n = verdictName(v);
    EXPECT_NE(n, "Unknown");
    for (const auto& seen : names) EXPECT_NE(n, seen);
    names.push_back(n);
  }
  EXPECT_STREQ(verdictName(Verdict::Allowed), "Allowed");
}
