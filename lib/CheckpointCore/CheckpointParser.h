#pragma once

// Internal: the validating pass behind CheckpointIndex::open().

#include "CheckpointIndex.h"
#include "JsonText.h"

namespace checkpoint::detail {

class CheckpointParser {
 public:
  CheckpointParser(CheckpointIndex& target, CheckpointSource& input);

  // Scans the source and fills the index. The index is usable when the
  // verdict is Valid.
  OpenResult run();

 private:
  enum class Stop : uint8_t { None, Syntax, Limit, Io };

  struct StringState;

  // Scanner (layer 1).
  void scan();
  void skipWhitespace();
  bool pushContainer(bool isObject, uint8_t slot, uint32_t offset);
  void popContainer();
  bool readMember(uint8_t& slot);
  uint8_t startItem();
  bool readScalar(uint8_t slot);
  bool readString(StringState& s);
  bool readNumber(uint8_t slot);
  bool readLiteral(uint8_t slot);
  void syntaxError(const char* detail);
  void structuralLimit(uint32_t offset, const char* detail);

  // Semantics (layers 2 and 3, and inputs to layer 4).
  uint8_t containerContext(bool isObject, uint8_t slot, uint32_t offset);
  void closeContext(const ParseFrame& frame);
  void onCodePoint(StringState& s, uint32_t cp);
  void finishString(StringState& s, uint32_t offset, uint32_t rawLength);
  void schemaError(uint32_t offset, const char* detail);
  void semanticLimit(uint32_t offset, const char* detail);
  bool commitPoolString(const StringState& s, uint16_t& poolOffset);
  void checkAnchors();
  void checkPromptText();

  // Cross-reference (layer 4).
  bool crossReference(OpenResult& result);

  // Paths.
  void pathPut(char c);
  void pathPutIndex(uint32_t index);
  void copyPath(char* out, const char* from, uint32_t length) const;
  int comparePath(const char* a, uint32_t lengthA, const char* b, uint32_t lengthB) const;

  OpenResult finish();

  CheckpointIndex& index;
  ParseState& st;
  CheckpointSource& source;
  SourceReader reader;
  uint32_t size = 0;

  uint8_t depth = 0;
  uint16_t keyTop = 0;
  uint32_t pathLength = 0;

  Stop stop = Stop::None;
  uint32_t stopOffset = 0;
  const char* stopDetail = "";

  bool duplicateKey = false;
  uint32_t duplicateKeyOffset = 0;
  bool loneSurrogate = false;
  uint32_t loneSurrogateOffset = 0;
  uint32_t loneSurrogatePathLength = 0;

  bool schemaFound = false;
  uint32_t schemaOffset = 0;
  uint32_t schemaPathLength = 0;
  const char* schemaDetail = "";

  bool limitFound = false;
  uint32_t limitOffset = 0;
  const char* limitDetail = "";

  // Version layer inputs.
  bool rootIsObject = false;
  bool formatMatches = false;
  bool versionAboveOne = false;
  uint32_t versionOffset = 0;

  // Entry being filled.
  uint16_t currentSection = NO_INDEX;
  uint16_t currentCheckpoint = NO_INDEX;
  uint16_t currentPrompt = NO_INDEX;

  // Section scratch for the anchor-name rule.
  bool sectionIdValid = false;
  uint8_t startAnchorLength = 0;
  uint8_t endAnchorLength = 0;
  bool startAnchorValid = false;
  bool endAnchorValid = false;
  bool startAnchorOverflow = false;
  bool endAnchorOverflow = false;
  uint32_t startAnchorOffset = 0;
  uint32_t endAnchorOffset = 0;

  // Prompt scratch for the plain-text and blank-text rules.
  uint8_t promptTextFlags = 0;
  uint8_t answerTextFlags = 0;

  // First cross-reference findings that are known during the scan.
  uint16_t anchorIssueSection = NO_INDEX;
  bool anchorIssueIsEnd = false;
  uint32_t anchorIssueOffset = 0;
  uint16_t textIssuePrompt = NO_INDEX;
  bool textIssueIsAnswer = false;
  IssueCode textIssueCode = IssueCode::None;
  uint32_t textIssueOffset = 0;
};

}  // namespace checkpoint::detail
