#include "OpfItemFinder.h"

#if CROSSPOINT_RSVP_ENABLED

#include <FsHelpers.h>
#include <expat.h>

#include <cstring>

namespace reading {

namespace {

// "opf:item" -> "item".
const char* localName(const char* name) {
  const char* colon = std::strrchr(name, ':');
  return colon ? colon + 1 : name;
}

}  // namespace

OpfItemFinder::~OpfItemFinder() {
  if (parser) XML_ParserFree(static_cast<XML_Parser>(parser));
}

bool OpfItemFinder::begin() {
  XML_Parser p = XML_ParserCreate(nullptr);
  if (!p) return false;
  parser = p;
  XML_SetUserData(p, this);
  XML_SetElementHandler(p, &OpfItemFinder::startElement, &OpfItemFinder::endElement);
  return true;
}

bool OpfItemFinder::feed(const uint8_t* data, const size_t length) {
  if (!parser || parseFailed || manifestDone) return false;
  XML_Parser p = static_cast<XML_Parser>(parser);
  if (XML_Parse(p, reinterpret_cast<const char*>(data), static_cast<int>(length), XML_FALSE) == XML_STATUS_ERROR) {
    // A stop from endElement() is not a failure.
    if (!manifestDone) parseFailed = true;
    return false;
  }
  return !manifestDone;
}

void OpfItemFinder::finish() {
  if (!parser || parseFailed || manifestDone) return;
  if (XML_Parse(static_cast<XML_Parser>(parser), "", 0, XML_TRUE) == XML_STATUS_ERROR && !manifestDone) {
    parseFailed = true;
  }
}

void OpfItemFinder::startElement(void* userData, const char* name, const char** atts) {
  auto* self = static_cast<OpfItemFinder*>(userData);
  const char* local = localName(name);
  if (std::strcmp(local, "manifest") == 0) {
    self->inManifest = true;
    return;
  }
  if (!self->inManifest || self->itemFound || std::strcmp(local, "item") != 0) return;
  const char* id = nullptr;
  const char* href = nullptr;
  const char* mediaType = nullptr;
  for (int i = 0; atts[i] && atts[i + 1]; i += 2) {
    const char* attr = localName(atts[i]);
    if (std::strcmp(attr, "id") == 0) id = atts[i + 1];
    if (std::strcmp(attr, "href") == 0) href = atts[i + 1];
    if (std::strcmp(attr, "media-type") == 0) mediaType = atts[i + 1];
  }
  if (id && href && std::strcmp(id, self->wantedId) == 0) {
    self->itemFound = true;
    self->itemHref = href;
    self->itemMediaType = mediaType ? mediaType : "";
  }
}

void OpfItemFinder::endElement(void* userData, const char* name) {
  auto* self = static_cast<OpfItemFinder*>(userData);
  if (std::strcmp(localName(name), "manifest") != 0 || !self->inManifest) return;
  self->inManifest = false;
  self->manifestDone = true;
  // The spine and guide that follow are not needed.
  XML_StopParser(static_cast<XML_Parser>(self->parser), XML_FALSE);
}

std::string resolveOpfHref(const std::string& opfPath, const std::string& href) {
  if (href.empty() || href[0] == '/' || href.find(':') != std::string::npos) return "";
  const size_t fragment = href.find('#');
  const std::string path = FsHelpers::decodeUriEscapes(fragment == std::string::npos ? href : href.substr(0, fragment));
  const size_t slash = opfPath.find_last_of('/');
  const std::string joined =
      FsHelpers::normalisePath((slash == std::string::npos ? std::string() : opfPath.substr(0, slash + 1)) + path);
  // normalisePath() resolves ".." but keeps "." segments, which no zip entry name has.
  std::string out;
  size_t start = 0;
  while (start <= joined.size()) {
    size_t end = joined.find('/', start);
    if (end == std::string::npos) end = joined.size();
    if (end > start && joined.compare(start, end - start, ".") != 0) {
      if (!out.empty()) out += '/';
      out.append(joined, start, end - start);
    }
    start = end + 1;
  }
  return out;
}

}  // namespace reading

#endif  // CROSSPOINT_RSVP_ENABLED
