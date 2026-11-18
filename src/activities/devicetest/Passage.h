#pragma once

// The benchmark's fixed passage (docs/PLAN.md section 10, "Benchmark sweep"): 150 words of plain
// English prose written for this project, with commas, a semicolon, a colon, a question and three
// paragraphs, so the pacing weights (lib/RsvpCore, "Pacing") are exercised. Portable: host-tested
// in test/device_test.

#include <WordSource.h>

#include <cstddef>
#include <cstdint>

namespace devicetest {

// Paragraphs are separated by a blank line ("\n\n"); words by single spaces or line breaks.
extern const char PASSAGE[];
inline constexpr uint32_t PASSAGE_WORDS = 150;

// Whitespace-separated words in PASSAGE (the host test checks it is PASSAGE_WORDS).
uint32_t countPassageWords();

// An rsvp::WordSource over the passage that hands out `limit` words: the passage in order, then
// from its start again, with word indexes counting on from 0 to limit - 1 (the endurance run uses
// 300). The last word of each paragraph carries TOKEN_PARAGRAPH_END; word limit - 1 also carries
// TOKEN_CHAPTER_END, and peek() is false after it. Token text points into PASSAGE, so it stays
// valid for the program's life. No heap.
class PassageSource final : public rsvp::WordSource {
 public:
  void reset(uint32_t wordLimit);
  bool peek(rsvp::Token& out) override;
  void advance() override;
  uint32_t position() const { return index; }
  uint32_t limit() const { return wordLimit; }

 private:
  bool locate();

  uint32_t wordLimit = 0;
  uint32_t index = 0;
  size_t offset = 0;  // where the search for the next word starts
  bool located = false;
  rsvp::Token next;
  size_t nextEnd = 0;  // byte after the located word
};

}  // namespace devicetest
