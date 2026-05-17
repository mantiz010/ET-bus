#pragma once

// Copy this file to secrets.h inside the sketch folder you are flashing.
// Do not commit secrets.h.

#define WIFI_SSID "CHANGE_ME"
#define WIFI_PASS "CHANGE_ME"

// 64 hex characters. Must match the ET-Bus PSK configured in Home Assistant.
// Leave empty only for plaintext bench testing.
#define ETBUS_PSK_HEX ""
