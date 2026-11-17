#pragma once

// The reader's way into speed reading (registration point in EpubReaderActivity): creates the RSVP
// activity for the page on screen and says how it was left. Without the RSVP flag, create() returns
// nothing and the reader's entry points do nothing.

#include <cstdint>
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
};

#if CROSSPOINT_RSVP_ENABLED

// `pageStartOffset`: the visible-text offset at which the page on screen starts. RSVP opens on its
// pause screen at the first word whose end is past it, with Play highlighted.
std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& input, std::shared_ptr<Epub> epub,
                                 int spineIndex, uint32_t pageStartOffset, std::shared_ptr<HandOff> handOff);

#else

inline std::unique_ptr<Activity> create(GfxRenderer&, MappedInputManager&, std::shared_ptr<Epub>, int, uint32_t,
                                        std::shared_ptr<HandOff>) {
  return nullptr;
}

#endif

}  // namespace rsvpentry
