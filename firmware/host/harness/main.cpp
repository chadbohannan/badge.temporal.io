// Host harness entry point. Runs the firmware's own setup() and loop() over the
// platform shim (host/shim/), with a live SDL window or a scripted headless run.
//
//   badge-host                          live window
//   badge-host --script FILE            scripted, headless, virtual clock
//   badge-host --script FILE --window   scripted, with the window
//
// See wiki/systems/host-test-harness.md for the design and the exit codes.

#include "harness.h"
#include "host_platform.h"
#include "wear_levelling.h"

#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void setup();
void loop();

namespace harness {
Options gOptions;
extern char gLastStep[200];

namespace {
Script gScript;
bool gHaveScript = false;
bool gBooted = false;
bool gLive = false;
uint32_t gMaxVirtualMs = 0;
uint64_t gScriptStartUs = 0;  // virtual time when setup() returned

// The yield point. delay(), vTaskDelay() and yield() land here after they move
// the clock, so this is the one place input is applied and frames are shown.
void onYield() {
  if (gLive) {
    if (!windowPollInput()) host_exit(kExitOk);
    windowPresent();
  }
  if (gHaveScript && gBooted) {
    if (gScript.tick(host_clock_peek_us())) host_exit(kExitOk);
    if (gMaxVirtualMs && host_clock_peek_us() - gScriptStartUs > (uint64_t)gMaxVirtualMs * 1000ull) {
      fprintf(stderr, "script timed out on the virtual clock at: %s\n", gLastStep);
      host_exit(kExitScriptTimeout);
    }
  }
}

void onExit(int code) {
  if (!gOptions.stateDir.empty()) host_flash_save((gOptions.stateDir + "/fat.img").c_str());
  if (gLive) windowClose();
  fflush(stdout);
  (void)code;
}

// A spin that never reads the clock or calls delay() cannot be caught on the
// virtual clock, so a real-time alarm ends the run and names the last step.
void onAlarm(int) {
  static const char msg[] = "\nwall-clock watchdog fired; last step: ";
  (void)!write(2, msg, sizeof msg - 1);
  (void)!write(2, gLastStep, strlen(gLastStep));
  (void)!write(2, "\n", 1);
  _exit(kExitWatchdog);
}

void usage() {
  fprintf(stderr,
          "usage: badge-host [--script FILE] [--window] [--repl] [--fs-dir SRC[:/DEST]]... [--out DIR] [--state-dir DIR]\n"
          "                  [--max-virtual-ms N] [--wall-timeout S]\n");
}
}  // namespace
}  // namespace harness

// Sanitizer reports use their own exit codes so a runner can tell them from a
// script failure.
// detect_stack_use_after_return=0: with it on, locals move to a "fake stack" and
// MicroPython's conservative stack scan (which starts from the address of a local)
// would run across unrelated memory.
extern "C" const char* __asan_default_options() {
  return "exitcode=6:detect_leaks=0:abort_on_error=0:detect_stack_use_after_return=0";
}
extern "C" const char* __ubsan_default_options() { return "halt_on_error=1:exitcode=7:print_stacktrace=1"; }

int main(int argc, char** argv) {
  using namespace harness;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    auto next = [&](const char* what) -> const char* {
      if (i + 1 >= argc) { fprintf(stderr, "%s needs a value\n", what); usage(); exit(kExitUsage); }
      return argv[++i];
    };
    if (a == "--script") gOptions.scriptPath = next("--script");
    else if (a == "--window") gOptions.window = true;
    else if (a == "--repl") gOptions.repl = true;
    else if (a == "--fs-dir") gOptions.fsDirs.push_back(next("--fs-dir"));
    else if (a == "--out") gOptions.outDir = next("--out");
    else if (a == "--state-dir") gOptions.stateDir = next("--state-dir");
    else if (a == "--max-virtual-ms") gOptions.maxVirtualMs = (uint32_t)atol(next("--max-virtual-ms"));
    else if (a == "--wall-timeout") gOptions.wallTimeoutS = (uint32_t)atol(next("--wall-timeout"));
    else { usage(); return kExitUsage; }
  }

  gHaveScript = !gOptions.scriptPath.empty();
  gLive = !gHaveScript || gOptions.window;
  setvbuf(stdout, nullptr, _IOLBF, 0);

  uint32_t wallS = gOptions.wallTimeoutS;
  if (gHaveScript) {
    std::string err;
    if (!gScript.load(gOptions.scriptPath, &err)) {
      fprintf(stderr, "%s\n", err.c_str());
      return kExitScriptParse;
    }
    gMaxVirtualMs = gOptions.maxVirtualMs ? gOptions.maxVirtualMs : gScript.totalMs() + 30000;
    // Boot plus the script, with slack for a slow machine and ASan.
    if (!wallS) wallS = 120 + gScript.totalMs() / 500;
  }

  host_set_exit_hook(onExit);
  host_set_yield_hook(onYield);
  if (gLive && !windowOpen()) {
    if (!gHaveScript) return kExitUsage;
    gLive = false;
  }
  // With a window the run is paced to the wall clock so it can be watched; a
  // headless script runs as fast as the machine allows.
  // --repl is for typing, so it runs in real time too.
  host_set_realtime(gLive || gOptions.repl);
  if (wallS && (gHaveScript || gOptions.wallTimeoutS)) {
    signal(SIGALRM, onAlarm);
    alarm(wallS);
  }

  if (!gOptions.stateDir.empty()) {
    mkdir(gOptions.stateDir.c_str(), 0755);
    host_kv_load((gOptions.stateDir + "/nvs.txt").c_str());
    host_flash_load((gOptions.stateDir + "/fat.img").c_str());
  }
  if (gOptions.repl) host_serial_stdin(1);
  inputInit();

  setup();
  if (!gOptions.fsDirs.empty() && !populateFilesystem(gOptions.fsDirs)) return kExitUsage;
  gBooted = true;
  gScriptStartUs = host_clock_peek_us();
  for (;;) loop();
}
