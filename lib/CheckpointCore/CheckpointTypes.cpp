#include "CheckpointTypes.h"

namespace checkpoint {

const char* readingModeName(ReadingMode mode) { return mode == ReadingMode::Rsvp ? "rsvp" : "page"; }

const char* gradeName(Grade grade) {
  switch (grade) {
    case Grade::Correct:
      return "correct";
    case Grade::Incorrect:
      return "incorrect";
    case Grade::Ungraded:
      break;
  }
  return "";
}

const char* navSourceName(NavSource source) {
  switch (source) {
    case NavSource::RsvpPlayback:
      return "rsvp-playback";
    case NavSource::NextPage:
      return "next-page";
    case NavSource::PreviousPage:
      return "previous-page";
    case NavSource::TableOfContents:
      return "table-of-contents";
    case NavSource::GoToPercent:
      return "go-to-percent";
    case NavSource::Bookmark:
      return "bookmark";
    case NavSource::ProgressSync:
      return "progress-sync";
    case NavSource::SentenceStep:
      return "sentence-step";
    case NavSource::ModeSwitch:
      return "mode-switch";
    case NavSource::OpenRestore:
      return "open-restore";
    case NavSource::RereadStart:
      return "reread-start";
    case NavSource::RereadReturn:
      return "reread-return";
  }
  return "";
}

}  // namespace checkpoint
