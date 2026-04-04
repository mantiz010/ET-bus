#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t r[5];
  uint32_t h[5];
  uint32_t pad[4];
  uint8_t  buf[16];
  size_t   buf_used;
} poly1305_context;

void poly1305_init(poly1305_context* ctx, const uint8_t key[32]);
void poly1305_update(poly1305_context* ctx, const uint8_t* m, size_t bytes);
void poly1305_finish(poly1305_context* ctx, uint8_t mac[16]);

// one-shot helpers
void poly1305_auth(uint8_t mac[16], const uint8_t* m, size_t bytes, const uint8_t key[32]);
int  poly1305_verify(const uint8_t mac1[16], const uint8_t mac2[16]);

#ifdef __cplusplus
}
#endif
