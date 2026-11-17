#pragma once

// Finds one manifest item of an EPUB package document (the OPF) by its id, streaming the OPF through
// expat: the item's href and media type, and nothing else. Used by CheckpointBook to find the
// prepared book's `rsvp-checkpoints` item (docs/PLAN.md section 5) without changing lib/Epub, whose
// parser keeps no manifest ids. Pure apart from expat and FsHelpers: host-tested in
// test/checkpoint_activity.
//
// Memory: the expat parser (about 1 KB plus its buffers, freed by the destructor) and the two short
// strings it finds. Elements are matched by local name ("item" and "opf:item" alike), so a prefixed
// package document works too.

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <cstddef>
#include <cstdint>
#include <string>

namespace reading {

class OpfItemFinder {
 public:
  // `id` must stay valid for the finder's life.
  explicit OpfItemFinder(const char* id) : wantedId(id) {}
  ~OpfItemFinder();
  OpfItemFinder(const OpfItemFinder&) = delete;
  OpfItemFinder& operator=(const OpfItemFinder&) = delete;

  // Creates the parser. False when there is no memory for it.
  bool begin();

  // Parses the next bytes of the OPF. False once nothing more is needed: the manifest has ended (the
  // item was found or is not there) or the XML is malformed (failed()).
  bool feed(const uint8_t* data, size_t length);
  // The OPF has ended: a document that is cut short or malformed at its end becomes failed().
  void finish();

  bool found() const { return itemFound; }
  bool failed() const { return parseFailed; }
  // The item's href as written (a URL relative to the OPF) and its media type. "" unless found().
  const std::string& href() const { return itemHref; }
  const std::string& mediaType() const { return itemMediaType; }

 private:
  static void startElement(void* userData, const char* name, const char** atts);
  static void endElement(void* userData, const char* name);

  const char* wantedId;
  void* parser = nullptr;  // XML_Parser, kept opaque so that this header does not include expat
  bool inManifest = false;
  bool manifestDone = false;
  bool itemFound = false;
  bool parseFailed = false;
  std::string itemHref;
  std::string itemMediaType;
};

// The zip path of an item: `href` (percent-decoded, fragment dropped) relative to the folder of
// `opfPath`, normalised as lib/Epub normalises item paths (FsHelpers), without "." segments. "" when
// the href is empty, absolute or has a scheme.
std::string resolveOpfHref(const std::string& opfPath, const std::string& href);

}  // namespace reading

#endif  // CROSSPOINT_RSVP_ENABLED
