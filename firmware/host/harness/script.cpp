// Scripted input: a plain text file of button, stick and wait commands, timed
// on the virtual clock and applied at the delay() yield point. Also the state
// dump and assertion commands that let a run report pass or fail by exit code.

#include "harness.h"
#include "host_platform.h"
#include "ui/GUI.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>

extern GUIManager guiManager;

namespace harness {

namespace {
// The debounce time in Inputs.h. A shorter press is dropped on release and the
// button then stays pressed, because the host has no contact bounce to trigger
// a later sample.
constexpr uint32_t kMinHoldMs = 20;
constexpr uint32_t kDefaultTapMs = 60;

// Read by the wall-clock watchdog's signal handler, which may only call
// async-signal-safe functions, so it is a plain buffer.
}  // namespace
char gLastStep[200] = "(script not started)";

struct Step {
  enum Type { Press, Release, Tap, Stick, Wait, Snap, Dump, Expect, Log, Open, Quit } type = Wait;
  int line = 0;
  std::string text;      // the source line, for messages
  Button button = Button::A;
  float x = 0, y = 0;
  uint32_t ms = 0;
  std::string arg;       // snap prefix, dump/expect target, log text
  std::string path;      // expect golden file
  int code = 0;
  ScreenId screen = kScreenNone;
};

struct Script::Impl {
  std::vector<Step> steps;
  size_t pc = 0;
  bool started = false;
  uint64_t resumeAt = 0;
  std::function<void()> endAction;
  std::string current = "(script not started)";
};

Script::Script() : impl_(new Impl) {}
Script::~Script() { delete impl_; }

namespace {

// Screens a script can open directly, so it does not depend on the home grid's tile order.
struct NamedScreen { const char* name; ScreenId id; };
const NamedScreen kScreens[] = {
    {"helgrind", kScreenHelgrind},
    {"vectortank", kScreenVectortank},
    {"packit", kScreenPackit},
    {"matrix", kScreenMatrixApps},
};

bool parseUint(const std::string& s, uint32_t* out) {
  if (s.empty()) return false;
  char* end = nullptr;
  const unsigned long v = strtoul(s.c_str(), &end, 10);
  if (*end) return false;
  *out = (uint32_t)v;
  return true;
}

bool parseFloat(const std::string& s, float* out) {
  if (s.empty()) return false;
  char* end = nullptr;
  const float v = strtof(s.c_str(), &end);
  if (*end || v < -1.0f || v > 1.0f) return false;
  *out = v;
  return true;
}

std::string readFile(const std::string& path, bool* ok) {
  std::ifstream in(path, std::ios::binary);
  *ok = (bool)in;
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Reports the first differing row, and writes the actual frame beside the golden
// file so a person or agent can diff the two.
bool expectFrame(const Step& s, const std::string& actual) {
  // HOST_UPDATE_GOLDENS=1 rewrites the golden file instead of comparing, for use
  // after an intended UI change (host/run-tests.sh --update).
  if (getenv("HOST_UPDATE_GOLDENS")) {
    if (FILE* f = fopen(s.path.c_str(), "w")) {
      fputs(actual.c_str(), f);
      fclose(f);
      printf("[script] updated golden %s\n", s.path.c_str());
      return true;
    }
    fprintf(stderr, "script line %d: cannot write golden file %s\n", s.line, s.path.c_str());
    return false;
  }
  bool ok = false;
  const std::string golden = readFile(s.path, &ok);
  if (!ok) {
    fprintf(stderr, "script line %d: cannot read golden file %s\n", s.line, s.path.c_str());
    return false;
  }
  if (golden == actual) return true;
  const std::string actualPath = s.path + ".actual";
  if (FILE* f = fopen(actualPath.c_str(), "w")) {
    fputs(actual.c_str(), f);
    fclose(f);
  }
  size_t row = 0, a = 0, g = 0;
  while (a < actual.size() && g < golden.size()) {
    size_t ae = actual.find('\n', a), ge = golden.find('\n', g);
    if (actual.substr(a, ae - a) != golden.substr(g, ge - g)) break;
    a = ae + 1; g = ge + 1; row++;
  }
  fprintf(stderr, "script line %d: %s differs from %s at row %zu (actual written to %s)\n",
          s.line, s.arg.c_str(), s.path.c_str(), row, actualPath.c_str());
  return false;
}

}  // namespace

bool Script::load(const std::string& path, std::string* err) {
  bool ok = false;
  const std::string text = readFile(path, &ok);
  if (!ok) { *err = "cannot read script " + path; return false; }

  std::istringstream in(text);
  std::string line;
  int lineNo = 0;
  uint32_t total = 0;
  auto fail = [&](const std::string& why) {
    *err = path + ":" + std::to_string(lineNo) + ": " + why + ": " + line;
    return false;
  };

  while (std::getline(in, line)) {
    lineNo++;
    std::string body = line.substr(0, line.find('#'));
    std::istringstream words(body);
    std::vector<std::string> w;
    for (std::string t; words >> t;) w.push_back(t);
    if (w.empty()) continue;

    Step s;
    s.line = lineNo;
    s.text = body;
    const std::string& cmd = w[0];

    if (cmd == "press" || cmd == "release") {
      if (w.size() != 2 || !parseButton(w[1], &s.button)) return fail("expected: " + cmd + " <Y|B|A|X>");
      s.type = cmd == "press" ? Step::Press : Step::Release;
    } else if (cmd == "tap") {
      if (w.size() < 2 || w.size() > 3 || !parseButton(w[1], &s.button)) return fail("expected: tap <Y|B|A|X> [hold_ms]");
      s.ms = kDefaultTapMs;
      if (w.size() == 3 && !parseUint(w[2], &s.ms)) return fail("hold_ms is not a number");
      if (s.ms < kMinHoldMs) {
        return fail("hold under the " + std::to_string(kMinHoldMs) +
                    " ms debounce time; the release would be dropped");
      }
      s.type = Step::Tap;
      total += s.ms;
    } else if (cmd == "stick") {
      if (w.size() != 4 || !parseFloat(w[1], &s.x) || !parseFloat(w[2], &s.y) || !parseUint(w[3], &s.ms)) {
        return fail("expected: stick <x -1..1> <y -1..1> <ms> (0 = hold until the next stick)");
      }
      s.type = Step::Stick;
      total += s.ms;
    } else if (cmd == "wait") {
      if (w.size() != 2 || !parseUint(w[1], &s.ms)) return fail("expected: wait <ms>");
      s.type = Step::Wait;
      total += s.ms;
    } else if (cmd == "snap") {
      if (w.size() != 2) return fail("expected: snap <prefix>");
      s.type = Step::Snap;
      s.arg = w[1];
    } else if (cmd == "dump") {
      if (w.size() != 2 || (w[1] != "oled" && w[1] != "matrix")) return fail("expected: dump <oled|matrix>");
      s.type = Step::Dump;
      s.arg = w[1];
    } else if (cmd == "expect") {
      if (w.size() != 3 || (w[1] != "oled" && w[1] != "matrix")) return fail("expected: expect <oled|matrix> <golden file>");
      s.type = Step::Expect;
      s.arg = w[1];
      s.path = w[2];
    } else if (cmd == "log") {
      s.type = Step::Log;
      const size_t at = body.find("log");
      s.arg = body.substr(at + 3);
      if (!s.arg.empty() && s.arg[0] == ' ') s.arg.erase(0, 1);
    } else if (cmd == "open") {
      if (w.size() != 2) return fail("expected: open <screen>");
      for (const NamedScreen& n : kScreens) {
        if (w[1] == n.name) s.screen = n.id;
      }
      if (s.screen == kScreenNone) {
        std::string names;
        for (const NamedScreen& n : kScreens) names += std::string(names.empty() ? "" : "|") + n.name;
        return fail("expected: open <" + names + ">");
      }
      s.type = Step::Open;
    } else if (cmd == "quit") {
      s.type = Step::Quit;
      uint32_t c = 0;
      if (w.size() > 2 || (w.size() == 2 && !parseUint(w[1], &c))) return fail("expected: quit [code]");
      s.code = (int)c;
    } else {
      return fail("unknown command");
    }
    impl_->steps.push_back(s);
  }
  totalMs_ = total;
  return true;
}

bool Script::tick(uint64_t nowUs) {
  Impl& s = *impl_;
  if (!s.started) {
    s.started = true;
    s.resumeAt = nowUs;
  }
  for (;;) {
    if (nowUs < s.resumeAt) return false;
    if (s.endAction) {
      s.endAction();
      s.endAction = nullptr;
    }
    if (s.pc >= s.steps.size()) return true;

    const Step& st = s.steps[s.pc++];
    snprintf(gLastStep, sizeof gLastStep, "script line %d:%s", st.line, st.text.c_str());
    switch (st.type) {
      case Step::Press: setButton(st.button, true); break;
      case Step::Release: setButton(st.button, false); break;
      case Step::Tap: {
        setButton(st.button, true);
        const Button b = st.button;
        s.endAction = [b]() { setButton(b, false); };
        s.resumeAt = nowUs + (uint64_t)st.ms * 1000;
        break;
      }
      case Step::Stick:
        setStick(st.x, st.y);
        // A duration of 0 leaves the stick where it is until the next stick step.
        if (st.ms) s.endAction = []() { setStick(0.0f, 0.0f); };
        s.resumeAt = nowUs + (uint64_t)st.ms * 1000;
        break;
      case Step::Wait: s.resumeAt = nowUs + (uint64_t)st.ms * 1000; break;
      case Step::Snap:
      {
        bool mismatch = false;
        if (!writeSnapshot(st.arg, &mismatch)) {
          fprintf(stderr, "script line %d: snapshot %s under %s failed%s\n", st.line, st.arg.c_str(),
                  gOptions.outDir.c_str(), mismatch ? ": the window does not show the frames" : "");
          // A window that disagrees with the frames is a failed check, like expect.
          if (mismatch) host_exit(kExitAssertion);
        }
        break;
      }
      case Step::Dump:
        fputs(st.arg == "oled" ? oledText(readOled()).c_str() : matrixText(readMatrix()).c_str(), stdout);
        break;
      case Step::Expect: {
        const std::string actual = st.arg == "oled" ? oledText(readOled()) : matrixText(readMatrix());
        if (!expectFrame(st, actual)) host_exit(kExitAssertion);
        break;
      }
      case Step::Log: printf("[script] %s\n", st.arg.c_str()); break;
      case Step::Open: guiManager.pushScreen(st.screen); break;
      case Step::Quit: host_exit(st.code); break;
    }
  }
}

std::string Script::where() const { return gLastStep; }

}  // namespace harness
