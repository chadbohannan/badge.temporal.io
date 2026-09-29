#pragma once
// The one partition the firmware reads and writes through wear-levelling: the
// FAT volume. host/shim/flash_shim.cpp backs it with a RAM image.
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

typedef enum { ESP_PARTITION_TYPE_APP = 0, ESP_PARTITION_TYPE_DATA = 1 } esp_partition_type_t;
typedef enum { ESP_PARTITION_SUBTYPE_DATA_FAT = 0x81 } esp_partition_subtype_t;
typedef struct {
  uint32_t address;
  uint32_t size;
  char label[17];
} esp_partition_t;

#ifdef __cplusplus
extern "C" {
#endif
const esp_partition_t* esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype,
                                                const char* label);
#ifdef __cplusplus
}
#endif
