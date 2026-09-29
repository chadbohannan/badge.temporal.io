#pragma once
#include "esp_err.h"
typedef int gpio_num_t;
#define GPIO_NUM_NC (-1)
typedef enum { GPIO_MODE_DISABLE, GPIO_MODE_INPUT, GPIO_MODE_OUTPUT, GPIO_MODE_INPUT_OUTPUT = 3 } gpio_mode_t;
#ifdef __cplusplus
extern "C" {
#endif
int gpio_get_level(gpio_num_t pin);
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level);
esp_err_t gpio_set_direction(gpio_num_t pin, gpio_mode_t mode);
esp_err_t gpio_reset_pin(gpio_num_t pin);
#ifdef __cplusplus
}
#endif
