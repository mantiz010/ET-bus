#pragma once

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

// ET-Bus Arduino library (ESP32 / WiFi UDP)
// - Multicast discovery
// - Unicast pong/state once hub IP is learned
// - JSON envelope compatible with your Home Assistant integration

class ETBus {
public:
  typedef void (*CommandHandler)(const char* dev_class, JsonObject payload);

  ETBus();

  void begin(
    const char* device_id,
    const char* device_class,
    const char* device_name,
    const char* fw_version
  );

  void loop();
  void onCommand(CommandHandler cb);

  // Protocol messages
  void sendDiscover();
  void sendPong();

  // Generic state sender (payload becomes "payload" object)
  void sendState(JsonObject payload);

  // Helpers
  void sendSwitchState(bool on);

  // RGB helper (no effects)
  void sendRgbState(bool on, uint8_t r, uint8_t g, uint8_t b, uint8_t brightness);

  // RGB helper with effects (WS2812B rings/strips etc.)
  // effect: short string, e.g. "solid", "rainbow", "cylon", "confetti"
  // speed: 1..255 (higher = faster)
  void sendRgbStateFx(bool on,
                      uint8_t r,
                      uint8_t g,
                      uint8_t b,
                      uint8_t brightness,
                      const char* effect,
                      uint8_t speed);

  // Fan helper (preset mode)
  void sendFanState(bool on, const char* preset);

  // Optional WiFi sleep control
  void setWifiNoSleep(bool on);

private:
  void _learnHub(const IPAddress& from, const char* msg_type);
  void _sendEnvelope(const char* type, JsonObject payload, bool allow_multicast);

  WiFiUDP _udp;

  const char* _id = nullptr;
  const char* _class = nullptr;
  const char* _name = nullptr;
  const char* _fw = nullptr;

  CommandHandler _cmdHandler = nullptr;

  bool _hubKnown = false;
  IPAddress _hubIP;

  unsigned long _lastPongMs = 0;
};
