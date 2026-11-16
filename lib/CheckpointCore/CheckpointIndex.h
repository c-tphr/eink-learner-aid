#pragma once

// Reader for rsvp/checkpoints.json (docs/PLAN.md section 5; rules in
// tools/prep/README.md). open() makes one validating pass over the source and
// keeps a compact index: ids, chapter hrefs, word counts and links in RAM;
// labels, the title, prompt ids, prompts and answers as file offsets that the
// load*() calls decode into caller buffers. README.md describes the design.
//
// Thread safety: none. Use one index from one task at a time; load*() calls
// read the source, so they also need exclusive use of it.
// Lifetime: the source passed to open() must stay valid until close(), a
// later open() or destruction. Pointers returned by the index (ids, hrefs,
// book fields) stay valid until then too.

#include <cstddef>
#include <cstdint>

#include "CheckpointLimits.h"
#include "CheckpointSource.h"

namespace checkpoint {

enum class Verdict : uint8_t { Valid, Invalid, UnsupportedVersion };

// The issue codes of tools/prep/checkpoints.py, plus two firmware-only codes:
// Limit (a firmware limit in CheckpointLimits.h is exceeded) and Io (the
// source failed to read).
enum class IssueCode : uint8_t {
  None,
  Parse,
  DuplicateKey,
  UnsupportedVersion,
  Schema,
  DuplicateId,
  AnchorName,
  UnknownSection,
  MultipleCheckpointsForSection,
  NotPlainText,
  BlankText,
  Limit,
  Io,
};

// The code as spelled in tools/prep and fixtures/checkpoints/manifest.json,
// for example "duplicate-id"; "limit" and "io" for the firmware-only codes.
const char* issueCodeName(IssueCode code);

struct OpenResult {
  Verdict verdict = Verdict::Invalid;
  IssueCode code = IssueCode::None;   // first issue; None when valid
  uint32_t offset = 0;                // byte offset of the value or byte at fault
  const char* detail = "";            // static English text for logs
  char path[PATH_BUFFER_BYTES] = {};  // RFC 6901 pointer, as tools/prep reports it
};

inline constexpr uint16_t NO_INDEX = 0xFFFF;

namespace detail {

class CheckpointParser;

inline constexpr uint16_t READ_BUFFER_BYTES = 256;
inline constexpr uint8_t KEY_CAPTURE_BYTES = 24;                    // longest known member name is 20
inline constexpr uint8_t ANCHOR_CAPTURE_BYTES = MAX_ID_CHARS + 12;  // "rsvp-" + id + "-start" + NUL

struct ParseFrame {
  uint32_t count;       // members or items so far
  uint32_t offset;      // byte offset of '{' or '['
  uint32_t pathLength;  // logical path length of this container
  uint16_t keyBase;     // first entry of this object in ParseState::keys
  uint8_t isObject;
  uint8_t context;
  uint8_t seen;  // known members present, one bit each
};

struct ParseKey {
  uint32_t hash;
  uint32_t offset;
};

// Working memory of open(), kept in the index so open() needs little stack.
struct ParseState {
  ParseFrame frames[MAX_DEPTH];
  ParseKey keys[MAX_OPEN_MEMBERS];
  uint8_t readBuffer[READ_BUFFER_BYTES];
  char path[PATH_BUFFER_BYTES];
  char schemaPath[PATH_BUFFER_BYTES];
  char surrogatePath[PATH_BUFFER_BYTES];
  char key[KEY_CAPTURE_BYTES];
  char startAnchor[ANCHOR_CAPTURE_BYTES];
  char endAnchor[ANCHOR_CAPTURE_BYTES];
};

}  // namespace detail

class CheckpointIndex {
 public:
  CheckpointIndex() = default;
  CheckpointIndex(const CheckpointIndex&) = delete;
  CheckpointIndex& operator=(const CheckpointIndex&) = delete;

  // Validates the whole file and builds the index. On any verdict other than
  // Valid the index stays closed and every query returns its empty value.
  OpenResult open(CheckpointSource& source);
  void close();
  bool isOpen() const { return source != nullptr; }

  // Book. The strings are "" while closed.
  const char* language() const { return languageText; }
  const char* sourceSha256() const { return sha256Text; }
  const char* instructionsVersion() const { return instructionsVersionText; }
  bool loadTitle(char* out, size_t outSize) const;  // TITLE_BUFFER_BYTES always fits

  // Sections, in file order. Lookups return -1 when nothing matches.
  uint16_t sectionCount() const { return sectionTotal; }
  int findSection(const char* id) const;
  // `anchorId` is an XHTML id such as "rsvp-s0007-end".
  int findSectionByEndAnchor(const char* anchorId) const;
  int findSectionByStartAnchor(const char* anchorId) const;
  const char* sectionId(uint16_t section) const;
  const char* sectionChapterHref(uint16_t section) const;
  uint16_t sectionChapter(uint16_t section) const;  // NO_INDEX when out of range
  uint32_t sectionWordCount(uint16_t section) const;
  int sectionCheckpoint(uint16_t section) const;                             // -1 when the section has none
  bool loadSectionLabel(uint16_t section, char* out, size_t outSize) const;  // LABEL_BUFFER_BYTES

  // Chapters: the distinct chapter_href values, in order of first use.
  // Hrefs are compared byte for byte, exactly as written in the file.
  uint16_t chapterCount() const { return chapterTotal; }
  int findChapter(const char* href) const;
  const char* chapterHref(uint16_t chapter) const;
  // Sections of a chapter in file order: start with after = -1, stop at -1.
  int nextSectionInChapter(uint16_t chapter, int after) const;

  // Checkpoints, in file order.
  uint16_t checkpointCount() const { return checkpointTotal; }
  int findCheckpoint(const char* id) const;
  const char* checkpointId(uint16_t checkpoint) const;
  uint16_t checkpointSection(uint16_t checkpoint) const;  // NO_INDEX when out of range
  uint16_t promptCount(uint16_t checkpoint) const;

  // Prompt `prompt` (0-based, file order) of a checkpoint, as NUL-terminated
  // UTF-8. False (and "") on a bad index, a read error, or a buffer that is too
  // small; ID_BUFFER_BYTES, PROMPT_BUFFER_BYTES and ANSWER_BUFFER_BYTES always fit.
  bool loadPromptId(uint16_t checkpoint, uint16_t prompt, char* out, size_t outSize) const;
  bool loadPromptText(uint16_t checkpoint, uint16_t prompt, char* out, size_t outSize) const;
  bool loadAnswerText(uint16_t checkpoint, uint16_t prompt, char* out, size_t outSize) const;

 private:
  friend class detail::CheckpointParser;

  struct SectionRecord {
    uint32_t idHash;
    uint32_t idOffset;
    uint32_t labelOffset;
    uint32_t wordCount;
    uint16_t labelLength;
    uint16_t id;          // pool offset
    uint16_t chapter;     // index into chapters
    uint16_t checkpoint;  // NO_INDEX when none
  };

  struct CheckpointRecord {
    uint32_t idHash;
    uint32_t idOffset;
    uint32_t sectionHash;
    uint32_t sectionOffset;
    uint16_t id;          // pool offset
    uint16_t sectionRef;  // pool offset of the section id as written
    uint16_t section;     // resolved section index
    uint16_t firstPrompt;
    uint16_t promptCount;
  };

  struct PromptRecord {
    uint32_t idHash;
    uint32_t idOffset;
    uint32_t promptOffset;
    uint32_t answerOffset;
    uint16_t idLength;  // raw bytes in the file, quotes included
    uint16_t promptLength;
    uint16_t answerLength;
  };

  struct ChapterRecord {
    uint32_t hash;
    uint16_t href;  // pool offset
  };

  const PromptRecord* promptAt(uint16_t checkpoint, uint16_t prompt) const;
  bool loadSpan(uint32_t offset, uint16_t length, char* out, size_t outSize) const;
  int findAnchor(const char* anchorId, const char* suffix) const;
  int findSectionBytes(const char* id, size_t length) const;
  void reset();

  CheckpointSource* source = nullptr;

  SectionRecord sections[MAX_SECTIONS];
  CheckpointRecord checkpoints[MAX_CHECKPOINTS];
  PromptRecord prompts[MAX_PROMPTS];
  ChapterRecord chapters[MAX_CHAPTERS];
  char pool[STRING_POOL_BYTES] = {};

  uint16_t sectionTotal = 0;
  uint16_t checkpointTotal = 0;
  uint16_t promptTotal = 0;
  uint16_t chapterTotal = 0;
  uint16_t poolUsed = 0;

  uint32_t titleOffset = 0;
  uint16_t titleLength = 0;
  char languageText[textBufferBytes(MAX_LANGUAGE_CHARS)] = {};
  char instructionsVersionText[textBufferBytes(MAX_INSTRUCTIONS_VERSION_CHARS)] = {};
  char sha256Text[65] = {};

  detail::ParseState parseState;
};

}  // namespace checkpoint
