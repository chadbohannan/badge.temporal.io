#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct {
  uint32_t state[8];
  uint64_t total;
  uint8_t buffer[64];
  size_t buflen;
} mbedtls_sha256_context;
#ifdef __cplusplus
extern "C" {
#endif
void mbedtls_sha256_init(mbedtls_sha256_context* ctx);
void mbedtls_sha256_free(mbedtls_sha256_context* ctx);
int mbedtls_sha256_starts(mbedtls_sha256_context* ctx, int is224);
int mbedtls_sha256_update(mbedtls_sha256_context* ctx, const unsigned char* input, size_t ilen);
int mbedtls_sha256_finish(mbedtls_sha256_context* ctx, unsigned char output[32]);
#ifdef __cplusplus
}
#endif
