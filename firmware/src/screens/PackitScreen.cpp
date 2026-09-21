#include "PackitScreen.h"

#include "../hardware/Inputs.h"
#include "../hardware/oled.h"
#include "../ui/GUI.h"
#include "PackitGame.h"

using namespace packit;

void PackitScreen::onEnter(GUIManager& gui) {
  (void)gui;
  gameBegin(millis());
  joyRamp_.reset();
}

void PackitScreen::render(oled& d, GUIManager& gui) {
  (void)gui;
  gameDraw(d.raw(), millis());
}

void PackitScreen::handleInput(const Inputs& inputs, int16_t cursorX,
                               int16_t cursorY, GUIManager& gui) {
  (void)cursorX;
  (void)cursorY;

  // Horizontal movement (and, only while the pause menu is open, vertical
  // menu navigation) live on the discrete grid (GameInput's move/menu
  // fields are all edge-triggered), so the continuous joystick reading is
  // converted to one-cell ticks the same way GridMenuScreen converts it to
  // cursor steps: JoyRamp picks the dominant axis and paces repeats.
  const int16_t dx = static_cast<int16_t>(inputs.joyX()) - 2047;
  const int16_t dy = static_cast<int16_t>(inputs.joyY()) - 2047;
  constexpr int16_t kJoyDeadband = 400;
  int8_t dir = 0;
  if (abs(dx) > abs(dy)) {
    if (dx > kJoyDeadband) dir = 1;
    else if (dx < -kJoyDeadband) dir = -1;
  } else {
    if (dy > kJoyDeadband) dir = 2;
    else if (dy < -kJoyDeadband) dir = -2;
  }

  GameInput in;
  if (joyRamp_.tick(dir, millis())) {
    if (dir == 1) in.moveRight = true;
    else if (dir == -1) in.moveLeft = true;
    else if (dir == 2) in.menuDown = true;
    else if (dir == -2) in.menuUp = true;
  }
  const Inputs::ButtonEdges& e = inputs.edges();
  in.rotateLeft = e.xPressed;
  in.rotateRight = e.bPressed;
  // Level-triggered: 2x fall rate while held. Y is the dedicated button for
  // it, but the stick's own down push should do the same thing (matching
  // the host SDL viewer's Down key, which drives softDrop directly rather
  // than through JoyRamp) -- dir/joyRamp_ above only cover the *dominant*
  // axis for discrete moves/menu nav, so a downward push while also past
  // the left/right deadband would otherwise never register as a drop.
  in.softDrop = inputs.buttons().y || dy > kJoyDeadband;
  in.a = e.aPressed;

  bool exit = gameStep(in, millis());
  if (exit) gui.popScreen();
}
