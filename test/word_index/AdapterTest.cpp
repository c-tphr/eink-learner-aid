// IndexWordSource: the fixture book's prepared chapters through reader -> adapter -> RsvpCore give
// the tokens, frames, stops and audit that test/rsvp_core derives from fixture.expected.json
// (FixtureBook.h, PlaybackSweepTest.cpp), and a footnote body in mid-chapter is passed over without a
// stop or an audit violation.

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "FixtureBook.h"
#include "IndexWordSource.h"
#include "PlaybackFakes.h"
#include "TestSupport.h"
#include "WordIndexTestSupport.h"
#include "lib/RsvpCore/FrameBuilder.h"
#include "lib/RsvpCore/Scheduler.h"

using namespace wordindex;
using rsvptest::OwnedToken;

namespace {

constexpr const char* CHAPTERS[] = {"text/title.xhtml", "text/chapter-1.xhtml", "text/chapter-2.xhtml",
                                    "text/chapter-3.xhtml", "text/endnotes.xhtml"};

// One prepared chapter, indexed, with the section ends of its anchors (checkpoints from the draft).
struct Chapter {
  explicit Chapter(const std::string& href) {
    const std::string& zipPath = rsvptest::requireString(rsvptest::fixtureDocument(href), "zip_path", href);
    wixtest::MemoryInput input(wixtest::readFile(std::string(WIX_DATA_DIR "/prepared/") + zipPath));
    WordIndexBuilder builder;
    EXPECT_EQ(builder.build(input, file), BuildStatus::Ok);
    EXPECT_EQ(reader.open(file), ReadStatus::Ok);
    const std::set<std::string> withCheckpoint = rsvptest::fixtureSectionsWithCheckpoint();
    const auto& sections = rsvptest::requireArray(rsvptest::fixtureExpected(), "sections", "fixture.expected.json");
    for (size_t i = 0; i < sections.size(); ++i) {
      if (rsvptest::requireString(sections[i], "chapter_href", "section") != href) continue;
      Anchor end;
      EXPECT_TRUE(reader.findAnchor(rsvptest::requireString(sections[i], "end_anchor", "section"), end));
      ends.push_back({end.wordIndex, withCheckpoint.count(rsvptest::requireString(sections[i], "id", "section")) > 0});
    }
    table = {ends.data(), static_cast<uint32_t>(ends.size())};
  }
  wixtest::MemoryFile file;
  WordIndexReader reader;
  std::vector<SectionEnd> ends;
  SectionEndTable table;
};

// FixtureBook.h's tokens. Like the adapter, they treat LINE_END (a word before <br>) as a paragraph
// end, as task R3 specifies.
std::vector<OwnedToken> expectedTokens(const std::string& href) {
  return rsvptest::chapterTokens(rsvptest::fixtureDocument(href));
}

std::vector<OwnedToken> drain(rsvp::WordSource& source, std::vector<uint32_t>* skipped = nullptr) {
  std::vector<OwnedToken> out;
  rsvp::Token token;
  while (source.peek(token)) {
    rsvp::Token again;
    EXPECT_TRUE(source.peek(again));  // repeated peeks return the same token
    EXPECT_EQ(again.wordIndex, token.wordIndex);
    out.push_back({std::string(token.text), token.wordIndex, token.flags});
    if (skipped) skipped->push_back(token.skippedBefore);
    source.advance();
  }
  return out;
}

struct Width {
  size_t maxBytes;
};
Width width{22};
bool fits(void* context, std::string_view text, uint16_t) {
  return text.size() <= static_cast<Width*>(context)->maxBytes;
}

}  // namespace

TEST(Adapter, TokensAreThoseOfTheExpectedFile) {
  for (const char* href : CHAPTERS) {
    SCOPED_TRACE(href);
    Chapter chapter(href);
    IndexWordSource source(chapter.reader, SectionEndTable::lookup, &chapter.table);
    std::vector<uint32_t> skipped;
    const auto got = drain(source, &skipped);
    auto want = expectedTokens(href);
    // The adapter also marks the chapter's last token, which RsvpCore infers anyway.
    if (!want.empty()) want.back().flags |= rsvp::TOKEN_CHAPTER_END;
    ASSERT_EQ(got.size(), want.size());
    for (size_t i = 0; i < got.size(); ++i) {
      EXPECT_EQ(got[i].text, want[i].text) << i;
      EXPECT_EQ(got[i].wordIndex, want[i].wordIndex) << i;
      EXPECT_EQ(got[i].flags, want[i].flags) << got[i].text;
      EXPECT_EQ(skipped[i], 0u);  // the fixture's notes all follow the last prose word
    }
    EXPECT_FALSE(source.failed());
  }
}

TEST(Adapter, FramesAreThoseOfTheExpectedFile) {
  for (const char* href : CHAPTERS) {
    SCOPED_TRACE(href);
    Chapter chapter(href);
    for (uint8_t n = 1; n <= rsvp::MAX_WORDS_PER_FRAME; ++n) {
      IndexWordSource source(chapter.reader, SectionEndTable::lookup, &chapter.table);
      rsvp::FrameBuilder fromIndex(source, fits, &width);
      fromIndex.setWordsPerFrame(n);
      rsvptest::VectorSource vector(expectedTokens(href));
      rsvp::FrameBuilder fromFile(vector, fits, &width);
      fromFile.setWordsPerFrame(n);
      const auto a = rsvptest::allFrames(fromIndex);
      const auto b = rsvptest::allFrames(fromFile);
      ASSERT_EQ(a.size(), b.size());
      for (size_t f = 0; f < a.size(); ++f) {
        EXPECT_EQ(std::string(a[f].view()), std::string(b[f].view()));
        EXPECT_EQ(a[f].firstWordIndex, b[f].firstWordIndex);
        EXPECT_EQ(a[f].lastWordIndex, b[f].lastWordIndex);
        EXPECT_EQ(a[f].holdMs, b[f].holdMs);
        EXPECT_EQ(a[f].stops, b[f].stops);
        EXPECT_EQ(a[f].focusByteOffset, b[f].focusByteOffset);
      }
    }
  }
}

// PlaybackSweepTest's stream without its prose sample: the four fixture documents in order.
TEST(Adapter, PlaybackStopsAndAuditAreThoseOfThePlaybackSweep) {
  for (const uint8_t n : {uint8_t{1}, uint8_t{3}}) {
    SCOPED_TRACE(int(n));
    rsvptest::FakeClock clock(5000);
    rsvptest::FakeDisplay display(clock, 160, 160);
    std::vector<uint8_t> stops;
    size_t words = 0;
    size_t tableAndCode = 0;
    // One source per chapter, all behind one forwarding source so the scheduler sees one supply.
    struct Forward : rsvp::WordSource {
      rsvp::WordSource* target = nullptr;
      bool peek(rsvp::Token& out) override { return target->peek(out); }
      void advance() override { target->advance(); }
    } forward;
    std::vector<std::unique_ptr<Chapter>> chapters;
    std::vector<std::unique_ptr<IndexWordSource>> sources;
    for (const char* href :
         {"text/title.xhtml", "text/chapter-1.xhtml", "text/chapter-2.xhtml", "text/chapter-3.xhtml"}) {
      chapters.push_back(std::make_unique<Chapter>(href));
      sources.push_back(
          std::make_unique<IndexWordSource>(chapters.back()->reader, SectionEndTable::lookup, &chapters.back()->table));
      words += rsvptest::chapterTokens(rsvptest::fixtureDocument(href)).size();
      for (const auto& m : rsvptest::fixtureMarkers(rsvptest::fixtureDocument(href))) {
        if (m.kind == "TABLE" || m.kind == "CODE") tableAndCode += m.end - m.start;
      }
    }
    forward.target = sources[0].get();
    rsvp::Scheduler scheduler(forward, clock, display, fits, &width);
    scheduler.setWordsPerFrame(n);
    for (size_t ch = 0; ch < sources.size(); ++ch) {
      if (ch > 0) {
        forward.target = sources[ch].get();
        scheduler.reposition();
      }
      scheduler.play();
      while (true) {
        const rsvp::StepResult r = scheduler.step();
        if (r.event == rsvp::StepEvent::STOPPED) {
          stops.push_back(r.stops);
          if (r.stops & rsvp::STOP_CHAPTER_END) break;
          clock.advance(30000);
          scheduler.play();
          continue;
        }
        ASSERT_TRUE(r.wake);
        clock.advanceTo(r.wakeAtMs);
      }
    }
    // PlaybackSweepTest's expected stops after the prose sample's chapter end.
    const std::vector<uint8_t> expectedStops = {rsvp::STOP_CHAPTER_END, rsvp::STOP_NON_PROSE,
                                                rsvp::STOP_CHECKPOINT,  rsvp::STOP_CHAPTER_END,
                                                rsvp::STOP_NON_PROSE,   rsvp::STOP_CHECKPOINT | rsvp::STOP_CHAPTER_END,
                                                rsvp::STOP_NON_PROSE,   rsvp::STOP_CHAPTER_END};
    EXPECT_EQ(stops, expectedStops);
    EXPECT_TRUE(scheduler.audit().inOrder());
    EXPECT_EQ(scheduler.audit().wordsDrawn(), words);
    EXPECT_EQ(scheduler.audit().segments(), 4u);
    EXPECT_EQ(scheduler.audit().skippedWords(), tableAndCode);
    EXPECT_EQ(scheduler.audit().declaredSkips(), 0u);
    EXPECT_EQ(scheduler.meter().stats().wordsShown, words);
  }
}

TEST(Adapter, EndnotesDocumentHandsOutNothing) {
  Chapter chapter("text/endnotes.xhtml");
  ASSERT_GT(chapter.reader.wordCount(), 0u);
  IndexWordSource source(chapter.reader);
  rsvp::Token token;
  EXPECT_FALSE(source.peek(token));
  EXPECT_EQ(source.position(), chapter.reader.wordCount());
  EXPECT_FALSE(source.failed());
}

namespace {

// A chapter with a footnote body between paragraphs, an image inside a note, a table followed by a
// note, and a section end anchor right before the note.
const char* MID_NOTE_BODY =
    "<p>Alpha beta gamma.</p>"
    "<aside epub:type=\"footnote\" id=\"fn1\"><p>Note one two.</p><p>Fig<img src=\"n.png\" alt=\"\"/>ure</p></aside>"
    "<p>Delta epsilon.<span id=\"rsvp-s1-end\"></span></p>"
    "<table><tr><td>cell</td></tr></table>"
    "<div role=\"doc-endnote\"><p>Second note.</p></div>"
    "<p>Zeta eta theta iota.</p>";

}  // namespace

TEST(Adapter, MidChapterNoteRegionIsPassedOverSilently) {
  wixtest::Built built = wixtest::buildIndex(wixtest::xhtml(MID_NOTE_BODY));
  ASSERT_EQ(built.status, BuildStatus::Ok);
  WordIndexReader reader;
  ASSERT_EQ(reader.open(built.file), ReadStatus::Ok);
  // Words: Alpha beta gamma. (0-2) | Note one two. Fig ure (3-7, NOTE) | Delta epsilon. (8-9) |
  // cell (10, TABLE) | Second note. (11-12, NOTE) | Zeta eta theta iota. (13-16)
  ASSERT_EQ(reader.wordCount(), 17u);
  Anchor end;
  ASSERT_TRUE(reader.findAnchor("rsvp-s1-end", end));
  EXPECT_EQ(end.wordIndex, 10u);
  const SectionEnd ends[] = {{end.wordIndex, true}};
  SectionEndTable table{ends, 1};

  IndexWordSource source(reader, SectionEndTable::lookup, &table);
  std::vector<uint32_t> skipped;
  const auto tokens = drain(source, &skipped);
  std::vector<uint32_t> indexes;
  for (const auto& t : tokens) indexes.push_back(t.wordIndex);
  EXPECT_EQ(indexes, (std::vector<uint32_t>{0, 1, 2, 8, 9, 13, 14, 15, 16}));
  EXPECT_EQ(skipped, (std::vector<uint32_t>{0, 0, 0, 5, 0, 2, 0, 0, 0}));
  // No stop for the note or its image; the table stops after "epsilon.", which also ends s1.
  EXPECT_EQ(tokens[2].flags, rsvp::TOKEN_PARAGRAPH_END);
  EXPECT_EQ(tokens[4].flags, rsvp::TOKEN_PARAGRAPH_END | rsvp::TOKEN_NON_PROSE_FOLLOWS | rsvp::TOKEN_SECTION_END |
                                 rsvp::TOKEN_CHECKPOINT);
  EXPECT_EQ(tokens.back().flags, rsvp::TOKEN_PARAGRAPH_END | rsvp::TOKEN_CHAPTER_END);

  for (uint8_t n = 1; n <= rsvp::MAX_WORDS_PER_FRAME; ++n) {
    SCOPED_TRACE(int(n));
    ASSERT_TRUE(source.seek(0));
    rsvptest::FakeClock clock(1000);
    rsvptest::FakeDisplay display(clock, 100, 100);
    rsvp::Scheduler scheduler(source, clock, display);
    scheduler.setWordsPerFrame(n);
    std::vector<uint8_t> stops;
    scheduler.play();
    while (true) {
      const rsvp::StepResult r = scheduler.step();
      if (r.event == rsvp::StepEvent::STOPPED) {
        stops.push_back(r.stops);
        if (r.stops & rsvp::STOP_CHAPTER_END) break;
        scheduler.play();
        continue;
      }
      ASSERT_TRUE(r.wake);
      clock.advanceTo(r.wakeAtMs);
    }
    EXPECT_EQ(stops, (std::vector<uint8_t>{rsvp::STOP_CHECKPOINT | rsvp::STOP_NON_PROSE, rsvp::STOP_CHAPTER_END}));
    const rsvp::WordOrderAudit& audit = scheduler.audit();
    EXPECT_TRUE(audit.inOrder());
    EXPECT_EQ(audit.wordsDrawn(), 9u);
    EXPECT_EQ(audit.segments(), 1u);
    EXPECT_EQ(audit.declaredSkips(), 7u);  // the two note bodies
    EXPECT_EQ(audit.skippedWords(), 1u);   // the table cell
    // No frame joins words across a note body.
    for (const auto& call : display.frameCalls()) {
      EXPECT_EQ(call.lastWordIndex - call.firstWordIndex + 1u, call.wordCount);
    }
  }
}

TEST(Adapter, SeekLandsOnProse) {
  wixtest::Built built = wixtest::buildIndex(wixtest::xhtml(MID_NOTE_BODY));
  ASSERT_EQ(built.status, BuildStatus::Ok);
  WordIndexReader reader;
  ASSERT_EQ(reader.open(built.file), ReadStatus::Ok);
  IndexWordSource source(reader);
  const std::vector<std::pair<uint32_t, uint32_t>> cases = {{0, 0},   {2, 2},   {3, 8},   {7, 8},   {8, 8},
                                                            {10, 13}, {11, 13}, {16, 16}, {17, 17}, {99, 17}};
  for (const auto& [target, landed] : cases) {
    ASSERT_TRUE(source.seek(target));
    EXPECT_EQ(source.position(), landed) << target;
    rsvp::Token token;
    if (landed < reader.wordCount()) {
      ASSERT_TRUE(source.peek(token));
      EXPECT_EQ(token.wordIndex, landed);
      EXPECT_EQ(token.skippedBefore, 0u);  // a seek opens a new audit segment anyway
    } else {
      EXPECT_FALSE(source.peek(token));
    }
  }
}

TEST(Adapter, LeadingAndTrailingNonProse) {
  wixtest::Built built = wixtest::buildIndex(
      wixtest::xhtml("<table><tr><td>t</td></tr></table><p>one two</p><img src=\"x.png\" alt=\"\"/>"));
  ASSERT_EQ(built.status, BuildStatus::Ok);
  WordIndexReader reader;
  ASSERT_EQ(reader.open(built.file), ReadStatus::Ok);
  IndexWordSource source(reader);
  const auto tokens = drain(source);
  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].wordIndex, 1u);  // the leading table is not handed out and cannot be flagged
  EXPECT_EQ(tokens[1].flags, rsvp::TOKEN_PARAGRAPH_END | rsvp::TOKEN_NON_PROSE_FOLLOWS | rsvp::TOKEN_CHAPTER_END);
}

TEST(Adapter, ACorruptBlockStopsTheSource) {
  wixtest::Built built =
      wixtest::buildIndex(wixtest::readFile(WIX_DATA_DIR "/test_reader_menu_progress_chapter1.xhtml"));
  ASSERT_EQ(built.status, BuildStatus::Ok);
  built.file.bytes[SLOT_BYTES * 3 + 100] ^= 0xFF;  // inside the third block's used bytes
  WordIndexReader reader;
  ASSERT_EQ(reader.open(built.file), ReadStatus::Ok);  // blocks are checked when read
  IndexWordSource source(reader);
  const auto tokens = drain(source);
  EXPECT_TRUE(source.failed());
  EXPECT_EQ(reader.status(), ReadStatus::Corrupt);
  EXPECT_LT(tokens.size(), reader.wordCount());
}
