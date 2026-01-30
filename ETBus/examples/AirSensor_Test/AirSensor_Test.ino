\
#include <WiFi.h>
#include <ETBus.h>

#include "secrets.h"

ETBus etbus;

void setup() {
  Serial.begin(115200);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.println(WiFi.localIP());

  // One device id, one class. For multi-sensor payload, HA creates sub-sensors.
  etbus.begin("air1", "sensor.air", "Air Sensor 1", "test-1.0");
}

void loop() {
  etbus.loop();

  static unsigned long lastSend = 0;
  if (millis() - lastSend > 2000) {
    lastSend = millis();

    StaticJsonDocument<128> p;
    p["co2"] = random(400, 900);            // ppm
    p["temp"] = random(200, 300) / 10.0;    // C
    p["humidity"] = random(400, 700) / 10.0; // %

    etbus.sendState(p.as<JsonObject>());
  }
}
