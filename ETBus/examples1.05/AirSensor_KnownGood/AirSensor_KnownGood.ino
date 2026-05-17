#include <WiFi.h>
#include <ArduinoJson.h>
#include <ETBus.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "../secrets.example.h"
#warning "Using placeholder secrets. Copy secrets.example.h to secrets.h and edit it before flashing real hardware."
#endif

static const char* DEVICE_ID = "sensor2";
static const char* DEVICE_NAME = "ET-Bus Air Sensor";
static const char* FW_VERSION = "examples1.7-air";

ETBus etbus;

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

static void publishState() {
  StaticJsonDocument<320> doc;
  JsonObject payload = doc.to<JsonObject>();

  payload["co2"] = random(420, 850);
  payload["temp"] = random(190, 280) / 10.0;
  payload["humidity"] = random(420, 700) / 10.0;
  payload["pressure"] = random(9900, 10350) / 10.0;
  payload["tvoc"] = random(0, 500);
  payload["pm2_5"] = random(0, 250) / 10.0;
  payload["lux"] = random(20, 800);
  payload["battery"] = random(850, 1000) / 10.0;

  etbus.sendState(payload);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  connectWiFi();

  if (strlen(ETBUS_PSK_HEX) == 64) {
    etbus.enableEncryptionHex(ETBUS_PSK_HEX);
  }

  etbus.begin(DEVICE_ID, "sensor.air", DEVICE_NAME, FW_VERSION);
  etbus.onSync(publishState);
  publishState();
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
  if (millis() - lastState > 10000) {
    lastState = millis();
    publishState();
  }
}
