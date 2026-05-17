# ET-Bus Examples 1.7

Known-good ET-Bus sketches for Home Assistant ET-Bus 1.7 with optional encryption.

## Setup

For each sketch folder:

1. Copy `secrets.example.h` to `secrets.h`.
2. Set `WIFI_SSID`, `WIFI_PASS`, and `ETBUS_PSK_HEX`.
3. Flash one device first.
4. Check Home Assistant logs for `ETBUS REPLAY`, `ETBUS ENC FAIL`, or `ETBUS DEC FAIL`.

`secrets.h` is git-ignored so real credentials do not get uploaded.

## Sketches

- `AirSensor_KnownGood`: encrypted fake air sensor state for live update testing with `onSync()` state refresh.
- `RelayBoard4_KnownGood`: encrypted 4-relay board with HA command handling, ack/error replies, sync discovery, and state reporting.
- `RGBLed_KnownGood`: encrypted WS2812 RGB light with effects, brightness, state reporting, and `onSync()` refresh. Requires the `FastLED` library.
- `WS2812_Main_KnownGood`: the main WS2812/WSB FastLED sketch using device ID `RGB1` with `onSync()` refresh.

## Notes

Encryption is enabled before `ETBus::begin()` when a 64-character PSK is provided. Discovery, ping, pong, ack, and error remain plaintext by protocol; state and command payloads are encrypted.

ETBus 1.7 adds boot IDs, packet sequence numbers, rate-limited rediscovery on ping, and optional `onSync()` callbacks. Examples use `onSync()` so Home Assistant can request current state without replaying old commands.

If `ETBUS_PSK_HEX` is empty, the sketch runs in plaintext for isolated testing only.
