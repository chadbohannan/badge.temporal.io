#pragma once
// RMT types, for the pure NEC encoder/decoder headers that BadgeIR.h includes.
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
typedef struct {
  union {
    struct {
      uint32_t duration0 : 15;
      uint32_t level0 : 1;
      uint32_t duration1 : 15;
      uint32_t level1 : 1;
    };
    uint32_t val;
  };
} rmt_symbol_word_t;
typedef struct rmt_channel_t* rmt_channel_handle_t;
