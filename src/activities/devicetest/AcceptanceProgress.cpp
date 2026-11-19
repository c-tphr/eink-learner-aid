#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "AcceptanceProgress.h"

namespace devicetest {

namespace {

constexpr const char MAGIC[] = "rsvp-acceptance-progress";
constexpr size_t FIGURE_NUMBERS = 28;
constexpr size_t MAX_TOKENS = 40;

class LineWriter {
 public:
  explicit LineWriter(JsonSink& out) : sink(out) {}
  void put(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char line[PROGRESS_LINE_BYTES];
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (n > 0) sink.write(line, static_cast<size_t>(n) < sizeof(line) ? static_cast<size_t>(n) : sizeof(line) - 1);
  }

 private:
  JsonSink& sink;
};

void figureNumbers(const RunRecord& r, uint64_t* out) {
  const StepFigures& f = r.figures;
  const SeriesFigures* series[2] = {&f.updates, &f.clears};
  size_t n = 0;
  for (const SeriesFigures* s : series) {
    out[n++] = s->count;
    out[n++] = s->medianMs;
    out[n++] = s->p95Ms;
    out[n++] = s->maxMs;
    out[n++] = s->minMs;
    out[n++] = s->meanMs;
    out[n++] = s->timeouts;
  }
  out[n++] = f.promotions;
  out[n++] = f.durationMs;
  out[n++] = f.hasPlayback ? 1 : 0;
  const PlaybackFigures& p = f.playback;
  out[n++] = p.deliveredWpmTenths;
  out[n++] = p.wordsShown;
  out[n++] = p.frames;
  out[n++] = p.clearingRefreshes;
  out[n++] = p.activeMs;
  out[n++] = p.inOrder ? 1 : 0;
  out[n++] = p.wordsDrawn;
  out[n++] = p.segments;
  out[n++] = p.violations;
  out[n++] = p.gaps;
  out[n++] = p.repeats;
}

void setFigures(RunRecord& r, const uint64_t* in) {
  StepFigures& f = r.figures;
  SeriesFigures* series[2] = {&f.updates, &f.clears};
  size_t n = 0;
  for (SeriesFigures* s : series) {
    s->count = static_cast<uint32_t>(in[n++]);
    s->medianMs = static_cast<uint32_t>(in[n++]);
    s->p95Ms = static_cast<uint32_t>(in[n++]);
    s->maxMs = static_cast<uint32_t>(in[n++]);
    s->minMs = static_cast<uint32_t>(in[n++]);
    s->meanMs = static_cast<uint32_t>(in[n++]);
    s->timeouts = static_cast<uint32_t>(in[n++]);
  }
  f.promotions = static_cast<uint32_t>(in[n++]);
  f.durationMs = static_cast<uint32_t>(in[n++]);
  f.hasPlayback = in[n++] != 0;
  PlaybackFigures& p = f.playback;
  p.deliveredWpmTenths = static_cast<uint32_t>(in[n++]);
  p.wordsShown = static_cast<uint32_t>(in[n++]);
  p.frames = static_cast<uint32_t>(in[n++]);
  p.clearingRefreshes = static_cast<uint32_t>(in[n++]);
  p.activeMs = in[n++];
  p.inOrder = in[n++] != 0;
  p.wordsDrawn = static_cast<uint32_t>(in[n++]);
  p.segments = static_cast<uint32_t>(in[n++]);
  p.violations = static_cast<uint32_t>(in[n++]);
  p.gaps = static_cast<uint32_t>(in[n++]);
  p.repeats = static_cast<uint32_t>(in[n++]);
}

// Splits `text` at single spaces into at most MAX_TOKENS tokens, in place in `buffer`.
size_t tokenize(const char* text, char* buffer, const size_t size, const char** tokens) {
  const size_t length = strnlen(text, size - 1);
  memcpy(buffer, text, length);
  buffer[length] = '\0';
  size_t count = 0;
  char* p = buffer;
  while (*p != '\0' && count < MAX_TOKENS) {
    while (*p == ' ') ++p;
    if (*p == '\0') break;
    tokens[count++] = p;
    while (*p != '\0' && *p != ' ') ++p;
    if (*p == ' ') *p++ = '\0';
  }
  return count;
}

bool number(const char* token, const uint64_t max, uint64_t& out) {
  if (token == nullptr || *token < '0' || *token > '9') return false;
  char* end = nullptr;
  const unsigned long long v = strtoull(token, &end, 10);
  if (end == nullptr || *end != '\0' || v > max) return false;
  out = v;
  return true;
}

template <typename E>
bool enumValue(const char* token, const E last, E& out) {
  uint64_t v = 0;
  if (!number(token, static_cast<uint64_t>(last), v)) return false;
  out = static_cast<E>(v);
  return true;
}

}  // namespace

void encodeProgress(const AcceptanceRecord& record, const Walkthrough& walk, JsonSink& sink) {
  LineWriter out(sink);
  out.put("%s %lu\n", MAGIC, static_cast<unsigned long>(ACCEPTANCE_PROGRESS_VERSION));
  out.put("run %u\n", static_cast<unsigned>(record.header.run));
  out.put("file %s\n", record.header.file);
  out.put("profile %u %u\n", static_cast<unsigned>(record.profile), static_cast<unsigned>(record.trialSetting));
  const ConditionsRecord& c = record.header.conditions;
  out.put("conditions %u %u %lu %s\n", static_cast<unsigned>(c.batteryPercent), c.usbConnected ? 1u : 0u,
          static_cast<unsigned long>(c.uptimeMs), c.clock[0] != '\0' ? c.clock : "-");
  out.put("part_a %u %u\n", static_cast<unsigned>(walk.partA.status), static_cast<unsigned>(walk.partA.slower));
  for (size_t i = 0; i < ACCEPTANCE_RUN_COUNT; ++i) {
    const RunRecord& r = walk.partA.runs[i];
    uint64_t v[FIGURE_NUMBERS] = {};
    figureNumbers(r, v);
    char numbers[PROGRESS_LINE_BYTES - 40];
    size_t used = 0;
    for (const uint64_t x : v) {
      const int w = snprintf(numbers + used, sizeof(numbers) - used, " %llu", static_cast<unsigned long long>(x));
      if (w > 0 && static_cast<size_t>(w) < sizeof(numbers) - used) used += static_cast<size_t>(w);
    }
    out.put("run_a %u %u %u %u %u%s\n", static_cast<unsigned>(i), static_cast<unsigned>(r.state),
            static_cast<unsigned>(r.attempts), r.hasFigures ? 1u : 0u, static_cast<unsigned>(r.readable), numbers);
  }
  for (size_t i = 0; i < WALK_STEP_COUNT; ++i) {
    const StepAnswer& a = walk.partB.steps[i];
    out.put("step %u %u %u %u\n", static_cast<unsigned>(i), static_cast<unsigned>(a.result),
            static_cast<unsigned>(a.reason), static_cast<unsigned>(a.answers));
  }
  out.put("part_b %u %u\n", static_cast<unsigned>(walk.partB.current), walk.partB.away ? 1u : 0u);
  out.put("end\n");
}

ProgressDecoder::ProgressDecoder() { w.reset(); }

bool ProgressDecoder::line(const char* text) {
  if (failed) return false;
  if (endSeen) {
    failed = true;  // nothing may follow "end"
    return false;
  }
  char buffer[PROGRESS_LINE_BYTES];
  const char* t[MAX_TOKENS] = {};
  const size_t n = tokenize(text, buffer, sizeof(buffer), t);
  if (n == 0) return true;  // a blank line
  uint64_t v = 0;
  bool ok = false;
  if (!headerSeen) {
    ok = n == 2 && strcmp(t[0], MAGIC) == 0 && number(t[1], 99, v) && v == ACCEPTANCE_PROGRESS_VERSION;
    headerSeen = ok;
  } else if (strcmp(t[0], "run") == 0) {
    ok = n == 2 && number(t[1], MAX_RUN_NUMBER, v) && v >= 1;
    rec.header.run = static_cast<uint16_t>(v);
  } else if (strcmp(t[0], "file") == 0) {
    ok = n == 2 && strlen(t[1]) < sizeof(rec.header.file) && runNumberOf(t[1]) != 0;
    if (ok) snprintf(rec.header.file, sizeof(rec.header.file), "%s", t[1]);
  } else if (strcmp(t[0], "profile") == 0) {
    uint64_t setting = 0;
    ok = n == 3 && number(t[1], displayprofile::PROFILE_COUNT - 1, v) && number(t[2], 255, setting);
    rec.profile = static_cast<ProfileId>(v);
    rec.trialSetting = static_cast<uint8_t>(setting);
  } else if (strcmp(t[0], "conditions") == 0) {
    uint64_t usb = 0;
    uint64_t uptime = 0;
    ok = n == 5 && number(t[1], 100, v) && number(t[2], 1, usb) && number(t[3], UINT32_MAX, uptime) &&
         strlen(t[4]) < sizeof(rec.header.conditions.clock);
    if (ok) {
      ConditionsRecord& c = rec.header.conditions;
      c.batteryPercent = static_cast<uint8_t>(v);
      c.usbConnected = usb != 0;
      c.uptimeMs = static_cast<uint32_t>(uptime);
      snprintf(c.clock, sizeof(c.clock), "%s", strcmp(t[4], "-") == 0 ? "" : t[4]);
    }
  } else if (strcmp(t[0], "part_a") == 0) {
    ok = n == 3 && enumValue(t[1], PartAStatus::SKIPPED, w.partA.status) && enumValue(t[2], Answer::NO, w.partA.slower);
  } else if (strcmp(t[0], "run_a") == 0) {
    uint64_t attempts = 0;
    uint64_t has = 0;
    ok = n == 6 + FIGURE_NUMBERS && number(t[1], ACCEPTANCE_RUN_COUNT - 1, v);
    if (ok) {
      RunRecord& r = w.partA.runs[v];
      ok = enumValue(t[2], RunState::STOPPED, r.state) && number(t[3], 255, attempts) && number(t[4], 1, has) &&
           enumValue(t[5], Answer::NO, r.readable);
      r.attempts = static_cast<uint8_t>(attempts);
      r.hasFigures = has != 0;
      uint64_t figures[FIGURE_NUMBERS] = {};
      for (size_t i = 0; ok && i < FIGURE_NUMBERS; ++i) ok = number(t[6 + i], UINT64_MAX, figures[i]);
      if (ok) setFigures(r, figures);
    }
  } else if (strcmp(t[0], "step") == 0) {
    uint64_t answers = 0;
    ok = n == 5 && number(t[1], WALK_STEP_COUNT - 1, v);
    if (ok) {
      StepAnswer& a = w.partB.steps[v];
      ok = enumValue(t[2], StepResult::SKIPPED, a.result) && enumValue(t[3], FailReason::OTHER, a.reason) &&
           number(t[4], 255, answers);
      a.answers = static_cast<uint8_t>(answers);
    }
  } else if (strcmp(t[0], "part_b") == 0) {
    uint64_t away = 0;
    ok = n == 3 && number(t[1], WALK_STEP_COUNT, v) && number(t[2], 1, away);
    w.partB.current = static_cast<uint8_t>(v);
    w.partB.away = away != 0;
  } else if (strcmp(t[0], "end") == 0) {
    ok = n == 1;
    endSeen = ok;
  }
  if (!ok) failed = true;
  return ok;
}

bool ProgressDecoder::finish(AcceptanceRecord& record, Walkthrough& walk) const {
  if (failed || !headerSeen || !endSeen || rec.header.run == 0 || rec.header.file[0] == '\0') return false;
  record.header.run = rec.header.run;
  snprintf(record.header.file, sizeof(record.header.file), "%s", rec.header.file);
  record.header.conditions = rec.header.conditions;
  record.profile = rec.profile;
  record.trialSetting = rec.trialSetting;
  walk = w;
  return true;
}

}  // namespace devicetest

#endif  // CROSSPOINT_RSVP_ENABLED
