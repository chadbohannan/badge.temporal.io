#pragma once
#include "esp_err.h"
#include "esp_log.h"
#define ESP_RETURN_ON_ERROR(x, tag, fmt, ...) do { esp_err_t r_ = (x); if (r_ != ESP_OK) return r_; } while (0)
#define ESP_GOTO_ON_ERROR(x, label, tag, fmt, ...) do { if ((x) != ESP_OK) goto label; } while (0)
#define ESP_RETURN_ON_FALSE(c, err, tag, fmt, ...) do { if (!(c)) return err; } while (0)
