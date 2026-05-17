#include <WiFi.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <ETBus.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "../secrets.example.h"
#warning "Using placeholder secrets. Copy secrets.example.h to secrets.h and edit it before flashing real hardware."
#endif

static const char* DEVICE_ID = "relay_board";
static const char* DEVICE_NAME = "ET-Bus 4 Relay Board";
static const char* FW_VERSION = "examples1.7-relay4";

static const uint8_t RELAY_COUNT = 4;
static const uint8_t RELAY_PINS[RELAY_COUNT] = {16, 17, 18, 19};
static const bool RELAY_ACTIVE_HIGH = true;

ETBus etbus;
Preferences prefs;
bool relayOn[RELAY_COUNT] = {false, false, false, false};

static void saveRelayState(uint8_t index) {
  char key[8];
  snprintf(key, sizeof(key), "r%u", (unsigned)index);
  prefs.putBool(key, relayOn[index]);
}

static void writeRelay(uint8_t index, bool on, bool persist = true) {
  if (index >= RELAY_COUNT) return;
  relayOn[index] = on;
  digitalWrite(RELAY_PINS[index], on == RELAY_ACTIVE_HIGH ? HIGH : LOW);
  if (persist) saveRelayState(index);
}

static void restoreRelayState() {
  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    char key[8];
    snprintf(key, sizeof(key), "r%u", (unsigned)i);
    writeRelay(i, prefs.getBool(key, false), false);
  }
}

static void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    if (millis() - start > 20000) {
      ESP.restart();
    }
  }
}

static void publishSwitchDiscovery() {
  StaticJsonDocument<384> doc;
  JsonObject payload = doc.to<JsonObject>();

  payload["name"] = DEVICE_NAME;
  payload["model"] = "4 Relay Board";
  payload["version"] = FW_VERSION;

  JsonArray switches = payload.createNestedArray("switches");
  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    JsonObject sw = switches.createNestedObject();
    sw["id"] = String(i + 1);
    sw["name"] = String("Relay ") + String(i + 1);
  }

  etbus.sendDiscover(payload);
}

static void publishRelayState() {
  StaticJsonDocument<256> doc;
  JsonObject payload = doc.to<JsonObject>();
  JsonObject switches = payload.createNestedObject("switches");

  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    switches[String(i + 1)] = relayOn[i];
  }

  etbus.sendState(payload);
}

static void onEtbusCommand(const char* dev_class, JsonObject payload) {
  if (!dev_class || strcmp(dev_class, "switch.multi") != 0) return;
  if (!payload.containsKey("switch_id") || !payload.containsKey("on")) {
    etbus.sendError("bad_command", "switch_id and on are required");
    return;
  }

  const char* switchId = payload["switch_id"] | "";
  const int index = atoi(switchId) - 1;
  if (index < 0 || index >= RELAY_COUNT) {
    etbus.sendError("bad_switch", "switch_id out of range");
    return;
  }

  writeRelay((uint8_t)index, (bool)payload["on"]);
  etbus.sendAck("switch", true);
  publishRelayState();
}

static void onEtbusSync() {
  publishSwitchDiscovery();
  publishRelayState();
}

void setup() {
  Serial.begin(115200);
  delay(300);

  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
  }
  prefs.begin("etbus-relay", false);
  restoreRelayState();

  connectWiFi();

  if (strlen(ETBUS_PSK_HEX) == 64) {
    etbus.enableEncryptionHex(ETBUS_PSK_HEX);
  }

  etbus.begin(DEVICE_ID, "switch.multi", DEVICE_NAME, FW_VERSION);
  etbus.onCommand(onEtbusCommand);
  etbus.onSync(onEtbusSync);

  publishSwitchDiscovery();
  publishRelayState();
}

void loop() {
  etbus.loop();

  static uint32_t lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 5000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      ESP.restart();
    }
  }

  static uint32_t lastState = 0;
  if (millis() - lastState > 30000) {
    lastState = millis();
    publishRelayState();
  }
}
