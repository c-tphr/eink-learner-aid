#pragma once

// The reader's way into speed reading (registration point in EpubReaderActivity): creates the RSVP
// activity for the page on screen and says how it was left. Without the RSVP flag, create() returns
// nothing, openedInRsvp() false, and the reader's entry points do nothing.

#include <cstdint>
#include <functional>
#include <memory>

#include "RsvpFeature.h"

class Activity;
class Epub;
class GfxRenderer;
class MappedInputManager;

namespace rsvpentry {

// How RSVP returns to the reader. The reader acts on it in its result handler (RSVP is pushed on top
// of the reader, which keeps its book and layout meanwhile).
struct HandOff {
  enum class Then : uint8_t {
    None,        // left another way (Home): the reader is gone
    PageView,    // show the page holding the position
    ReaderMenu,  // the same, with the reader menu open
    ExitBook,    // save the position as the reader's progress and go Home
  };
  Then then = Then::None;
  int spineIndex = 0;
  uint32_t visibleTextOffset = 0;  // the start of the word RSVP would show next

  // Set by the reader: saves `visibleTextOffset` in `spineIndex` as the reader's own progress (the
  // page holding it). RSVP calls it on the routes that do not return to the reader (Home gesture,
  // sleep), while the reader is still alive under it, and the reader's Exit handling uses it too.
  std::function<void(int spineIndex, uint32_t visibleTextOffset)> saveReaderProgress;
};

// Where the reader asks RSVP to open.
struct Entry {
  int spineIndex = 0;
  uint32_t pageStartOffset = 0;         // the visible-text offset at which the page on screen starts
  uint32_t pageEndOffset = UINT32_MAX;  // where the next page starts; UINT32_MAX on the chapter's last page
  bool resume = false;                  // the book opened in RSVP mode (openedInRsvp())
};

#if CROSSPOINT_RSVP_ENABLED

// RSVP opens on its pause screen with Play highlighted, at the word chosen by reading::chooseEntry()
// (README.md, "Where RSVP opens").
std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& input, std::shared_ptr<Epub> epub,
                                 const Entry& entry, std::shared_ptr<HandOff> handOff);

// True when the book's state file says it was left in RSVP: the reader then opens RSVP by itself once
// its first page is shown. Reads the state file (src/reading/BookStateFile.h).
bool openedInRsvp(const Epub& epub);

#else

inline std::unique_ptr<Activity> create(GfxRenderer&, MappedInputManager&, std::shared_ptr<Epub>, const Entry&,
                                        std::shared_ptr<HandOff>) {
  return nullptr;
}

inline bool openedInRsvp(const Epub&) { return false; }

#endif

}  // namespace rsvpentry
