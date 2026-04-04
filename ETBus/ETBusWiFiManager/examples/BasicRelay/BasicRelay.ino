/*
 * BasicRelay.ino — ET-Bus WiFi Manager Example
 *
 * First boot → creates "ETBus-Relay" WiFi AP
 * Connect from phone → branded portal auto-opens
 * Enter WiFi + encryption key + device name → saves to NVS
 * Next boot → auto-connects, no portal
 *
 * Hold BOOT button during startup to reset and force portal.
 */

#include <ETBusWiFiManager.h>
#include <ETBus.h>

ETBusWiFiManager wm;
ETBus etbus;

// Keep Strings alive globally so .c_str() pointers stay valid
String devName;
String psk;

#define RESET_PIN 0  // BOOT button on most ESP32 boards

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== ET-Bus Relay Board ===");

    // Hold BOOT button at startup → clear saved config, force portal
    pinMode(RESET_PIN, INPUT_PULLUP);
    if (digitalRead(RESET_PIN) == LOW) {
        Serial.println("[BOOT] Reset held — clearing settings");
        wm.resetSettings();
    }

    // Optional: set portal AP password (8+ chars) or leave empty for open
    // wm.apPassword = "etbus1234";

    // Optional: portal timeout (default 180s, then reboots)
    // wm.portalTimeout = 300;

    // Blocks until WiFi connected. Opens portal if no saved creds.
    wm.begin("ETBus-Relay");

    // ── WiFi is now connected ──
    Serial.printf("[WIFI] SSID: %s\n", wm.getSSID().c_str());
    Serial.printf("[WIFI] IP:   %s\n", WiFi.localIP().toString().c_str());

    // Store Strings so .c_str() pointers stay valid
    devName = wm.getDevName();
    if (devName.length() == 0) devName = "relay_board";

    psk = wm.getPSK();

    etbus.begin(devName.c_str(), "switch.multi", "4 Relay Board", "v2.0");

    // Encryption key from portal
    if (psk.length() == 64) {
        if (etbus.enableEncryptionHex(psk.c_str())) {
            Serial.println("[ETBUS] Encrypted (key from portal)");
        }
    } else {
        Serial.println("[ETBUS] No PSK — running unencrypted");
    }

    Serial.println("[BOOT] READY\n");
}

void loop() {
    etbus.loop();
    delay(0);
}
