// Host driver for the Helgrind game core — the same HelgrindGame.cpp the
// badge runs, built for this machine with u8g2 drawing into memory.
//
//   helgrind-host <script> [outdir]   run a script, write PNGs (script.h)
//   helgrind-host --play [outdir]     play in an SDL2 window (window.h)
//   helgrind-host --strwidth <s>...   print each string's width in the
//                                     badge's text font (used by check-world.py)
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "display.h"
#include "script.h"
#include "window.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <script> [outdir] | --play [outdir] | --strwidth <string>...\n", argv[0]);
    return 2;
  }
  display::init();

  if (std::strcmp(argv[1], "--strwidth") == 0) {
    u8g2_SetFont(display::u8g2(), u8g2_font_smallsimple_tr);
    for (int i = 2; i < argc; i++) {
      std::printf("%3d px  %s\n", u8g2_GetStrWidth(display::u8g2(), argv[i]), argv[i]);
    }
    return 0;
  }

  std::string outDir = argc > 2 ? argv[2] : "out";
  std::error_code ec;
  std::filesystem::create_directories(outDir, ec);
  if (ec) {
    std::fprintf(stderr, "cannot create %s: %s\n", outDir.c_str(), ec.message().c_str());
    return 2;
  }

  if (std::strcmp(argv[1], "--play") == 0) return window::play(outDir);

  std::ifstream f(argv[1]);
  if (!f) {
    std::fprintf(stderr, "cannot open %s\n", argv[1]);
    return 2;
  }
  return script::run(f, outDir);
}
