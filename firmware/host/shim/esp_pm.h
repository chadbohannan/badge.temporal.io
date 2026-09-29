#pragma once
#include "esp_err.h"
#include <stdint.h>
typedef struct { int max_freq_mhz; int min_freq_mhz; bool light_sleep_enable; } esp_pm_config_t;
typedef struct esp_pm_lock* esp_pm_lock_handle_t;
typedef enum { ESP_PM_CPU_FREQ_MAX, ESP_PM_APB_FREQ_MAX, ESP_PM_NO_LIGHT_SLEEP } esp_pm_lock_type_t;
#ifdef __cplusplus
extern "C" {
#endif
// Reports ESP_ERR_NOT_SUPPORTED, as an IDF build without CONFIG_PM_ENABLE does.
esp_err_t esp_pm_configure(const void* config);
esp_err_t esp_pm_lock_create(esp_pm_lock_type_t t, int arg, const char* name, esp_pm_lock_handle_t* out);
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t h);
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t h);
#ifdef __cplusplus
}
#endif
