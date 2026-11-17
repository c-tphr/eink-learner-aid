#pragma once

// The reader's way into speed reading (registration point in EpubReaderActivity): creates the RSVP
// activity for the page on screen and says how it was left. Without the RSVP flag, create() returns
// nothing, openedInRsvp() false, and the reader's entry points do nothing.

#include <cstdint>
#include <functional>
#include <memory>

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED
#include <DeliveredWpmMeter.h>
#include <WordOrderAudit.h>

#include "reading/RsvpValues.h"
#endif

class Activity;
class Epub;
class GfxRenderer;
class MappedInputManager;

namespace rsvpentry {

#if CROSSPOINT_RSVP_ENABLED
// What RSVP keeps while page view shows a non-prose item (task R5; README.md, "Non-prose items").
struct ItemStop {
  uint8_t kind = 0;                                     // wordindex::MarkerKind of the first item of the run
  uint32_t nextWord = 0;                                // the first prose word after the run: where Continue opens RSVP
  reading::RsvpValues values = reading::RSVP_DEFAULTS;  // the pace in use, saved or not
  rsvp::DeliveredWpmMeter meter;
  rsvp::WordOrderAudit audit;
  bool afterNonProseStop = false;  // the audit may skip the item's own word indexes once
};
#endif

// How RSVP returns to the reader. The reader acts on it in its result handler (RSVP is pushed on top
// of the reader, which keeps its book and layout meanwhile).
struct HandOff {
  enum class Then : uint8_t {
    None,        // left another way (Home): the reader is gone
    PageView,    // show the page holding the position
    ReaderMenu,  // the same, with the reader menu open
    ExitBook,    // save the position as the reader's progress and go Home
    ShowItem,    // RSVP stopped at an image, table or code block: show its page with the banner
    Continue,    // the banner's Continue: open RSVP again after the item
  };
  Then then = Then::None;
  int spineIndex = 0;
  uint32_t visibleTextOffset = 0;  // the start of the word RSVP would show next; ShowItem: of the item
  int8_t pageTurn = 0;             // the banner was left by a swipe: +1 next page, -1 previous page

  // Set by the reader: saves `visibleTextOffset` in `spineIndex` as the reader's own progress (the
  // page holding it). RSVP calls it on the routes that do not return to the reader (Home gesture,
  // sleep), while the reader is still alive under it, and the reader's Exit handling uses it too.
  std::function<void(int spineIndex, uint32_t visibleTextOffset)> saveReaderProgress;

#if CROSSPOINT_RSVP_ENABLED
  ItemStop item;  // ShowItem and Continue
#endif
};

// Where the reader asks RSVP to open.
struct Entry {
  int spineIndex = 0;
  uint32_t pageStartOffset = 0;         // the visible-text offset at which the page on screen starts
  uint32_t pageEndOffset = UINT32_MAX;  // where the next page starts; UINT32_MAX on the chapter's last page
  bool resume = false;                  // the book opened in RSVP mode (openedInRsvp())
  std::shared_ptr<HandOff> after;       // Continue after a non-prose item: its HandOff (task R5)
};

#if CROSSPOINT_RSVP_ENABLED

// RSVP opens on its pause screen with Play highlighted, at the word chosen by reading::chooseEntry()
// (README.md, "Where RSVP opens").
std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& input, std::shared_ptr<Epub> epub,
                                 const Entry& entry, std::shared_ptr<HandOff> handOff);

// True when the book's state file says it was left in RSVP: the reader then opens RSVP by itself once
// its first page is shown. Reads the state file (src/reading/BookStateFile.h).
bool openedInRsvp(const Epub& epub);

// The banner over the page that shows a non-prose item (HandOff::Then::ShowItem; README.md,
// "Non-prose items"). Pushed by the reader once that page is on screen; it draws over it. On leaving
// it sets `handOff->then` to Continue, or keeps ShowItem when the page must be drawn again under it,
// and `pageTurn` when a swipe left it; otherwise None (Stay).
std::unique_ptr<Activity> createItemBanner(GfxRenderer& renderer, MappedInputManager& input, std::shared_ptr<Epub> epub,
                                           std::shared_ptr<HandOff> handOff);

#else

inline std::unique_ptr<Activity> create(GfxRenderer&, MappedInputManager&, std::shared_ptr<Epub>, const Entry&,
                                        std::shared_ptr<HandOff>) {
  return nullptr;
}

inline bool openedInRsvp(const Epub&) { return false; }

inline std::unique_ptr<Activity> createItemBanner(GfxRenderer&, MappedInputManager&, std::shared_ptr<Epub>,
                                                  std::shared_ptr<HandOff>) {
  return nullptr;
}

#endif

}  // namespace rsvpentry
