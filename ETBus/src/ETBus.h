#pragma once

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

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

  // Helpers used by your framework
  void sendSwitchState(bool on);
  void sendRgbState(bool on, uint8_t r, uint8_t g, uint8_t b, uint8_t brightness);
  void sendFanState(bool on, const char* preset);

  // Optional
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
