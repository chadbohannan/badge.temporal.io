#pragma once
#include "esp_err.h"
typedef enum { ESP_BT_MODE_IDLE, ESP_BT_MODE_BLE, ESP_BT_MODE_CLASSIC_BT, ESP_BT_MODE_BTDM } esp_bt_mode_t;
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t esp_bt_mem_release(esp_bt_mode_t mode);
#ifdef __cplusplus
}
#endif
