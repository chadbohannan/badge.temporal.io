#include "window.h"

#include <cstdio>

#ifndef PACKIT_SDL

namespace window {
int play() {
  std::fprintf(stderr, "built without SDL2; install libsdl2-dev and `make` again\n");
  return 2;
}
}  // namespace window

#else

#include <SDL2/SDL.h>

#include <algorithm>
#include <filesystem>
#include <string>

#include "PackitGame.h"
#include "display.h"

using namespace packit;

namespace window {
namespace {

// Layout: OLED (6x) and the 8x8 LED matrix side by side, matching
// firmware/host/helgrind/window.cpp's OLED+minimap layout.
constexpr int kGap = 16;
constexpr int kOledW = display::kOledW * display::kOledScale;
constexpr int kOledH = display::kOledH * display::kOledScale;
constexpr int kMatrixW = packit::kMatrixCols * display::kLedScale;
constexpr int kMatrixH = packit::kMatrixRows * display::kLedScale;
constexpr int kScreensH = std::max(kOledH, kMatrixH);
constexpr int kWinW = kOledW + kGap + kMatrixW;
constexpr int kWinH = kScreensH;

struct Rgb { uint8_t r, g, b; };
constexpr Rgb kBoard{30, 30, 34};
constexpr Rgb kOledOn{230, 240, 255}, kOledOff{8, 10, 14};

// Game clock: milliseconds since the window opened, offset so `now` never
// starts at 0 (the game's timers treat 0 as "never").
uint32_t gStartTicks = 0;
uint32_t nowMs() { return SDL_GetTicks() - gStartTicks + 1000; }

void blit(SDL_Texture* tex, const display::OledFrame& f) {
  uint8_t* pixels = nullptr;
  int pitch = 0;
  SDL_LockTexture(tex, nullptr, reinterpret_cast<void**>(&pixels), &pitch);
  for (int y = 0; y < display::kOledH; y++) {
    for (int x = 0; x < display::kOledW; x++) {
      Rgb c = f[y * display::kOledW + x] ? kOledOn : kOledOff;
      uint8_t* p = pixels + y * pitch + x * 3;
      p[0] = c.r; p[1] = c.g; p[2] = c.b;
    }
  }
  SDL_UnlockTexture(tex);
}

void printHelp() {
  std::printf(
      "PACKIT (host SDL viewer) -- real 128x64 1-bit OLED pixels\n"
      "  Left/Right    move the piece; also moves the pause-menu selection\n"
      "                when the menu is open (Up/Down there instead)\n"
      "  J             rotate CCW (physical X)\n"
      "  L             rotate CW  (physical B)  -- also confirms in the menu\n"
      "  Down (held)   soft drop, 2x fall rate (physical Y)\n"
      "  K             open/back out of the pause menu (physical A)\n"
      "  S             snapshot -> out/   R  force new game   Esc  quit\n");
}

// Keyboard -> one tick of GameInput. Move/rotate/menu fields are
// edge-triggered (true only on the frame a key was newly pressed);
// softDrop is level-triggered, sampled every tick via SDL_GetKeyboardState
// so holding Down actually reads as "held," matching Y's real behavior.
GameInput readInput(bool& running, bool& newGame, bool& snap) {
  GameInput in;
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    if (ev.type == SDL_QUIT) running = false;
    if (ev.type != SDL_KEYDOWN || ev.key.repeat) continue;
    switch (ev.key.keysym.sym) {
      case SDLK_ESCAPE: running = false; break;
      case SDLK_LEFT: in.moveLeft = true; break;
      case SDLK_RIGHT: in.moveRight = true; break;
      case SDLK_UP: in.menuUp = true; break;
      case SDLK_j: in.rotateLeft = true; break;
      case SDLK_l: in.rotateRight = true; break;
      case SDLK_k: in.a = true; break;
      case SDLK_r: newGame = true; break;
      case SDLK_s: snap = true; break;
      default: break;
    }
  }
  const uint8_t* keys = SDL_GetKeyboardState(nullptr);
  if (keys[SDL_SCANCODE_DOWN]) {
    in.softDrop = true;
    in.menuDown = true;  // menuDown still fires as a discrete key regardless
  }
  return in;
}

}  // namespace

int play() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 2;
  }
  SDL_Window* win = SDL_CreateWindow("PACKIT (host)", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, kWinW, kWinH, 0);
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
  SDL_Texture* oledTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
                                           display::kOledW, display::kOledH);

  display::init();
  std::filesystem::create_directories("out");
  printHelp();
  gStartTicks = SDL_GetTicks();
  gameBegin(nowMs());
  int lastRowsCleared = 0;
  int snaps = 0;

  bool running = true;
  while (running) {
    bool newGame = false, snap = false;
    GameInput in = readInput(running, newGame, snap);
    uint32_t now = nowMs();
    if (newGame) {
      gameBegin(now);
      lastRowsCleared = 0;
      std::printf("-- new game --\n");
    }
    if (gameStep(in, now)) {
      std::printf("Exit selected from the pause menu -- closing.\n");
      running = false;
    }

    GameStatus st = gameStatus();
    if (st.rowsCleared != lastRowsCleared) {
      std::printf("row cleared! total=%d\n", st.rowsCleared);
      lastRowsCleared = st.rowsCleared;
    }
    if (st.gameOver) {
      static bool announced = false;
      if (!announced) {
        std::printf("game over -- topped out. rows cleared=%d. R for a new game.\n",
                    st.rowsCleared);
        announced = true;
      }
      if (newGame) announced = false;
    }
    if (snap) {
      char name[32];
      std::snprintf(name, sizeof name, "play-%03d", snaps++);
      display::snapshot(std::string("out/") + name, now);
      std::printf("saved out/%s-oled.png\n", name);
    }

    SDL_SetRenderDrawColor(ren, kBoard.r, kBoard.g, kBoard.b, 255);
    SDL_RenderClear(ren);

    blit(oledTex, display::renderOled(now));
    SDL_Rect oledDst = {0, (kScreensH - kOledH) / 2, kOledW, kOledH};
    SDL_RenderCopy(ren, oledTex, nullptr, &oledDst);

    // 8x8 LED matrix: warm-white LEDs on a dark board, the 2-digit score
    // as a tiny pixel font (see PackitGame.cpp's gameMatrix/kDigitFont).
    display::Matrix px;
    display::renderMatrix(px);
    for (int row = 0; row < kMatrixRows; row++) {
      for (int col = 0; col < kMatrixCols; col++) {
        const int s = display::kLedScale;
        SDL_Rect cell = {kOledW + kGap + col * s + 1, (kScreensH - kMatrixH) / 2 + row * s + 1, s - 2, s - 2};
        uint8_t b = px[row * kMatrixCols + col];
        if (b) SDL_SetRenderDrawColor(ren, b, b * 3 / 4, b / 3, 255);
        else SDL_SetRenderDrawColor(ren, 12, 12, 12, 255);
        SDL_RenderFillRect(ren, &cell);
      }
    }

    SDL_RenderPresent(ren);
  }
  SDL_DestroyTexture(oledTex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}

}  // namespace window

#endif  // PACKIT_SDL
