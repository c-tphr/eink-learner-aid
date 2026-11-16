#include "ProfileDisplaySink.h"

namespace displayprofile {

void ProfileDisplaySink::showFrame(const rsvp::Frame& frame) {
  painter.paintFrame(frame);
  backend.update();
}

void ProfileDisplaySink::clearingRefresh() {
  painter.paintClearedBand();
  backend.clear();
}

}  // namespace displayprofile
