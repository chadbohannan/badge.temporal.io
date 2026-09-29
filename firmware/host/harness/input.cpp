// The input injector: screen-space joystick and named buttons, written to the
// virtual pins the firmware reads. Buttons go through host_pin_set so the
// attached ISRs run; the stick goes through the analog values.

#include "harness.h"
#include "host_platform.h"

#include "hardware/HardwareConfig.h"

#include <algorithm>
#include <cstring>

namespace harness {

namespace {
// Per firmware/src/ui/ButtonGlyphs.h: Up is Y, Right is B, Down is A, Left is X.
int pinFor(Button b) {
  switch (b) {
    case Button::Y: return BUTTON_UP;
    case Button::B: return BUTTON_RIGHT;
    case Button::A: return BUTTON_DOWN;
    case Button::X: return BUTTON_LEFT;
  }
  return -1;
}
}  // namespace

bool parseButton(const std::string& name, Button* out) {
  if (name.size() != 1) return false;
  switch (name[0]) {
    case 'Y': case 'y': *out = Button::Y; return true;
    case 'B': case 'b': *out = Button::B; return true;
    case 'A': case 'a': *out = Button::A; return true;
    case 'X': case 'x': *out = Button::X; return true;
  }
  return false;
}

const char* buttonName(Button b) {
  switch (b) {
    case Button::Y: return "Y";
    case Button::B: return "B";
    case Button::A: return "A";
    case Button::X: return "X";
  }
  return "?";
}

// PanicReset arms its button poll before Inputs::begin() calls
// pinMode(INPUT_PULLUP), so the pins are read while still unconfigured. On the
// badge the board holds them high. The harness starts every button released, and
// the stick centred.
void inputInit() {
  for (Button b : {Button::Y, Button::B, Button::A, Button::X}) setButton(b, false);
  setStick(0.0f, 0.0f);
}

// Buttons are active low. The firmware's INPUT_PULLUP holds them high, so a
// release only has to stop driving low.
void setButton(Button b, bool pressed) {
  host_pin_set(pinFor(b), pressed ? 0 : 1);
}

// Inputs::service() rotates the board-mounted stick 90 degrees:
//   joyX = 4095 - physicalY   (physicalY is read from JOY_Y)
//   joyY = physicalX          (physicalX is read from JOY_X)
// and every screen reads joyX > 2047 as right, joyY > 2047 as down. This
// function is the inverse, so scripts and key maps stay in screen directions.
void setStick(float x, float y) {
  auto raw = [](float v) {
    v = std::max(-1.0f, std::min(1.0f, v));
    return (int)(2047.0f + v * 2047.0f + 0.5f);
  };
  const int joyX = raw(x);
  const int joyY = raw(y);
  host_pin_set_analog(JOY_Y, 4095 - joyX);
  host_pin_set_analog(JOY_X, joyY);
}

}  // namespace harness
