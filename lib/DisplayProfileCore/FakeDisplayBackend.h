#pragma once

// Host tests only (it uses std::vector; nothing in the firmware includes it): a DisplayBackend
// driven by a fake clock. It is the simulator's ModelledBackend with a fake presenter and a fake
// sleeper, so a test exercises the same "sleep the remainder" code the simulator runs:
//
//   FakeClock clock;
//   FakeDisplayBackend display(clock, model);   // model: a TimingModel
//   display.realUpdateMs = 5;                   // the "real" refresh before the remainder sleep
//   display.update();                           // clock advances by max(model, 5)
//
// The event log records every select, update and clear with its profile and clock times, so tests
// can check call sequences (for example that a clear runs on pause, as D2's safety rules require).
//
// FakeRefreshPort (at the end) is the device backend's hardware side for TimedBackend tests:
//
//   FakeRefreshPort port(clock);
//   TimedBackend display(port, clock, PanelId::DEPG0397);
//   port.updateTransferMs = 38; port.updateBusyMs = 380;
//   display.update();                           // returns 418, the transfer start to BUSY end

#include <Clock.h>

#include <cstdint>
#include <vector>

#include "DisplayBackend.h"
#include "TimedBackend.h"

namespace displayprofile {

class FakeClock final : public rsvp::Clock {
 public:
  explicit FakeClock(const uint32_t start = 0) : now(start) {}
  uint32_t nowMs() override { return now; }
  void advance(const uint32_t ms) { now += ms; }
  // Moves forward to `t` if it is later (wrap-safe); never moves back.
  void advanceTo(const uint32_t t) {
    if (rsvp::timeBefore(now, t)) now = t;
  }
  uint32_t now;
};

struct FakeEvent {
  enum Kind { SELECT, UPDATE, CLEAR } kind = UPDATE;
  // SELECT: the profile now selected. UPDATE: the profile whose word waveform ran (STOCK for a
  // screen update, or when the allow rule refused the selected trial). CLEAR: the selected profile.
  ProfileId profile = ProfileId::STOCK;
  uint32_t startMs = 0;
  uint32_t endMs = 0;
  ModelledCall call;         // UPDATE and CLEAR only
  RefreshDecision decision;  // UPDATE and CLEAR only: what was asked, and why it ran as it did
};

namespace fake_detail {

// Base-from-member: the presenter and sleeper must exist before ModelledBackend stores references.
struct Parts {
  class Presenter final : public displayprofile::Presenter {
   public:
    explicit Presenter(FakeClock& clock) : clock(clock) {}
    void present(const RefreshKind kind) override {
      ++(kind == RefreshKind::UPDATE ? updates : clears);
      clock.advance(kind == RefreshKind::UPDATE ? realUpdateMs : realClearMs);
    }
    bool inverted() const override { return invertedScreen; }
    FakeClock& clock;
    bool invertedScreen = false;
    uint32_t realUpdateMs = 0;
    uint32_t realClearMs = 0;
    uint32_t updates = 0;
    uint32_t clears = 0;
  };

  class Sleeper final : public displayprofile::Sleeper {
   public:
    explicit Sleeper(FakeClock& clock) : clock(clock) {}
    void sleepMs(const uint32_t ms) override {
      ++calls;
      totalMs += ms;
      clock.advance(ms);
    }
    FakeClock& clock;
    uint32_t calls = 0;
    uint64_t totalMs = 0;
  };

  explicit Parts(FakeClock& clock) : fakePresenter(clock), fakeSleeper(clock) {}
  Presenter fakePresenter;
  Sleeper fakeSleeper;
};

}  // namespace fake_detail

class FakeDisplayBackend final : private fake_detail::Parts, public ModelledBackend {
 public:
  FakeDisplayBackend(FakeClock& clock, const TimingModel& model = TimingModel(), PanelId panel = PanelId::SIMULATED)
      : Parts(clock), ModelledBackend(fakePresenter, clock, fakeSleeper, model, panel), clock(clock) {}

  // The real refresh time the fake presenter spends before the remainder is slept.
  void setRealMs(const uint32_t updateMs, const uint32_t clearMs) {
    fakePresenter.realUpdateMs = updateMs;
    fakePresenter.realClearMs = clearMs;
  }
  // The screen's inversion, as the allow rule sees it.
  void setInverted(const bool inverted) { fakePresenter.invertedScreen = inverted; }
  uint32_t sleeps() const { return fakeSleeper.calls; }
  uint64_t sleptMs() const { return fakeSleeper.totalMs; }

  std::vector<FakeEvent> events;

 protected:
  void onSelect(const ProfileId id) override {
    events.push_back({FakeEvent::SELECT, id, clock.now, clock.now, {}, {}});
  }
  uint32_t presentUpdate(const ProfileId waveform) override {
    return record(FakeEvent::UPDATE, waveform, ModelledBackend::presentUpdate(waveform));
  }
  uint32_t presentClear() override {
    return record(FakeEvent::CLEAR, profile(), ModelledBackend::presentClear());
  }

 private:
  uint32_t record(const FakeEvent::Kind kind, const ProfileId id, const uint32_t ms) {
    events.push_back({kind, id, clock.now - ms, clock.now, lastCall(), lastDecision()});
    return ms;
  }

  FakeClock& clock;
};

// The device backend's hardware side, faked: each refresh advances the fake clock by a transfer
// time and a BUSY time, and a refresh started "elsewhere" can be left pending for finishPending()
// to wait out. The log records the order of the calls, and for each refresh which table (if any)
// was loaded when it started, so tests can check that no table is ever loaded across a clear or a
// stock refresh.
class FakeRefreshPort final : public RefreshPort {
 public:
  enum class Op : uint8_t { FINISH_PENDING, TRANSFER_START, BUSY_END, LOAD_TABLE, RELEASE_TABLE };
  struct Entry {
    Op op;
    RefreshKind kind;
    uint32_t atMs;
    const uint8_t* table = nullptr;  // TRANSFER_START: the table loaded then; LOAD_TABLE: the table
    bool ui = false;                 // TRANSFER_START: refreshUi() (reader flag off)
  };

  explicit FakeRefreshPort(FakeClock& clock) : clock(clock) {}

  void finishPending() override {
    log.push_back({Op::FINISH_PENDING, RefreshKind::UPDATE, clock.now});
    clock.advance(pendingMs);
    pendingMs = 0;
  }
  void refresh(const RefreshKind kind) override { run(kind, false); }
  void refreshUi() override { run(RefreshKind::UPDATE, true); }
  void loadTable(const uint8_t* table) override {
    log.push_back({Op::LOAD_TABLE, RefreshKind::UPDATE, clock.now, table});
    loaded = table;
  }
  void releaseTable() override {
    log.push_back({Op::RELEASE_TABLE, RefreshKind::UPDATE, clock.now});
    loaded = nullptr;
  }
  bool inverted() const override { return invertedScreen; }

  // The entries of one kind, in order.
  std::vector<Entry> only(const Op op) const {
    std::vector<Entry> out;
    for (const Entry& e : log) {
      if (e.op == op) out.push_back(e);
    }
    return out;
  }

  FakeClock& clock;
  uint32_t updateTransferMs = 0;
  uint32_t updateBusyMs = 0;
  uint32_t clearTransferMs = 0;
  uint32_t clearBusyMs = 0;
  uint32_t pendingMs = 0;  // the rest of a refresh started elsewhere, waited out once
  bool invertedScreen = false;
  const uint8_t* loaded = nullptr;  // the table the "driver" would run on the next FAST refresh
  std::vector<Entry> log;

 private:
  void run(const RefreshKind kind, const bool ui) {
    log.push_back({Op::TRANSFER_START, kind, clock.now, loaded, ui});
    const bool update = kind == RefreshKind::UPDATE;
    clock.advance(update ? updateTransferMs : clearTransferMs);
    clock.advance(update ? updateBusyMs : clearBusyMs);
    log.push_back({Op::BUSY_END, kind, clock.now});
  }
};

}  // namespace displayprofile
