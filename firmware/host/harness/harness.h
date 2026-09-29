#pragma once
// Shared declarations for the host harness: exit codes, the run options, and the
// small interfaces between main, the panels, the input injector and the script.

#include <stdint.h>
#include <string>
#include <vector>

namespace harness {

// Exit status. An agent's process runner can tell the outcomes apart without
// looking at images. 42 and 43 come from host_platform.h.
enum ExitCode {
  kExitOk = 0,
  kExitUsage = 1,          // bad command line or unreadable input file
  kExitScriptParse = 2,    // the script did not parse
  kExitAssertion = 3,      // an expect_* command failed
  kExitScriptTimeout = 4,  // the script ran past --max-virtual-ms
  kExitWatchdog = 5,       // the wall-clock watchdog fired
  kExitSanitizer = 6,      // AddressSanitizer report
  kExitUbsan = 7,          // UndefinedBehaviorSanitizer report
  // 42: esp_restart() ran.  43: esp_deep_sleep_start() ran.
};

struct Options {
  std::string scriptPath;       // empty: live mode
  bool repl = false;            // feed stdin to the MicroPython REPL
  // Host directories copied onto the FAT volume after boot, as SRC or SRC:/DEST.
  std::vector<std::string> fsDirs;
  bool window = false;          // show the SDL window (live mode always does)
  std::string outDir = ".pio/host-out";  // where snap writes its files (git-ignored)
  std::string stateDir;         // persist fat.img and nvs.txt here when set
  uint32_t maxVirtualMs = 0;    // 0: script length + slack
  uint32_t wallTimeoutS = 0;    // 0: derived from the script length
};

extern Options gOptions;

// ── Filesystem ────────────────────────────────────────────────────────────
// Copy host directory trees onto the mounted FAT volume, skipping files already
// there, then rescan /apps. Each spec is SRC (copied to /) or SRC:/DEST. Called
// once after setup(), when MicroPython has mounted the volume.
bool populateFilesystem(const std::vector<std::string>& specs);

// ── Panels ────────────────────────────────────────────────────────────────
constexpr int kOledW = 128;
constexpr int kOledH = 64;
constexpr int kMatrixN = 8;

// One byte per pixel, 0 or 255, row-major.
using OledFrame = std::vector<uint8_t>;
OledFrame readOled();
// The 8x8 LED matrix as a viewer sees it. One brightness per LED, row-major.
std::vector<uint8_t> readMatrix();

// Text renderings: '#' on, '.' off, one row per line. The matrix uses a
// brightness digit 0-9 so a dimmed LED differs from a bright one.
std::string oledText(const OledFrame& f);
std::string matrixText(const std::vector<uint8_t>& m);

// False if a file could not be written. `windowMismatch`, when given, is set if a
// window capture disagreed with the frames.
bool writeSnapshot(const std::string& prefix, bool* windowMismatch = nullptr);

// ── Input injector ────────────────────────────────────────────────────────
enum class Button { Y, B, A, X };
bool parseButton(const std::string& name, Button* out);
const char* buttonName(Button b);
void inputInit();
void setButton(Button b, bool pressed);
// Screen-space deflection, -1..1. x is positive to the right, y is positive down.
void setStick(float x, float y);

// ── Script ────────────────────────────────────────────────────────────────
struct Step;
class Script {
 public:
  bool load(const std::string& path, std::string* err);
  // Advance to the virtual time `nowUs`. Returns true once every step has run.
  bool tick(uint64_t nowUs);
  uint32_t totalMs() const { return totalMs_; }
  std::string where() const;
  ~Script();
  Script();

 private:
  struct Impl;
  Impl* impl_;
  uint32_t totalMs_ = 0;
};

// ── Window ────────────────────────────────────────────────────────────────
// Live viewer. present() draws both panels; pollInput() reads keys and the
// controller into the injector and returns false once the window is closed.
bool windowOpen();
bool windowPollInput();
void windowPresent();
void windowClose();
// Draws the current frame, reads the window's pixels back, writes them to
// <basePath>-window.png, and checks every pixel against what readOled() and
// readMatrix() say should be drawn. True when there is no window or all match.
bool windowCapture(const std::string& basePath);

}  // namespace harness
