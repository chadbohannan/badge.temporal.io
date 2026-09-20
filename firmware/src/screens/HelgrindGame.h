#pragma once
#include <stdint.h>

#include "HelgrindWorld.h"
#include "clib/u8g2.h"

// ─── Helgrind game core ─────────────────────────────────────────────────────
//
// Everything that is "the game" — player, enemies, rooms, the artifact
// chain, the message box, the pause/death/victory menus, and drawing —
// with no firmware dependency beyond u8g2's C API. HelgrindScreen is a
// thin shell that feeds this from the badge's Inputs/millis/oled/LED
// matrix/NVS, and firmware/test/host/helgrind/ drives the same code on a
// host machine with a scripted clock and inputs, dumping frames as images.
//
// Drawing goes straight to a u8g2_t: on the badge that's the object inside
// the oled wrapper (oled::raw()), on the host a buffer-only instance. Same
// code, same fonts, so host frames are the badge's pixels.
//
// State is module-static, matching the rest of the firmware's one-instance
// screen convention.

namespace helgrind {

// One tick of input. Stick axes are -1..1 (deadzone applied by the game);
// the four buttons are edge-triggered "pressed this tick" flags, using the
// badge's semantic names (A pause, B attack/confirm, X cycle, Y action).
struct GameInput {
  float stickX = 0.0f, stickY = 0.0f;
  bool a = false, b = false, x = false, y = false;
};

// The durable part of the game, as the shell stores it (one NVS key each).
struct SaveData {
  uint8_t room = kStartRoom;
  uint8_t hp = 0;
  uint16_t silver = 0;
  uint8_t arts = 0;
  uint8_t weapon = 0;
  bool won = false;
  uint8_t entryX = 64, entryY = 40;
  uint64_t loot = 0;
};

// Read-only view for the shell's footer-free needs and for host tests.
struct GameStatus {
  uint8_t room;
  float x, y;
  int hp;
  uint16_t silver;
  uint8_t arts;
  uint8_t weapon;
  bool won;
  uint8_t mode;     // 0 play, 1 message, 2 pause, 3 game over, 4 victory
  int enemies;      // alive in the current room
};

// Start play: resume from `save`, or nullptr for a new game (which queues
// the intro pages). `now` seeds the game clock.
void gameBegin(const SaveData* save, uint32_t now);

// Advance one tick. Returns true when the player picked Exit from a menu.
bool gameStep(const GameInput& in, uint32_t now);

// Draw the whole 128x64 frame into `u` (the caller clears/sends the buffer).
void gameDraw(u8g2_t* u, uint32_t now);

// Fill one brightness per room (index = row * 8 + col) for the LED minimap.
void gameMinimap(uint8_t pixels[kRoomCount], uint32_t now);

// Set when durable state changed since the last call; the shell saves then.
bool gameTakeSaveDirty();
SaveData gameSave();

GameStatus gameStatus();

}  // namespace helgrind
