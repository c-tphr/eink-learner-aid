#include "CheckpointSimEntry.h"

#if CROSSPOINT_RSVP_ENABLED && defined(SIMULATOR)

#include <Arduino.h>
#include <CheckpointTrigger.h>
#include <Epub.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "CheckpointLaunch.h"
#include "CrossPointState.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "reading/ChapterWordIndex.h"
#include "reading/CheckpointBook.h"

namespace {

constexpr uint32_t READER_SETTLE_MS = 500;  // the reader's first page is on screen and saved

enum class State { UNCHECKED, WAITING, DONE };

// The Fire step of reading past the last word of checkpoint `c`'s section: its word range from the
// chapter's word index (anchors), and the trigger rule's own resume position.
bool fireStep(const Epub& epub, const reading::CheckpointBook& book, const uint16_t c, checkpoint::Step& out) {
  const checkpoint::CheckpointIndex& index = book.index();
  const uint16_t section = index.checkpointSection(c);
  const int spine = book.sectionSpine(epub, section);
  char startId[checkpoint::ID_BUFFER_BYTES + 16];
  char endId[checkpoint::ID_BUFFER_BYTES + 16];
  if (spine < 0 || !book.sectionStartAnchor(section, startId, sizeof(startId)) ||
      !book.sectionEndAnchor(section, endId, sizeof(endId))) {
    LOG_ERR("CPT", "sim entry: section %s is not in the spine", index.sectionId(section));
    return false;
  }
  reading::ChapterWordIndex words;
  wordindex::Anchor start;
  wordindex::Anchor end;
  if (!words.open(epub, spine) || !words.reader().findAnchor(startId, start) ||
      !words.reader().findAnchor(endId, end) || end.wordIndex <= start.wordIndex) {
    LOG_ERR("CPT", "sim entry: anchors %s / %s not found in spine %d", startId, endId, spine);
    return false;
  }
  // 6 KB for this call (every section of a book may share one chapter).
  auto sections = makeUniqueNoThrow<checkpoint::ChapterSections>();
  if (!sections) return false;
  const uint16_t next = spine + 1 < epub.getSpineItemsCount() ? static_cast<uint16_t>(spine + 1) : checkpoint::NO_CHAPTER;
  sections->reset(static_cast<uint16_t>(spine), words.reader().wordCount(), next);
  if (sections->add(section, start.wordIndex, end.wordIndex, c) != checkpoint::ChapterSections::AddResult::Added) {
    return false;
  }
  out = checkpoint::rsvpStep(*sections, end.wordIndex - 1);
  return out.kind == checkpoint::StepKind::Fire;
}

void openCheckpoint(GfxRenderer& renderer, MappedInputManager& input, const std::string& id,
                    const checkpoint::ReadingMode mode) {
  // The reader's render task stays out while the book is read and the index built.
  RenderLock lock;
  const std::string path = APP_STATE.openEpubPath;
  // A second Epub over the reader's cache (metadata only), for this temporary entry.
  auto epub = std::make_shared<Epub>(path, "/.crosspoint");
  if (!epub->load(false, true)) {
    LOG_ERR("CPT", "sim entry: cannot load %s", path.c_str());
    return;
  }
  reading::CheckpointData data = reading::CheckpointData::None;
  std::shared_ptr<reading::CheckpointBook> book = reading::CheckpointBook::open(*epub, &data);
  if (!book) {
    LOG_ERR("CPT", "sim entry: %s has no usable checkpoint data (%s)", path.c_str(), reading::checkpointDataName(data));
    return;
  }
  const int c = book->findCheckpoint(id.c_str());
  if (c < 0) {
    LOG_ERR("CPT", "sim entry: no checkpoint %s in %s", id.c_str(), path.c_str());
    return;
  }
  checkpointentry::Request request;
  request.mode = mode;
  if (!fireStep(*epub, *book, static_cast<uint16_t>(c), request.fire)) return;
  LOG_DBG("CPT", "sim entry checkpoint=%s mode=%s ch=%u start=%lu end=%lu", id.c_str(),
          mode == checkpoint::ReadingMode::Rsvp ? "rsvp" : "page", request.fire.section.chapter,
          static_cast<unsigned long>(request.fire.section.start), static_cast<unsigned long>(request.fire.section.end));
  auto activity = checkpointentry::create(renderer, input, path, std::move(book), request, nullptr);
  if (activity) activityManager.pushActivity(std::move(activity));
}

}  // namespace

void runCheckpointSimEntryIfRequested(GfxRenderer& renderer, MappedInputManager& input) {
  static State state = State::UNCHECKED;
  static std::string id;
  static checkpoint::ReadingMode mode = checkpoint::ReadingMode::Page;
  static uint32_t readerSince = 0;
  if (state == State::DONE) return;

  if (state == State::UNCHECKED) {
    const char* text = std::getenv("CROSSPOINT_SIM_CHECKPOINT");
    if (text == nullptr || *text == '\0') {
      state = State::DONE;
      return;
    }
    const std::string value = text;
    const size_t comma = value.find(',');
    id = value.substr(0, comma);
    const std::string modeText = comma == std::string::npos ? "page" : value.substr(comma + 1);
    if (id.empty() || (modeText != "page" && modeText != "rsvp")) {
      LOG_ERR("CPT", "CROSSPOINT_SIM_CHECKPOINT='%s' must be <checkpoint id>[,rsvp|page]", text);
      std::abort();
    }
    mode = modeText == "rsvp" ? checkpoint::ReadingMode::Rsvp : checkpoint::ReadingMode::Page;
    state = State::WAITING;
  }

  // An EPUB in front with a page laid out, and the reader not drawing (its fields are read under
  // the RenderLock).
  bool laidOut = false;
  if (activityManager.isForegroundReader()) {
    RenderLock lock(RenderLock::Mode::Try);
    if (lock.ownsLock()) {
      const ScreenshotInfo info = activityManager.getScreenshotInfo();
      laidOut = info.readerType == ScreenshotInfo::ReaderType::Epub && info.totalPages > 0;
    }
  }
  if (!laidOut) {
    readerSince = 0;
    return;
  }
  const uint32_t now = static_cast<uint32_t>(millis());
  if (readerSince == 0) readerSince = now == 0 ? 1 : now;
  if (now - readerSince < READER_SETTLE_MS) return;

  state = State::DONE;
  openCheckpoint(renderer, input, id, mode);
}

#endif  // CROSSPOINT_RSVP_ENABLED && defined(SIMULATOR)
