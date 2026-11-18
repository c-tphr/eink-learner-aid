#pragma once

// Test-only helpers for the trigger, flow, grade and state tests: the
// synthetic fixture book (fixtures/books) as a CheckpointIndex plus a
// BookLayout, a fixed-size page model, and ReaderSim, a minimal stand-in for
// ReadingSession that drives the library the way README.md says it should.

#include <BookState.h>
#include <CheckpointFlow.h>
#include <CheckpointIndex.h>
#include <CheckpointTrigger.h>
#include <StateCodec.h>
#include <StateRestore.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "CheckpointTestSupport.h"

namespace checkpoint_test {

using namespace checkpoint;

inline std::string booksPath(const std::string& relative) { return std::string(CHECKPOINT_BOOKS_DIR) + "/" + relative; }
inline std::string testDataPath(const std::string& relative) {
  return std::string(CHECKPOINT_TEST_DATA_DIR) + "/" + relative;
}

inline std::string encodeToString(const BookState& state) {
  std::vector<char> buffer(STATE_ENCODED_MAX_BYTES);
  BufferStateSink sink(buffer.data(), buffer.size());
  if (!encodeState(state, sink)) return std::string();
  return std::string(buffer.data(), sink.length());
}

inline DecodeResult decodeString(const std::string& text, BookState& out, const char* sha = "") {
  return decodeState(reinterpret_cast<const uint8_t*>(text.data()), text.size(), sha, out);
}

// A BookLayout over explicit chapter word counts and section ranges.
class FakeLayout : public BookLayout {
 public:
  uint16_t chapterCount() const override { return static_cast<uint16_t>(words.size()); }
  bool chapterWordCount(uint16_t chapter, uint32_t& out) override {
    if (chapter >= words.size() || words[chapter] < 0) return false;
    out = static_cast<uint32_t>(words[chapter]);
    return true;
  }
  bool sectionRange(uint16_t section, SectionRange& out) override {
    auto it = sections.find(section);
    if (it == sections.end()) return false;
    out = it->second;
    return true;
  }

  std::vector<int64_t> words;  // per chapter; -1 = unknown
  std::map<uint16_t, SectionRange> sections;
};

// The synthetic book "The Lighthouse Keeper's Almanac": the checkpoint file
// prep build writes for it (data/fixture-book-checkpoints.json) and the word
// ranges of fixtures/books/fixture.expected.json. Reader chapters are its
// spine indices: 1 title page, 2 to 4 chapters 1 to 3; spine 0 is the nav
// document, with no words here. To regenerate the data file, run
// `python3 -m tools.prep build fixtures/books/fixture.epub
// fixtures/books/fixture.draft.json -o OUT.epub` and copy
// OEBPS/rsvp/checkpoints.json out of OUT.epub.
struct FixtureBook {
  std::vector<uint8_t> bytes;
  std::unique_ptr<MemoryCheckpointSource> source;
  std::unique_ptr<CheckpointIndex> index = std::make_unique<CheckpointIndex>();
  FakeLayout layout;
  std::map<std::string, uint16_t> chapterOfHref;  // href -> spine index
  bool ok = false;

  FixtureBook() {
    bytes = readFile(testDataPath("fixture-book-checkpoints.json"));
    source = std::make_unique<MemoryCheckpointSource>(bytes.data(), bytes.size());
    if (index->open(*source).verdict != Verdict::Valid) return;
    const std::vector<uint8_t> expectedBytes = readFile(booksPath("fixture.expected.json"));
    const std::string text(expectedBytes.begin(), expectedBytes.end());
    JsonValue root;
    JsonReader reader(text);
    if (!reader.parse(root)) return;
    const JsonValue* documents = root.get("documents");
    const JsonValue* sections = root.get("sections");
    if (documents == nullptr || sections == nullptr) return;
    layout.words.assign(1, 0);
    for (const JsonValue& d : documents->items) {
      const size_t spine = static_cast<size_t>(d.get("spine_index")->number);
      if (layout.words.size() <= spine) layout.words.resize(spine + 1, 0);
      layout.words[spine] = static_cast<int64_t>(d.get("word_count")->number);
      chapterOfHref[d.str("href")] = static_cast<uint16_t>(spine);
    }
    for (const JsonValue& s : sections->items) {
      const int section = index->findSection(s.str("id").c_str());
      if (section < 0) return;
      SectionRange r;
      r.section = static_cast<uint16_t>(section);
      r.chapter = chapterOfHref[s.str("chapter_href")];
      r.start = static_cast<uint32_t>(s.get("first_word")->number);
      r.end = static_cast<uint32_t>(s.get("last_word")->number) + 1;
      layout.sections[r.section] = r;
    }
    ok = true;
  }

  uint16_t checkpoint(const char* id) const { return static_cast<uint16_t>(index->findCheckpoint(id)); }
  uint16_t section(const char* id) const { return static_cast<uint16_t>(index->findSection(id)); }
  std::string promptId(uint16_t c, uint16_t p) const {
    char id[ID_BUFFER_BYTES];
    index->loadPromptId(c, p, id, sizeof(id));
    return id;
  }

  // Next reader chapter after `chapter`, or NO_CHAPTER at the end.
  uint16_t nextChapter(uint16_t chapter) const {
    return chapter + 1u < layout.words.size() ? static_cast<uint16_t>(chapter + 1) : NO_CHAPTER;
  }

  void loadChapter(ChapterSections& out, uint16_t chapter) {
    out.reset(chapter, 0, nextChapter(chapter));
    for (const auto& entry : chapterOfHref) {
      if (entry.second == chapter) {
        const int href = index->findChapter(entry.first.c_str());
        if (href >= 0) {
          out.load(*index, static_cast<uint16_t>(href), layout, chapter, nextChapter(chapter));
          return;
        }
      }
    }
    uint32_t words = 0;
    layout.chapterWordCount(chapter, words);
    out.reset(chapter, words, nextChapter(chapter));
  }
};

// Page view with a fixed number of words per page.
struct PageModel {
  uint32_t wordsPerPage = 40;

  uint32_t firstWord(uint32_t word) const { return word / wordsPerPage * wordsPerPage; }
  uint32_t lastWord(uint32_t word, uint32_t chapterWords) const {
    const uint32_t last = firstWord(word) + wordsPerPage - 1;
    return last < chapterWords - 1 ? last : chapterWords - 1;
  }
};

// Drives the library the way ReadingSession and CheckpointActivity are meant
// to. Every transition that asks for Save::Now is encoded at once; `saves`
// counts them and `lastSaved` holds the newest file.
struct ReaderSim {
  FixtureBook& book;
  std::unique_ptr<BookState> state = std::make_unique<BookState>();
  std::unique_ptr<CheckpointFlow> flow;
  std::unique_ptr<ChapterSections> layout = std::make_unique<ChapterSections>();
  PageModel pages;
  Place place;  // what is on screen while reading (position or reread place)
  int saves = 0;
  std::string lastSaved;

  explicit ReaderSim(FixtureBook& b) : book(b) {
    flow = std::make_unique<CheckpointFlow>(*state, *book.index);
    memcpy(state->bookSha256, book.index->sourceSha256(), SHA256_HEX_CHARS + 1);
  }

  void openAt(const Place& p) {
    place = p;
    book.loadChapter(*layout, p.position.chapter);
  }

  bool reading() const { return flow->state() == FlowState::Idle || flow->state() == FlowState::Finished; }
  bool rereading() const { return flow->state() == FlowState::Rereading; }
  uint32_t chapterWords() const { return layout->wordCount(); }

  void persist(const Transition& t) {
    if (t.save == Save::Now) {
      lastSaved = encodeToString(*state);
      ++saves;
    }
  }

  void moveTo(const Position& to) {
    if (to.chapter != layout->chapter()) book.loadChapter(*layout, to.chapter);
    place.position = to;
    if (reading()) {
      state->hasPosition = true;
      state->position = place;
    }
  }

  // Shows the place a transition left on screen and runs the page-view chain
  // rule: another armed end on the page of the resume word fires next.
  Transition apply(const Transition& t) {
    persist(t);
    if (!t.accepted) return t;
    if (t.finished) {
      place.mode = t.resume.mode;
      moveTo(t.resume.position);
      if (place.mode == ReadingMode::Page && t.resume.position.chapter == layout->chapter() &&
          t.resume.position.word < chapterWords()) {
        const Step next = pageAdvance(*layout, t.resume.position.word,
                                      pages.lastWord(t.resume.position.word, chapterWords()), NO_INDEX);
        if (next.kind == StepKind::Fire) apply(flow->start(next, ReadingMode::Page));
      }
    } else if (t.state == FlowState::Rereading && state->active.rereading) {
      place = state->active.reread;
      if (place.position.chapter != layout->chapter()) book.loadChapter(*layout, place.position.chapter);
    } else if (t.state == FlowState::PromptHidden) {
      place = state->hasPosition ? state->position : place;
    }
    return t;
  }

  Transition handle(FlowEvent e, const char* at = "2026-12-01T19:04:00") { return apply(flow->handle(e, at)); }

  Step onStep(const Step& step, ReadingMode mode) {
    if (step.kind == StepKind::Fire) apply(flow->start(step, mode));
    if (step.kind == StepKind::RereadEnd) apply(flow->handle(FlowEvent::RereadReachedEnd));
    return step;
  }

  // RSVP playback from the place on screen until it stops: a checkpoint, the
  // end of a reread, `maxWords` words, or the end of the book.
  Step play(uint32_t maxWords = 100000) {
    for (uint32_t n = 0; n < maxWords; ++n) {
      if (!reading() && !rereading()) return Step{};
      const uint32_t w = place.position.word;
      if (w >= chapterWords()) {
        if (layout->nextChapter() == NO_CHAPTER) return Step{};
        moveTo(Position{layout->nextChapter(), 0});
        continue;
      }
      // Word w is on screen. Ask before drawing w + 1.
      const Step step = rsvpStep(*layout, w, flow->rereadSection());
      if (step.kind != StepKind::None) return onStep(step, ReadingMode::Rsvp);
      const Position next = layout->after(w);
      if (rereading()) {
        persist(flow->rereadMoved(Place{next, ReadingMode::Rsvp}, NavSource::RsvpPlayback));
        if (!rereading()) return Step{};
      }
      moveTo(next);
    }
    return Step{};
  }

  // Page view next-page action.
  Step nextPage() {
    const uint32_t w = place.position.word;
    if (w >= chapterWords()) {
      if (layout->nextChapter() != NO_CHAPTER) moveTo(Position{layout->nextChapter(), 0});
      return Step{};
    }
    const uint32_t last = pages.lastWord(w, chapterWords());
    const Step step = pageAdvance(*layout, w, last, flow->rereadSection());
    if (step.kind != StepKind::None) return onStep(step, ReadingMode::Page);
    const Position next = layout->after(last);
    if (rereading()) {
      const Transition t = flow->rereadMoved(Place{next, ReadingMode::Page}, NavSource::NextPage);
      persist(t);
      if (!rereading()) {
        apply(t);
        return Step{};
      }
    }
    moveTo(next);
    return Step{};
  }

  // Any non-reading navigation (table of contents, go to percent...).
  Step jump(const Position& to, NavSource source) {
    const Step step = evaluateMove(*layout, place.position, to, source, flow->rereadSection());
    if (step.kind != StepKind::None) return onStep(step, place.mode);
    if (rereading()) {
      const Transition t = flow->rereadMoved(Place{to, place.mode}, source);
      persist(t);
      if (!rereading()) {
        apply(t);
        return Step{};
      }
    }
    moveTo(to);
    return Step{};
  }

  Grade stored(uint16_t c, uint16_t p) const { return state->grades.find(book.promptId(c, p).c_str()); }

  // Close: the last saved file. Reopen it in a fresh simulator.
  static std::unique_ptr<ReaderSim> reopen(FixtureBook& book, const std::string& file, RestorePlan* planOut = nullptr) {
    auto sim = std::make_unique<ReaderSim>(book);
    if (decodeString(file, *sim->state, book.index->sourceSha256()).status != DecodeStatus::Ok) return nullptr;
    applyBookChangeRule(*sim->state, book.index.get());
    const RestorePlan plan = planRestore(*sim->state, book.index.get(), book.layout);
    if (planOut != nullptr) *planOut = plan;
    if (plan.view == OpenView::Reread || plan.view == OpenView::Prompt) {
      if (!sim->flow->restore(plan).accepted) return nullptr;
      sim->openAt(plan.view == OpenView::Reread ? plan.place : sim->state->active.resume);
    } else if (plan.view == OpenView::Position) {
      sim->openAt(plan.place);
    } else {
      sim->openAt(Place{Position{0, 0}, ReadingMode::Page});
    }
    return sim;
  }
};

}  // namespace checkpoint_test
