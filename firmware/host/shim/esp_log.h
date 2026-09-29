#pragma once
#include <stdio.h>
typedef enum { ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG, ESP_LOG_VERBOSE } esp_log_level_t;
#ifdef __cplusplus
extern "C" {
#endif
void esp_log_level_set(const char* tag, esp_log_level_t level);
#ifdef __cplusplus
}
#endif
// ESP_LOG* go to stdout, like Serial.
#define ESP_LOGE(tag, fmt, ...) printf("E (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("W (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) printf("I (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_LOGV(tag, fmt, ...) ((void)0)
#define log_e(fmt, ...) printf("E " fmt "\n", ##__VA_ARGS__)
#define log_w(fmt, ...) printf("W " fmt "\n", ##__VA_ARGS__)
#define log_i(fmt, ...) printf("I " fmt "\n", ##__VA_ARGS__)
#define log_d(fmt, ...) ((void)0)
#define log_v(fmt, ...) ((void)0)
