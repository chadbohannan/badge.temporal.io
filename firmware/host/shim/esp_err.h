#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
typedef int32_t esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NVS_BASE 0x1100
#define ESP_ERR_NVS_NOT_FOUND (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_NO_FREE_PAGES (ESP_ERR_NVS_BASE + 0x0d)
#define ESP_ERR_NVS_NEW_VERSION_FOUND (ESP_ERR_NVS_BASE + 0x10)
#ifdef __cplusplus
extern "C" {
#endif
const char* esp_err_to_name(esp_err_t code);
#ifdef __cplusplus
}
#endif
#define ESP_ERROR_CHECK(x) do { esp_err_t rc_ = (x); if (rc_ != ESP_OK) { fprintf(stderr, "ESP_ERROR_CHECK failed: %s (%d) at %s:%d\n", esp_err_to_name(rc_), (int)rc_, __FILE__, __LINE__); abort(); } } while (0)
#define ESP_ERROR_CHECK_WITHOUT_ABORT(x) ((x))
