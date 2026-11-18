#pragma once

// The guided acceptance's progress file (task D3b): everything needed to come back to the step in
// progress after the owner leaves to do it, after sleep, a crash or a flat battery, and to rewrite the
// results file. It is written after every answer, before the results file, through a temporary file
// and a rename. Plain text, one record per line, so it can be read back without a JSON parser and
// without allocating:
//
//   rsvp-acceptance-progress 1
//   run 7
//   file 0007-acceptance.json
//   profile 0 0                   profile id, stored byte at the start
//   conditions 87 1 123456 2026-12-01T19:04:00     battery, USB, uptime, clock ("-": none)
//   part_a 2 1                    Part A status, the "slower" answer
//   run_a 0 1 1 1 1 <28 numbers>  index, state, attempts, has figures, readable, then the figures
//   step 3 2 1 1                  index, result, fail reason, answers
//   part_b 4 1                    current step, away
//   end
//
// Numbers are the enums' values. A file without its first or its last line, or with a line that
// does not parse, is refused as a whole (the walk-through then starts again). Portable: host-tested
// in test/device_test.

#include <cstddef>
#include <cstdint>

#include "Acceptance.h"
#include "BenchJson.h"

namespace devicetest {

inline constexpr const char ACCEPTANCE_PROGRESS_PATH[] = "/.crosspoint/acceptance-progress.txt";
inline constexpr uint32_t ACCEPTANCE_PROGRESS_VERSION = 1;
// The longest line encodeProgress() writes, with its line feed and a NUL.
inline constexpr size_t PROGRESS_LINE_BYTES = 320;

// Writes the progress through `sink` (any byte sink).
void encodeProgress(const AcceptanceRecord& record, const Walkthrough& walk, JsonSink& sink);

// Reads it back one line at a time (without the line feed).
class ProgressDecoder {
 public:
  ProgressDecoder();
  // False once a line was refused; later lines are ignored.
  bool line(const char* text);
  // True when the whole file was read and valid; fills the record's run, file, conditions, profile
  // and byte (the rest of the header is the device's own) and the walk-through.
  bool finish(AcceptanceRecord& record, Walkthrough& walk) const;

 private:
  AcceptanceRecord rec;
  Walkthrough w;
  bool headerSeen = false;
  bool endSeen = false;
  bool failed = false;
};

}  // namespace devicetest
