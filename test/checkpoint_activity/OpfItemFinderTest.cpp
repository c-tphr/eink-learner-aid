// The OPF manifest lookup of the checkpoint data loader (src/reading/OpfItemFinder.h): the
// `rsvp-checkpoints` item of a prepared book (docs/PLAN.md section 5) found by id, in one piece or fed
// in small chunks, with or without a namespace prefix; absent in an ordinary book; malformed XML
// reported; the stream stops once the manifest has ended. And the item's zip path.

#include <gtest/gtest.h>

#include <string>

#include "reading/OpfItemFinder.h"

using reading::OpfItemFinder;

namespace {

// The fixture's package document (fixtures/books/fixture.prepared.epub, OEBPS/content.opf).
const char* const PREPARED_OPF = R"(<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="book-id" xml:lang="en">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:identifier id="book-id">urn:uuid:2b6afafc-70f4-5bfc-99c8-9b5cc02d7b39</dc:identifier>
<dc:title>The Lighthouse Keeper's Almanac</dc:title>
<meta property="dcterms:modified">2026-01-01T00:00:00Z</meta>
</metadata>
<manifest>
<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
<item id="chapter-1" href="text/chapter-1.xhtml" media-type="application/xhtml+xml"/>
<item id="rsvp-checkpoints" href="rsvp/checkpoints.json" media-type="application/json"/>
</manifest>
<spine toc="ncx">
<itemref idref="chapter-1"/>
</spine>
</package>
)";

struct FindResult {
  bool found = false;
  bool failed = false;
  bool stoppedEarly = false;  // feed() asked for no more before the end of the input
  std::string href;
  std::string mediaType;
};

FindResult find(const std::string& xml, const size_t chunk, const char* id = "rsvp-checkpoints") {
  OpfItemFinder finder(id);
  EXPECT_TRUE(finder.begin());
  FindResult run;
  size_t at = 0;
  while (at < xml.size()) {
    const size_t n = xml.size() - at < chunk ? xml.size() - at : chunk;
    const bool more = finder.feed(reinterpret_cast<const uint8_t*>(xml.data() + at), n);
    at += n;
    if (!more) {
      run.stoppedEarly = at < xml.size();
      break;
    }
  }
  finder.finish();
  run.found = finder.found();
  run.failed = finder.failed();
  run.href = finder.href();
  run.mediaType = finder.mediaType();
  return run;
}

}  // namespace

TEST(OpfItemFinder, FindsThePreparedBooksItemInAnyChunking) {
  for (const size_t chunk : {size_t{1}, size_t{7}, size_t{64}, size_t{1024}, size_t{100000}}) {
    const FindResult run = find(PREPARED_OPF, chunk);
    EXPECT_TRUE(run.found) << chunk;
    EXPECT_FALSE(run.failed) << chunk;
    EXPECT_EQ(run.href, "rsvp/checkpoints.json");
    EXPECT_EQ(run.mediaType, "application/json");
    EXPECT_TRUE(run.stoppedEarly || chunk >= std::string(PREPARED_OPF).size()) << "the spine is not read";
  }
}

TEST(OpfItemFinder, AnOrdinaryBookHasNoItem) {
  std::string opf = PREPARED_OPF;
  opf.erase(opf.find("<item id=\"rsvp-checkpoints\""), opf.find("</manifest>") - opf.find("<item id=\"rsvp-checkpoints\""));
  const FindResult run = find(opf, 64);
  EXPECT_FALSE(run.found);
  EXPECT_FALSE(run.failed);
  EXPECT_EQ(run.href, "");
  // Ids are matched exactly.
  EXPECT_FALSE(find(PREPARED_OPF, 64, "rsvp-checkpoint").found);
  EXPECT_FALSE(find(PREPARED_OPF, 64, "RSVP-CHECKPOINTS").found);
}

TEST(OpfItemFinder, PrefixedPackageAndAttributeOrder) {
  const std::string opf = R"(<?xml version="1.0"?>
<opf:package xmlns:opf="http://www.idpf.org/2007/opf" version="2.0">
<opf:manifest>
  <opf:item media-type="application/json" href="data/cp.json" id="rsvp-checkpoints"/>
</opf:manifest>
</opf:package>)";
  const FindResult run = find(opf, 16);
  EXPECT_TRUE(run.found);
  EXPECT_EQ(run.href, "data/cp.json");
  EXPECT_EQ(run.mediaType, "application/json");
}

TEST(OpfItemFinder, OnlyManifestItemsCount) {
  // An element with the id elsewhere (a guide reference, metadata) is not the manifest item.
  const std::string opf = R"(<package><metadata><item id="rsvp-checkpoints" href="wrong.json"/></metadata>
<manifest><item id="other" href="x.xhtml"/></manifest></package>)";
  const FindResult run = find(opf, 32);
  EXPECT_FALSE(run.found);
  EXPECT_FALSE(run.failed);
}

TEST(OpfItemFinder, MalformedXmlIsReported) {
  const FindResult run = find("<package><manifest><item id=\"rsvp-checkpoints\" href=\"a.json\"></manifest>", 8);
  EXPECT_TRUE(run.failed);
}

TEST(OpfItemFinder, ResolvesTheItemsZipPath) {
  EXPECT_EQ(reading::resolveOpfHref("OEBPS/content.opf", "rsvp/checkpoints.json"), "OEBPS/rsvp/checkpoints.json");
  EXPECT_EQ(reading::resolveOpfHref("epub/content.opf", "rsvp/checkpoints.json"), "epub/rsvp/checkpoints.json");
  EXPECT_EQ(reading::resolveOpfHref("content.opf", "rsvp/checkpoints.json"), "rsvp/checkpoints.json");
  EXPECT_EQ(reading::resolveOpfHref("a/b/content.opf", "../rsvp/c.json"), "a/rsvp/c.json");
  EXPECT_EQ(reading::resolveOpfHref("OEBPS/content.opf", "./rsvp/check%20points.json#x"),
            "OEBPS/rsvp/check points.json");
  EXPECT_EQ(reading::resolveOpfHref("OEBPS/content.opf", ""), "");
  EXPECT_EQ(reading::resolveOpfHref("OEBPS/content.opf", "/abs.json"), "");
  EXPECT_EQ(reading::resolveOpfHref("OEBPS/content.opf", "http://example.com/c.json"), "");
}
