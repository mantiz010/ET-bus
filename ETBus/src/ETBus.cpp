#include "ETBus.h"

#if defined(ARDUINO_ARCH_ESP32)
  #include "mbedtls/sha256.h"
  #include "mbedtls/base64.h"
#endif

static const uint32_t PONG_INTERVAL_MS = 10000;
static const uint32_t DISCOVER_INTERVAL_MS = 10000;

ETBus::ETBus() {
  _port = ETBUS_DEFAULT_PORT;
  _mcastIP = IPAddress(ETBUS_MCAST_A, ETBUS_MCAST_B, ETBUS_MCAST_C, ETBUS_MCAST_D);
  _hubKnown = false;
  _hubIP = IPAddress(0,0,0,0);
  _lastPongMs = 0;
  _lastDiscoverMs = 0;
  _seq = 0;

  _crypto_enabled = false;
  _kid = 1;
  for (int i=0;i<32;i++){ _psk[i]=0; _key[i]=0; }

  _tx_state_ctr = 0;
  _rx_cmd_last_ctr = 0;
  _makeBootId();
}

void ETBus::_makeBootId() {
  uint32_t a = (uint32_t)micros();
  uint32_t b = (uint32_t)millis();
#if defined(ARDUINO_ARCH_ESP32)
  b ^= (uint32_t)esp_random();
#endif
  static const char* H = "0123456789abcdef";
  for (int i = 0; i < 8; i++) {
    uint8_t v = (uint8_t)(a >> ((7 - i) * 4));
    _bootId[i] = H[v & 0x0F];
  }
  for (int i = 0; i < 8; i++) {
    uint8_t v = (uint8_t)(b >> ((7 - i) * 4));
    _bootId[i + 8] = H[v & 0x0F];
  }
  _bootId[16] = 0;
}

void ETBus::setWifiNoSleep(bool on) {
#if defined(ARDUINO_ARCH_ESP32)
  WiFi.setSleep(!on);
#else
  (void)on;
#endif
}

void ETBus::setPort(uint16_t port) {
  if (port == 0) return;
  _port = port;
}

void ETBus::_hexPrint(const uint8_t* b, size_t n) {
  static const char* H = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    uint8_t v = b[i];
    Serial.print(H[v >> 4]);
    Serial.print(H[v & 0x0F]);
  }
}

bool ETBus::_hex32_from_str(const char* s, uint8_t out32[32]) {
  if (!s) return false;

  char buf[65];
  int bi = 0;

  for (const char* p = s; *p && bi < 64; ++p) {
    char c = *p;
    bool ok =
      (c >= '0' && c <= '9') ||
      (c >= 'a' && c <= 'f') ||
      (c >= 'A' && c <= 'F');
    if (ok) {
      // tolower without <ctype.h>
      if (c >= 'A' && c <= 'F') c = (char)(c - 'A' + 'a');
      buf[bi++] = c;
    }
  }
  if (bi != 64) return false;
  buf[64] = 0;

  auto hexv = [](char c)->uint8_t {
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint8_t)(10 + (c - 'a'));
    return 0;
  };

  for (int i = 0; i < 32; i++) {
    out32[i] = (uint8_t)((hexv(buf[i*2]) << 4) | hexv(buf[i*2+1]));
  }
  return true;
}

void ETBus::_u64_le(uint8_t out8[8], uint64_t v) {
  out8[0] = (uint8_t)(v);
  out8[1] = (uint8_t)(v >> 8);
  out8[2] = (uint8_t)(v >> 16);
  out8[3] = (uint8_t)(v >> 24);
  out8[4] = (uint8_t)(v >> 32);
  out8[5] = (uint8_t)(v >> 40);
  out8[6] = (uint8_t)(v >> 48);
  out8[7] = (uint8_t)(v >> 56);
}

bool ETBus::_b64decode(const char* in_b64, uint8_t* out, size_t out_max, size_t& out_len) {
#if defined(ARDUINO_ARCH_ESP32)
  if (!in_b64) return false;
  size_t ilen = 0;
  while (in_b64[ilen]) ilen++;

  size_t req = 0;
  (void)mbedtls_base64_decode(nullptr, 0, &req, (const unsigned char*)in_b64, ilen);
  if (req > out_max) return false;

  int rc = mbedtls_base64_decode(out, out_max, &out_len, (const unsigned char*)in_b64, ilen);
  return rc == 0;
#else
  (void)in_b64; (void)out; (void)out_max; (void)out_len;
  return false;
#endif
}

bool ETBus::_b64encode(const uint8_t* in, size_t in_len, char* out_b64, size_t out_max) {
#if defined(ARDUINO_ARCH_ESP32)
  size_t out_len = 0;
  if (!out_b64 || out_max == 0) return false;
  int rc = mbedtls_base64_encode((unsigned char*)out_b64, out_max, &out_len, in, in_len);
  if (rc != 0) return false;
  if (out_len >= out_max) return false;
  out_b64[out_len] = 0;
  return true;
#else
  (void)in; (void)in_len; (void)out_b64; (void)out_max;
  return false;
#endif
}

void ETBus::_deriveKeyFromPskAndId() {
#if ETBUS_ENABLE_ENCRYPTION && defined(ARDUINO_ARCH_ESP32)
  if (!_id) return;

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, _psk, 32);

  // strlen without <string.h>
  size_t idlen = 0;
  while (_id[idlen]) idlen++;
  mbedtls_sha256_update(&ctx, (const unsigned char*)_id, idlen);

  mbedtls_sha256_finish(&ctx, _key);
  mbedtls_sha256_free(&ctx);

#if ETBUS_DEBUG_CRYPTO
  Serial.print("[ETBUS] derived key sha256(psk||id) id=");
  Serial.println(_id);
  Serial.print("[ETBUS] key0=");
  _hexPrint(_key, 4);
  Serial.println();
#endif
#endif
}

void ETBus::enableEncryption(const uint8_t master_secret_32[32]) {
#if ETBUS_ENABLE_ENCRYPTION
  for (int i=0;i<32;i++) _psk[i] = master_secret_32[i];
  _kid = 1;
  _crypto_enabled = true;
  _tx_state_ctr = 0;
  _rx_cmd_last_ctr = 0;

  _deriveKeyFromPskAndId();

#if ETBUS_DEBUG_CRYPTO
  Serial.println("[ETBUS] encryption ENABLED (kid=1, per-device key, AAD empty)");
#endif
#else
  (void)master_secret_32;
#endif
}

bool ETBus::enableEncryptionHex(const char* psk_hex_64) {
#if ETBUS_ENABLE_ENCRYPTION
  uint8_t ms[32];
  if (!_hex32_from_str(psk_hex_64, ms)) {
#if ETBUS_DEBUG_CRYPTO
    Serial.println("[ETBUS] enableEncryptionHex failed: need 64 hex chars");
#endif
    return false;
  }
  enableEncryption(ms);
  for (int i=0;i<32;i++) ms[i]=0;
  return true;
#else
  (void)psk_hex_64;
  return false;
#endif
}

void ETBus::disableEncryption() {
#if ETBUS_ENABLE_ENCRYPTION
  _crypto_enabled = false;
  for (int i=0;i<32;i++){ _psk[i]=0; _key[i]=0; }
  _tx_state_ctr = 0;
  _rx_cmd_last_ctr = 0;
#if ETBUS_DEBUG_CRYPTO
  Serial.println("[ETBUS] encryption DISABLED");
#endif
#endif
}

void ETBus::begin(const char* device_id,
                  const char* device_class,
                  const char* device_name,
                  const char* fw_version) {
  _id = device_id;
  _class = device_class;
  _name = device_name;
  _fw = fw_version;

  setWifiNoSleep(true);

  _udp.beginMulticast(_mcastIP, _port);

  if (_crypto_enabled) _deriveKeyFromPskAndId();

  sendDiscover();
  sendPong();
  _lastPongMs = millis();
  _lastDiscoverMs = _lastPongMs;
}

void ETBus::onCommand(CommandHandler cb) {
  _cmdHandler = cb;
#if ETBUS_DEBUG
  Serial.println("[ETBUS] command handler attached");
#endif
}

void ETBus::onSync(SyncHandler cb) {
  _syncHandler = cb;
#if ETBUS_DEBUG
  Serial.println("[ETBUS] sync handler attached");
#endif
}

bool ETBus::_discoverRateReady(unsigned long now) const {
  return _lastDiscoverMs == 0 || now - _lastDiscoverMs >= DISCOVER_INTERVAL_MS;
}

void ETBus::_learnHub(const IPAddress& from, const char* msg_type) {
  if (!_hubKnown || _hubIP != from) {
    _hubIP = from;
    _hubKnown = true;
#if ETBUS_DEBUG
    Serial.print("[ETBUS] learned hub IP from ");
    Serial.print(msg_type);
    Serial.print(": ");
    Serial.println(_hubIP);
#endif
  }
}

void ETBus::_maybeLearnPortFromPing(JsonObject payload) {
  if (!payload.isNull() && payload.containsKey("port")) {
    int p = (int)payload["port"];
    if (p > 0 && p <= 65535 && (uint16_t)p != _port) {
#if ETBUS_DEBUG
      Serial.print("[ETBUS] learned port from ping: ");
      Serial.println(p);
#endif
      setPort((uint16_t)p);
    }
  }
}

void ETBus::_printCryptoPacket(const char* dir, uint64_t ctr,
                               const uint8_t* nonce12,
                               const uint8_t* ct, size_t ct_len,
                               const uint8_t* tag16) {
#if ETBUS_DEBUG_CRYPTO
  Serial.print("[ETBUS] ");
  Serial.print(dir);
  Serial.print(" ctr=");
  Serial.println((unsigned long)ctr);

  Serial.print("[ETBUS] nonce=");
  _hexPrint(nonce12, 12);
  Serial.println();

  Serial.print("[ETBUS] key0=");
  _hexPrint(_key, 4);
  Serial.println();

  if (ct && ct_len) {
    Serial.print("[ETBUS] ct0=");
    _hexPrint(ct, ct_len >= 4 ? 4 : ct_len);
    Serial.println();
  }

  Serial.print("[ETBUS] tag0=");
  _hexPrint(tag16, 4);
  Serial.println();
#else
  (void)dir; (void)ctr; (void)nonce12; (void)ct; (void)ct_len; (void)tag16;
#endif
}

void ETBus::_printCryptoFull(const char* dir,
                             const char* nonce_b64,
                             const char* ct_b64,
                             const char* tag_b64) {
#if ETBUS_DEBUG_CRYPTO_FULL
  Serial.print("[ETBUS] ");
  Serial.print(dir);
  Serial.println(" FULL:");
  Serial.print("[ETBUS] nonce_b64=");
  Serial.println(nonce_b64 ? nonce_b64 : "(null)");
  Serial.print("[ETBUS] ct_b64=");
  Serial.println(ct_b64 ? ct_b64 : "(null)");
  Serial.print("[ETBUS] tag_b64=");
  Serial.println(tag_b64 ? tag_b64 : "(null)");
#else
  (void)dir; (void)nonce_b64; (void)ct_b64; (void)tag_b64;
#endif
}

bool ETBus::_decryptIncomingCommand(JsonObject in_wrapper, JsonObject out_plain_obj) {
#if ETBUS_ENABLE_ENCRYPTION
  if (!_crypto_enabled) return false;
  if (!(in_wrapper.containsKey("_enc") && (int)in_wrapper["_enc"] == 1)) return false;

  const int kid = (int)(in_wrapper["kid"] | 0);
  if (kid != (int)_kid) return false;

  uint64_t ctr = (uint64_t)((uint64_t)(in_wrapper["ctr"] | 0));
  if (ctr == 0) return false;

  if (ctr <= _rx_cmd_last_ctr) {
    // Hub reboot detection: when HA restarts, its tx_ctr resets to low values
    uint64_t drop = _rx_cmd_last_ctr - ctr;
    if (ctr <= 5 || drop >= 50) {
#if ETBUS_DEBUG_CRYPTO
      Serial.print("[ETBUS] hub reboot detected: cmd ctr=");
      Serial.print((unsigned long)ctr);
      Serial.print(" last=");
      Serial.println((unsigned long)_rx_cmd_last_ctr);
#endif
      _rx_cmd_last_ctr = 0;  // reset so this and subsequent commands are accepted
    } else {
#if ETBUS_DEBUG_CRYPTO
      Serial.println("[ETBUS] decrypt fail: replay/old ctr");
#endif
      return false;
    }
  }

  const char* nonce_b64 = in_wrapper["nonce"];
  const char* ct_b64    = in_wrapper["ct"];
  const char* tag_b64   = in_wrapper["tag"];
  if (!nonce_b64 || !ct_b64 || !tag_b64) return false;

  uint8_t nonce[12]; size_t nonce_len = 0;
  if (!_b64decode(nonce_b64, nonce, sizeof(nonce), nonce_len) || nonce_len != 12) return false;

  static uint8_t ct[1024]; size_t ct_len = 0;
  if (!_b64decode(ct_b64, ct, sizeof(ct), ct_len) || ct_len == 0) return false;

  uint8_t tag[16]; size_t tag_len = 0;
  if (!_b64decode(tag_b64, tag, sizeof(tag), tag_len) || tag_len != 16) return false;

  static uint8_t pt[1024];
  if (ct_len >= sizeof(pt)) return false;

  _printCryptoPacket("DEC CMD", ctr, nonce, ct, ct_len, tag);
  _printCryptoFull("DEC CMD", nonce_b64, ct_b64, tag_b64);

  bool ok = ETChaCha20Poly1305::decrypt(
    _key,
    nonce,
    nullptr, 0,            // AAD EMPTY
    ct, ct_len,
    tag,
    pt
  );

  if (!ok) {
#if ETBUS_DEBUG_CRYPTO
    Serial.print("[ETBUS] decrypt fail: auth/tag (ctr=");
    Serial.print((unsigned long)ctr);
    Serial.println(")");
#endif
    return false;
  }

  pt[ct_len] = 0;

  static StaticJsonDocument<1024> tmp;
  tmp.clear();
  if (deserializeJson(tmp, (const char*)pt)) return false;

  if (!tmp.is<JsonObject>()) return false;

  // copy fields into caller-provided object
  for (JsonPair kv : tmp.as<JsonObject>()) {
    out_plain_obj[kv.key()] = kv.value();
  }

  _rx_cmd_last_ctr = ctr;
  return true;
#else
  (void)in_wrapper; (void)out_plain_obj;
  return false;
#endif
}

bool ETBus::_encryptWrapperState(JsonObject plain, uint64_t ctr, JsonObject out_wrapper_obj) {
#if ETBUS_ENABLE_ENCRYPTION
  if (!_crypto_enabled) return false;

  static char pt_buf[768];
  size_t pt_len = serializeJson(plain, pt_buf, sizeof(pt_buf));
  if (pt_len == 0 || pt_len >= sizeof(pt_buf)) return false;

  uint8_t nonce[12];
  nonce[0] = 0x01; nonce[1] = 0x00; nonce[2] = 0x00; nonce[3] = 0x00;
  _u64_le(&nonce[4], ctr);

  static uint8_t ct[768];
  uint8_t tag[16];

  bool ok = ETChaCha20Poly1305::encrypt(
    _key,
    nonce,
    nullptr, 0,                      // AAD EMPTY
    (const uint8_t*)pt_buf, pt_len,
    ct,
    tag
  );
  if (!ok) return false;

  _printCryptoPacket("ENC STATE", ctr, nonce, ct, pt_len, tag);

  static char nonce_b64[64], ct_b64[1100], tag_b64[64];
  if (!_b64encode(nonce, sizeof(nonce), nonce_b64, sizeof(nonce_b64))) return false;
  if (!_b64encode(ct, pt_len, ct_b64, sizeof(ct_b64))) return false;
  if (!_b64encode(tag, sizeof(tag), tag_b64, sizeof(tag_b64))) return false;

  _printCryptoFull("ENC STATE", nonce_b64, ct_b64, tag_b64);

  out_wrapper_obj["_enc"] = 1;
  out_wrapper_obj["kid"]  = (int)_kid;
  out_wrapper_obj["ctr"]  = (uint64_t)ctr;
  out_wrapper_obj["nonce"]= nonce_b64;
  out_wrapper_obj["ct"]   = ct_b64;
  out_wrapper_obj["tag"]  = tag_b64;

#if ETBUS_DEBUG_CRYPTO
  Serial.print("[ETBUS] encrypt ok (type=state ctr=");
  Serial.print((unsigned long)ctr);
  Serial.println(")");
#endif
  return true;
#else
  (void)plain; (void)ctr; (void)out_wrapper_obj;
  return false;
#endif
}

void ETBus::_sendEnvelopePlain(const char* type, JsonObject payload, bool allow_multicast) {
  static StaticJsonDocument<1200> doc;
  doc.clear();

  doc["v"] = 1;
  doc["type"] = type;
  doc["id"] = _id ? _id : "";
  doc["class"] = _class ? _class : "";
  doc["boot"] = _bootId;
  doc["seq"] = ++_seq;

  JsonObject p = doc.createNestedObject("payload");
  for (JsonPair kv : payload) {
    p[kv.key()] = kv.value();
  }

  IPAddress target = _hubKnown ? _hubIP : _mcastIP;
  if (!_hubKnown && !allow_multicast) return;

  static char buf[1500];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  if (!n) return;

  _udp.beginPacket(target, _port);
  _udp.write((const uint8_t*)buf, n);
  _udp.endPacket();
}

void ETBus::_sendEnvelopeEncryptedState(JsonObject plain_payload) {
#if ETBUS_ENABLE_ENCRYPTION
  // IMPORTANT: wrapper is built INSIDE the final envelope doc (no cross-doc object assignment)
  static StaticJsonDocument<1700> env;
  env.clear();

  env["v"] = 1;
  env["type"] = "state";
  env["id"] = _id ? _id : "";
  env["class"] = _class ? _class : "";
  env["boot"] = _bootId;
  env["seq"] = ++_seq;

  JsonObject wrapper = env.createNestedObject("payload");

  _tx_state_ctr++;
  if (!_encryptWrapperState(plain_payload, _tx_state_ctr, wrapper)) return;

  IPAddress target = _hubKnown ? _hubIP : _mcastIP;

  static char buf[1900];
  size_t n = serializeJson(env, buf, sizeof(buf));
  if (!n) return;

  _udp.beginPacket(target, _port);
  _udp.write((const uint8_t*)buf, n);
  _udp.endPacket();
#else
  (void)plain_payload;
#endif
}

void ETBus::sendDiscover() {
  StaticJsonDocument<384> pdoc;
  JsonObject p = pdoc.to<JsonObject>();
  p["name"] = _name ? _name : "";
  p["fw"] = _fw ? _fw : "";
  p["lib"] = ETBUS_LIBRARY_VERSION;
  p["boot"] = _bootId;
  JsonArray features = p.createNestedArray("features");
  features.add("encrypted");
  features.add("ack");
  features.add("sync");
  _sendEnvelopePlain("discover", p, true);
}

void ETBus::sendDiscover(JsonObject extra_payload) {
  StaticJsonDocument<768> pdoc;
  JsonObject p = pdoc.to<JsonObject>();
  p["name"] = _name ? _name : "";
  p["fw"] = _fw ? _fw : "";
  p["lib"] = ETBUS_LIBRARY_VERSION;
  p["boot"] = _bootId;
  JsonArray features = p.createNestedArray("features");
  features.add("encrypted");
  features.add("ack");
  features.add("sync");

  for (JsonPair kv : extra_payload) {
    p[kv.key()] = kv.value();
  }

  _sendEnvelopePlain("discover", p, true);
}

void ETBus::sendPong() {
  StaticJsonDocument<256> pdoc;
  JsonObject p = pdoc.to<JsonObject>();
  _sendEnvelopePlain("pong", p, true);
}

void ETBus::sendAck(const char* command_id, bool ok) {
  StaticJsonDocument<256> pdoc;
  JsonObject p = pdoc.to<JsonObject>();
  p["ok"] = ok;
  if (command_id && command_id[0]) p["cmd"] = command_id;
  _sendEnvelopePlain("ack", p, true);
}

void ETBus::sendError(const char* code, const char* message) {
  StaticJsonDocument<384> pdoc;
  JsonObject p = pdoc.to<JsonObject>();
  p["code"] = code ? code : "error";
  if (message && message[0]) p["message"] = message;
  _sendEnvelopePlain("error", p, true);
}

void ETBus::sendState(JsonObject payload) {
  if (_crypto_enabled) {
    _sendEnvelopeEncryptedState(payload);
    return;
  }
  _sendEnvelopePlain("state", payload, true);
}

void ETBus::sendSwitchState(bool on) {
  StaticJsonDocument<128> d;
  JsonObject p = d.to<JsonObject>();
  p["on"] = on;
  sendState(p);
}

void ETBus::sendRgbStateFx(bool on, uint8_t r, uint8_t g, uint8_t b, uint8_t brightness,
                           const char* effect, uint8_t speed) {
  StaticJsonDocument<256> d;
  JsonObject p = d.to<JsonObject>();
  p["on"] = on;
  p["r"] = r;
  p["g"] = g;
  p["b"] = b;
  p["brightness"] = brightness;
  p["effect"] = effect ? effect : "solid";
  p["speed"] = speed;
  sendState(p);
}

void ETBus::loop() {
  if (millis() - _lastPongMs > PONG_INTERVAL_MS) {
    sendPong();
    _lastPongMs = millis();
#if ETBUS_DEBUG
    Serial.println("[ETBUS] plaintext pong");
#endif
  }

  int packetSize = _udp.parsePacket();
  if (!packetSize) return;

  IPAddress from = _udp.remoteIP();

  char buf[1900];
  int len = _udp.read(buf, sizeof(buf) - 1);
  if (len <= 0) return;
  buf[len] = 0;

  StaticJsonDocument<1700> doc;
  if (deserializeJson(doc, buf)) return;

  int v = doc["v"] | 0;
  const char* type = doc["type"] | "";
  const char* cls  = doc["class"] | "";

  if (v != 1 || !type[0]) return;

  JsonObject payload = doc["payload"].as<JsonObject>();

  if (!payload.isNull() && (type[0]=='p' && type[1]=='i' && type[2]=='n' && type[3]=='g')) {
    _learnHub(from, "ping");
    _maybeLearnPortFromPing(payload);
    unsigned long now = millis();
    if (_discoverRateReady(now)) {
      sendDiscover();
      _lastDiscoverMs = now;
    }
    sendPong();
    _lastPongMs = now;
    if (_syncHandler) _syncHandler();
    return;
  }

  if (type[0]=='s' && type[1]=='y' && type[2]=='n' && type[3]=='c') {
    _learnHub(from, "sync");
    if (_syncHandler) _syncHandler();
    return;
  }

  if (type[0]=='c' && type[1]=='o' && type[2]=='m' && type[3]=='m' && type[4]=='a' && type[5]=='n' && type[6]=='d') {
    _learnHub(from, "command");

    if (_cmdHandler) {
      if (_crypto_enabled && !payload.isNull() && payload.containsKey("_enc")) {
        StaticJsonDocument<1024> plainDoc;
        JsonObject plainObj = plainDoc.to<JsonObject>();

        if (_decryptIncomingCommand(payload, plainObj)) {
          _cmdHandler(cls, plainObj);
        } else {
          Serial.println("[ETBUS] decrypt failed (command)");
        }
      } else {
        _cmdHandler(cls, payload);
      }
    }
  }
}
