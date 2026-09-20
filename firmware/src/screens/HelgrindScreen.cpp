#include "HelgrindScreen.h"

#include <Preferences.h>

#include "../hardware/Inputs.h"
#include "../hardware/LEDmatrix.h"
#include "../hardware/oled.h"
#include "../led/LEDAppRuntime.h"
#include "../ui/GUI.h"
#include "HelgrindGame.h"

extern LEDmatrix badgeMatrix;

using namespace helgrind;

namespace {

constexpr uint32_t kMinimapIntervalMs = 100;

// ── Persistence ──────────────────────────────────────────────────────────
//
// One NVS namespace for the whole save, using the `badge_` prefix
// firmware/docs/STORAGE-MODEL.md's namespace map asks for (and where it's
// listed). Written only when the game flags durable state as changed.
constexpr const char* kNvsNamespace = "badge_helgrind";

void writeSave(const SaveData& s) {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) return;
  prefs.putUChar("room", s.room);
  prefs.putUChar("hp", s.hp);
  prefs.putUShort("silver", s.silver);
  prefs.putUChar("arts", s.arts);
  prefs.putUChar("weapon", s.weapon);
  prefs.putUChar("won", s.won ? 1 : 0);
  prefs.putUChar("ex", s.entryX);
  prefs.putUChar("ey", s.entryY);
  prefs.putULong64("loot", s.loot);
  prefs.end();
}

bool readSave(SaveData& s) {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) return false;
  bool has = prefs.isKey("room");
  if (has) {
    s.room = prefs.getUChar("room", kStartRoom);
    s.hp = prefs.getUChar("hp", 0);
    s.silver = prefs.getUShort("silver", 0);
    s.arts = prefs.getUChar("arts", 0);
    s.weapon = prefs.getUChar("weapon", kWeaponSword);
    s.won = prefs.getUChar("won", 0) != 0;
    s.entryX = prefs.getUChar("ex", 64);
    s.entryY = prefs.getUChar("ey", 40);
    s.loot = prefs.getULong64("loot", 0);
  }
  prefs.end();
  return has;
}

void saveIfDirty() {
  if (gameTakeSaveDirty()) writeSave(gameSave());
}

}  // namespace

void HelgrindScreen::onEnter(GUIManager& gui) {
  (void)gui;
  SaveData save;
  gameBegin(readSave(save) ? &save : nullptr, millis());
  saveIfDirty();
  lastMinimapMs_ = 0;
  ledAppRuntime.beginOverride();
}

void HelgrindScreen::onExit(GUIManager& gui) {
  (void)gui;
  writeSave(gameSave());
  ledAppRuntime.endOverride();
}

void HelgrindScreen::render(oled& d, GUIManager& gui) {
  (void)gui;
  uint32_t now = millis();
  if (now - lastMinimapMs_ >= kMinimapIntervalMs) {
    lastMinimapMs_ = now;
    uint8_t px[kRoomCount];
    gameMinimap(px, now);
    badgeMatrix.beginFrameBatch();
    badgeMatrix.clear(0);
    for (int r = 0; r < kRoomCount; r++) {
      if (px[r]) badgeMatrix.setPixel(r % kWorldCols, r / kWorldCols, px[r]);
    }
    badgeMatrix.endFrameBatch();
  }
  gameDraw(d.raw(), now);
}

void HelgrindScreen::handleInput(const Inputs& inp, int16_t cursorX,
                                 int16_t cursorY, GUIManager& gui) {
  (void)cursorX;
  (void)cursorY;
  GameInput in;
  in.stickX = (static_cast<float>(inp.joyX()) - 2047.0f) / 2047.0f;
  in.stickY = (static_cast<float>(inp.joyY()) - 2047.0f) / 2047.0f;
  in.a = inp.edges().aPressed;
  in.b = inp.edges().bPressed;
  in.x = inp.edges().xPressed;
  in.y = inp.edges().yPressed;
  bool exit = gameStep(in, millis());
  saveIfDirty();
  if (exit) gui.popScreen();
}
