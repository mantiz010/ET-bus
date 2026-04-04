# ETBusWiFiManager

Branded WiFi + Encryption Key captive portal for ET-Bus ESP32 devices.

## Features

- **Branded dark UI** with ET-Bus logo (dark blue ET, white Bus)
- **WiFi scanning** with signal strength bars and encryption indicators
- **ChaCha20-Poly1305 PSK** configuration (64-char hex key)
- **Device naming** — set device ID from the portal
- **NVS persistence** — saves WiFi, PSK, and device name to flash
- **Captive portal** — auto-opens on Android, iOS, and Windows
- **Auto-reconnect** — connects to saved WiFi on boot, falls back to portal
- **Portal timeout** — reboots after configurable timeout (default 180s)
- **Reset button** — hold BOOT to clear settings and force portal

## Installation

Copy the `ETBusWiFiManager` folder to your Arduino libraries directory:

```
Arduino/libraries/ETBusWiFiManager/
├── library.properties
├── README.md
├── src/
│   ├── ETBusWiFiManager.h
│   └── ETBusWiFiManager.cpp
└── examples/
    └── BasicRelay/
        └── BasicRelay.ino
```

Or in PlatformIO, place it in your `lib/` folder.

## Usage

```cpp
#include <ETBusWiFiManager.h>
#include <ETBus.h>

ETBusWiFiManager wm;
ETBus etbus;

// IMPORTANT: keep these as global/class Strings so .c_str() stays valid
String devName;
String psk;

void setup() {
    Serial.begin(115200);

    // Hold BOOT button to force portal
    pinMode(0, INPUT_PULLUP);
    if (digitalRead(0) == LOW) wm.resetSettings();

    wm.begin("ETBus-Relay");  // blocks until WiFi connected

    // Store Strings BEFORE calling .c_str()
    // (getDevName() returns a temporary — pointer dies immediately)
    devName = wm.getDevName();
    if (devName.length() == 0) devName = "relay_board";

    psk = wm.getPSK();

    etbus.begin(devName.c_str(), "switch.multi", "Relay", "v1.0");

    if (psk.length() == 64) {
        etbus.enableEncryptionHex(psk.c_str());
    }
}

void loop() {
    etbus.loop();
}
```

> **⚠️ Common mistake:** Do NOT do `etbus.begin(wm.getDevName().c_str(), ...)` — the
> temporary String is destroyed before `begin()` reads the pointer. Always store it
> in a `String` variable first.

## API

| Method | Returns | Description |
|--------|---------|-------------|
| `begin(apName)` | `void` | Connect to saved WiFi or open portal. Blocks. |
| `getSSID()` | `String` | Saved WiFi SSID |
| `getPassword()` | `String` | Saved WiFi password |
| `getPSK()` | `String` | 64-char hex encryption key |
| `getDevName()` | `String` | Device name |
| `resetSettings()` | `void` | Clear all saved config from NVS |
| `isPortalActive()` | `bool` | True if portal is currently running |

## Configuration

| Property | Default | Description |
|----------|---------|-------------|
| `portalTimeout` | `180` | Seconds before portal times out and reboots |
| `apPassword` | `""` | AP password (empty = open, must be 8+ chars if set) |

## Portal Fields

1. **WiFi** — SSID (with scan list) + password
2. **Encryption** — 64-char hex PSK (validated client-side and server-side)
3. **Device** — Device name for ET-Bus identification
