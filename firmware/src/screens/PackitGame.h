#pragma once
#include <stdint.h>

#include "clib/u8g2.h"

// ─── Packit game core (flat-board redesign) ────────────────────────────────
//
// A standard face-on Tetris board: 7 classic tetrominoes fall down a flat
// 9x8 grid viewed straight-on, not the earlier top-down 3D shaft (see
// wiki/components/packit.md's "Radical redesign" section for why). Every
// settled or falling block still renders as a real perspective cube --
// near face, far face, 4 connecting edges, the exact math the shaft
// version used -- but every cell now sits at the *same* fixed depth (the
// board is one cube thick), so a cell's screen position comes from simple
// even grid spacing instead of a vanishing-point formula that varies with
// depth. The falling piece is opaque, same rendering as a settled block,
// not a wireframe.
//
// Fall motion is pixel-level, not cell-level: the piece's front (bottom)
// face moves 1px per tick (2px/tick while soft-dropping), and only when
// that pixel motion crosses a full cell boundary does the piece's logical
// row actually advance (and collide/lock). Movement and rotation stay
// fully discrete, one grid step per press.
//
// State is module-static, matching the rest of the firmware's one-instance
// screen convention. Core/shell split, same as Helgrind and the shaft
// version of this game: no firmware dependency beyond u8g2's C API, so
// firmware/host/packit/ can build and run this on a host machine.

namespace packit {

constexpr int kBoardW = 9;  // columns, stick left/right
constexpr int kBoardH = 8;  // rows, fall axis

// Columns and rows sit the same kCellH px apart -- every rendered cube is
// a true square (see PackitGame.cpp's kCubeWorld/kPitchXWorld comment for
// why forcing kBoardW=9 columns across the full 128px width, at kCellW=14,
// stretched each cube into a 14x8 rectangle and was abandoned). At 8px
// pitch, 9 columns span 72px, centered with a real margin rather than
// touching both screen edges.
constexpr int kCellW = 8;
constexpr int kCellH = 8;   // pixels; kBoardH*kCellH=64, fills the screen exactly
constexpr int kBoardOffsetX = (128 - kBoardW * kCellW) / 2;

constexpr int kCellsPerPiece = 4;
constexpr int kRotationStates = 4;
constexpr int kPieceBox = 4;  // each piece's cells live in a 4x4 bounding box

// The 7 classic tetrominoes.
enum PieceKind : uint8_t { kPieceI, kPieceO, kPieceT, kPieceS, kPieceZ, kPieceL, kPieceJ, kPieceCount };

// One cell's offset within a piece's 4x4 bounding box.
struct Cell { int8_t dx, dy; };

// One tick of input. Movement and rotation are edge-triggered (one grid
// step/turn per press, matching the discrete board); softDrop is
// level-triggered -- read every tick, true for as long as Y is held, since
// it's a fall-rate multiplier rather than a single discrete action.
struct GameInput {
  bool moveLeft = false, moveRight = false;  // stick: -x / +x
  bool menuUp = false, menuDown = false;     // stick, pause-menu navigation only
  bool rotateLeft = false;                   // X: CCW
  bool rotateRight = false;                  // B: CW
  bool softDrop = false;                     // Y held: 2x fall rate
  bool a = false;                            // open pause menu / back out of it
};

// The pause menu A opens, in on-screen order.
enum PauseMenuItem : uint8_t { kMenuResume, kMenuNewGame, kMenuExit, kMenuItemCount };

// Read-only view for drawing and host tests.
struct GameStatus {
  int rowsCleared;
  bool gameOver;
  uint8_t pieceKind;
  int8_t anchorCol, anchorRow;
  uint8_t rotState;
  bool menuOpen;
  uint8_t menuSelect;  // a PauseMenuItem, valid while menuOpen
};

// Start play. `now` seeds the piece-order RNG and the fall clock, so a
// host script with a fixed clock gets a reproducible piece sequence.
void gameBegin(uint32_t now);

// Advance one tick: applies at most one horizontal move and one rotation
// from `in` (edge-triggered), then advances the piece's pixel-level fall
// position if `now` has crossed the next fall step (faster while
// in.softDrop is held). Locks the piece, clears full rows, and spawns the
// next piece as needed; sets GameStatus::gameOver when a new piece can't
// fit at spawn. While the pause menu is open, `in` instead navigates and
// confirms it (menuUp/menuDown move the selection, rotateRight confirms, A
// backs out to Resume) and gameplay is frozen. Returns true when Exit was
// confirmed, the same "caller pops back to the main menu" contract
// Helgrind's gameStep uses.
bool gameStep(const GameInput& in, uint32_t now);

GameStatus gameStatus();

// Draw the whole 128x64 frame into `u` (the caller clears/sends the
// buffer): the flat board, drawn left-to-right/top-to-bottom, each cell a
// real perspective cube at a fixed depth, the falling piece on top.
void gameDraw(u8g2_t* u, uint32_t now);

// Settled-cell and falling-piece access for gameDraw and for host tests,
// without exposing the whole internal grid.
bool wellFilled(int col, int row);
void pieceCellWorld(int i, int8_t& col, int8_t& row);

// Test-support only: forces a settled cell on/off, bypassing normal piece
// placement. Lets firmware/host/packit/ set up row-clear and top-out
// scenarios directly instead of needing a specific RNG-dependent piece
// sequence.
void debugSetWellCell(int col, int row, bool filled);

}  // namespace packit
