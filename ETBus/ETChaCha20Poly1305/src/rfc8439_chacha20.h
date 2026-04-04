#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void rfc8439_chacha20_xor(
  const uint8_t key[32],
  const uint8_t nonce[12],
  uint32_t counter,
  const uint8_t* in,
  uint8_t* out,
  size_t len
);

void rfc8439_chacha20_block(
  const uint8_t key[32],
  const uint8_t nonce[12],
  uint32_t counter,
  uint8_t out64[64]
);

#ifdef __cplusplus
}
#endif
