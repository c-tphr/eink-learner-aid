#include "CheckpointIndex.h"

#include <cstring>

#include "CheckpointParser.h"
#include "JsonText.h"

namespace checkpoint {

namespace {

void clearText(char* out, size_t outSize) {
  if (out != nullptr && outSize > 0) {
    out[0] = '\0';
  }
}

size_t boundedLength(const char* text, size_t limit) {
  size_t n = 0;
  while (n < limit && text[n] != '\0') {
    ++n;
  }
  return n;
}

}  // namespace

const char* issueCodeName(IssueCode code) {
  switch (code) {
    case IssueCode::None:
      return "";
    case IssueCode::Parse:
      return "parse";
    case IssueCode::DuplicateKey:
      return "duplicate-key";
    case IssueCode::UnsupportedVersion:
      return "unsupported-version";
    case IssueCode::Schema:
      return "schema";
    case IssueCode::DuplicateId:
      return "duplicate-id";
    case IssueCode::AnchorName:
      return "anchor-name";
    case IssueCode::UnknownSection:
      return "unknown-section";
    case IssueCode::MultipleCheckpointsForSection:
      return "multiple-checkpoints-for-section";
    case IssueCode::NotPlainText:
      return "not-plain-text";
    case IssueCode::BlankText:
      return "blank-text";
    case IssueCode::Limit:
      return "limit";
    case IssueCode::Io:
      return "io";
  }
  return "";
}

OpenResult CheckpointIndex::open(CheckpointSource& src) {
  reset();
  detail::CheckpointParser parser(*this, src);
  const OpenResult result = parser.run();
  if (result.verdict == Verdict::Valid) {
    source = &src;
  } else {
    reset();
  }
  return result;
}

void CheckpointIndex::close() { reset(); }

void CheckpointIndex::reset() {
  source = nullptr;
  sectionTotal = 0;
  checkpointTotal = 0;
  promptTotal = 0;
  chapterTotal = 0;
  poolUsed = 0;
  titleOffset = 0;
  titleLength = 0;
  languageText[0] = '\0';
  instructionsVersionText[0] = '\0';
  sha256Text[0] = '\0';
}

bool CheckpointIndex::loadSpan(uint32_t offset, uint16_t length, char* out, size_t outSize) const {
  clearText(out, outSize);
  if (source == nullptr) {
    return false;
  }
  return detail::loadJsonString(*source, offset, length, out, outSize);
}

bool CheckpointIndex::loadTitle(char* out, size_t outSize) const {
  return loadSpan(titleOffset, titleLength, out, outSize);
}

int CheckpointIndex::findSectionBytes(const char* id, size_t length) const {
  if (id == nullptr || length == 0 || length > MAX_ID_CHARS) {
    return -1;
  }
  const uint32_t hash = detail::fnvBytes(id, length);
  for (uint16_t i = 0; i < sectionTotal; ++i) {
    const char* candidate = pool + sections[i].id;
    if (sections[i].idHash == hash && strncmp(candidate, id, length) == 0 && candidate[length] == '\0') {
      return i;
    }
  }
  return -1;
}

int CheckpointIndex::findSection(const char* id) const {
  return id == nullptr ? -1 : findSectionBytes(id, boundedLength(id, MAX_ID_CHARS + 1));
}

int CheckpointIndex::findAnchor(const char* anchorId, const char* suffix) const {
  if (anchorId == nullptr) {
    return -1;
  }
  const size_t length = boundedLength(anchorId, MAX_ID_CHARS + 16);
  const size_t suffixLength = strlen(suffix);
  if (length <= 5 + suffixLength || strncmp(anchorId, "rsvp-", 5) != 0 ||
      strcmp(anchorId + length - suffixLength, suffix) != 0) {
    return -1;
  }
  return findSectionBytes(anchorId + 5, length - 5 - suffixLength);
}

int CheckpointIndex::findSectionByEndAnchor(const char* anchorId) const { return findAnchor(anchorId, "-end"); }

int CheckpointIndex::findSectionByStartAnchor(const char* anchorId) const { return findAnchor(anchorId, "-start"); }

const char* CheckpointIndex::sectionId(uint16_t section) const {
  return section < sectionTotal ? pool + sections[section].id : nullptr;
}

const char* CheckpointIndex::sectionChapterHref(uint16_t section) const {
  return section < sectionTotal ? chapterHref(sections[section].chapter) : nullptr;
}

uint16_t CheckpointIndex::sectionChapter(uint16_t section) const {
  return section < sectionTotal ? sections[section].chapter : NO_INDEX;
}

uint32_t CheckpointIndex::sectionWordCount(uint16_t section) const {
  return section < sectionTotal ? sections[section].wordCount : 0;
}

int CheckpointIndex::sectionCheckpoint(uint16_t section) const {
  if (section >= sectionTotal || sections[section].checkpoint == NO_INDEX) {
    return -1;
  }
  return sections[section].checkpoint;
}

bool CheckpointIndex::loadSectionLabel(uint16_t section, char* out, size_t outSize) const {
  if (section >= sectionTotal) {
    clearText(out, outSize);
    return false;
  }
  return loadSpan(sections[section].labelOffset, sections[section].labelLength, out, outSize);
}

int CheckpointIndex::findChapter(const char* href) const {
  if (href == nullptr) {
    return -1;
  }
  const uint32_t hash = detail::fnvBytes(href, strlen(href));
  for (uint16_t i = 0; i < chapterTotal; ++i) {
    if (chapters[i].hash == hash && strcmp(pool + chapters[i].href, href) == 0) {
      return i;
    }
  }
  return -1;
}

const char* CheckpointIndex::chapterHref(uint16_t chapter) const {
  return chapter < chapterTotal ? pool + chapters[chapter].href : nullptr;
}

int CheckpointIndex::nextSectionInChapter(uint16_t chapter, int after) const {
  for (int i = after < 0 ? 0 : after + 1; i < sectionTotal; ++i) {
    if (sections[i].chapter == chapter) {
      return i;
    }
  }
  return -1;
}

int CheckpointIndex::findCheckpoint(const char* id) const {
  if (id == nullptr) {
    return -1;
  }
  const size_t length = boundedLength(id, MAX_ID_CHARS + 1);
  const uint32_t hash = detail::fnvBytes(id, length);
  for (uint16_t i = 0; i < checkpointTotal; ++i) {
    if (checkpoints[i].idHash == hash && strcmp(pool + checkpoints[i].id, id) == 0) {
      return i;
    }
  }
  return -1;
}

const char* CheckpointIndex::checkpointId(uint16_t checkpoint) const {
  return checkpoint < checkpointTotal ? pool + checkpoints[checkpoint].id : nullptr;
}

uint16_t CheckpointIndex::checkpointSection(uint16_t checkpoint) const {
  return checkpoint < checkpointTotal ? checkpoints[checkpoint].section : NO_INDEX;
}

uint16_t CheckpointIndex::promptCount(uint16_t checkpoint) const {
  return checkpoint < checkpointTotal ? checkpoints[checkpoint].promptCount : 0;
}

const CheckpointIndex::PromptRecord* CheckpointIndex::promptAt(uint16_t checkpoint, uint16_t prompt) const {
  if (checkpoint >= checkpointTotal || prompt >= checkpoints[checkpoint].promptCount) {
    return nullptr;
  }
  return &prompts[checkpoints[checkpoint].firstPrompt + prompt];
}

bool CheckpointIndex::loadPromptId(uint16_t checkpoint, uint16_t prompt, char* out, size_t outSize) const {
  const PromptRecord* record = promptAt(checkpoint, prompt);
  if (record == nullptr) {
    clearText(out, outSize);
    return false;
  }
  return loadSpan(record->idOffset, record->idLength, out, outSize);
}

bool CheckpointIndex::loadPromptText(uint16_t checkpoint, uint16_t prompt, char* out, size_t outSize) const {
  const PromptRecord* record = promptAt(checkpoint, prompt);
  if (record == nullptr) {
    clearText(out, outSize);
    return false;
  }
  return loadSpan(record->promptOffset, record->promptLength, out, outSize);
}

bool CheckpointIndex::loadAnswerText(uint16_t checkpoint, uint16_t prompt, char* out, size_t outSize) const {
  const PromptRecord* record = promptAt(checkpoint, prompt);
  if (record == nullptr) {
    clearText(out, outSize);
    return false;
  }
  return loadSpan(record->answerOffset, record->answerLength, out, outSize);
}

}  // namespace checkpoint
