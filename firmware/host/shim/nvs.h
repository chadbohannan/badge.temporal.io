#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
typedef enum { NVS_TYPE_U8 = 0x01, NVS_TYPE_I8 = 0x11, NVS_TYPE_U16 = 0x02, NVS_TYPE_I16 = 0x12,
               NVS_TYPE_U32 = 0x04, NVS_TYPE_I32 = 0x14, NVS_TYPE_U64 = 0x08, NVS_TYPE_I64 = 0x18,
               NVS_TYPE_STR = 0x21, NVS_TYPE_BLOB = 0x42, NVS_TYPE_ANY = 0xff } nvs_type_t;
typedef struct { char namespace_name[16]; char key[16]; nvs_type_t type; } nvs_entry_info_t;
typedef struct nvs_opaque_iterator_t* nvs_iterator_t;
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t nvs_open(const char* name, nvs_open_mode_t mode, nvs_handle_t* out);
void nvs_close(nvs_handle_t h);
esp_err_t nvs_commit(nvs_handle_t h);
esp_err_t nvs_erase_key(nvs_handle_t h, const char* key);
esp_err_t nvs_erase_all(nvs_handle_t h);
esp_err_t nvs_set_i16(nvs_handle_t h, const char* key, int16_t v);
esp_err_t nvs_get_i16(nvs_handle_t h, const char* key, int16_t* v);
esp_err_t nvs_set_u8(nvs_handle_t h, const char* key, uint8_t v);
esp_err_t nvs_get_u8(nvs_handle_t h, const char* key, uint8_t* v);
esp_err_t nvs_set_u32(nvs_handle_t h, const char* key, uint32_t v);
esp_err_t nvs_get_u32(nvs_handle_t h, const char* key, uint32_t* v);
esp_err_t nvs_set_blob(nvs_handle_t h, const char* key, const void* v, size_t len);
esp_err_t nvs_get_blob(nvs_handle_t h, const char* key, void* v, size_t* len);
esp_err_t nvs_set_str(nvs_handle_t h, const char* key, const char* v);
esp_err_t nvs_get_str(nvs_handle_t h, const char* key, char* v, size_t* len);
esp_err_t nvs_entry_find(const char* part, const char* ns, nvs_type_t type, nvs_iterator_t* out);
esp_err_t nvs_entry_next(nvs_iterator_t* it);
esp_err_t nvs_entry_info(const nvs_iterator_t it, nvs_entry_info_t* info);
void nvs_release_iterator(nvs_iterator_t it);
#ifdef __cplusplus
}
#endif
