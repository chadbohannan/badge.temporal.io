// Scripted host tests for PackitGame's core logic, run without any
// firmware/rendering dependency.
#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "PackitGame.h"

using namespace packit;

namespace {

int gFailures = 0;

void expect(bool cond, const char* what) {
  if (!cond) {
    std::printf("FAIL: %s\n", what);
    gFailures++;
  }
}

// gameBegin() now always opens on the pause menu (see menuTest for that
// behavior itself) rather than dropping straight into play, so every other
// test that wants to exercise gameplay needs one extra step to back out of
// it first -- A, same as a real player confirming Resume.
void beginPlaying(uint32_t now) {
  gameBegin(now);
  GameInput resume;
  resume.a = true;
  gameStep(resume, now);
}

void spawnAndBoundsTest() {
  beginPlaying(1);
  GameStatus s = gameStatus();
  expect(!s.gameOver, "spawnAndBoundsTest: fresh game is not over");
  expect(s.rowsCleared == 0, "spawnAndBoundsTest: no rows cleared yet");
  for (int i = 0; i < kCellsPerPiece; i++) {
    int8_t col, row;
    pieceCellWorld(i, col, row);
    expect(col >= 0 && col < kBoardW, "spawnAndBoundsTest: cell col in bounds");
    expect(row >= 0 && row < kBoardH, "spawnAndBoundsTest: cell row in bounds");
  }
}

void moveBoundsTest() {
  beginPlaying(1000);
  GameInput left;
  left.moveLeft = true;
  for (int i = 0; i < kBoardW; i++) gameStep(left, 1000);
  GameStatus s1 = gameStatus();
  expect(s1.anchorCol >= 0, "moveBoundsTest: anchor never goes negative");
  gameStep(left, 1000);  // one more shove into the wall
  GameStatus s2 = gameStatus();
  expect(s2.anchorCol == s1.anchorCol, "moveBoundsTest: wall blocks further movement");
}

void rotateChangesStateTest() {
  beginPlaying(2);
  GameStatus before = gameStatus();
  GameInput rotate;
  rotate.rotateRight = true;  // B: CW
  gameStep(rotate, 2);
  GameStatus after = gameStatus();
  expect(after.anchorCol == before.anchorCol && after.anchorRow == before.anchorRow,
         "rotateChangesStateTest: rotation doesn't move the anchor");
  // O never changes shape, so only assert state advanced for pieces where it can.
  if (before.pieceKind != kPieceO) {
    expect(after.rotState != before.rotState, "rotateChangesStateTest: rotState advances");
  }
  GameInput rotateBack;
  rotateBack.rotateLeft = true;  // X: CCW, should undo it
  gameStep(rotateBack, 2);
  expect(gameStatus().rotState == before.rotState,
         "rotateChangesStateTest: CCW undoes the prior CW turn");
}

void softDropIsFasterTest() {
  beginPlaying(10);
  GameStatus s0 = gameStatus();
  GameInput drop;
  drop.softDrop = true;
  // One pixel-fall tick (100ms) with soft drop should move 2px -- less
  // than a full kCellH-px row, so the anchor row shouldn't have advanced
  // yet, but two more normal ticks' worth (200ms) should tip it over a
  // smaller threshold than 8 normal (non-drop) ticks would.
  gameStep(drop, 110);
  GameStatus s1 = gameStatus();
  expect(s1.anchorRow == s0.anchorRow, "softDropIsFasterTest: one soft-drop tick isn't a full row yet");
  // kCellH=8px row; soft drop moves 2px/tick, so 4 ticks (400ms) cross it.
  uint32_t now = 110;
  for (int i = 0; i < 4; i++) {
    now += 100;
    gameStep(drop, now);
  }
  GameStatus s2 = gameStatus();
  expect(s2.anchorRow == s0.anchorRow + 1, "softDropIsFasterTest: 4 soft-drop ticks advance one row");
}

// Every spawned piece's rotState-0 shape bottoms out at dy=1 (checked for
// all 7 kShapes entries), so from spawn row 0 it takes exactly
// (kBoardH-2) row transitions -- kCellH ticks each -- to reach anchorRow
// kBoardH-2, plus one more tick whose up-front check (stepFallPixel checks
// before animating, not after) finds the floor blocked and starts hovering
// there instead of falling further.
constexpr uint32_t kTicksToFloorLock = static_cast<uint32_t>(kBoardH - 2) * kCellH + 1;

// Once blocked, the piece hovers for PackitGame.cpp's kLockDelayMs (500ms)
// -- still free to slide -- before actually locking; not exposed via
// PackitGame.h, so mirrored here as a plain constant.
constexpr uint32_t kLockDelayMs = 500;

// Drives gameStep in real kFallIntervalMs-sized (100ms) steps up to (and
// past) `untilMs`, matching how the firmware calls gameStep once per frame
// with the real clock -- unlike a single big time jump, this lets the
// lock-delay hover (which measures its own deadline off the `now` passed
// to the call where it first got blocked) actually expire mid-test.
void stepTicksTo(const GameInput& in, uint32_t untilMs, uint32_t& now) {
  while (now < untilMs) {
    now += 100;
    gameStep(in, now);
  }
}

void fallLockSpawnTest() {
  beginPlaying(0);
  uint32_t now = 0;
  stepTicksTo(GameInput{}, kTicksToFloorLock * 100 + kLockDelayMs, now);
  GameStatus s = gameStatus();
  expect(!s.gameOver, "fallLockSpawnTest: bottom is otherwise empty, next piece fits");
  expect(s.anchorRow == 0, "fallLockSpawnTest: a new piece spawned back at row 0");
  bool anyLockedAtBottom = false;
  for (int col = 0; col < kBoardW && !anyLockedAtBottom; col++) {
    if (wellFilled(col, kBoardH - 1)) anyLockedAtBottom = true;
  }
  expect(anyLockedAtBottom, "fallLockSpawnTest: the first piece locked at the floor");
}

void lockDelaySlideTest() {
  beginPlaying(6);
  uint32_t now = 6;
  // Drive the piece down to where it first finds the floor blocked, but
  // stop just short of the lock-delay deadline.
  stepTicksTo(GameInput{}, 6 + kTicksToFloorLock * 100, now);
  GameStatus hovering = gameStatus();
  expect(hovering.anchorRow == kBoardH - 2,
         "lockDelaySlideTest: piece is hovering at the floor, not yet locked");

  // A slide during the grace window should still land -- prove the piece
  // is still movable and hasn't locked in place yet.
  GameInput slide;
  slide.moveRight = true;
  now += 50;
  gameStep(slide, now);
  GameStatus afterSlide = gameStatus();
  expect(afterSlide.anchorCol == hovering.anchorCol + 1 || afterSlide.anchorCol == hovering.anchorCol,
         "lockDelaySlideTest: a slide during the grace window is accepted (or blocked by the wall)");
  expect(!wellFilled(hovering.anchorCol, kBoardH - 1) || afterSlide.anchorCol != hovering.anchorCol,
         "lockDelaySlideTest: piece hasn't locked at its pre-slide column yet");

  // Once the deadline passes, it locks -- wherever the slide left it.
  stepTicksTo(slide, now + kLockDelayMs, now);
  GameStatus after = gameStatus();
  expect(after.anchorRow == 0, "lockDelaySlideTest: piece locked and the next one spawned");
}

void rowClearTest() {
  beginPlaying(3);
  // The piece locks with its bottom row (dy=1 cells, the max dy in every
  // kShapes rotState-0 entry) landing on the board's bottom row. Fill
  // every other column of that row first, so locking completes it.
  int8_t fcol[kCellsPerPiece], frow[kCellsPerPiece];
  for (int i = 0; i < kCellsPerPiece; i++) pieceCellWorld(i, fcol[i], frow[i]);
  int bottomRow = kBoardH - 1;
  int8_t maxRelRow = 0;
  for (int i = 0; i < kCellsPerPiece; i++) maxRelRow = std::max(maxRelRow, frow[i]);
  auto isFootprintAtBottom = [&](int col) {
    for (int i = 0; i < kCellsPerPiece; i++) {
      if (fcol[i] == col && frow[i] == maxRelRow) return true;
    }
    return false;
  };
  for (int col = 0; col < kBoardW; col++) {
    if (!isFootprintAtBottom(col)) debugSetWellCell(col, bottomRow, true);
  }
  uint32_t now = 3;
  stepTicksTo(GameInput{}, 3 + kTicksToFloorLock * 100 + kLockDelayMs, now);
  GameStatus after = gameStatus();
  expect(after.rowsCleared == 1, "rowClearTest: completing the row clears exactly one");
  expect(!after.gameOver, "rowClearTest: clearing the row keeps the spawn point open");
}

void gameOverTest() {
  beginPlaying(4);
  // Block every spawn-box cell so the *next* spawn attempt collides;
  // the current piece's own state doesn't consult the well array, so
  // this doesn't retroactively invalidate the piece already in play.
  // Blocking the whole box also means the very first fall tick collides
  // immediately, locking the current piece right where it spawned and
  // triggering that next (blocked) spawn attempt.
  constexpr int kSpawnCol = (kBoardW - kPieceBox) / 2;  // matches PackitGame.cpp's private constant
  for (int r = 0; r < kPieceBox; r++) {
    for (int c = 0; c < kPieceBox; c++) debugSetWellCell(kSpawnCol + c, r, true);
  }
  // stepFallPixel checks the next row before animating into it, so with
  // the spawn box already fully blocked, the very first tick finds it
  // blocked and starts the lock-delay hover; the lock (and the next,
  // failing spawn attempt) doesn't happen until that hover expires.
  uint32_t now = 4;
  stepTicksTo(GameInput{}, 4 + 100 + kLockDelayMs, now);
  GameStatus s = gameStatus();
  expect(s.gameOver, "gameOverTest: spawn blocked at the top tops the game out");
  expect(s.menuOpen, "gameOverTest: topping out opens the pause menu");
  expect(s.menuSelect == kMenuNewGame, "gameOverTest: game-over menu opens on New Game, not Resume");

  // Resume isn't offered once the game is over: A (the usual "back to
  // Resume" cancel) does nothing, and navigation skips straight past it.
  GameInput back;
  back.a = true;
  gameStep(back, now);
  expect(gameStatus().menuOpen, "gameOverTest: A doesn't back out of the game-over menu");

  GameInput up;
  up.menuUp = true;
  gameStep(up, now);
  expect(gameStatus().menuSelect == kMenuExit,
         "gameOverTest: navigating up from New Game skips Resume, landing on Exit");

  GameInput down;
  down.menuDown = true;
  gameStep(down, now);
  gameStep(down, now);
  expect(gameStatus().menuSelect == kMenuExit,
         "gameOverTest: navigating down from New Game skips Resume, wrapping to Exit");

  // New Game still works from the game-over menu, same as a normal pause.
  GameInput selectNewGame;
  selectNewGame.menuUp = true;
  gameStep(selectNewGame, now);  // Exit -> New Game (skipping Resume)
  GameInput confirm;
  confirm.rotateRight = true;
  gameStep(confirm, now);
  GameStatus restarted = gameStatus();
  expect(!restarted.gameOver, "gameOverTest: New Game clears game-over");
  expect(!restarted.menuOpen, "gameOverTest: New Game closes the menu");
}

void menuTest() {
  gameBegin(5);
  GameStatus s = gameStatus();
  expect(s.menuOpen, "menuTest: game always starts on the pause menu");
  expect(s.menuSelect == kMenuResume, "menuTest: initial menu opens on Resume");

  int8_t colBefore = s.anchorCol;
  GameInput move;
  move.moveLeft = true;
  gameStep(move, 5);
  expect(gameStatus().anchorCol == colBefore, "menuTest: movement is ignored while the menu is open");

  GameInput down;
  down.menuDown = true;
  gameStep(down, 5);
  expect(gameStatus().menuSelect == kMenuNewGame, "menuTest: menuDown advances the selection");
  gameStep(down, 5);
  expect(gameStatus().menuSelect == kMenuExit, "menuTest: menuDown advances to Exit");
  gameStep(down, 5);
  expect(gameStatus().menuSelect == kMenuResume, "menuTest: selection wraps from Exit to Resume");

  GameInput up;
  up.menuUp = true;
  gameStep(up, 5);
  expect(gameStatus().menuSelect == kMenuExit, "menuTest: menuUp wraps backward from Resume to Exit");

  GameInput back;
  back.a = true;
  gameStep(back, 5);
  expect(!gameStatus().menuOpen, "menuTest: A closes the menu (back/cancel) without confirming Exit");

  GameInput openMenu;
  openMenu.a = true;
  expect(!gameStep(openMenu, 5), "menuTest: re-opening the menu mid-play doesn't exit");
  expect(gameStatus().menuOpen, "menuTest: A re-opens the menu");
  expect(gameStatus().menuSelect == kMenuResume, "menuTest: re-opened menu starts on Resume");

  debugSetWellCell(0, 0, true);
  gameStep(down, 5);  // Resume -> New Game
  GameInput confirm;
  confirm.rotateRight = true;
  expect(!gameStep(confirm, 5), "menuTest: confirming New Game doesn't exit");
  expect(!gameStatus().menuOpen, "menuTest: confirming an item closes the menu");
  expect(!wellFilled(0, 0), "menuTest: New Game clears the well");

  gameStep(openMenu, 5);
  gameStep(down, 5);
  gameStep(down, 5);  // Resume -> New Game -> Exit
  expect(gameStep(confirm, 5), "menuTest: confirming Exit returns true");
}

}  // namespace

int main() {
  spawnAndBoundsTest();
  moveBoundsTest();
  rotateChangesStateTest();
  softDropIsFasterTest();
  fallLockSpawnTest();
  lockDelaySlideTest();
  rowClearTest();
  gameOverTest();
  menuTest();
  if (gFailures == 0) {
    std::printf("OK\n");
    return 0;
  }
  std::printf("%d failure(s)\n", gFailures);
  return 1;
}
