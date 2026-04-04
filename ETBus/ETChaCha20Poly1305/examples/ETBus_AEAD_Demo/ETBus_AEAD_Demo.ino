#include <ETChaCha20Poly1305.h>

static const uint8_t KEY[32] = {
  0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
  0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,
  0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,
  0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F
};

static uint32_t msg_counter = 1;

static void make_nonce(uint8_t nonce[12]) {
  uint32_t device_id = 0xAABBCCDD;
  uint32_t boot_id   = 0x11223344;
  uint32_t ctr       = msg_counter++;

  // little-endian fields
  memcpy(nonce + 0, &device_id, 4);
  memcpy(nonce + 4, &boot_id,   4);
  memcpy(nonce + 8, &ctr,       4);
}

static void print_hex(const char* label, const uint8_t* b, size_t n) {
  Serial.print(label);
  for (size_t i = 0; i < n; i++) {
    Serial.printf("%02X", b[i]);
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);

  const char* aad_str = "ETBUS:v1|dev=relay1|type=cmd";
  const char* pt_str  = "{\"on\":true,\"ts\":123456}";

  const size_t aad_len = strlen(aad_str);
  const size_t pt_len  = strlen(pt_str);

  uint8_t nonce[12];
  make_nonce(nonce);

  uint8_t ct[128] = {0};
  uint8_t tag[16] = {0};

  bool enc_ok = ETChaCha20Poly1305::encrypt(
    KEY, nonce,
    (const uint8_t*)aad_str, aad_len,
    (const uint8_t*)pt_str,  pt_len,
    ct,
    tag
  );

  Serial.println(enc_ok ? "ENCRYPT OK" : "ENCRYPT FAIL");
  print_hex("NONCE: ", nonce, 12);
  print_hex("CT:    ", ct, pt_len);
  print_hex("TAG:   ", tag, 16);

  uint8_t out[128] = {0};
  bool dec_ok = ETChaCha20Poly1305::decrypt(
    KEY, nonce,
    (const uint8_t*)aad_str, aad_len,
    ct, pt_len,
    tag,
    out
  );

  Serial.println(dec_ok ? "DECRYPT OK" : "DECRYPT FAIL");
  if (dec_ok) {
    out[pt_len] = 0;
    Serial.print("PT: ");
    Serial.println((char*)out);
  }
}

void loop() {}
