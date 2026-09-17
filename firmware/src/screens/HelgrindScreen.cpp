#include "HelgrindScreen.h"

#include "../hardware/Inputs.h"
#include "../hardware/oled.h"
#include "../ui/ButtonGlyphs.h"
#include "../ui/GUI.h"

void HelgrindScreen::render(oled& d, GUIManager& gui) {
  (void)gui;
  d.setDrawColor(1);

  d.setFontPreset(FONT_SMALL);
  const char* title = "HELGRIND";
  int tw = d.getStrWidth(title);
  d.drawStr((128 - tw) / 2, 24, title);

  d.setFontPreset(FONT_TINY);
  const char* line1 = "TODO: game not built yet.";
  int w1 = d.getStrWidth(line1);
  d.drawStr((128 - w1) / 2, 38, line1);
  const char* line2 = "Check back soon.";
  int w2 = d.getStrWidth(line2);
  d.drawStr((128 - w2) / 2, 48, line2);

  d.drawHLine(0, 54, 128);
  ButtonGlyphs::drawInlineHint(d, 2, 63, "Cancel:Back");
}

void HelgrindScreen::handleInput(const Inputs& inp, int16_t cursorX,
                                 int16_t cursorY, GUIManager& gui) {
  (void)cursorX;
  (void)cursorY;
  if (inp.edges().cancelPressed || inp.edges().confirmPressed) {
    gui.popScreen();
  }
}
