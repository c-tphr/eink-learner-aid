#pragma once

// The banner over page view when RSVP stopped at an image, table or code block (task R5; S4 rows 15
// and 16, wireframe in section 2). The reader pushes it once the item's page is on screen; it draws
// only the bottom strip, over the page left in the frame buffer. See README.md, "Non-prose items".

#include "RsvpFeature.h"

#if CROSSPOINT_RSVP_ENABLED

#include <atomic>
#include <memory>

#include "RsvpLaunch.h"
#include "activities/Activity.h"
#include "controls/ControlInput.h"
#include "controls/ControlMap.h"

class Epub;

class ItemBanner final : public Activity {
 public:
  ItemBanner(GfxRenderer& renderer, MappedInputManager& input, std::shared_ptr<Epub> epub,
             std::shared_ptr<rsvpentry::HandOff> handOff);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  void paint();
  void paintButton(int index);
  void drawRequest();
  void activate(controls::Item item);
  // Stay: normal page view follows; `pageTurn` (+1, -1) when a swipe left the banner.
  void stay(int8_t pageTurn);

  std::shared_ptr<Epub> epub;
  std::shared_ptr<rsvpentry::HandOff> handOff;
  controls::ControlInput controlInput;
  int focus = controls::BANNER.defaultIndex;  // written by the main task under the RenderLock
  // Set before each update this screen asks for. A render without it was asked for by someone else:
  // a screen that covered this one has closed and the page under the banner is gone.
  std::atomic<bool> drawAsked{false};
  std::atomic<bool> pageLost{false};
};

#endif  // CROSSPOINT_RSVP_ENABLED
