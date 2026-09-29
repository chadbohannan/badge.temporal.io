// Host platform shim: virtual clock, yield point, virtual pins, Serial and the
// other Arduino-core objects. See host_platform.h for the harness-facing side.

#include "Arduino.h"
#include "Wire.h"
#include "SPI.h"
#include "WiFi.h"
#include "esp_timer.h"

#include <random>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>

// ── Virtual clock ─────────────────────────────────────────────────────────

namespace {
uint64_t gClockUs = 1000000;  // boot at 1 s so "0 means never" timers behave
host_yield_fn gYieldHook = nullptr;
bool gRealtime = false;
uint64_t gWallStartNs = 0;
uint64_t gVirtStartUs = 0;
bool gPaceStarted = false;
int gYieldDepth = 0;

uint64_t wallNs() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// Live mode: hold the virtual clock to the wall clock. It sleeps only when
// virtual time is ahead, so a slow frame never has to be paid back.
void pace() {
  if (!gRealtime) return;
  if (!gPaceStarted) {
    gPaceStarted = true;
    gWallStartNs = wallNs();
    gVirtStartUs = gClockUs;
    return;
  }
  const uint64_t virtNs = (gClockUs - gVirtStartUs) * 1000ull;
  const uint64_t wall = wallNs() - gWallStartNs;
  if (virtNs > wall) {
    const uint64_t ns = virtNs - wall;
    timespec ts{(time_t)(ns / 1000000000ull), (long)(ns % 1000000000ull)};
    nanosleep(&ts, nullptr);
  }
}

// ── esp_timer ─────────────────────────────────────────────────────────────
struct HostTimer {
  esp_timer_cb_t cb;
  void* arg;
  bool active = false;
  bool periodic = false;
  uint64_t periodUs = 0;
  uint64_t nextUs = 0;
};
std::vector<HostTimer*> gTimers;

void runTimers() {
  for (size_t i = 0; i < gTimers.size(); i++) {
    HostTimer* t = gTimers[i];
    if (!t->active || gClockUs < t->nextUs) continue;
    if (t->periodic) {
      // A long delay() can skip many periods; fire once, not once per period.
      t->nextUs = gClockUs + t->periodUs;
    } else {
      t->active = false;
    }
    t->cb(t->arg);
  }
}

void afterAdvance() {
  runTimers();
  pace();
  if (gYieldHook && gYieldDepth == 0) {
    gYieldDepth++;
    gYieldHook();
    gYieldDepth--;
  }
}
}  // namespace

extern "C" {
uint64_t host_clock_us(void) { return gClockUs++; }
uint64_t host_clock_peek_us(void) { return gClockUs; }
void host_clock_advance_us(uint64_t us) { gClockUs += us; }
void host_set_yield_hook(host_yield_fn fn) { gYieldHook = fn; }
void host_set_realtime(int on) { gRealtime = on != 0; gPaceStarted = false; }
}

unsigned long millis(void) { return (unsigned long)(host_clock_us() / 1000); }
unsigned long micros(void) { return (unsigned long)host_clock_us(); }

void delay(uint32_t ms) {
  gClockUs += (uint64_t)ms * 1000;
  afterAdvance();
}

void delayMicroseconds(uint32_t us) { gClockUs += us; }

// yield() moves time on by one tick, like vTaskDelay(1). A loop that waits for
// something only the harness can change (scripted input) would never see it
// if yield() left the clock alone.
void yield(void) { delay(1); }

// ── Process control ───────────────────────────────────────────────────────

namespace {
host_exit_fn gExitHook = nullptr;
}
extern "C" {
void host_set_exit_hook(host_exit_fn fn) { gExitHook = fn; }
void host_exit(int code) {
  if (gExitHook) gExitHook(code);
  fflush(stdout);
  _exit(code);
}
}

// ── Randomness ────────────────────────────────────────────────────────────
// A fixed seed keeps a scripted run repeatable.

namespace {
std::mt19937& rng() {
  static std::mt19937 r(0xBADC0DE);
  return r;
}
}  // namespace
long random(long max) { return max <= 0 ? 0 : (long)(rng()() % (uint32_t)max); }
long random(long min, long max) { return min >= max ? min : min + random(max - min); }
void randomSeed(unsigned long seed) { rng().seed((uint32_t)seed); }
extern "C" uint32_t esp_random(void) { return rng()(); }
extern "C" void esp_fill_random(void* buf, size_t len) {
  uint8_t* p = (uint8_t*)buf;
  for (size_t i = 0; i < len; i++) p[i] = (uint8_t)(rng()() & 0xff);
}

// ── Virtual pins ──────────────────────────────────────────────────────────
// One record per GPIO. Digital level and analog value are separate because
// GPIO 13 is both JOY_X and INT_PWR_PIN on the real board.

namespace {
struct Pin {
  uint8_t mode = INPUT;
  int level = LOW;
  int analog = 0;
  bool driven = false;  // set once the harness drives it; overrides pull-ups
  voidFuncPtr isr = nullptr;
  voidFuncPtrArg isrArg = nullptr;
  void* arg = nullptr;
  int trigger = DISABLED;
};
Pin gPins[HOST_PIN_COUNT];

Pin* pinAt(int p) { return (p >= 0 && p < HOST_PIN_COUNT) ? &gPins[p] : nullptr; }
}  // namespace

extern "C" {
void pinMode(uint8_t pin, uint8_t mode) {
  Pin* p = pinAt(pin);
  if (!p) return;
  p->mode = mode;
  if (p->driven) return;  // an external source wins over the internal pull
  if (mode == INPUT_PULLUP) p->level = HIGH;
  else if (mode == INPUT_PULLDOWN || mode == INPUT) p->level = LOW;
}

void digitalWrite(uint8_t pin, uint8_t val) {
  Pin* p = pinAt(pin);
  if (!p) return;
  if ((p->mode & 0x02) || !p->driven) p->level = val ? HIGH : LOW;
}

int digitalRead(uint8_t pin) {
  Pin* p = pinAt(pin);
  return p ? p->level : LOW;
}

void attachInterrupt(uint8_t pin, voidFuncPtr handler, int mode) {
  Pin* p = pinAt(pin);
  if (!p) return;
  p->isr = handler;
  p->isrArg = nullptr;
  p->trigger = mode;
}

void attachInterruptArg(uint8_t pin, voidFuncPtrArg handler, void* arg, int mode) {
  Pin* p = pinAt(pin);
  if (!p) return;
  p->isr = nullptr;
  p->isrArg = handler;
  p->arg = arg;
  p->trigger = mode;
}

// detachInterrupt clears the handler, so Inputs::suspendInterrupts() really
// suppresses input, as it does on the badge.
void detachInterrupt(uint8_t pin) {
  Pin* p = pinAt(pin);
  if (!p) return;
  p->isr = nullptr;
  p->isrArg = nullptr;
  p->trigger = DISABLED;
}

// ISRs only run from the yield hook, so they never run concurrently with the
// code they interrupt. Critical sections have nothing to guard.
void noInterrupts(void) {}
void interrupts(void) {}

void host_pin_set(int pin, int level) {
  Pin* p = pinAt(pin);
  if (!p) return;
  level = level ? HIGH : LOW;
  const int old = p->level;
  p->driven = true;
  p->level = level;
  if (old == level) return;
  bool fire = false;
  switch (p->trigger) {
    case CHANGE: fire = true; break;
    case RISING: fire = level == HIGH; break;
    case FALLING: fire = level == LOW; break;
    default: break;
  }
  if (!fire) return;
  if (p->isr) p->isr();
  else if (p->isrArg) p->isrArg(p->arg);
}

void host_pin_set_analog(int pin, int value) {
  Pin* p = pinAt(pin);
  if (p) p->analog = value < 0 ? 0 : (value > 4095 ? 4095 : value);
}
int host_pin_get(int pin) { Pin* p = pinAt(pin); return p ? p->level : 0; }
int host_pin_get_analog(int pin) { Pin* p = pinAt(pin); return p ? p->analog : 0; }
int host_pin_mode(int pin) { Pin* p = pinAt(pin); return p ? p->mode : 0; }
int host_pin_has_isr(int pin) { Pin* p = pinAt(pin); return p && (p->isr || p->isrArg); }

int analogRead(uint8_t pin) { return host_pin_get_analog(pin); }
uint32_t analogReadMilliVolts(uint8_t pin) { return (uint32_t)host_pin_get_analog(pin) * 3300u / 4095u; }
void analogReadResolution(uint8_t) {}
void analogSetAttenuation(adc_attenuation_t) {}
void analogSetPinAttenuation(uint8_t, adc_attenuation_t) {}
void analogWrite(uint8_t, int) {}

// driver/gpio.h
int gpio_get_level(int pin) { return digitalRead((uint8_t)pin); }
esp_err_t gpio_set_level(int pin, uint32_t level) { digitalWrite((uint8_t)pin, (uint8_t)level); return ESP_OK; }
esp_err_t gpio_set_direction(int, int) { return ESP_OK; }
esp_err_t gpio_reset_pin(int) { return ESP_OK; }
esp_err_t rtc_gpio_init(int) { return ESP_OK; }
esp_err_t rtc_gpio_set_direction(int, int) { return ESP_OK; }
esp_err_t rtc_gpio_pullup_en(int) { return ESP_OK; }
esp_err_t rtc_gpio_pulldown_dis(int) { return ESP_OK; }

// ── CPU frequency ─────────────────────────────────────────────────────────
static uint32_t gCpuMhz = 240;
bool setCpuFrequencyMhz(uint32_t mhz) { gCpuMhz = mhz; return true; }
uint32_t getCpuFrequencyMhz(void) { return gCpuMhz; }
uint32_t getXtalFrequencyMhz(void) { return 40; }
uint32_t getApbFrequency(void) { return 80000000; }
}

// ── esp_timer ─────────────────────────────────────────────────────────────
extern "C" {
struct esp_timer : HostTimer {};
esp_err_t esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* out) {
  esp_timer* t = new esp_timer();
  t->cb = args->callback;
  t->arg = args->arg;
  gTimers.push_back(t);
  *out = t;
  return ESP_OK;
}
esp_err_t esp_timer_start_periodic(esp_timer_handle_t t, uint64_t period_us) {
  t->active = true;
  t->periodic = true;
  t->periodUs = period_us;
  t->nextUs = gClockUs + period_us;
  return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t t, uint64_t timeout_us) {
  t->active = true;
  t->periodic = false;
  t->nextUs = gClockUs + timeout_us;
  return ESP_OK;
}
esp_err_t esp_timer_stop(esp_timer_handle_t t) { t->active = false; return ESP_OK; }
esp_err_t esp_timer_delete(esp_timer_handle_t t) {
  t->active = false;
  for (size_t i = 0; i < gTimers.size(); i++) {
    if (gTimers[i] == t) { gTimers.erase(gTimers.begin() + i); break; }
  }
  delete t;
  return ESP_OK;
}
int64_t esp_timer_get_time(void) { return (int64_t)host_clock_us(); }
}

// ── Serial ────────────────────────────────────────────────────────────────

HardwareSerial Serial;
size_t HardwareSerial::write(uint8_t c) { return fwrite(&c, 1, 1, stdout); }
size_t HardwareSerial::write(const uint8_t* buffer, size_t size) { return fwrite(buffer, 1, size, stdout); }
namespace {
bool gStdinSerial = false;
std::vector<uint8_t> gRx;
size_t gRxAt = 0;

// Pull whatever stdin has right now; never blocks.
void fillRx() {
  if (!gStdinSerial || gRxAt < gRx.size()) return;
  gRx.clear();
  gRxAt = 0;
  uint8_t buf[256];
  const ssize_t n = ::read(0, buf, sizeof buf);
  if (n > 0) gRx.assign(buf, buf + n);
}
}  // namespace

extern "C" void host_serial_stdin(int on) {
  gStdinSerial = on != 0;
  if (gStdinSerial) fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
}

int HardwareSerial::available() { fillRx(); return (int)(gRx.size() - gRxAt); }
int HardwareSerial::read() { fillRx(); return gRxAt < gRx.size() ? gRx[gRxAt++] : -1; }
int HardwareSerial::peek() { fillRx(); return gRxAt < gRx.size() ? gRx[gRxAt] : -1; }
void HardwareSerial::flush() { fflush(stdout); }

// ── ESP object ────────────────────────────────────────────────────────────

EspClass ESP;
void EspClass::restart() { host_exit(HOST_EXIT_RESTART); }
uint32_t EspClass::getFreeHeap() { return 200 * 1024; }
uint32_t EspClass::getHeapSize() { return 320 * 1024; }
uint32_t EspClass::getFreePsram() { return 7u * 1024 * 1024; }
uint32_t EspClass::getPsramSize() { return 8u * 1024 * 1024; }
uint32_t EspClass::getMinFreeHeap() { return 150 * 1024; }
uint32_t EspClass::getMaxAllocHeap() { return 100 * 1024; }
uint64_t EspClass::getEfuseMac() { return 0x0000A0B1C2D3E4F5ull; }

// ── Inert buses and radios ────────────────────────────────────────────────

TwoWire Wire;
SPIClass SPI;
WiFiClass WiFi;
