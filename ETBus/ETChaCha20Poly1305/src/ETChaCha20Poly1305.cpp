#include "ETChaCha20Poly1305.h"

extern "C" {
  #include "rfc8439_chacha20.h"
  #include "poly1305.h"
}

static void _pad16(poly1305_context* ctx, size_t n) {
  size_t rem = n & 15;
  if (rem == 0) return;
  uint8_t z[16];
  for (int i=0;i<16;i++) z[i] = 0;
  poly1305_update(ctx, z, 16 - rem);
}

void ETChaCha20Poly1305::_store64_le(uint8_t out[8], uint64_t v) {
  out[0] = (uint8_t)(v);
  out[1] = (uint8_t)(v >> 8);
  out[2] = (uint8_t)(v >> 16);
  out[3] = (uint8_t)(v >> 24);
  out[4] = (uint8_t)(v >> 32);
  out[5] = (uint8_t)(v >> 40);
  out[6] = (uint8_t)(v >> 48);
  out[7] = (uint8_t)(v >> 56);
}

void ETChaCha20Poly1305::_poly1305_mac(
  uint8_t out_tag[16],
  const uint8_t poly_key[32],
  const uint8_t* aad, size_t aad_len,
  const uint8_t* ct,  size_t ct_len
) {
  poly1305_context ctx;
  poly1305_init(&ctx, poly_key);

  // RFC8439: MAC = Poly1305(AAD || pad16 || CT || pad16 || le64(aad_len) || le64(ct_len))
  if (aad_len && aad) poly1305_update(&ctx, aad, aad_len);
  _pad16(&ctx, aad_len);

  if (ct_len && ct) poly1305_update(&ctx, ct, ct_len);
  _pad16(&ctx, ct_len);

  uint8_t lens[16];
  _store64_le(&lens[0],  (uint64_t)aad_len);
  _store64_le(&lens[8],  (uint64_t)ct_len);
  poly1305_update(&ctx, lens, 16);

  poly1305_finish(&ctx, out_tag);
}

bool ETChaCha20Poly1305::encrypt(
  const uint8_t key[32],
  const uint8_t nonce[12],
  const uint8_t* aad, size_t aad_len,
  const uint8_t* pt,  size_t pt_len,
  uint8_t* ct_out,
  uint8_t tag_out[16]
) {
  if (!key || !nonce || (!pt && pt_len) || (!ct_out && pt_len) || !tag_out) return false;

  // 1) poly1305 one-time key = ChaCha20 block(key, nonce, counter=0)[0..31]
  uint8_t block0[64];
  rfc8439_chacha20_block(key, nonce, 0, block0);

  uint8_t poly_key[32];
  for (int i=0;i<32;i++) poly_key[i] = block0[i];

  // 2) Encrypt plaintext using ChaCha20 stream starting counter=1
  if (pt_len) {
    rfc8439_chacha20_xor(key, nonce, 1, pt, ct_out, pt_len);
  }

  // 3) Tag = Poly1305 over (aad, ct) with poly_key
  _poly1305_mac(tag_out, poly_key, aad, aad_len, ct_out, pt_len);

  // wipe sensitive temp
  for (int i=0;i<64;i++) block0[i] = 0;
  for (int i=0;i<32;i++) poly_key[i] = 0;

  return true;
}

bool ETChaCha20Poly1305::decrypt(
  const uint8_t key[32],
  const uint8_t nonce[12],
  const uint8_t* aad, size_t aad_len,
  const uint8_t* ct,  size_t ct_len,
  const uint8_t tag[16],
  uint8_t* pt_out
) {
  if (!key || !nonce || (!ct && ct_len) || (!pt_out && ct_len) || !tag) return false;

  // 1) poly1305 one-time key = ChaCha20 block(key, nonce, counter=0)[0..31]
  uint8_t block0[64];
  rfc8439_chacha20_block(key, nonce, 0, block0);

  uint8_t poly_key[32];
  for (int i=0;i<32;i++) poly_key[i] = block0[i];

  // 2) Compute expected tag
  uint8_t expect[16];
  _poly1305_mac(expect, poly_key, aad, aad_len, ct, ct_len);

  // wipe temp
  for (int i=0;i<64;i++) block0[i] = 0;
  for (int i=0;i<32;i++) poly_key[i] = 0;

  // 3) Constant-time verify
  if (poly1305_verify(expect, tag) != 0) {
    // wipe
    for (int i=0;i<16;i++) expect[i] = 0;
    return false;
  }

  // 4) Decrypt using ChaCha20 stream starting counter=1
  if (ct_len) {
    rfc8439_chacha20_xor(key, nonce, 1, ct, pt_out, ct_len);
  }

  // wipe
  for (int i=0;i<16;i++) expect[i] = 0;
  return true;
}
