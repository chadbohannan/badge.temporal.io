#pragma once
#include "esp_err.h"
#include <stdint.h>
typedef enum {
  ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_ALL, ESP_SLEEP_WAKEUP_EXT0, ESP_SLEEP_WAKEUP_EXT1,
  ESP_SLEEP_WAKEUP_TIMER, ESP_SLEEP_WAKEUP_TOUCHPAD, ESP_SLEEP_WAKEUP_ULP, ESP_SLEEP_WAKEUP_GPIO,
  ESP_SLEEP_WAKEUP_UART
} esp_sleep_wakeup_cause_t;
typedef enum { ESP_EXT1_WAKEUP_ALL_LOW, ESP_EXT1_WAKEUP_ANY_LOW, ESP_EXT1_WAKEUP_ANY_HIGH } esp_sleep_ext1_wakeup_mode_t;
typedef enum { ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_DOMAIN_XTAL, ESP_PD_DOMAIN_MAX } esp_sleep_pd_domain_t;
typedef enum { ESP_PD_OPTION_OFF, ESP_PD_OPTION_ON, ESP_PD_OPTION_AUTO } esp_sleep_pd_option_t;
#ifdef __cplusplus
extern "C" {
#endif
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void);
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t source);
esp_err_t esp_sleep_pd_config(esp_sleep_pd_domain_t domain, esp_sleep_pd_option_t option);
esp_err_t esp_sleep_enable_ext1_wakeup(uint64_t mask, esp_sleep_ext1_wakeup_mode_t mode);
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t time_in_us);
esp_err_t esp_light_sleep_start(void);
void esp_deep_sleep_start(void);
#ifdef __cplusplus
}
#endif
