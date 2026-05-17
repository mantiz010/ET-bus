# ET-Bus Examples 1.05

Known-good ET-Bus sketches for Home Assistant ET-Bus with optional encryption.

## Setup

For each sketch folder:

1. Copy `secrets.example.h` to `secrets.h`.
2. Set `WIFI_SSID`, `WIFI_PASS`, and `ETBUS_PSK_HEX`.
3. Flash one device first.
4. Check Home Assistant logs for `ETBUS REPLAY`, `ETBUS ENC FAIL`, or `ETBUS DEC FAIL`.

`secrets.h` is git-ignored so real credentials do not get uploaded.

## Sketches

- `AirSensor_KnownGood`: encrypted fake air sensor state for live update testing.
- `RelayBoard4_KnownGood`: encrypted 4-relay board with HA command handling and state reporting.
- `RGBLed_KnownGood`: encrypted WS2812 RGB light with effects, brightness, and state reporting. Requires the `FastLED` library.

## Notes

Encryption is enabled before `ETBus::begin()` when a 64-character PSK is provided. Discovery and pong remain plaintext by protocol; state and command payloads are encrypted.

If `ETBUS_PSK_HEX` is empty, the sketch runs in plaintext for isolated testing only.
