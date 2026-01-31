#include <WiFi.h>
#include <ETBus.h>

// -----------------------------
// HARD-CODED WIFI CREDENTIALS
// -----------------------------
static const char* WIFI_SSID = "home";
static const char* WIFI_PASS = "test";

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

  Serial.print("[RELAY] set to: ");
  Serial.println(relayOn ? "ON" : "OFF");

  // confirm state back to Home Assistant
  etbus.sendSwitchState(relayOn);
  Serial.println("[ETBUS] sent switch state");
}

// -----------------------------
// ET-Bus command handler
// -----------------------------
static void onEtbusCommand(const char* dev_class, JsonObject payload) {
  Serial.print("[ETBUS] command for class: ");
  Serial.println(dev_class ? dev_class : "(null)");

  if (payload.containsKey("on")) {
    bool on = (bool)payload["on"];
    Serial.print("[ETBUS] payload on=");
    Serial.println(on ? "true" : "false");
    applyRelay(on);
  } else {
    Serial.println("[ETBUS] payload missing key: on");
  }
}

// -----------------------------
// WiFi connect helper
// -----------------------------
static void connectWiFi() {
  Serial.println();
  Serial.print("[WIFI] Connecting to: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // helps stability on some setups
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    if (millis() - start > 20000) {
      Serial.println();
      Serial.println("[WIFI] connect timeout - restarting");
      delay(300);
      ESP.restart();
    }
  }

  Serial.println();
  Serial.println("[WIFI] Connected!");
  Serial.print("[WIFI] IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("[WIFI] RSSI: ");
  Serial.println(WiFi.RSSI());
}

void setup() {
  Serial.begin(115200);
  delay(300); // ✅ allow ESP32 boot pins to settle

  Serial.println();
  Serial.println("=== ET-Bus Relay Test (Hardcoded WiFi) ===");

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  relayOn = false;

  connectWiFi();

  // ET-Bus
  Serial.println("[ETBUS] begin()");
  etbus.begin(
    "relay1",       // device id
    "switch.relay", // class
    "Test Relay 1", // name
    "test-1.0"      // firmware
  );

  etbus.onCommand(onEtbusCommand);
  Serial.println("[ETBUS] onCommand handler attached");

  // publish initial state
  etbus.sendSwitchState(relayOn);
  Serial.println("[ETBUS] initial switch state sent");
}

void loop() {
  etbus.loop();

  // WiFi keep-alive / auto-reconnect
  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 5000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WIFI] lost connection - reconnecting...");
      connectWiFi();
      // re-announce current state after reconnect
      etbus.sendSwitchState(relayOn);
      Serial.println("[ETBUS] resent state after WiFi reconnect");
    }
  }

  // optional heartbeat (keeps HA history alive)
  static unsigned long lastBeat = 0;
  if (millis() - lastBeat > 30000) {
    lastBeat = millis();
    etbus.sendSwitchState(relayOn);
    Serial.println("[ETBUS] heartbeat state sent");
  }
}
