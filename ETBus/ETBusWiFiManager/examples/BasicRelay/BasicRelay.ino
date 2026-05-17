/*
 * BasicRelay.ino — ET-Bus WiFi Manager 4 Relay Example
 *
 * First boot creates the "ETBus-Relay" setup AP.
 * Enter WiFi, ET-Bus PSK, and device name in the portal.
 * Hold BOOT button during startup to reset saved settings.
 */

#include <WiFi.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <ETBusWiFiManager.h>
#include <ETBus.h>

ETBusWiFiManager wm;
ETBus etbus;
Preferences prefs;

String devName;
String psk;

#define RESET_PIN 0

static const uint8_t RELAY_COUNT = 4;
static const uint8_t RELAY_PINS[RELAY_COUNT] = {16, 17, 18, 19};
static const bool RELAY_ACTIVE_HIGH = true;

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
    Serial.printf("[RELAY] %u -> %s\n", (unsigned)(index + 1), on ? "ON" : "OFF");
}

static void restoreRelayState() {
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        char key[8];
        snprintf(key, sizeof(key), "r%u", (unsigned)i);
        writeRelay(i, prefs.getBool(key, false), false);
    }
}

static void publishSwitchDiscovery() {
    StaticJsonDocument<384> doc;
    JsonObject payload = doc.to<JsonObject>();

    payload["name"] = "ET-Bus 4 Relay Board";
    payload["model"] = "4 Relay Board";
    payload["version"] = "wifimanager-relay4-1.7";

    JsonArray switches = payload.createNestedArray("switches");
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        JsonObject sw = switches.createNestedObject();
        sw["id"] = String(i + 1);
        sw["name"] = String("Relay ") + String(i + 1);
    }

    etbus.sendState(payload);
    Serial.println("[ETBUS] relay discovery/state sent");
}

static void publishRelayState() {
    StaticJsonDocument<256> doc;
    JsonObject payload = doc.to<JsonObject>();
    JsonObject switches = payload.createNestedObject("switches");

    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        switches[String(i + 1)] = relayOn[i];
    }

    etbus.sendState(payload);
    Serial.printf("[ETBUS] relay state sent: 1=%d 2=%d 3=%d 4=%d\n",
                  relayOn[0], relayOn[1], relayOn[2], relayOn[3]);
}

static void onCommand(const char* dev_class, JsonObject payload) {
    if (!dev_class || strcmp(dev_class, "switch.multi") != 0) return;
    if (!payload.containsKey("switch_id") || !payload.containsKey("on")) {
        etbus.sendError("bad_command", "switch_id and on are required");
        return;
    }

    const char* switchId = payload["switch_id"] | "";
    int index = atoi(switchId) - 1;
    if (index < 0 || index >= RELAY_COUNT) {
        etbus.sendError("bad_switch", "switch_id out of range");
        return;
    }

    Serial.printf("[ETBUS] command relay %s -> %s\n", switchId, ((bool)payload["on"]) ? "ON" : "OFF");
    writeRelay((uint8_t)index, (bool)payload["on"]);
    etbus.sendAck("switch", true);
    publishRelayState();
}

static void onSync() {
    publishSwitchDiscovery();
    publishRelayState();
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== ET-Bus WiFiManager 4 Relay Board ===");

    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        pinMode(RELAY_PINS[i], OUTPUT);
    }
    prefs.begin("etbus-relay", false);
    restoreRelayState();

    pinMode(RESET_PIN, INPUT_PULLUP);
    if (digitalRead(RESET_PIN) == LOW) {
        Serial.println("[BOOT] Reset held - clearing settings");
        wm.resetSettings();
    }

    wm.begin("ETBus-Relay");

    Serial.printf("[WIFI] SSID: %s\n", wm.getSSID().c_str());
    Serial.printf("[WIFI] IP:   %s\n", WiFi.localIP().toString().c_str());
    WiFi.setSleep(false);

    devName = wm.getDevName();
    if (devName.length() == 0) devName = "relay_board";

    psk = wm.getPSK();
    if (psk.length() == 64) {
        if (etbus.enableEncryptionHex(psk.c_str())) {
            Serial.println("[ETBUS] Encrypted (key from portal)");
        }
    } else {
        Serial.println("[ETBUS] No PSK - running unencrypted");
    }

    etbus.begin(devName.c_str(), "switch.multi", "4 Relay Board", "wifimanager-relay4-1.7");
    etbus.onCommand(onCommand);
    etbus.onSync(onSync);

    publishSwitchDiscovery();
    publishRelayState();

    Serial.println("[BOOT] READY\n");
}

void loop() {
    etbus.loop();
    delay(0);
}
