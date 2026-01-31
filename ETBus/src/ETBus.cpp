#include "ETBus.h"

// Defaults match the Home Assistant ET-Bus hub
static IPAddress ETBUS_MCAST(239, 10, 0, 1);
static const uint16_t ETBUS_PORT = 5555;

// Library heartbeat.
// Keep this aligned with your HA OFFLINE_TIMEOUT.
// Example: if OFFLINE_TIMEOUT=75s, then 30s pong is fine.
static const uint32_t PONG_INTERVAL_MS = 30000;

ETBus::ETBus() {}

void ETBus::setWifiNoSleep(bool on) {
#if defined(ARDUINO_ARCH_ESP32)
  // on=true => disable WiFi sleep (best for UDP reliability)
  WiFi.setSleep(!on);
#else
  (void)on;
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

  // Commercial-grade Wi-Fi UDP stability
  setWifiNoSleep(true);

  // ESP32 Arduino core 2.0.x: beginMulticast(multicast_ip, port)
  _udp.beginMulticast(ETBUS_MCAST, ETBUS_PORT);

  // Announce
  sendDiscover();
  sendPong();
  _lastPongMs = millis();
}

void ETBus::onCommand(CommandHandler cb) {
  _cmdHandler = cb;
}

void ETBus::_learnHub(const IPAddress& from, const char* msg_type) {
  if (!_hubKnown || _hubIP != from) {
    _hubIP = from;
    _hubKnown = true;

    Serial.print("[ETBUS] learned hub IP from ");
    Serial.print(msg_type);
    Serial.print(": ");
    Serial.println(_hubIP);
  }
}

void ETBus::loop() {
  int size = _udp.parsePacket();
  if (size > 0) {
    StaticJsonDocument<896> doc;
    DeserializationError err = deserializeJson(doc, _udp);
    if (!err) {
      const int v = doc["v"] | 0;
      const char* type = doc["type"] | "";
      const char* cls  = doc["class"] | "";

      if (v == 1) {
        // Learn hub IP from any ping/command we receive
        if (strcmp(type, "ping") == 0 || strcmp(type, "command") == 0) {
          _learnHub(_udp.remoteIP(), type);
        }
      }

      // Dispatch commands (HA -> device)
      if (v == 1 && strcmp(type, "command") == 0) {
        if (_cmdHandler) {
          JsonObject payload = doc["payload"].as<JsonObject>();
          _cmdHandler(cls, payload);
        }
      }
    }
  }

  // Heartbeat
  unsigned long now = millis();
  if (now - _lastPongMs >= PONG_INTERVAL_MS) {
    sendPong();
    _lastPongMs = now;
  }
}

void ETBus::sendDiscover() {
  StaticJsonDocument<512> doc;
  doc["v"] = 1;
  doc["type"] = "discover";
  doc["id"] = _id;
  doc["class"] = _class;

  JsonObject payload = doc.createNestedObject("payload");
  payload["name"] = _name;
  payload["fw"] = _fw;

  // Discover is multicast so the hub can learn device IP immediately
  _udp.beginPacket(ETBUS_MCAST, ETBUS_PORT);
  serializeJson(doc, _udp);
  _udp.endPacket();
}

void ETBus::sendPong() {
  StaticJsonDocument<512> doc;
  doc["v"] = 1;
  doc["type"] = "pong";
  doc["id"] = _id;
  doc["class"] = _class;

  JsonObject payload = doc.createNestedObject("payload");
  payload["uptime"] = (uint32_t)(millis() / 1000);
  payload["rssi"] = WiFi.RSSI();
  payload["name"] = _name;
  payload["fw"] = _fw;

  // Once hub known -> UNICAST (fixes Wi-Fi multicast stall problems)
  if (_hubKnown) {
    _udp.beginPacket(_hubIP, ETBUS_PORT);
  } else {
    _udp.beginPacket(ETBUS_MCAST, ETBUS_PORT);
  }

  serializeJson(doc, _udp);
  _udp.endPacket();
}

void ETBus::_sendEnvelope(const char* type, JsonObject payload, bool allow_multicast) {
  StaticJsonDocument<896> doc;
  doc["v"] = 1;
  doc["type"] = type;
  doc["id"] = _id;
  doc["class"] = _class;

  JsonObject out = doc.createNestedObject("payload");
  for (JsonPair kv : payload) {
    out[kv.key()] = kv.value();
  }
  // Always include a stable name so HA can show friendly names
  out["name"] = _name;
  out["fw"] = _fw;

  if (_hubKnown) {
    _udp.beginPacket(_hubIP, ETBUS_PORT);
  } else if (allow_multicast) {
    _udp.beginPacket(ETBUS_MCAST, ETBUS_PORT);
  } else {
    return;
  }

  serializeJson(doc, _udp);
  _udp.endPacket();
}

void ETBus::sendState(JsonObject payload) {
  // Allow multicast only as fallback until hub is learned
  _sendEnvelope("state", payload, true);
}

// ----------------------------
// Helpers
// ----------------------------

void ETBus::sendSwitchState(bool on) {
  StaticJsonDocument<160> p;
  p["on"] = on;
  sendState(p.as<JsonObject>());
}

void ETBus::sendRgbState(bool on, uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
  sendRgbStateFx(on, r, g, b, brightness, nullptr, 0);
}

void ETBus::sendRgbStateFx(bool on,
                          uint8_t r,
                          uint8_t g,
                          uint8_t b,
                          uint8_t brightness,
                          const char* effect,
                          uint8_t speed) {
  StaticJsonDocument<320> p;
  p["on"] = on;
  p["r"] = r;
  p["g"] = g;
  p["b"] = b;
  p["brightness"] = brightness;
  if (effect && *effect) {
    p["effect"] = effect;
  }
  if (speed > 0) {
    p["speed"] = speed;
  }
  sendState(p.as<JsonObject>());
}

void ETBus::sendFanState(bool on, const char* preset) {
  StaticJsonDocument<192> p;
  p["on"] = on;
  p["preset"] = (preset && *preset) ? preset : (on ? "low" : "off");
  sendState(p.as<JsonObject>());
}
