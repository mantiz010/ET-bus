#pragma once

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

#include <ETChaCha20Poly1305.h>

#define ETBUS_LIBRARY_VERSION "1.7"

#ifndef ETBUS_ENABLE_ENCRYPTION
#define ETBUS_ENABLE_ENCRYPTION 1
#endif

#ifndef ETBUS_DEBUG
#define ETBUS_DEBUG 0
#endif

#ifndef ETBUS_DEBUG_CRYPTO
#define ETBUS_DEBUG_CRYPTO 0
#endif

// Print full base64 fields for encrypted packets.
#ifndef ETBUS_DEBUG_CRYPTO_FULL
#define ETBUS_DEBUG_CRYPTO_FULL 0
#endif

// Default multicast + port (match HA)
#ifndef ETBUS_DEFAULT_PORT
#define ETBUS_DEFAULT_PORT 5555
#endif

#ifndef ETBUS_MCAST_A
#define ETBUS_MCAST_A 239
#define ETBUS_MCAST_B 10
#define ETBUS_MCAST_C 0
#define ETBUS_MCAST_D 1
#endif

class ETBus {
public:
  typedef void (*CommandHandler)(const char* dev_class, JsonObject payload);
  typedef void (*SyncHandler)();

  ETBus();

  void begin(const char* device_id,
             const char* device_class,
             const char* device_name,
             const char* fw_version);

  void loop();
  void onCommand(CommandHandler cb);
  void onSync(SyncHandler cb);

  void setPort(uint16_t port);
  uint16_t port() const { return _port; }

  // Plaintext discovery plane
  void sendDiscover();
  void sendPong();
  void sendAck(const char* command_id = nullptr, bool ok = true);
  void sendError(const char* code, const char* message = nullptr);

  // State (will encrypt if enabled)
  void sendState(JsonObject payload);

  // Convenience
  void sendSwitchState(bool on);
  void sendRgbStateFx(bool on, uint8_t r, uint8_t g, uint8_t b, uint8_t brightness,
                      const char* effect, uint8_t speed);

  // Crypto: PSK is 32 bytes (same psk_hex in HA options)
  void enableEncryption(const uint8_t master_secret_32[32]);   // per-device derived key
  bool enableEncryptionHex(const char* psk_hex_64);            // convenience
  void disableEncryption();
  bool encryptionEnabled() const { return _crypto_enabled; }

  void setWifiNoSleep(bool on);

private:
  // Core send
  void _sendEnvelopePlain(const char* type, JsonObject payload, bool allow_multicast);
  void _sendEnvelopeEncryptedState(JsonObject plain_payload);

  // Hub learn
  void _learnHub(const IPAddress& from, const char* msg_type);
  void _maybeLearnPortFromPing(JsonObject payload);
  bool _discoverRateReady(unsigned long now) const;
  void _makeBootId();

  // Crypto
  void _deriveKeyFromPskAndId();        // key = sha256(psk||device_id)

  bool _decryptIncomingCommand(JsonObject in_wrapper, JsonObject out_plain_obj);
  bool _encryptWrapperState(JsonObject plain, uint64_t ctr, JsonObject out_wrapper_obj);

  // Helpers
  static bool _hex32_from_str(const char* s, uint8_t out32[32]);
  static void _u64_le(uint8_t out8[8], uint64_t v);

  bool _b64decode(const char* in_b64, uint8_t* out, size_t out_max, size_t& out_len);
  bool _b64encode(const uint8_t* in, size_t in_len, char* out_b64, size_t out_max);

  void _hexPrint(const uint8_t* b, size_t n);
  void _printCryptoPacket(const char* dir, uint64_t ctr,
                          const uint8_t* nonce12,
                          const uint8_t* ct, size_t ct_len,
                          const uint8_t* tag16);

  void _printCryptoFull(const char* dir,
                        const char* nonce_b64,
                        const char* ct_b64,
                        const char* tag_b64);

  WiFiUDP _udp;

  // Identity
  const char* _id = nullptr;
  const char* _class = nullptr;
  const char* _name = nullptr;
  const char* _fw = nullptr;

  CommandHandler _cmdHandler = nullptr;
  SyncHandler _syncHandler = nullptr;

  // Network
  IPAddress _mcastIP = IPAddress(ETBUS_MCAST_A, ETBUS_MCAST_B, ETBUS_MCAST_C, ETBUS_MCAST_D);
  uint16_t _port = ETBUS_DEFAULT_PORT;

  bool _hubKnown = false;
  IPAddress _hubIP;

  unsigned long _lastPongMs = 0;
  unsigned long _lastDiscoverMs = 0;
  uint32_t _seq = 0;
  char _bootId[17] = {0};

  // Crypto state
  bool _crypto_enabled = false;
  uint8_t _kid = 1;

  uint8_t _psk[32] = {0};
  uint8_t _key[32] = {0};              // derived per-device key

  // Counters
  uint64_t _tx_state_ctr = 0;           // device -> HA
  uint64_t _rx_cmd_last_ctr = 0;        // last accepted HA->device command ctr
};
