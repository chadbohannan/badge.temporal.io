#pragma once
// Host shim for the Arduino core. Only what the badge firmware calls.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <inttypes.h>
#include <algorithm>
#include <string>
#include <type_traits>

#include "host_platform.h"
// The real Arduino.h pulls FreeRTOS, esp_err and the log macros in through
// esp32-hal.h; firmware files rely on that.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "WString.h"
#include "Print.h"
#include "Stream.h"
#include "HardwareSerial.h"
#include "esp32-hal-gpio.h"
#include "esp32-hal-cpu.h"

using std::max;
using std::min;

typedef uint8_t byte;
typedef bool boolean;
typedef unsigned int uint;

#ifndef PROGMEM
#define PROGMEM
#endif
#define PGM_P const char*
#define PSTR(s) (s)
#define pgm_read_byte(addr) (*(const unsigned char*)(addr))
#define pgm_read_word(addr) (*(const unsigned short*)(addr))
#define pgm_read_dword(addr) (*(const unsigned int*)(addr))
#define IRAM_ATTR
#define DRAM_ATTR
#define RTC_DATA_ATTR
#define RTC_NOINIT_ATTR
#define EXT_RAM_ATTR
#define EXT_RAM_BSS_ATTR
#define WORD_ALIGNED_ATTR
#define __NOINIT_ATTR
#define SET_LOOP_TASK_STACK_SIZE(sz)

// The firmware allocates large buffers with ps_malloc(); on the host that is malloc.
#define ps_malloc(n) malloc(n)
#define ps_calloc(n, s) calloc(n, s)
#define ps_realloc(p, n) realloc(p, n)

#ifndef PI
#define PI 3.1415926535897932384626433832795
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#endif
#define radians(deg) ((deg) * DEG_TO_RAD)
#define degrees(rad) ((rad) * RAD_TO_DEG)
#define sq(x) ((x) * (x))
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define lowByte(w) ((uint8_t)((w) & 0xff))
#define highByte(w) ((uint8_t)((w) >> 8))
#define _BV(bit) (1UL << (bit))

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

static inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// ── Time ──────────────────────────────────────────────────────────────────
unsigned long millis(void);
unsigned long micros(void);
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
void yield(void);

// ── Randomness ────────────────────────────────────────────────────────────
long random(long max);
long random(long min, long max);
void randomSeed(unsigned long seed);

// ── ESP object ────────────────────────────────────────────────────────────
class EspClass {
 public:
  void restart();
  uint32_t getFreeHeap();
  uint32_t getHeapSize();
  uint32_t getFreePsram();
  uint32_t getPsramSize();
  uint32_t getMinFreeHeap();
  uint32_t getMaxAllocHeap();
  uint32_t getCpuFreqMHz() { return 240; }
  const char* getSdkVersion() { return "host"; }
  const char* getChipModel() { return "host"; }
  uint32_t getFlashChipSize() { return 16u * 1024u * 1024u; }
  uint64_t getEfuseMac();
};
extern EspClass ESP;
