#include "rfc8439_chacha20.h"

static uint32_t rotl32(uint32_t x, int n) {
  return (x << n) | (x >> (32 - n));
}

static uint32_t load32_le(const uint8_t* p) {
  return ((uint32_t)p[0]) |
         ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static void store32_le(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v);
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

#define QR(a,b,c,d)        \
  do {                     \
    a += b; d ^= a; d = rotl32(d,16); \
    c += d; b ^= c; b = rotl32(b,12); \
    a += b; d ^= a; d = rotl32(d, 8); \
    c += d; b ^= c; b = rotl32(b, 7); \
  } while (0)

void rfc8439_chacha20_block(
  const uint8_t key[32],
  const uint8_t nonce[12],
  uint32_t counter,
  uint8_t out64[64]
) {
  const uint32_t c0 = 0x61707865;
  const uint32_t c1 = 0x3320646e;
  const uint32_t c2 = 0x79622d32;
  const uint32_t c3 = 0x6b206574;

  uint32_t x[16];
  uint32_t s[16];

  s[0]  = c0; s[1]  = c1; s[2]  = c2; s[3]  = c3;
  s[4]  = load32_le(key + 0);
  s[5]  = load32_le(key + 4);
  s[6]  = load32_le(key + 8);
  s[7]  = load32_le(key + 12);
  s[8]  = load32_le(key + 16);
  s[9]  = load32_le(key + 20);
  s[10] = load32_le(key + 24);
  s[11] = load32_le(key + 28);

  s[12] = counter;
  s[13] = load32_le(nonce + 0);
  s[14] = load32_le(nonce + 4);
  s[15] = load32_le(nonce + 8);

  for (int i = 0; i < 16; i++) x[i] = s[i];

  for (int i = 0; i < 10; i++) {
    QR(x[0], x[4], x[8],  x[12]);
    QR(x[1], x[5], x[9],  x[13]);
    QR(x[2], x[6], x[10], x[14]);
    QR(x[3], x[7], x[11], x[15]);

    QR(x[0], x[5], x[10], x[15]);
    QR(x[1], x[6], x[11], x[12]);
    QR(x[2], x[7], x[8],  x[13]);
    QR(x[3], x[4], x[9],  x[14]);
  }

  for (int i = 0; i < 16; i++) x[i] += s[i];

  for (int i = 0; i < 16; i++) {
    store32_le(out64 + (4 * i), x[i]);
  }
}

void rfc8439_chacha20_xor(
  const uint8_t key[32],
  const uint8_t nonce[12],
  uint32_t counter,
  const uint8_t* in,
  uint8_t* out,
  size_t len
) {
  uint8_t block[64];
  size_t off = 0;

  while (off < len) {
    rfc8439_chacha20_block(key, nonce, counter, block);
    counter++;

    size_t n = (len - off > 64) ? 64 : (len - off);
    for (size_t i = 0; i < n; i++) {
      out[off + i] = in[off + i] ^ block[i];
    }
    off += n;
  }
}
