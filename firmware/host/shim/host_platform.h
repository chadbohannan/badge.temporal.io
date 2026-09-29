#pragma once
// Harness-facing side of the platform shim: the virtual clock, the yield
// point, and the virtual pin model. The firmware never includes this header;
// the harness (host/harness/) and the shim itself do.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Virtual clock ─────────────────────────────────────────────────────────
// One clock in microseconds. millis()/micros() read it, and every read
// advances it by 1 us, so a loop that spins on the clock still ends in
// bounded virtual time. delay() makes the large jumps.
uint64_t host_clock_us(void);
void host_clock_advance_us(uint64_t us);
// Clock without the per-read tick. For the harness's own bookkeeping.
uint64_t host_clock_peek_us(void);

// ── Yield point ───────────────────────────────────────────────────────────
// delay(), vTaskDelay() and yield() call the hook after advancing the clock.
// The harness applies due input and presents frames from it.
typedef void (*host_yield_fn)(void);
void host_set_yield_hook(host_yield_fn fn);
// Live mode sleeps for the requested time inside delay(); scripted mode does not.
void host_set_realtime(int on);

// ── Virtual pins ──────────────────────────────────────────────────────────
#define HOST_PIN_COUNT 64
// Set a digital level and deliver the attached interrupt if the trigger
// matches. Runs only from the yield hook, so ISRs never run concurrently.
void host_pin_set(int pin, int level);
// Set the analog value (0..4095) that analogRead() returns for the pin.
void host_pin_set_analog(int pin, int value);
int host_pin_get(int pin);
int host_pin_get_analog(int pin);
int host_pin_mode(int pin);
int host_pin_has_isr(int pin);

// ── Key-value store ───────────────────────────────────────────────────────
// Preferences, nvs_* and nvs_flash_* share one store. Ephemeral until this is
// called; with a path it loads the file and writes it back on every commit.
void host_kv_load(const char* path);

// ── Serial input ──────────────────────────────────────────────────────────
// Off by default. When on, Serial.read()/available() take bytes from stdin without
// blocking, which is how a person or a script types into the MicroPython REPL.
void host_serial_stdin(int on);

// ── Process control ───────────────────────────────────────────────────────
// esp_restart() and esp_deep_sleep_start() call this. The harness exits with a
// distinct status so a script can check that a reboot happened.
typedef void (*host_exit_fn)(int code);
#define HOST_EXIT_RESTART 42
#define HOST_EXIT_DEEP_SLEEP 43
void host_set_exit_hook(host_exit_fn fn);
void host_exit(int code);

#ifdef __cplusplus
}
#endif
