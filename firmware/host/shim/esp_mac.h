#pragma once
#include "esp_err.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Returns a fixed host MAC so the badge UID is stable across runs.
esp_err_t esp_efuse_mac_get_default(uint8_t* mac);
#ifdef __cplusplus
}
#endif
