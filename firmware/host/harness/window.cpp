// Live viewer: the OLED at 6x beside the 8x8 LED matrix and a panel of virtual
// controls, with the keyboard, mouse and an SDL game controller feeding the input
// injector. Built without SDL it reports that and the scripted headless mode
// still works.

#include "harness.h"
#include "png.h"
#include "host_platform.h"
#include "hardware/HardwareConfig.h"

#include <cstdio>

#ifndef HOST_HAVE_SDL

namespace harness {
bool windowOpen() {
  fprintf(stderr, "built without SDL2; install libsdl2-dev for the live window (scripted runs still work)\n");
  return false;
}
bool windowPollInput() { return true; }
void windowPresent() {}
void windowClose() {}
bool windowCapture(const std::string&) { return true; }
}  // namespace harness

#else

#include <SDL2/SDL.h>
#include "font5x7.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace harness {

namespace {
constexpr int kOledScale = 6;
constexpr int kLedScale = 16;
constexpr int kGap = 16;
constexpr int kOledPxW = kOledW * kOledScale;
constexpr int kOledPxH = kOledH * kOledScale;
constexpr int kMatrixPx = kMatrixN * kLedScale;
// Right-hand column: the LED matrix on top, virtual controls and the key list below.
constexpr int kColX = kOledPxW + kGap;
constexpr int kColW = 264;
constexpr int kMatrixX = kColX + (kColW - kMatrixPx) / 2;
constexpr int kMatrixY = 12;
constexpr int kControlsY = kMatrixY + kMatrixPx + 16;  // everything below is the controls panel
constexpr int kWinW = kColX + kColW;
constexpr int kWinH = kOledPxH;

// Keys for the four face buttons form a diamond that matches the badge:
// I on top (Y), L right (B), K bottom (A), J left (X).
struct KeyButton { SDL_Scancode key; Button button; };
constexpr KeyButton kKeyButtons[] = {
    {SDL_SCANCODE_I, Button::Y},
    {SDL_SCANCODE_L, Button::B},
    {SDL_SCANCODE_K, Button::A},
    {SDL_SCANCODE_J, Button::X},
};
// SDL names controller buttons by position, so the diamond maps straight over.
struct PadButton { SDL_GameControllerButton pad; Button button; };
constexpr PadButton kPadButtons[] = {
    {SDL_CONTROLLER_BUTTON_Y, Button::Y},
    {SDL_CONTROLLER_BUTTON_B, Button::B},
    {SDL_CONTROLLER_BUTTON_A, Button::A},
    {SDL_CONTROLLER_BUTTON_X, Button::X},
};
constexpr float kPadDeadzone = 0.15f;

// ── Virtual controls ──────────────────────────────────────────────────────
// Drawn from the virtual pins, so a script and the keyboard show up the same way.
// Each control is also a mouse target.
constexpr int kPad = 34;
constexpr int kStickCx = kColX + 60, kStickCy = kControlsY + 58;
constexpr int kFaceCx = kColX + 200, kFaceCy = kControlsY + 58;
struct Target { SDL_Rect r; int dx, dy; Button button; bool isButton; };
const Target kTargets[] = {
    {{kStickCx - kPad / 2, kStickCy - kPad - kPad / 2 - 2, kPad, kPad}, 0, -1, Button::Y, false},
    {{kStickCx - kPad / 2, kStickCy + kPad / 2 + 2, kPad, kPad}, 0, 1, Button::Y, false},
    {{kStickCx - kPad - kPad / 2 - 2, kStickCy - kPad / 2, kPad, kPad}, -1, 0, Button::Y, false},
    {{kStickCx + kPad / 2 + 2, kStickCy - kPad / 2, kPad, kPad}, 1, 0, Button::Y, false},
    {{kFaceCx - kPad / 2, kFaceCy - kPad - kPad / 2 - 2, kPad, kPad}, 0, 0, Button::Y, true},
    {{kFaceCx + kPad / 2 + 2, kFaceCy - kPad / 2, kPad, kPad}, 0, 0, Button::B, true},
    {{kFaceCx - kPad / 2, kFaceCy + kPad / 2 + 2, kPad, kPad}, 0, 0, Button::A, true},
    {{kFaceCx - kPad - kPad / 2 - 2, kFaceCy - kPad / 2, kPad, kPad}, 0, 0, Button::X, true},
};

SDL_Window* gWin = nullptr;
SDL_Renderer* gRen = nullptr;
SDL_Texture* gOledTex = nullptr;
SDL_GameController* gPad = nullptr;
bool gButtonDown[4] = {false, false, false, false};
float gStickX = 0, gStickY = 0;
int gSnaps = 0;
uint32_t gLastPresentTicks = 0;

void openFirstPad() {
  if (gPad) return;
  for (int i = 0; i < SDL_NumJoysticks(); i++) {
    if (SDL_IsGameController(i)) {
      gPad = SDL_GameControllerOpen(i);
      if (gPad) {
        printf("[host] controller: %s\n", SDL_GameControllerName(gPad));
        return;
      }
    }
  }
}

float axis(SDL_GameControllerAxis a) {
  const float v = SDL_GameControllerGetAxis(gPad, a) / 32767.0f;
  return std::abs(v) < kPadDeadzone ? 0.0f : std::max(-1.0f, std::min(1.0f, v));
}
}  // namespace

bool windowOpen() {
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return false;
  }
  gWin = SDL_CreateWindow("Replay 2026 Badge (host)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                          kWinW, kWinH, 0);
  gRen = gWin ? SDL_CreateRenderer(gWin, -1, 0) : nullptr;
  if (!gRen) {
    fprintf(stderr, "SDL window: %s\n", SDL_GetError());
    return false;
  }
  gOledTex = SDL_CreateTexture(gRen, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, kOledW, kOledH);
  openFirstPad();
  printf("[host] keys: arrows = joystick (Shift = half), I/L/K/J = Y/B/A/X, S = snapshot, Esc = quit\n");
  return true;
}

bool windowPollInput() {
  if (!gWin) return true;
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
      case SDL_QUIT: return false;
      case SDL_KEYDOWN:
        if (ev.key.repeat) break;
        if (ev.key.keysym.sym == SDLK_ESCAPE) return false;
        if (ev.key.keysym.sym == SDLK_s) {
          char name[32];
          snprintf(name, sizeof name, "live-%03d", gSnaps++);
          if (writeSnapshot(name)) printf("[host] saved %s/%s-oled.png\n", gOptions.outDir.c_str(), name);
        }
        break;
      case SDL_CONTROLLERDEVICEADDED: openFirstPad(); break;
      case SDL_CONTROLLERDEVICEREMOVED:
        if (gPad && ev.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gPad))) {
          SDL_GameControllerClose(gPad);
          gPad = nullptr;
          openFirstPad();
        }
        break;
      default: break;
    }
  }

  const uint8_t* keys = SDL_GetKeyboardState(nullptr);

  // Stick: arrow keys win over the controller. Shift halves the deflection so
  // deadzone and sensitivity settings can be checked from the keyboard.
  float kx = (keys[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f);
  float ky = (keys[SDL_SCANCODE_DOWN] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_UP] ? 1.0f : 0.0f);
  if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) { kx *= 0.5f; ky *= 0.5f; }
  float sx = kx, sy = ky;

  // Mouse: holding the left button on a virtual control presses it.
  int mx = 0, my = 0;
  const bool mouseDown = (SDL_GetMouseState(&mx, &my) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
  bool mouseButton[4] = {false, false, false, false};
  if (mouseDown) {
    const SDL_Point pt = {mx, my};
    for (const Target& t : kTargets) {
      if (!SDL_PointInRect(&pt, &t.r)) continue;
      if (t.isButton) mouseButton[(int)t.button] = true;
      else if (sx == 0 && sy == 0) { sx = (float)t.dx; sy = (float)t.dy; }
    }
  }
  if (sx == 0 && sy == 0 && gPad) {
    sx = axis(SDL_CONTROLLER_AXIS_LEFTX);
    sy = axis(SDL_CONTROLLER_AXIS_LEFTY);
  }
  if (sx != gStickX || sy != gStickY) {
    gStickX = sx;
    gStickY = sy;
    setStick(sx, sy);
  }

  // Buttons: either source holds one down. Only changes reach the pins, so the
  // firmware sees one edge per press, as it would from a real switch.
  for (const KeyButton& kb : kKeyButtons) {
    bool down = keys[kb.key] != 0 || mouseButton[(int)kb.button];
    if (gPad) {
      for (const PadButton& pb : kPadButtons) {
        if (pb.button == kb.button && SDL_GameControllerGetButton(gPad, pb.pad)) down = true;
      }
    }
    const int i = (int)kb.button;
    if (down != gButtonDown[i]) {
      gButtonDown[i] = down;
      setButton(kb.button, down);
    }
  }
  return true;
}

// Window colours. Kept in one place so windowCapture() checks against the same
// values the drawing uses.
namespace {
constexpr uint8_t kBoardRgb[3] = {30, 30, 34};
constexpr uint8_t kOledOnRgb[3] = {230, 240, 255};
constexpr uint8_t kOledOffRgb[3] = {8, 10, 14};
constexpr uint8_t kLedOffRgb[3] = {12, 12, 12};
// The LEDs are red. The badge runs them at a low PWM (about 10 of 255 by
// default), which is bright to the eye but would draw as near-black here, so
// every lit LED is mapped into the top half of the range: PWM 1 draws as 128 and
// PWM 255 as 255. An unlit LED stays dark grey.
void ledRgb(uint8_t b, uint8_t out[3]) {
  if (!b) { out[0] = kLedOffRgb[0]; out[1] = kLedOffRgb[1]; out[2] = kLedOffRgb[2]; return; }
  out[0] = (uint8_t)(128 + (int)b * 127 / 255);
  out[1] = 0;
  out[2] = 0;
}
SDL_Rect ledCell(int row, int col) {
  return {kMatrixX + col * kLedScale + 1, kMatrixY + row * kLedScale + 1, kLedScale - 2, kLedScale - 2};
}

int buttonPin(Button b) {
  switch (b) {
    case Button::Y: return BUTTON_UP;
    case Button::B: return BUTTON_RIGHT;
    case Button::A: return BUTTON_DOWN;
    case Button::X: return BUTTON_LEFT;
  }
  return -1;
}
bool buttonHeld(Button b) { return host_pin_get(buttonPin(b)) == 0; }

// Screen-space stick, recovered from the analog pins by inverting the same
// rotation the injector applies (see input.cpp).
void stickNow(float* x, float* y) {
  *x = ((4095 - host_pin_get_analog(JOY_Y)) - 2047) / 2047.0f;
  *y = (host_pin_get_analog(JOY_X) - 2047) / 2047.0f;
}

void drawText(int x, int y, int scale, const char* text, const uint8_t rgb[3]) {
  SDL_SetRenderDrawColor(gRen, rgb[0], rgb[1], rgb[2], 255);
  for (; *text; text++, x += 6 * scale) {
    const char* g = glyph(*text);
    if (!g) continue;
    for (int row = 0; row < 7; row++) {
      for (int col = 0; col < 5; col++) {
        if (g[row * 5 + col] == '#') {
          SDL_Rect px = {x + col * scale, y + row * scale, scale, scale};
          SDL_RenderFillRect(gRen, &px);
        }
      }
    }
  }
}

// Arrowhead pointing along (dx, dy): a one-pixel-thick line per row, widening
// from the tip.
void fillTriangle(const SDL_Rect& r, int dx, int dy, const uint8_t rgb[3]) {
  SDL_SetRenderDrawColor(gRen, rgb[0], rgb[1], rgb[2], 255);
  constexpr int kLen = 9;
  const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  for (int half = 0; half <= kLen; half++) {
    const int back = kLen - half - kLen / 2;  // distance from the centre, toward the tip
    if (dy) SDL_RenderDrawLine(gRen, cx - half, cy + dy * back, cx + half, cy + dy * back);
    else SDL_RenderDrawLine(gRen, cx + dx * back, cy - half, cx + dx * back, cy + half);
  }
}

constexpr uint8_t kIdleRgb[3] = {52, 52, 60}, kHeldRgb[3] = {230, 240, 255};
constexpr float kArrowDead = 0.1f;

bool targetOn(const Target& t) {
  if (t.isButton) return buttonHeld(t.button);
  float sx, sy;
  stickNow(&sx, &sy);
  return (t.dx * sx > kArrowDead) || (t.dy * sy > kArrowDead);
}

void drawControls() {
  const uint8_t* idle = kIdleRgb;
  const uint8_t* held = kHeldRgb;
  const uint8_t ink[3] = {200, 205, 215}, dark[3] = {20, 20, 24};
  float sx, sy;
  stickNow(&sx, &sy);

  for (const Target& t : kTargets) {
    const bool on = targetOn(t);
    const uint8_t* c = on ? held : idle;
    SDL_SetRenderDrawColor(gRen, c[0], c[1], c[2], 255);
    SDL_RenderFillRect(gRen, &t.r);
    if (!t.isButton) {
      fillTriangle(t.r, t.dx, t.dy, on ? dark : ink);
    } else {
      char l[2] = {*buttonName(t.button), 0};
      drawText(t.r.x + (kPad - 10) / 2 - 1, t.r.y + (kPad - 14) / 2, 2, l, on ? dark : ink);
    }
  }
  // Stick position inside the middle of the cross.
  const SDL_Rect mid = {kStickCx - kPad / 2, kStickCy - kPad / 2, kPad, kPad};
  SDL_SetRenderDrawColor(gRen, 40, 40, 48, 255);
  SDL_RenderFillRect(gRen, &mid);
  SDL_Rect dot = {kStickCx - 3 + (int)(sx * (kPad / 2 - 4)), kStickCy - 3 + (int)(sy * (kPad / 2 - 4)), 6, 6};
  SDL_SetRenderDrawColor(gRen, held[0], held[1], held[2], 255);
  SDL_RenderFillRect(gRen, &dot);

  // Key list.
  static const char* kLines[] = {
      "ARROWS: JOYSTICK",
      "SHIFT: HALF TILT",
      "I=Y  L=B  K=A  J=X",
      "S: SNAPSHOT",
      "ESC: QUIT",
      "MOUSE: CLICK CONTROLS",
  };
  int y = kControlsY + 112;
  for (const char* line : kLines) {
    drawText(kColX + 8, y, 2, line, ink);
    y += 16;
  }
}

// Draws into the back buffer; the caller presents.
void drawFrame(const OledFrame& f, const std::vector<uint8_t>& m) {
  SDL_SetRenderDrawColor(gRen, kBoardRgb[0], kBoardRgb[1], kBoardRgb[2], 255);
  SDL_RenderClear(gRen);

  uint8_t* px = nullptr;
  int pitch = 0;
  SDL_LockTexture(gOledTex, nullptr, reinterpret_cast<void**>(&px), &pitch);
  for (int y = 0; y < kOledH; y++) {
    for (int x = 0; x < kOledW; x++) {
      uint8_t* p = px + y * pitch + x * 3;
      const uint8_t* c = f[y * kOledW + x] ? kOledOnRgb : kOledOffRgb;
      p[0] = c[0]; p[1] = c[1]; p[2] = c[2];
    }
  }
  SDL_UnlockTexture(gOledTex);
  SDL_Rect dst = {0, 0, kOledPxW, kOledPxH};
  SDL_RenderCopy(gRen, gOledTex, nullptr, &dst);

  for (int row = 0; row < kMatrixN; row++) {
    for (int col = 0; col < kMatrixN; col++) {
      SDL_Rect cell = ledCell(row, col);
      uint8_t c[3];
      ledRgb(m[row * kMatrixN + col], c);
      SDL_SetRenderDrawColor(gRen, c[0], c[1], c[2], 255);
      SDL_RenderFillRect(gRen, &cell);
    }
  }
  drawControls();
}
}  // namespace

void windowPresent() {
  if (!gRen) return;
  // At most ~60 presents a second of wall time; a fast script must not pay for
  // thousands of frames.
  const uint32_t now = SDL_GetTicks();
  if (now - gLastPresentTicks < 16) return;
  gLastPresentTicks = now;
  drawFrame(readOled(), readMatrix());
  SDL_RenderPresent(gRen);
}

bool windowCapture(const std::string& basePath) {
  if (!gRen) return true;
  const OledFrame f = readOled();
  const std::vector<uint8_t> m = readMatrix();
  drawFrame(f, m);

  std::vector<uint8_t> rgb((size_t)kWinW * kWinH * 3);
  if (SDL_RenderReadPixels(gRen, nullptr, SDL_PIXELFORMAT_RGB24, rgb.data(), kWinW * 3) != 0) {
    fprintf(stderr, "window capture: %s\n", SDL_GetError());
    SDL_RenderPresent(gRen);
    return false;
  }
  SDL_RenderPresent(gRen);
  png::writeRgb(basePath + "-window.png", kWinW, kWinH, rgb);

  // Every window pixel must be the colour the frames call for: each OLED pixel
  // fills a scale x scale block, each LED an inset square, and the rest is board.
  std::vector<uint8_t> want((size_t)kWinW * kWinH * 3);
  auto put = [&](int x, int y, const uint8_t* c) {
    uint8_t* p = &want[((size_t)y * kWinW + x) * 3];
    p[0] = c[0]; p[1] = c[1]; p[2] = c[2];
  };
  for (int y = 0; y < kWinH; y++) for (int x = 0; x < kWinW; x++) put(x, y, kBoardRgb);
  for (int y = 0; y < kOledPxH; y++) {
    for (int x = 0; x < kOledPxW; x++) {
      put(x, y, f[(y / kOledScale) * kOledW + x / kOledScale] ? kOledOnRgb : kOledOffRgb);
    }
  }
  for (int row = 0; row < kMatrixN; row++) {
    for (int col = 0; col < kMatrixN; col++) {
      const SDL_Rect r = ledCell(row, col);
      uint8_t c[3];
      ledRgb(m[row * kMatrixN + col], c);
      for (int y = r.y; y < r.y + r.h; y++) for (int x = r.x; x < r.x + r.w; x++) put(x, y, c);
    }
  }
  // The controls panel below the matrix is drawn from the virtual pins and a
  // fixed key list, not from the frames, so the pixel comparison skips it.
  size_t bad = 0;
  int firstX = -1, firstY = -1;
  for (int y = 0; y < kWinH; y++) {
    for (int x = 0; x < kWinW; x++) {
      if (x >= kColX && y >= kControlsY) continue;
      const size_t i = ((size_t)y * kWinW + x) * 3;
      if (memcmp(&rgb[i], &want[i], 3) != 0) {
        if (!bad) { firstX = x; firstY = y; }
        bad++;
      }
    }
  }
  if (bad) {
    fprintf(stderr, "window capture: %zu of %d pixels differ from the frames (first at %d,%d); see %s-window.png\n",
            bad, kWinW * kWinH, firstX, firstY, basePath.c_str());
    return false;
  }

  // The controls panel is only checked for what the pins say: the top-left pixel
  // of each control (clear of the arrow or letter drawn in its middle) must be
  // the held or idle colour to match the pin state.
  for (const Target& t : kTargets) {
    const uint8_t* c = targetOn(t) ? kHeldRgb : kIdleRgb;
    const size_t i = ((size_t)(t.r.y + 1) * kWinW + t.r.x + 1) * 3;
    if (memcmp(&rgb[i], c, 3) != 0) {
      fprintf(stderr, "window capture: control at %d,%d is drawn %s but the pins say %s; see %s-window.png\n",
              t.r.x, t.r.y, targetOn(t) ? "idle" : "held", targetOn(t) ? "held" : "idle", basePath.c_str());
      return false;
    }
  }
  printf("[host] window capture matches the frames (OLED, LED matrix and control state, %dx%d window)\n", kWinW, kWinH);
  return true;
}

void windowClose() {
  if (gPad) SDL_GameControllerClose(gPad);
  if (gOledTex) SDL_DestroyTexture(gOledTex);
  if (gRen) SDL_DestroyRenderer(gRen);
  if (gWin) SDL_DestroyWindow(gWin);
  SDL_Quit();
  gPad = nullptr; gOledTex = nullptr; gRen = nullptr; gWin = nullptr;
}

}  // namespace harness

#endif  // HOST_HAVE_SDL
