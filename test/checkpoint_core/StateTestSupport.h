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

#include <gtest/gtest.h>

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

// Members of fixtures/books/fixture.expected.json the tests read. A missing
// member or one of the wrong type is a test failure, never a silent zero.
inline const JsonValue* requireJson(const JsonValue* object, const char* key, JsonValue::Type type,
                                    const std::string& where) {
  if (object == nullptr || object->type != JsonValue::Type::Object) {
    ADD_FAILURE() << where << " is not an object (looking for '" << key << "')";
    return nullptr;
  }
  const JsonValue* value = object->get(key);
  if (value == nullptr) {
    ADD_FAILURE() << where << " has no member '" << key << "'";
    return nullptr;
  }
  if (value->type != type) {
    ADD_FAILURE() << where << "." << key << " has the wrong type";
    return nullptr;
  }
  return value;
}

// A non-negative integer member; false (after a test failure) otherwise.
inline bool requireJsonIndex(const JsonValue* object, const char* key, const std::string& where, uint32_t& out) {
  const JsonValue* value = requireJson(object, key, JsonValue::Type::Number, where);
  if (value == nullptr) return false;
  if (value->number < 0 || value->number > 4294967295.0 || value->number != static_cast<double>(static_cast<uint32_t>(value->number))) {
    ADD_FAILURE() << where << "." << key << " is not a non-negative integer";
    return false;
  }
  out = static_cast<uint32_t>(value->number);
  return true;
}

// The synthetic book "The Lighthouse Keeper's Almanac": the checkpoint file
// prep build writes for it (data/fixture-book-checkpoints.json) and the word
// model of fixtures/books/fixture.expected.json (version 2): a chapter's word
// indexes count every word of the chapter, table, code and footnote-body words
// included, and a section is [start, end) between its anchors. Reader chapters
// are its spine indices: 1 title page, 2 to 4 chapters 1 to 3; reader chapter
// 0 has no words here.
//
// To regenerate the data file:
//   python3 -m tools.prep build fixtures/books/fixture.epub fixtures/books/fixture.draft.json -o OUT.epub
//   unzip -p OUT.epub OEBPS/rsvp/checkpoints.json > test/checkpoint_core/data/fixture-book-checkpoints.json
struct FixtureBook {
  std::vector<uint8_t> bytes;
  std::unique_ptr<MemoryCheckpointSource> source;
  std::unique_ptr<CheckpointIndex> index = std::make_unique<CheckpointIndex>();
  FakeLayout layout;
  std::map<std::string, uint16_t> chapterOfHref;  // href -> spine index
  std::map<std::string, SectionRange> sectionById;  // from fixture.expected.json
  std::string epubSha256;                         // fixture.expected.json's epub_sha256
  bool ok = false;

  FixtureBook() {
    bytes = readFile(testDataPath("fixture-book-checkpoints.json"));
    source = std::make_unique<MemoryCheckpointSource>(bytes.data(), bytes.size());
    if (index->open(*source).verdict != Verdict::Valid) {
      ADD_FAILURE() << "data/fixture-book-checkpoints.json does not open";
      return;
    }
    const std::vector<uint8_t> expectedBytes = readFile(booksPath("fixture.expected.json"));
    const std::string text(expectedBytes.begin(), expectedBytes.end());
    JsonValue root;
    JsonReader reader(text);
    if (!reader.parse(root)) {
      ADD_FAILURE() << "fixture.expected.json does not parse";
      return;
    }
    const std::string file = "fixture.expected.json";
    uint32_t version = 0;
    if (!requireJsonIndex(&root, "version", file, version)) return;
    if (version != 2) {
      ADD_FAILURE() << file << " has format version " << version << "; these helpers read version 2";
      return;
    }
    const JsonValue* sha = requireJson(&root, "epub_sha256", JsonValue::Type::String, file);
    const JsonValue* documents = requireJson(&root, "documents", JsonValue::Type::Array, file);
    const JsonValue* sections = requireJson(&root, "sections", JsonValue::Type::Array, file);
    if (sha == nullptr || documents == nullptr || sections == nullptr) return;
    epubSha256 = sha->text;
    layout.words.assign(1, 0);
    for (size_t i = 0; i < documents->items.size(); ++i) {
      const JsonValue& d = documents->items[i];
      const std::string where = file + " documents[" + std::to_string(i) + "]";
      uint32_t spine = 0;
      uint32_t words = 0;
      const JsonValue* href = requireJson(&d, "href", JsonValue::Type::String, where);
      if (!requireJsonIndex(&d, "spine_index", where, spine) || !requireJsonIndex(&d, "word_count", where, words) ||
          href == nullptr) {
        return;
      }
      if (layout.words.size() <= spine) layout.words.resize(spine + 1, 0);
      layout.words[spine] = static_cast<int64_t>(words);
      chapterOfHref[href->text] = static_cast<uint16_t>(spine);
    }
    for (size_t i = 0; i < sections->items.size(); ++i) {
      const JsonValue& s = sections->items[i];
      const std::string where = file + " sections[" + std::to_string(i) + "]";
      const JsonValue* id = requireJson(&s, "id", JsonValue::Type::String, where);
      const JsonValue* href = requireJson(&s, "chapter_href", JsonValue::Type::String, where);
      SectionRange r;
      if (id == nullptr || href == nullptr || !requireJsonIndex(&s, "start", where, r.start) ||
          !requireJsonIndex(&s, "end", where, r.end)) {
        return;
      }
      const int section = index->findSection(id->text.c_str());
      if (section < 0 || chapterOfHref.count(href->text) == 0 || r.end <= r.start) {
        ADD_FAILURE() << where << ": section " << id->text << " is not in the checkpoint file or has a bad range";
        return;
      }
      r.section = static_cast<uint16_t>(section);
      r.chapter = chapterOfHref[href->text];
      layout.sections[r.section] = r;
      sectionById[id->text] = r;
    }
    ok = true;
  }

  // The fixture's word range of section `id` (fails the test if unknown).
  SectionRange range(const char* id) const {
    const auto it = sectionById.find(id);
    if (it == sectionById.end()) {
      ADD_FAILURE() << "no section " << id << " in fixture.expected.json";
      return SectionRange{};
    }
    return it->second;
  }
  // Words of reader chapter `chapter`.
  uint32_t chapterWords(uint16_t chapter) const {
    return chapter < layout.words.size() ? static_cast<uint32_t>(layout.words[chapter]) : 0;
  }
  // The section's first word and its last word (end - 1).
  Position firstWordOf(const char* id) const {
    const SectionRange r = range(id);
    return Position{r.chapter, r.start};
  }
  Position lastWordOf(const char* id) const {
    const SectionRange r = range(id);
    return Position{r.chapter, r.end - 1};
  }
  // The first word after the section's break, from the fixture's own numbers:
  // its end, or word 0 of the next chapter when the section ends its chapter.
  Position resumeAfter(const char* id) const {
    const SectionRange r = range(id);
    if (r.end < chapterWords(r.chapter)) return Position{r.chapter, r.end};
    return Position{nextChapter(r.chapter), 0};
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
