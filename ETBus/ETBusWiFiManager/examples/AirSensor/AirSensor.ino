/*
 * AirSensor.ino — ET-Bus WiFi Manager + Sensor Example
 *
 * First boot → creates "ETBus-AirSensor" WiFi AP
 * Connect from phone → enter WiFi, PSK, device name
 * Next boot → auto-connects
 *
 * Hold BOOT button during startup to reset.
 */

#include <WiFi.h>
#include <ArduinoJson.h>
#include <ETBusWiFiManager.h>
#include <ETBus.h>

ETBusWiFiManager wm;
ETBus etbus;

// Keep Strings alive so .c_str() pointers stay valid
String devName;
String psk;

#define RESET_PIN 0

static void publishState() {
    StaticJsonDocument<128> payload;
    payload["co2"] = random(400, 900);
    payload["temp"] = random(200, 300) / 10.0;
    payload["humidity"] = random(400, 700) / 10.0;

    Serial.print("[ETBUS] sending: ");
    serializeJson(payload, Serial);
    Serial.println();

    etbus.sendState(payload.as<JsonObject>());
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== ET-Bus Air Sensor ===");

    pinMode(RESET_PIN, INPUT_PULLUP);
    if (digitalRead(RESET_PIN) == LOW) {
        Serial.println("[BOOT] Reset held — clearing settings");
        wm.resetSettings();
    }

    wm.begin("ETBus-AirSensor");

    Serial.printf("[WIFI] Connected to: %s\n", wm.getSSID().c_str());
    Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
    WiFi.setSleep(false);

    // Store Strings so pointers stay valid
    devName = wm.getDevName();
    if (devName.length() == 0) devName = "air1";

    psk = wm.getPSK();

    if (psk.length() == 64) {
        if (etbus.enableEncryptionHex(psk.c_str())) {
            Serial.println("[ETBUS] Encrypted (key from portal)");
        }
    } else {
        Serial.println("[ETBUS] No PSK — running unencrypted");
    }

    etbus.begin(devName.c_str(), "sensor.air", "Air Sensor", "v1.7");
    etbus.onSync(publishState);
    publishState();
    Serial.println("[BOOT] READY\n");
}

void loop() {
    etbus.loop();

    // WiFi keep-alive
    static unsigned long lastWifiCheck = 0;
    if (millis() - lastWifiCheck > 5000) {
        lastWifiCheck = millis();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WIFI] lost — rebooting");
            ESP.restart();
        }
    }

    // Send fake sensor data every 2s
    static unsigned long lastSend = 0;
    if (millis() - lastSend > 2000) {
        lastSend = millis();
        publishState();
    }
}
