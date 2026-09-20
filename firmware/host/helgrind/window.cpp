#include "window.h"

#include <cstdio>

#ifndef HELGRIND_SDL

namespace window {
int play(const std::string&) {
  std::fprintf(stderr, "built without SDL2; install libsdl2-dev and `make` again\n");
  return 2;
}
}  // namespace window

#else

#include <SDL2/SDL.h>

#include <algorithm>

#include "HelgrindGame.h"
#include "display.h"
#include "status.h"

using namespace helgrind;

namespace window {
namespace {

// Layout: OLED (4x) and minimap side by side, help panel under them. The
// help panel is itself a u8g2 buffer — a 256x64 display at 2x — so the
// window needs no font library beyond the one the game links.
constexpr int kGap = 16;
constexpr int kOledW = display::kOledW * display::kOledScale;
constexpr int kOledH = display::kOledH * display::kOledScale;
constexpr int kMapW = kWorldCols * display::kLedScale;
constexpr int kMapH = kWorldRows * display::kLedScale;
constexpr int kHelpW = 256, kHelpH = 64, kHelpScale = 2;
constexpr int kScreensH = std::max(kOledH, kMapH);
constexpr int kWinW = kOledW + kGap + kMapW;
constexpr int kWinH = kScreensH + kGap + kHelpH * kHelpScale;

struct Rgb { uint8_t r, g, b; };
constexpr Rgb kBoard{30, 30, 34};
constexpr Rgb kOledOn{230, 240, 255}, kOledOff{8, 10, 14};
constexpr Rgb kHelpOn{170, 175, 185}, kHelpOff{30, 30, 34};

// Game clock: milliseconds since the window opened, offset so `now` never
// starts at 0 (the game's timers treat 0 as "never").
uint32_t gStartTicks = 0;
uint32_t nowMs() { return SDL_GetTicks() - gStartTicks + 1000; }

void blit(SDL_Texture* tex, const display::OledFrame& f, int w, int h, Rgb on, Rgb off) {
  uint8_t* pixels = nullptr;
  int pitch = 0;
  SDL_LockTexture(tex, nullptr, reinterpret_cast<void**>(&pixels), &pitch);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      Rgb c = f[y * w + x] ? on : off;
      uint8_t* p = pixels + y * pitch + x * 3;
      p[0] = c.r; p[1] = c.g; p[2] = c.b;
    }
  }
  SDL_UnlockTexture(tex);
}

void drawHelp(u8g2_t* u, uint32_t now) {
  u8g2_ClearBuffer(u);
  u8g2_SetFont(u, u8g2_font_6x10_tr);
  u8g2_DrawStr(u, 0, 9,  "Arrows walk   Z A:pause   X B:attack/ok");
  u8g2_DrawStr(u, 0, 20, "C X:weapon    V Y:talk/buy/scroll");
  u8g2_DrawStr(u, 0, 31, "S snapshot -> out/   F5 new game   Esc quit");
  u8g2_DrawHLine(u, 0, 37, kHelpW);
  GameStatus st = gameStatus();
  static const char* const kModes[] = {"play", "message", "pause", "game over", "victory"};
  char line[64];
  std::snprintf(line, sizeof line, "%s (%d)  %s  t=%us", roomDef(st.room).name, st.room,
                kModes[st.mode < 5 ? st.mode : 0], static_cast<unsigned>(now / 1000));
  u8g2_DrawStr(u, 0, 49, line);
  std::snprintf(line, sizeof line, "x=%.0f y=%.0f hp=%d silver=%u arts=%02x enemies=%d",
                st.x, st.y, st.hp, st.silver, st.arts, st.enemies);
  u8g2_DrawStr(u, 0, 60, line);
}

// Keyboard → one tick of GameInput. Buttons are edges from key-down
// events; the stick is the arrows' held state.
GameInput readInput(bool& running, bool& newGame, bool& snap) {
  GameInput in;
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    if (ev.type == SDL_QUIT) running = false;
    if (ev.type != SDL_KEYDOWN || ev.key.repeat) continue;
    switch (ev.key.keysym.sym) {
      case SDLK_ESCAPE: running = false; break;
      case SDLK_z: in.a = true; break;
      case SDLK_x: in.b = true; break;
      case SDLK_c: in.x = true; break;
      case SDLK_v: in.y = true; break;
      case SDLK_F5: newGame = true; break;
      case SDLK_s: snap = true; break;
      default: break;
    }
  }
  const Uint8* keys = SDL_GetKeyboardState(nullptr);
  if (keys[SDL_SCANCODE_LEFT]) in.stickX = -1.0f;
  if (keys[SDL_SCANCODE_RIGHT]) in.stickX = 1.0f;
  if (keys[SDL_SCANCODE_UP]) in.stickY = -1.0f;
  if (keys[SDL_SCANCODE_DOWN]) in.stickY = 1.0f;
  return in;
}

}  // namespace

int play(const std::string& snapDir) {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 2;
  }
  SDL_Window* win = SDL_CreateWindow("Helgrind (host)", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, kWinW, kWinH, 0);
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
  SDL_Texture* oledTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
                                           display::kOledW, display::kOledH);
  SDL_Texture* helpTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
                                           kHelpW, kHelpH);
  u8g2_t help;
  u8g2_Setup_ssd1322_nhd_256x64_f(&help, U8G2_R0, u8x8_byte_empty, u8x8_dummy_cb);

  gStartTicks = SDL_GetTicks();
  gameBegin(nullptr, nowMs());
  int snaps = 0;
  bool running = true;
  while (running) {
    bool newGame = false, snap = false;
    GameInput in = readInput(running, newGame, snap);
    uint32_t now = nowMs();
    if (newGame) gameBegin(nullptr, now);
    if (snap) {
      char name[32];
      std::snprintf(name, sizeof name, "play-%03d", snaps++);
      display::snapshot(snapDir + "/" + name, now);
      std::printf("saved %s/%s-{oled,matrix}.png  %s\n", snapDir.c_str(), name, status::line().c_str());
    }
    if (gameStep(in, now)) running = false;  // Exit picked from a menu

    SDL_SetRenderDrawColor(ren, kBoard.r, kBoard.g, kBoard.b, 255);
    SDL_RenderClear(ren);

    blit(oledTex, display::renderOled(now), display::kOledW, display::kOledH, kOledOn, kOledOff);
    SDL_Rect oledDst = {0, (kScreensH - kOledH) / 2, kOledW, kOledH};
    SDL_RenderCopy(ren, oledTex, nullptr, &oledDst);

    // Minimap: warm-white LEDs on a dark board, brightness as-is.
    display::Minimap px;
    display::renderMinimap(px, now);
    for (int r = 0; r < kRoomCount; r++) {
      const int s = display::kLedScale;
      SDL_Rect cell = {kOledW + kGap + (r % kWorldCols) * s + 2, (r / kWorldCols) * s + 2, s - 4, s - 4};
      uint8_t b = px[r];
      if (b) SDL_SetRenderDrawColor(ren, b, b * 3 / 4, b / 3, 255);
      else SDL_SetRenderDrawColor(ren, 12, 12, 12, 255);
      SDL_RenderFillRect(ren, &cell);
    }

    drawHelp(&help, now);
    blit(helpTex, display::unpack(&help, kHelpW, kHelpH), kHelpW, kHelpH, kHelpOn, kHelpOff);
    SDL_Rect helpDst = {0, kScreensH + kGap, kHelpW * kHelpScale, kHelpH * kHelpScale};
    SDL_RenderCopy(ren, helpTex, nullptr, &helpDst);

    SDL_RenderPresent(ren);
  }
  SDL_DestroyTexture(helpTex);
  SDL_DestroyTexture(oledTex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}

}  // namespace window

#endif  // HELGRIND_SDL
