#pragma once
#include <stdint.h>
#include <stddef.h>

class ETChaCha20Poly1305 {
public:
  // Encrypt:
  //   key[32], nonce[12]
  //   aad may be nullptr if aad_len==0
  //   pt -> ct, tag[16]
  static bool encrypt(
    const uint8_t key[32],
    const uint8_t nonce[12],
    const uint8_t* aad, size_t aad_len,
    const uint8_t* pt,  size_t pt_len,
    uint8_t* ct_out,
    uint8_t tag_out[16]
  );

  // Decrypt:
  //   key[32], nonce[12]
  //   aad may be nullptr if aad_len==0
  //   ct + tag -> pt
  static bool decrypt(
    const uint8_t key[32],
    const uint8_t nonce[12],
    const uint8_t* aad, size_t aad_len,
    const uint8_t* ct,  size_t ct_len,
    const uint8_t tag[16],
    uint8_t* pt_out
  );

private:
  static void _store64_le(uint8_t out[8], uint64_t v);
  static void _poly1305_mac(
    uint8_t out_tag[16],
    const uint8_t poly_key[32],
    const uint8_t* aad, size_t aad_len,
    const uint8_t* ct,  size_t ct_len
  );
};
