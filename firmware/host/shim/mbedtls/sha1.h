#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int mbedtls_sha1(const unsigned char* input, size_t ilen, unsigned char output[20]);
int mbedtls_sha1_ret(const unsigned char* input, size_t ilen, unsigned char output[20]);
#ifdef __cplusplus
}
#endif
