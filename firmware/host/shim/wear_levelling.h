#pragma once
#include "esp_partition.h"
typedef int32_t wl_handle_t;
#define WL_INVALID_HANDLE (-1)
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t wl_mount(const esp_partition_t* partition, wl_handle_t* out);
esp_err_t wl_unmount(wl_handle_t handle);
esp_err_t wl_erase_range(wl_handle_t handle, size_t start, size_t size);
esp_err_t wl_write(wl_handle_t handle, size_t dest, const void* src, size_t size);
esp_err_t wl_read(wl_handle_t handle, size_t src, void* dest, size_t size);
size_t wl_size(wl_handle_t handle);
size_t wl_sector_size(wl_handle_t handle);
// Harness side: keep the image between runs.
void host_flash_load(const char* path);
void host_flash_save(const char* path);
#ifdef __cplusplus
}
#endif
