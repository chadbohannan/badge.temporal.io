#pragma once
#include "driver/gpio.h"
typedef enum { RTC_GPIO_MODE_INPUT_ONLY, RTC_GPIO_MODE_OUTPUT_ONLY, RTC_GPIO_MODE_INPUT_OUTPUT, RTC_GPIO_MODE_DISABLED } rtc_gpio_mode_t;
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t rtc_gpio_init(gpio_num_t pin);
esp_err_t rtc_gpio_set_direction(gpio_num_t pin, rtc_gpio_mode_t mode);
esp_err_t rtc_gpio_pullup_en(gpio_num_t pin);
esp_err_t rtc_gpio_pulldown_dis(gpio_num_t pin);
#ifdef __cplusplus
}
#endif
