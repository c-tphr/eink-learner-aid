#include "CheckpointTrigger.h"

namespace checkpoint {

void ChapterSections::reset(uint16_t chapter, uint32_t wordCount, uint16_t nextChapter) {
  total = 0;
  chapterNumber = chapter;
  words = wordCount;
  following = nextChapter;
}

ChapterSections::AddResult ChapterSections::add(uint16_t section, uint32_t start, uint32_t end, uint16_t checkpoint) {
  if (start >= end) {
    return AddResult::EmptyRange;
  }
  if (end > words) {
    return AddResult::OutOfChapter;
  }
  if (total >= MAX_SECTIONS) {
    return AddResult::Full;
  }
  uint16_t at = total;
  while (at > 0 && spans[at - 1].start > start) {
    --at;
  }
  if ((at > 0 && spans[at - 1].end > start) || (at < total && spans[at].start < end)) {
    return AddResult::Overlap;
  }
  for (uint16_t i = total; i > at; --i) {
    spans[i] = spans[i - 1];
  }
  spans[at] = SectionSpan{start, end, section, checkpoint};
  ++total;
  return AddResult::Added;
}

ChapterSections::LoadResult ChapterSections::load(const CheckpointIndex& index, uint16_t hrefChapter,
                                                  BookLayout& layout, uint16_t chapter, uint16_t nextChapter) {
  LoadResult result;
  uint32_t wordCount = 0;
  result.chapterKnown = layout.chapterWordCount(chapter, wordCount);
  reset(chapter, result.chapterKnown ? wordCount : 0, nextChapter);
  for (int s = index.nextSectionInChapter(hrefChapter, -1); s >= 0; s = index.nextSectionInChapter(hrefChapter, s)) {
    const uint16_t section = static_cast<uint16_t>(s);
    SectionRange r;
    const int checkpoint = index.sectionCheckpoint(section);
    if (result.chapterKnown && layout.sectionRange(section, r) && r.chapter == chapter &&
        add(section, r.start, r.end, checkpoint < 0 ? NO_INDEX : static_cast<uint16_t>(checkpoint)) ==
            AddResult::Added) {
      ++result.added;
    } else {
      ++result.failed;
    }
  }
  return result;
}

SectionRange ChapterSections::range(uint16_t i) const {
  SectionRange r;
  if (i < total) {
    r.section = spans[i].section;
    r.chapter = chapterNumber;
    r.start = spans[i].start;
    r.end = spans[i].end;
  }
  return r;
}

int ChapterSections::findSection(uint16_t section) const {
  for (uint16_t i = 0; i < total; ++i) {
    if (spans[i].section == section) {
      return i;
    }
  }
  return -1;
}

int ChapterSections::spanContaining(uint32_t word) const {
  for (uint16_t i = 0; i < total && spans[i].start <= word; ++i) {
    if (word < spans[i].end) {
      return i;
    }
  }
  return -1;
}

Position ChapterSections::after(uint32_t lastWord) const {
  if (lastWord + 1 < words) {
    return Position{chapterNumber, lastWord + 1};
  }
  if (following != NO_CHAPTER) {
    return Position{following, 0};
  }
  return Position{chapterNumber, words};
}

namespace {

// Words [first, last] were read forward. Which section end do they cross?
Step crossing(const ChapterSections& layout, uint32_t first, uint32_t last, uint16_t rereadSection) {
  Step step;
  for (uint16_t i = 0; i < layout.count(); ++i) {
    const SectionSpan& s = layout.span(i);
    const uint32_t lastWord = s.end - 1;
    if (lastWord > last) {
      break;  // spans are sorted and disjoint, so their ends increase
    }
    if (lastWord < first) {
      continue;
    }
    if (rereadSection != NO_INDEX) {
      if (s.section == rereadSection) {
        step.kind = StepKind::RereadEnd;
        step.section = layout.range(i);
        return step;
      }
      continue;
    }
    if (s.checkpoint == NO_INDEX) {
      continue;
    }
    if (step.kind == StepKind::None) {
      step.kind = StepKind::Fire;
      step.checkpoint = s.checkpoint;
      step.section = layout.range(i);
      step.resume = layout.after(lastWord);
    }
    ++step.crossed;
  }
  return step;
}

}  // namespace

Step evaluateMove(const ChapterSections& layout, const Position& from, const Position& to, NavSource source,
                  uint16_t rereadSection) {
  if (!isOrdinaryForward(source) || from.chapter != layout.chapter()) {
    return Step{};
  }
  if (to.chapter == from.chapter) {
    if (to.word <= from.word) {
      return Step{};
    }
    return crossing(layout, from.word, to.word - 1, rereadSection);
  }
  if (to.chapter < from.chapter || layout.wordCount() == 0 || from.word >= layout.wordCount()) {
    return Step{};
  }
  return crossing(layout, from.word, layout.wordCount() - 1, rereadSection);
}

Step rsvpStep(const ChapterSections& layout, uint32_t shownWord, uint16_t rereadSection) {
  return crossing(layout, shownWord, shownWord, rereadSection);
}

bool stopAfterWord(const ChapterSections& layout, uint32_t shownWord, uint16_t rereadSection) {
  return rsvpStep(layout, shownWord, rereadSection).kind != StepKind::None;
}

Step pageAdvance(const ChapterSections& layout, uint32_t fromWord, uint32_t pageLastWord, uint16_t rereadSection) {
  if (fromWord > pageLastWord) {
    return Step{};
  }
  return crossing(layout, fromWord, pageLastWord, rereadSection);
}

int armedCheckpoint(const ChapterSections& layout, uint32_t word) {
  const int i = layout.spanContaining(word);
  if (i < 0 || layout.span(static_cast<uint16_t>(i)).checkpoint == NO_INDEX) {
    return -1;
  }
  return layout.span(static_cast<uint16_t>(i)).checkpoint;
}

}  // namespace checkpoint
