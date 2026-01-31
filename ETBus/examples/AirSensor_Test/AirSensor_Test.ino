#include <WiFi.h>
#include <ETBus.h>

// -----------------------------
// HARD-CODED WIFI CREDENTIALS
// -----------------------------
static const char* WIFI_SSID = "home";
static const char* WIFI_PASS = "test";

ETBus etbus;

// -----------------------------
// WiFi connect helper
// -----------------------------
static void connectWiFi() {
  Serial.println();
  Serial.print("[WIFI] Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    if (millis() - start > 20000) {
      Serial.println();
      Serial.println("[WIFI] timeout, rebooting");
      delay(300);
      ESP.restart();
    }
  }

  Serial.println();
  Serial.println("[WIFI] Connected");
  Serial.print("[WIFI] IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("[WIFI] RSSI: ");
  Serial.println(WiFi.RSSI());
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("=== ET-Bus Air Sensor ===");

  connectWiFi();

  // ET-Bus device
  Serial.println("[ETBUS] begin()");
  etbus.begin(
    "air1",          // device id
    "sensor.air",    // device class
    "Air Sensor 1",  // name
    "test-1.0"       // firmware
  );

  Serial.println("[ETBUS] device registered");
}

void loop() {
  etbus.loop();

  // WiFi keep-alive
  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 5000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WIFI] lost connection - reconnecting");
      connectWiFi();
    }
  }

  static unsigned long lastSend = 0;
  if (millis() - lastSend > 2000) {
    lastSend = millis();

    StaticJsonDocument<128> payload;
    payload["co2"] = random(400, 900);               // ppm
    payload["temp"] = random(200, 300) / 10.0;       // °C
    payload["humidity"] = random(400, 700) / 10.0;   // %

    Serial.print("[ETBUS] sending payload: ");
    serializeJson(payload, Serial);
    Serial.println();

    etbus.sendState(payload.as<JsonObject>());

    Serial.println("[ETBUS] state sent");
  }
}
