// Writes sample results files for test_results_schema.py: <dir>/0001-bench.json (a run with a
// profile skipped part-way, ended by the owner), <dir>/0012-bench.json (a complete SE0400NQW47 run)
// and <dir>/0002-controls.json.

#include <sys/stat.h>

#include <cstdio>
#include <string>

#include "SampleRuns.h"

namespace {

bool save(const std::string& dir, const char* name, const std::string& text) {
  const std::string path = dir + "/" + name;
  FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return false;
  const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
  return std::fclose(f) == 0 && ok;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: DeviceTestSamples <output dir>\n");
    return 2;
  }
  const std::string dir = argv[1];
  mkdir(dir.c_str(), 0755);
  const bool ok = save(dir, "0001-bench.json", devicetest_samples::skippedRunJson()) &&
                  save(dir, "0012-bench.json", devicetest_samples::se0400RunJson()) &&
                  save(dir, "0002-controls.json", devicetest_samples::controlsJson());
  std::printf("%s three sample results files in %s\n", ok ? "wrote" : "FAILED to write", dir.c_str());
  return ok ? 0 : 1;
}
