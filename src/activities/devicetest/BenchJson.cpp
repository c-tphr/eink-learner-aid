#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <DisplayProfiles.h>
#include <PanelRecords.h>
#include <Scheduler.h>
#include <TrialWaveforms.h>

#include <cstdio>
#include <cstring>

#include "BenchJson.h"
#include "Passage.h"

namespace devicetest {

namespace {

using displayprofile::profileInfo;

// A streaming JSON writer over a JsonSink: no buffer of its own beyond one number, no heap. Members
// and array items are separated by commas automatically; nesting is at most MAX_DEPTH deep.
class JsonWriter {
 public:
  explicit JsonWriter(JsonSink& out) : sink(out) {}

  void beginObject() { open('{'); }
  void endObject() { close('}'); }
  void beginArray() { open('['); }
  void endArray() { close(']'); }

  void key(const char* name) {
    separate();
    string(name);
    raw(":", 1);
    afterKey = true;
  }

  void str(const char* value) {
    separate();
    string(value);
  }
  void u(const uint64_t value) {
    char buf[24];
    const int n = snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
    separate();
    raw(buf, static_cast<size_t>(n));
  }
  void i(const int32_t value) {
    char buf[16];
    const int n = snprintf(buf, sizeof(buf), "%ld", static_cast<long>(value));
    separate();
    raw(buf, static_cast<size_t>(n));
  }
  // A number with one decimal from tenths: 2405 -> 240.5.
  void tenths(const uint32_t value) {
    char buf[24];
    const int n = snprintf(buf, sizeof(buf), "%lu.%lu", static_cast<unsigned long>(value / 10),
                           static_cast<unsigned long>(value % 10));
    separate();
    raw(buf, static_cast<size_t>(n));
  }
  void boolean(const bool value) {
    separate();
    raw(value ? "true" : "false", value ? 4 : 5);
  }
  void null() {
    separate();
    raw("null", 4);
  }

  // Shorthands for "key": value.
  void member(const char* k, const char* v) {
    key(k);
    str(v);
  }
  void memberU(const char* k, const uint64_t v) {
    key(k);
    u(v);
  }
  void memberI(const char* k, const int32_t v) {
    key(k);
    i(v);
  }
  void memberB(const char* k, const bool v) {
    key(k);
    boolean(v);
  }
  void memberNull(const char* k) {
    key(k);
    null();
  }
  // A string member that is null when empty.
  void memberText(const char* k, const char* v) {
    key(k);
    if (v == nullptr || v[0] == '\0') {
      null();
    } else {
      str(v);
    }
  }
  // An unsigned member that is null when 0 (a prediction that cannot be computed).
  void memberUOrNull(const char* k, const uint64_t v) {
    key(k);
    if (v == 0) {
      null();
    } else {
      u(v);
    }
  }

 private:
  static constexpr int MAX_DEPTH = 8;

  void open(const char c) {
    separate();
    raw(&c, 1);
    if (depth < MAX_DEPTH) first[depth] = true;
    ++depth;
  }
  void close(const char c) {
    if (depth > 0) --depth;
    raw(&c, 1);
    afterKey = false;
  }
  void separate() {
    if (afterKey) {
      afterKey = false;
      return;
    }
    if (depth == 0 || depth > MAX_DEPTH) return;
    if (!first[depth - 1]) raw(",", 1);
    first[depth - 1] = false;
  }
  void raw(const char* data, const size_t length) { sink.write(data, length); }
  void string(const char* s) {
    raw("\"", 1);
    const char* run = s;
    for (const char* p = s; *p != '\0'; ++p) {
      const unsigned char c = static_cast<unsigned char>(*p);
      if (c != '"' && c != '\\' && c >= 0x20) continue;
      raw(run, static_cast<size_t>(p - run));
      char esc[8];
      int n = 2;
      if (c == '"') {
        memcpy(esc, "\\\"", 2);
      } else if (c == '\\') {
        memcpy(esc, "\\\\", 2);
      } else {
        n = snprintf(esc, sizeof(esc), "\\u%04x", c);
      }
      raw(esc, static_cast<size_t>(n));
      run = p + 1;
    }
    raw(run, strlen(run));
    raw("\"", 1);
  }

  JsonSink& sink;
  int depth = 0;
  bool first[MAX_DEPTH] = {};
  bool afterKey = false;
};

void writeSeries(JsonWriter& w, const char* name, const SeriesFigures& s) {
  w.key(name);
  w.beginObject();
  w.memberU("count", s.count);
  w.memberU("median_ms", s.medianMs);
  w.memberU("p95_ms", s.p95Ms);
  w.memberU("max_ms", s.maxMs);
  w.memberU("min_ms", s.minMs);
  w.memberU("mean_ms", s.meanMs);
  w.memberU("timeouts", s.timeouts);
  w.endObject();
}

void writeTrialList(JsonWriter& w, const char* name, const displayprofile::TrialMask mask) {
  w.key(name);
  w.beginArray();
  for (const auto& info : displayprofile::PROFILES) {
    if (displayprofile::isTrial(info.id) && (mask & displayprofile::trialBit(info.id))) w.str(info.key);
  }
  w.endArray();
}

void writeCommon(JsonWriter& w, const RecordHeader& h, const char* kind, const RunStatus status) {
  w.member("format", RESULTS_FORMAT);
  w.memberU("version", RESULTS_VERSION);
  w.member("kind", kind);
  w.memberU("run", h.run);
  w.member("file", h.file);
  w.member("status", runStatusName(status));

  w.key("firmware");
  w.beginObject();
  w.member("version", h.firmware.version);
  w.member("elf_sha256", h.firmware.elfSha256);
  w.endObject();

  const displayprofile::PanelRecord& panel = displayprofile::panelRecord(h.device.panel);
  w.key("device");
  w.beginObject();
  w.member("board", h.device.board);
  w.member("panel", displayprofile::panelName(h.device.panel));
  w.memberU("panel_variant", h.device.panelVariant);
  w.member("controller", displayprofile::controllerName(panel.controller));
  w.member("driver", h.device.driver);
  w.memberB("recognised", panel.recognised);
  w.memberB("simulated", h.device.simulated);
  w.memberB("fading_fix", h.device.fadingFix);
  w.memberB("inverted", h.device.inverted);
  w.memberB("async_split", panel.asyncSplit);
  w.member("stock_update_waveform", panel.updateWaveform);
  w.member("clear_waveform", panel.clearWaveform);
  w.memberUOrNull("predicted_update_ms", panel.predictedUpdateMs);
  w.memberUOrNull("predicted_clear_ms", panel.predictedClearMs);
  w.endObject();

  const ConditionsRecord& c = h.conditions;
  w.key("conditions");
  w.beginObject();
  if (c.hasTemperature) {
    w.key("room_temperature_c");
    if (c.temperatureKnown) {
      w.i(c.temperatureC);
    } else {
      w.null();
    }
  }
  w.memberU("battery_percent", c.batteryPercent);
  w.memberB("usb_connected", c.usbConnected);
  w.memberText("clock", c.clock);
  w.memberU("uptime_ms", c.uptimeMs);
  w.endObject();

  w.key("simulator");
  if (h.simulatorTimeScale == 0) {
    w.null();
  } else {
    w.beginObject();
    w.memberU("time_scale", h.simulatorTimeScale);
    w.endObject();
  }
}

// The figures of one series: "updates", "clears", "promotions", "duration_ms", "playback" (each null
// when the series never ran).
void writeFigures(JsonWriter& w, const bool hasFigures, const StepFigures& f) {
  if (!hasFigures) {
    w.memberNull("updates");
    w.memberNull("clears");
    w.memberNull("promotions");
    w.memberNull("duration_ms");
    w.memberNull("playback");
    return;
  }
  writeSeries(w, "updates", f.updates);
  writeSeries(w, "clears", f.clears);
  w.memberU("promotions", f.promotions);
  w.memberU("duration_ms", f.durationMs);
  w.key("playback");
  if (!f.hasPlayback) {
    w.null();
    return;
  }
  const PlaybackFigures& p = f.playback;
  w.beginObject();
  w.key("delivered_wpm");
  w.tenths(p.deliveredWpmTenths);
  w.memberU("words_shown", p.wordsShown);
  w.memberU("frames", p.frames);
  w.memberU("clearing_refreshes", p.clearingRefreshes);
  w.memberU("active_ms", p.activeMs);
  w.key("order");
  w.beginObject();
  w.memberB("in_order", p.inOrder);
  w.memberU("words_drawn", p.wordsDrawn);
  w.memberU("segments", p.segments);
  w.memberU("violations", p.violations);
  w.memberU("gaps", p.gaps);
  w.memberU("repeats", p.repeats);
  w.endObject();
  w.endObject();
}

void writeStep(JsonWriter& w, const StepSpec& spec, const StepRecord& r) {
  w.beginObject();
  w.member("step", spec.key);
  w.member("kind", stepKindName(spec.kind));
  w.member("status", stepStatusName(r.status));
  w.memberU("attempts", r.attempts);
  w.memberU("requested", spec.count);
  if (usesScheduler(spec.kind)) {
    w.memberU("target_wpm", spec.wpm);
    w.memberU("words_per_update", spec.wordsPerUpdate);
  } else {
    w.memberNull("target_wpm");
    w.memberNull("words_per_update");
  }

  writeFigures(w, r.hasFigures, r.figures);

  if (spec.rated && r.rating != Rating::NONE) {
    w.member("rating", ratingName(r.rating));
  } else {
    w.memberNull("rating");
  }
  if (asksResidue(spec) && r.residue != Residue::NONE) {
    w.member("residue", residueName(r.residue));
  } else {
    w.memberNull("residue");
  }
  w.endObject();
}

void writeProfile(JsonWriter& w, const ProfileRecord& p, const PanelId panel) {
  const displayprofile::ProfileInfo& info = profileInfo(p.id);
  const displayprofile::WordWaveform waveform = displayprofile::wordWaveform(p.id, panel);
  w.beginObject();
  w.member("key", info.key);
  w.member("name", info.name);
  w.member("status", profileStatusName(p.status));
  w.key("reason");
  if (p.reason == NotRunReason::NONE) {
    w.null();
  } else {
    w.str(notRunReasonName(p.reason));
  }
  w.key("skipped_from");
  if (p.skippedFrom >= 0 && static_cast<size_t>(p.skippedFrom) < STEP_COUNT) {
    w.str(STEPS[p.skippedFrom].key);
  } else {
    w.null();
  }
  w.key("clearing");
  w.beginObject();
  w.memberU("soft", info.clearing.softLimit);
  w.memberU("hard", info.clearing.hardLimit);
  w.endObject();
  w.member("waveform", waveform.name);
  w.memberUOrNull("predicted_waveform_ms", waveform.predictedMs);
  w.key("steps");
  w.beginArray();
  for (size_t i = 0; i < STEP_COUNT; ++i) writeStep(w, STEPS[i], p.steps[i]);
  w.endArray();
  w.endObject();
}

// A step result, or null when nothing is known yet.
void memberResultOrNull(JsonWriter& w, const char* k, const StepResult r) {
  w.key(k);
  if (r == StepResult::PASS || r == StepResult::FAIL) {
    w.boolean(r == StepResult::PASS);
  } else {
    w.null();
  }
}

void memberAnswer(JsonWriter& w, const char* k, const Answer a) {
  if (a == Answer::NONE) {
    w.memberNull(k);
  } else {
    w.member(k, answerName(a));
  }
}

// "C2, C3" -> ["C2", "C3"].
void writeDependsOn(JsonWriter& w, const char* text) {
  w.key("depends_on");
  w.beginArray();
  char item[8];
  size_t used = 0;
  for (const char* p = text;; ++p) {
    if (*p == ',' || *p == '\0') {
      item[used] = '\0';
      if (used > 0) w.str(item);
      used = 0;
      if (*p == '\0') break;
    } else if (*p != ' ' && used + 1 < sizeof(item)) {
      item[used++] = *p;
    }
  }
  w.endArray();
}

void writeRun(JsonWriter& w, const StepSpec& spec, const RunRecord& r) {
  w.beginObject();
  w.member("run", spec.key);
  w.memberU("target_wpm", spec.wpm);
  w.memberU("words_per_update", spec.wordsPerUpdate);
  w.memberU("requested", spec.count);
  w.member("status", runStateName(r.state));
  w.memberU("attempts", r.attempts);
  writeFigures(w, r.hasFigures, r.figures);
  w.key("measured_ok");
  if (r.state == RunState::DONE) {
    w.boolean(runMeasuredOk(spec, r));
  } else {
    w.null();
  }
  memberAnswer(w, "readable", r.readable);
  w.endObject();
}

}  // namespace

const char* sideLayoutName(const uint8_t layout) {
  static constexpr const char* NAMES[] = {"prev-next", "next-prev", "disabled", "next-next", "prev-prev"};
  return layout < sizeof(NAMES) / sizeof(NAMES[0]) ? NAMES[layout] : "unknown";
}

const char* shortPowerName(const uint8_t action) {
  static constexpr const char* NAMES[] = {"ignore",   "sleep",    "page-turn", "force-refresh", "footnotes", "confirm",
                                          "clipping", "bookmark", "sync",      "reader-menu",   "frontlight"};
  return action < sizeof(NAMES) / sizeof(NAMES[0]) ? NAMES[action] : "unknown";
}

const char* logicalButtonName(const controls::LogicalButton button) {
  switch (button) {
    case controls::LogicalButton::PageBack:
      return "PageBack";
    case controls::LogicalButton::PageForward:
      return "PageForward";
    case controls::LogicalButton::Up:
      return "Up";
    case controls::LogicalButton::Down:
      return "Down";
    case controls::LogicalButton::Confirm:
      return "Confirm";
  }
  return "?";
}

void writeBenchmark(const BenchmarkRecord& record, const Sweep& sweep, JsonSink& sink) {
  JsonWriter w(sink);
  w.beginObject();
  writeCommon(w, record.header, "benchmark", sweep.status());

  w.key("plan");
  w.beginObject();
  w.memberU("word_band_updates", STEPS[0].count);
  w.memberU("word_band_wpm", WORD_BAND_WPM);
  w.memberU("full_screen_updates", STEPS[1].count);
  w.memberU("clearing_refreshes", STEPS[2].count);
  w.memberU("passage_words", PASSAGE_WORDS);
  w.memberU("endurance_updates", STEPS[STEP_COUNT - 1].count);
  w.memberU("endurance_wpm", ENDURANCE_WPM);
  w.memberU("min_dwell_ms", rsvp::DEFAULT_MIN_DWELL_MS);
  w.endObject();

  const displayprofile::TrialSetting before = displayprofile::decodeTrialSetting(record.trialSettingBefore);
  const displayprofile::TrialSetting after = displayprofile::decodeTrialSetting(record.trialSettingAfter);
  w.key("trial_setting");
  w.beginObject();
  w.memberU("before", record.trialSettingBefore);
  w.memberU("after", record.trialSettingAfter);
  writeTrialList(w, "unlocked_before", before.unlocked);
  writeTrialList(w, "unlocked_after", after.unlocked);
  w.member("chosen_before", profileInfo(before.chosen).key);
  w.member("chosen_after", profileInfo(after.chosen).key);
  w.endObject();

  w.key("profiles");
  w.beginArray();
  for (const ProfileRecord& p : sweep.profiles) writeProfile(w, p, record.header.device.panel);
  w.endArray();
  w.endObject();
  sink.write("\n", 1);
}

void writeControls(const ControlsRecord& record, JsonSink& sink) {
  JsonWriter w(sink);
  w.beginObject();
  writeCommon(w, record.header, "controls", record.status);

  w.key("settings");
  w.beginObject();
  w.member("side_button_layout", sideLayoutName(record.sideButtonLayout));
  w.memberU("side_button_layout_value", record.sideButtonLayout);
  w.member("short_power_button", shortPowerName(record.shortPowerButton));
  w.memberU("short_power_button_value", record.shortPowerButton);
  w.member("prev_key", logicalButtonName(record.keys.prev));
  w.member("next_key", logicalButtonName(record.keys.next));
  w.memberB("confirm_selects", record.confirmSelects);
  w.memberU("hold_ms", controls::HOLD_MS);
  w.memberU("exit_hold_ms", EXIT_HOLD_MS);
  w.endObject();

  const size_t count = record.count < MAX_CONTROL_EVENTS ? record.count : MAX_CONTROL_EVENTS;
  const ControlsSummary s = summarise(record.events, count);
  w.key("summary");
  w.beginObject();
  w.memberB("up_pressed", s.upPressed);
  w.memberB("up_held", s.upHeld);
  w.memberB("down_pressed", s.downPressed);
  w.memberB("down_held", s.downHeld);
  w.memberB("confirm_pressed", s.confirmPressed);
  w.memberB("power_clicked", s.powerClicked);
  w.memberB("touched", s.touched);
  w.endObject();

  w.key("events");
  w.beginArray();
  for (size_t n = 0; n < count; ++n) {
    const ControlEvent& e = record.events[n];
    const bool key = e.source == EventSource::KEY;
    w.beginObject();
    w.memberU("n", n + 1);
    w.memberU("t_ms", e.atMs);
    w.member("source", eventSourceName(e.source));
    w.member("kind", eventKindName(e.kind));
    if (key) {
      w.memberU("raw", e.raw);
      w.member("raw_name", rawKeyName(e.raw));
    } else {
      w.memberNull("raw");
      w.memberNull("raw_name");
    }
    w.key("logical");
    w.beginArray();
    for (uint8_t b = 0; b < LOGICAL_COUNT; ++b) {
      if (e.logical & logicalBit(b)) w.str(logicalName(b));
    }
    w.endArray();
    w.member("role", roleName(e.role));
    if (key) {
      w.memberU("held_ms", e.heldMs);
    } else {
      w.memberNull("held_ms");
    }
    const int16_t pos[4] = {e.x, e.y, e.x2, e.y2};
    static constexpr const char* POS_NAMES[4] = {"x", "y", "x2", "y2"};
    for (int k = 0; k < 4; ++k) {
      if (pos[k] == NO_POSITION) {
        w.memberNull(POS_NAMES[k]);
      } else {
        w.memberI(POS_NAMES[k], pos[k]);
      }
    }
    w.endObject();
  }
  w.endArray();
  w.memberU("events_dropped", record.dropped);
  w.endObject();
  sink.write("\n", 1);
}

void writeAcceptance(const AcceptanceRecord& record, const Walkthrough& walk, JsonSink& sink) {
  JsonWriter w(sink);
  w.beginObject();
  writeCommon(w, record.header, "acceptance", walk.status());

  const displayprofile::ProfileInfo& info = profileInfo(record.profile);
  w.key("profile");
  w.beginObject();
  w.member("key", info.key);
  w.member("name", info.name);
  w.member("waveform", displayprofile::wordWaveform(record.profile, record.header.device.panel).name);
  w.memberU("trial_setting", record.trialSetting);
  w.endObject();

  w.key("part_a");
  w.beginObject();
  w.member("status", partAStatusName(walk.partA.status));
  w.memberU("passage_words", PASSAGE_WORDS);
  w.memberU("min_dwell_ms", rsvp::DEFAULT_MIN_DWELL_MS);
  w.key("runs");
  w.beginArray();
  for (size_t i = 0; i < ACCEPTANCE_RUN_COUNT; ++i) writeRun(w, ACCEPTANCE_RUNS[i], walk.partA.runs[i]);
  w.endArray();
  memberAnswer(w, "slower", walk.partA.slower);
  w.endObject();

  w.key("part_b");
  w.beginObject();
  w.member("book", WALK_BOOK);
  w.key("current");
  if (walk.partB.current < WALK_STEP_COUNT) {
    w.str(WALK_STEPS[walk.partB.current].id);
  } else {
    w.null();
  }
  w.memberB("away", walk.partB.away);
  w.memberU("answered", walk.answered());
  w.key("steps");
  w.beginArray();
  for (size_t i = 0; i < WALK_STEP_COUNT; ++i) {
    const WalkStep& step = WALK_STEPS[i];
    const StepAnswer& a = walk.partB.steps[i];
    w.beginObject();
    w.memberU("n", i + 1);
    w.member("id", step.id);
    w.key("criteria");
    w.beginArray();
    for (unsigned k = 1; k <= CRITERION_COUNT; ++k) {
      if (step.criteria & criterionBit(k)) w.u(k);
    }
    w.endArray();
    w.member("trace", step.trace);
    writeDependsOn(w, step.dependsOn);
    w.member("result", stepResultName(a.result));
    if (a.result == StepResult::FAIL) {
      w.member("reason", failReasonName(a.reason));
    } else {
      w.memberNull("reason");
    }
    w.memberU("answers", a.answers);
    w.endObject();
  }
  w.endArray();
  w.endObject();

  w.key("criteria");
  w.beginArray();
  for (unsigned k = 1; k <= CRITERION_COUNT; ++k) {
    w.beginObject();
    w.memberU("criterion", k);
    w.member("result", stepResultName(walk.criterion(k)));
    w.key("evidence");
    w.beginArray();
    if (k == 5) w.str("part-a");
    for (size_t i = 0; i < WALK_STEP_COUNT; ++i) {
      if (WALK_STEPS[i].criteria & criterionBit(k)) w.str(WALK_STEPS[i].id);
    }
    w.endArray();
    w.endObject();
  }
  w.endArray();

  const Criterion5 c5 = walk.criterion5();
  w.key("criterion_5");
  w.beginObject();
  memberResultOrNull(w, "measured_pass", c5.measured);
  memberResultOrNull(w, "owner_pass", c5.owner);
  w.member("measured", stepResultName(c5.measured));
  w.member("owner", stepResultName(c5.owner));
  w.member("pause_screen", stepResultName(c5.pauseScreen));
  w.endObject();
  w.endObject();
  sink.write("\n", 1);
}

void writeChoice(const ChoiceRecord& record, JsonSink& sink) {
  JsonWriter w(sink);
  w.beginObject();
  writeCommon(w, record.header, "choice", RunStatus::COMPLETE);

  const displayprofile::TrialSetting before = displayprofile::decodeTrialSetting(record.settingBefore);
  const displayprofile::TrialSetting after = displayprofile::decodeTrialSetting(record.settingAfter);
  w.member("chosen", profileInfo(record.chosen).key);
  w.memberB("saved", record.saved);
  w.key("trial_setting");
  w.beginObject();
  w.memberU("before", record.settingBefore);
  w.memberU("after", record.settingAfter);
  writeTrialList(w, "unlocked_before", before.unlocked);
  writeTrialList(w, "unlocked_after", after.unlocked);
  w.member("chosen_before", profileInfo(before.chosen).key);
  w.member("chosen_after", profileInfo(after.chosen).key);
  w.endObject();
  writeTrialList(w, "kept_before", record.keptBefore);
  writeTrialList(w, "kept_after", record.keptAfter);
  writeTrialList(w, "locked", record.locked);
  w.memberText("benchmark_file", record.bench.file);

  w.key("candidates");
  w.beginArray();
  for (uint8_t i = 0; i < record.candidates.count; ++i) {
    const ProfileId id = record.candidates.ids[i];
    const displayprofile::ProfileInfo& info = profileInfo(id);
    const ProfileRatings& r = record.bench.profiles[static_cast<size_t>(id)];
    w.beginObject();
    w.member("key", info.key);
    w.member("name", info.name);
    w.memberB("chosen", id == record.chosen);
    w.memberB("kept", id == ProfileId::STOCK || (record.keptAfter & displayprofile::trialBit(id)) != 0);
    w.key("ratings");
    if (r.found) {
      w.beginObject();
      size_t n = 0;
      for (const StepSpec& s : STEPS) {
        if (!s.rated) continue;
        const Rating rating = n < RATED_STEP_COUNT ? r.ratings[n] : Rating::NONE;
        if (rating == Rating::NONE) {
          w.memberNull(s.key);
        } else {
          w.member(s.key, ratingName(rating));
        }
        ++n;
      }
      w.endObject();
    } else {
      w.null();
    }
    if (r.found && r.residue != Residue::NONE) {
      w.member("residue", residueName(r.residue));
    } else {
      w.memberNull("residue");
    }
    w.key("delivered_wpm_240");
    if (r.found && r.hasDelivered240) {
      w.tenths(r.delivered240Tenths);
    } else {
      w.null();
    }
    w.endObject();
  }
  w.endArray();
  w.endObject();
  sink.write("\n", 1);
}

}  // namespace devicetest

#endif  // CROSSPOINT_RSVP_ENABLED
