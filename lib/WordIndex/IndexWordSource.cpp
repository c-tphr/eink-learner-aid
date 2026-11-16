#include "IndexWordSource.h"

namespace wordindex {

uint8_t SectionEndTable::lookup(void* context, const uint32_t firstAnchorIndex, const uint32_t lastAnchorIndex) {
  const auto* table = static_cast<const SectionEndTable*>(context);
  uint8_t flags = 0;
  for (uint32_t i = 0; i < table->count; ++i) {
    const SectionEnd& end = table->ends[i];
    if (end.endAnchorIndex < firstAnchorIndex || end.endAnchorIndex > lastAnchorIndex) continue;
    flags |= rsvp::TOKEN_SECTION_END;
    if (end.checkpoint) flags |= rsvp::TOKEN_CHECKPOINT;
  }
  return flags;
}

IndexWordSource::IndexWordSource(WordIndexReader& wordIndex, const SectionEndFn ends, void* endsContext)
    : reader(wordIndex), sectionEnds(ends), context(endsContext) {
  seek(0);
}

bool IndexWordSource::nextProse(const uint32_t from, uint32_t& prose, uint32_t& noteWords, bool& nonProse) {
  const uint32_t count = reader.wordCount();
  noteWords = 0;
  nonProse = false;
  uint32_t at = from;
  while (true) {
    // An image outside a note sits right before word `at`.
    bool image = false;
    if (!imageBefore(at, image)) return false;
    if (image) nonProse = true;
    if (at >= count) break;
    uint8_t wordFlags = 0;
    if (!reader.wordFlags(at, wordFlags)) return false;
    if ((wordFlags & (NOTE | NONPROSE)) == 0) break;
    const bool note = (wordFlags & NOTE) != 0;
    Marker region;
    const uint8_t kinds = note ? kindBit(MARKER_NOTE) : (kindBit(MARKER_TABLE) | kindBit(MARKER_CODE));
    uint32_t next = at + 1;
    if (reader.regionContaining(at, kinds, region) && region.wordEnd > at) next = region.wordEnd;
    if (reader.status() != ReadStatus::Ok) return false;
    if (note) {
      noteWords += next - at;
    } else {
      nonProse = true;
    }
    at = next;
  }
  prose = at;
  return true;
}

bool IndexWordSource::imageBefore(const uint32_t at, bool& image) {
  // The next image is cached: playback asks for every word in order.
  if (!imageCacheValid || at < imageCacheFrom || at > imageCacheNext) {
    imageCacheNext = UINT32_MAX;
    Marker m;
    uint32_t position = 0;
    bool found = reader.nextMarker(at, kindBit(MARKER_IMAGE), m, &position);
    while (found && (m.flags & MARKER_IN_NOTE)) {
      found = false;
      for (uint32_t i = position + 1; i < reader.markerCount() && !found; ++i) {
        if (!reader.marker(i, m)) break;
        if (m.kind == MARKER_IMAGE) {
          found = true;
          position = i;
        }
      }
    }
    if (reader.status() != ReadStatus::Ok) return false;
    if (found) imageCacheNext = m.wordStart;
    imageCacheFrom = at;
    imageCacheValid = true;
  }
  image = at == imageCacheNext;
  return true;
}

bool IndexWordSource::seek(const uint32_t index) {
  prepared = false;
  skippedBefore = 0;
  uint32_t prose = reader.wordCount();
  uint32_t notes = 0;
  bool nonProse = false;
  if (index < reader.wordCount() && !nextProse(index, prose, notes, nonProse)) {
    error = true;
    return false;
  }
  current = prose;
  error = false;
  return true;
}

bool IndexWordSource::prepare() {
  uint8_t wordFlags = 0;
  bool nonProse = false;
  if (!reader.wordFlags(current, wordFlags) || !nextProse(current + 1, following, followingSkipped, nonProse)) {
    error = true;
    return false;
  }
  flags = 0;
  if (wordFlags & (PARA_END | LINE_END)) flags |= rsvp::TOKEN_PARAGRAPH_END;
  if (nonProse) flags |= rsvp::TOKEN_NON_PROSE_FOLLOWS;
  if (following >= reader.wordCount()) flags |= rsvp::TOKEN_CHAPTER_END;
  if (sectionEnds != nullptr) {
    flags |= static_cast<uint8_t>(sectionEnds(context, current + 1, following) &
                                  (rsvp::TOKEN_SECTION_END | rsvp::TOKEN_CHECKPOINT));
  }
  prepared = true;
  return true;
}

bool IndexWordSource::peek(rsvp::Token& out) {
  if (error || current >= reader.wordCount()) return false;
  if (!prepared && !prepare()) return false;
  WordInfo w;
  if (!reader.word(current, w)) {
    error = true;
    return false;
  }
  out.text = w.text;
  out.wordIndex = current;
  out.flags = flags;
  out.skippedBefore = skippedBefore;
  return true;
}

void IndexWordSource::advance() {
  if (error || current >= reader.wordCount()) return;
  if (!prepared && !prepare()) return;
  current = following;
  skippedBefore = followingSkipped;
  prepared = false;
}

}  // namespace wordindex
