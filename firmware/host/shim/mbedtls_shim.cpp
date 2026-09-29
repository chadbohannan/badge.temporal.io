// SHA-1 and SHA-256 for BadgeUID and AssetRegistry. Small self-contained
// implementations so the host build does not need a system mbedtls.

#include "mbedtls/sha1.h"
#include "mbedtls/sha256.h"
#include <string.h>
#include <stdlib.h>

namespace {
inline uint32_t rol(uint32_t v, int s) { return (v << s) | (v >> (32 - s)); }
inline uint32_t ror(uint32_t v, int s) { return (v >> s) | (v << (32 - s)); }
}  // namespace

extern "C" {

int mbedtls_sha1(const unsigned char* input, size_t ilen, unsigned char out[20]) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  uint64_t bits = (uint64_t)ilen * 8;
  size_t total = ((ilen + 8) / 64 + 1) * 64;
  unsigned char* msg = (unsigned char*)calloc(total, 1);
  memcpy(msg, input, ilen);
  msg[ilen] = 0x80;
  for (int i = 0; i < 8; i++) msg[total - 1 - i] = (unsigned char)(bits >> (8 * i));
  for (size_t off = 0; off < total; off += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
      w[i] = (uint32_t)msg[off + 4 * i] << 24 | (uint32_t)msg[off + 4 * i + 1] << 16 |
             (uint32_t)msg[off + 4 * i + 2] << 8 | msg[off + 4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  free(msg);
  for (int i = 0; i < 5; i++)
    for (int j = 0; j < 4; j++) out[4 * i + j] = (unsigned char)(h[i] >> (24 - 8 * j));
  return 0;
}
int mbedtls_sha1_ret(const unsigned char* input, size_t ilen, unsigned char out[20]) {
  return mbedtls_sha1(input, ilen, out);
}

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static void sha256Block(mbedtls_sha256_context* c, const uint8_t* p) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++)
    w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = c->state[0], b = c->state[1], cc = c->state[2], d = c->state[3];
  uint32_t e = c->state[4], f = c->state[5], g = c->state[6], h = c->state[7];
  for (int i = 0; i < 64; i++) {
    uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = h + S1 + ch + K256[i] + w[i];
    uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
    uint32_t mj = (a & b) ^ (a & cc) ^ (b & cc);
    uint32_t t2 = S0 + mj;
    h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
  }
  c->state[0] += a; c->state[1] += b; c->state[2] += cc; c->state[3] += d;
  c->state[4] += e; c->state[5] += f; c->state[6] += g; c->state[7] += h;
}

void mbedtls_sha256_init(mbedtls_sha256_context* c) { memset(c, 0, sizeof *c); }
void mbedtls_sha256_free(mbedtls_sha256_context* c) { memset(c, 0, sizeof *c); }
int mbedtls_sha256_starts(mbedtls_sha256_context* c, int) {
  static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  memcpy(c->state, init, sizeof init);
  c->total = 0;
  c->buflen = 0;
  return 0;
}
int mbedtls_sha256_update(mbedtls_sha256_context* c, const unsigned char* in, size_t n) {
  c->total += n;
  while (n) {
    size_t take = 64 - c->buflen < n ? 64 - c->buflen : n;
    memcpy(c->buffer + c->buflen, in, take);
    c->buflen += take; in += take; n -= take;
    if (c->buflen == 64) { sha256Block(c, c->buffer); c->buflen = 0; }
  }
  return 0;
}
int mbedtls_sha256_finish(mbedtls_sha256_context* c, unsigned char out[32]) {
  uint64_t bits = c->total * 8;
  unsigned char pad = 0x80;
  mbedtls_sha256_update(c, &pad, 1);
  unsigned char zero = 0;
  while (c->buflen != 56) mbedtls_sha256_update(c, &zero, 1);
  unsigned char len[8];
  for (int i = 0; i < 8; i++) len[i] = (unsigned char)(bits >> (56 - 8 * i));
  mbedtls_sha256_update(c, len, 8);
  for (int i = 0; i < 8; i++)
    for (int j = 0; j < 4; j++) out[4 * i + j] = (unsigned char)(c->state[i] >> (24 - 8 * j));
  return 0;
}

}  // extern "C"
