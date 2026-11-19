#include "WordIndexTestSupport.h"

#include <gtest/gtest.h>

namespace wixtest {

Contents readAll(MemoryFile& file) {
  Contents out;
  wordindex::WordIndexReader reader;
  const wordindex::ReadStatus status = reader.open(file);
  EXPECT_EQ(status, wordindex::ReadStatus::Ok) << wordindex::readStatusName(status);
  if (status != wordindex::ReadStatus::Ok) return out;
  for (uint32_t i = 0; i < reader.wordCount(); ++i) {
    wordindex::WordInfo w;
    if (!reader.word(i, w)) {
      ADD_FAILURE() << "word " << i << ": " << wordindex::readStatusName(reader.status());
      return out;
    }
    out.words.push_back({std::string(w.text), w.start, w.end, w.flags});
  }
  for (uint32_t i = 0; i < reader.markerCount(); ++i) {
    wordindex::Marker m;
    EXPECT_TRUE(reader.marker(i, m));
    out.markers.push_back(m);
  }
  wordindex::AnchorCursor cursor;
  wordindex::Anchor anchor;
  while (reader.nextAnchor(cursor, anchor)) out.anchors.push_back(anchor);
  EXPECT_EQ(out.anchors.size(), reader.anchorCount());
  return out;
}

const char* markerKindName(const uint8_t kind) {
  switch (kind) {
    case wordindex::MARKER_IMAGE:
      return "IMAGE";
    case wordindex::MARKER_TABLE:
      return "TABLE";
    case wordindex::MARKER_CODE:
      return "CODE";
    case wordindex::MARKER_NOTE:
      return "NOTE";
  }
  return "?";
}

}  // namespace wixtest
