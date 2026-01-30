#include <WiFi.h>
#include <ETBus.h>
#include "secrets.h"

// ✅ SAFE GPIO (not a boot strap pin)
static const int RELAY_PIN = 26;

ETBus etbus;
static bool relayOn = false;

// -----------------------------
// Apply relay state + feedback
// -----------------------------
static void applyRelay(bool on) {
  relayOn = on;
  digitalWrite(RELAY_PIN, relayOn ? HIGH : LOW);

  // confirm state back to Home Assistant
  etbus.sendSwitchState(relayOn);
}

// -----------------------------
// ET-Bus command handler
// -----------------------------
static void onEtbusCommand(const char* dev_class, JsonObject payload) {
  if (payload.containsKey("on")) {
    applyRelay((bool)payload["on"]);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);                 // ✅ allow ESP32 boot pins to settle

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  relayOn = false;

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  Serial.println(WiFi.localIP());

  // ET-Bus
  etbus.begin(
    "relay4",                 // device id
    "switch.relay",           // class
    "Test Relay 1",           // name
    "test-1.0"                // firmware
  );

  etbus.onCommand(onEtbusCommand);

  // publish initial state
  etbus.sendSwitchState(relayOn);
}

void loop() {
  etbus.loop();

  // optional heartbeat (keeps HA history alive)
  static unsigned long last = 0;
  if (millis() - last > 30000) {
    last = millis();
    etbus.sendSwitchState(relayOn);
  }
}
