#pragma once
// Host heap_caps: plain malloc. The Python heap budget (Phase 2) sets the real
// limit, so heap_caps_get_largest_free_block() reports "no limit".
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "multi_heap.h"
#include "esp_system.h"

#define MALLOC_CAP_EXEC (1 << 0)
#define MALLOC_CAP_32BIT (1 << 1)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)
#define MALLOC_CAP_IRAM_8BIT (1 << 13)
#define MALLOC_CAP_RETENTION (1 << 14)
#define MALLOC_CAP_RTCRAM (1 << 15)

#ifdef __cplusplus
extern "C" {
#endif
static inline void* heap_caps_malloc(size_t n, uint32_t caps) { (void)caps; return malloc(n); }
static inline void* heap_caps_calloc(size_t c, size_t n, uint32_t caps) { (void)caps; return calloc(c, n); }
static inline void* heap_caps_realloc(void* p, size_t n, uint32_t caps) { (void)caps; return realloc(p, n); }
static inline void heap_caps_free(void* p) { free(p); }
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_total_size(uint32_t caps);
size_t heap_caps_get_minimum_free_size(uint32_t caps);
size_t heap_caps_get_largest_free_block(uint32_t caps);
void heap_caps_get_info(multi_heap_info_t* info, uint32_t caps);
void heap_caps_print_heap_info(uint32_t caps);
void heap_caps_malloc_extmem_enable(size_t limit);
// Everything malloc returns counts as PSRAM, as ps_malloc() is malloc here.
static inline bool esp_ptr_external_ram(const void* p) { (void)p; return true; }
#ifdef __cplusplus
}
#endif
