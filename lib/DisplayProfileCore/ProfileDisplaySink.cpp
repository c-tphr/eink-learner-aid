#include "ProfileDisplaySink.h"

namespace displayprofile {

void ProfileDisplaySink::showFrame(const rsvp::Frame& frame) {
  painter.paintFrame(frame);
  backend.update(UpdateKind::WORD);  // the only updates that may run a trial waveform
}

void ProfileDisplaySink::clearingRefresh() {
  painter.paintClearedBand();
  backend.clear();
}

}  // namespace displayprofile
