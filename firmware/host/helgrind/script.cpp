#include "script.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "HelgrindGame.h"
#include "display.h"
#include "status.h"

using namespace helgrind;

namespace script {
namespace {

constexpr uint32_t kTickMs = 16;

struct Runner {
  std::string outDir;
  uint32_t now = 1000;
  GameInput held;     // stick persists between commands
  GameInput pending;  // button edges, consumed by the next tick
  bool begun = false;
  int snaps = 0;
  int line = 0;

  void ensureBegun() {
    if (!begun) begin(nullptr);
  }

  void begin(const SaveData* save) {
    gameBegin(save, now);
    begun = true;
  }

  void tick() {
    ensureBegun();
    GameInput in = held;
    in.a = pending.a; in.b = pending.b; in.x = pending.x; in.y = pending.y;
    pending = GameInput();
    now += kTickMs;
    gameStep(in, now);
  }

  int fail(const std::string& why) {
    std::fprintf(stderr, "line %d: %s\n  status: %s\n", line, why.c_str(), status::line().c_str());
    return 1;
  }

  int bad(const std::string& why) {
    std::fprintf(stderr, "line %d: %s\n", line, why.c_str());
    return 2;
  }

  // Returns -1 to keep going, else the exit code.
  int command(std::istringstream& ss, const std::string& cmd) {
    if (cmd == "new") {
      begin(nullptr);
    } else if (cmd == "load") {
      SaveData s;
      s.hp = 12;
      std::string kv;
      while (ss >> kv) {
        size_t eq = kv.find('=');
        if (eq == std::string::npos) return bad("load expects k=v: " + kv);
        std::string k = kv.substr(0, eq);
        unsigned long long v = std::strtoull(kv.c_str() + eq + 1, nullptr, 0);
        if (k == "room") s.room = v; else if (k == "hp") s.hp = v;
        else if (k == "silver") s.silver = v; else if (k == "arts") s.arts = v;
        else if (k == "weapon") s.weapon = v; else if (k == "won") s.won = v != 0;
        else if (k == "loot") s.loot = v; else if (k == "ex") s.entryX = v;
        else if (k == "ey") s.entryY = v;
        else return bad("unknown load field " + k);
      }
      begin(&s);
    } else if (cmd == "hold") {
      std::string dirs;
      ss >> dirs;
      held.stickX = held.stickY = 0.0f;
      for (char c : dirs) {
        if (c == 'u') held.stickY = -1.0f;
        if (c == 'd') held.stickY = 1.0f;
        if (c == 'l') held.stickX = -1.0f;
        if (c == 'r') held.stickX = 1.0f;
      }
    } else if (cmd == "press") {
      std::string b;
      ss >> b;
      if (b == "a") pending.a = true; else if (b == "b") pending.b = true;
      else if (b == "x") pending.x = true; else if (b == "y") pending.y = true;
      else return bad("bad button " + b);
      tick();
    } else if (cmd == "wait") {
      uint32_t ms = 0;
      ss >> ms;
      for (uint32_t t = 0; t < ms; t += kTickMs) tick();
    } else if (cmd == "until") {
      std::string expr;
      uint32_t ms = 0;
      ss >> expr >> ms;
      bool known = false, ok = false;
      for (uint32_t t = 0; t < ms && !ok; t += kTickMs) {
        tick();
        ok = status::matches(expr, known);
        if (!known) return bad("unknown field in " + expr);
      }
      if (!ok) return fail("until " + expr + " timed out");
    } else if (cmd == "snap") {
      ensureBegun();
      std::string name;
      if (!(ss >> name)) {
        char b[16];
        std::snprintf(b, sizeof b, "%03d", snaps);
        name = b;
      }
      snaps++;
      display::snapshot(outDir + "/" + name, now);
      std::printf("snap %s  %s\n", name.c_str(), status::line().c_str());
    } else if (cmd == "status") {
      ensureBegun();
      std::printf("%s\n", status::line().c_str());
    } else if (cmd == "assert") {
      ensureBegun();
      std::string expr;
      while (ss >> expr) {
        bool known = false;
        if (!status::matches(expr, known)) {
          if (!known) return bad("unknown field in " + expr);
          return fail("assert " + expr + " failed");
        }
      }
    } else {
      return bad("unknown command " + cmd);
    }
    return -1;
  }
};

}  // namespace

int run(std::istream& in, const std::string& outDir) {
  Runner r;
  r.outDir = outDir;
  std::string text;
  while (std::getline(in, text)) {
    r.line++;
    size_t hash = text.find('#');
    if (hash != std::string::npos) text.erase(hash);
    std::istringstream ss(text);
    std::string cmd;
    if (!(ss >> cmd)) continue;
    int rc = r.command(ss, cmd);
    if (rc >= 0) return rc;
  }
  return 0;
}

}  // namespace script
