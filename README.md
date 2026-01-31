# ET-Bus (ElectronicsTech Bus)

ET-Bus is a **local-first, real-time device bus** designed for **ESP32-based automation systems** and **Home Assistant**.

It is built for **speed, reliability, and deterministic control** on a local LAN — **without MQTT brokers**, **without cloud dependencies**, and **without retained-message complexity**.

ET-Bus is **not a replacement** for MQTT, Zigbee, or ESPHome.  
It is a **complementary protocol** for **always-on, mains-powered, low-latency devices**.

---

## Why ET-Bus Exists

Home Assistant users regularly encounter real-world problems such as:

- MQTT broker failures stopping all automation
- Retained messages causing **state desynchronisation**
- Devices appearing online when physically offline
- Slow or unpredictable command response
- Devices failing to recover after Home Assistant restarts

These are architectural issues, not misconfiguration.

ET-Bus was created to eliminate these failure modes by moving to a **device-owned, UDP-based state model**.

---

## Key Features

- Ultra-low latency (UDP on LAN)
- No broker required
- Device-owned state (no retained lies)
- Automatic resync after reconnect
- Multicast discovery
- Local-only by default
- Human-readable JSON (Wireshark friendly)
- Designed specifically for Home Assistant

---

## What ET-Bus Is (and Is Not)

### Designed for

- Relays and switches
- WS2812 / RGB lighting
- Fans and motors
- Pumps and valves
- Industrial & hydroponics controllers
- Server rack & homelab automation

### Not designed for

- Battery-powered sensors
- Sleepy devices
- Cloud-first IoT
- Internet-scale messaging

Use Zigbee / LoRa / BLE for battery devices.  
Use ET-Bus for **fast, deterministic, always-on control**.

---

## Architecture Overview

- Devices communicate with Home Assistant using **UDP**
- Multicast is used for discovery and health checks
- Unicast is used for commands once the device IP is known
- Devices send periodic **pong heartbeats**
- Home Assistant tracks **true online/offline state**

No broker.  
No retained messages.  
No single point of failure.

---

## Protocol Summary

All messages are **JSON over UDP**.

### Common message envelope

```json
{
  "v": 1,
  "type": "state | command | discover | ping | pong",
  "id": "device_id",
  "class": "switch.relay | light.rgb | fan.speed",
  "payload": {}
}
```

### Message Types

| Type     | Purpose |
|---------|---------|
| discover | Device announces itself |
| state    | Device publishes current state |
| command  | Home Assistant sends a command |
| ping     | Health check from Home Assistant |
| pong     | Device heartbeat |

---

## Device Lifecycle

1. Device boots
2. Sends `discover` (multicast)
3. Home Assistant registers device
4. Device sends initial `state`
5. Device sends periodic `pong`
6. Home Assistant marks device online/offline

If Home Assistant restarts:

- Devices re-announce automatically
- State is rebuilt from live devices
- No stale or ghost entities remain

---

## Command Flow

```
Home Assistant UI
        ↓
     command
        ↓
     ESP32 device
        ↓
   apply action
        ↓
   state confirmation
        ↓
 Home Assistant updates entity
```

State is **never assumed** — only confirmed.

---

## Reliability Model

ET-Bus follows an **industrial reliability model**:

- No heartbeat → offline
- No device → no state
- No confirmation → no assumption

Explicit failure is preferred over silent failure.

---

## Why ET-Bus Avoids Retained Messages

### MQTT retained failure

- Broker retains `relay = ON`
- Device loses power
- Home Assistant still shows `ON`
- Automations behave incorrectly

### ET-Bus behaviour

- Device loses power
- Heartbeats stop
- Device marked offline
- State becomes unknown

ET-Bus refuses to lie about physical reality.

---

## Failure Scenarios

### MQTT Broker Restart

- Broker goes down
- Devices disconnect
- Home Assistant shows old retained state
- Automations misfire

### ET-Bus Controller Restart

- Home Assistant restarts
- Devices re-announce
- State rebuilt automatically
- System stabilises cleanly

---

## Security Model

ET-Bus is designed for trusted LANs and supports optional encryption.

- ChaCha20-Poly1305 authenticated encryption
- Pre-shared keys
- Key ID support for rotation

ET-Bus does **not** replace VPNs or firewalls.  
For remote access, use a VPN and keep ET-Bus local.

---

## ESP32 Example Behaviour

### Relay

```cpp
void onCommand(JsonObject payload) {
  if (payload.containsKey("on")) {
    bool on = payload["on"];
    digitalWrite(RELAY_PIN, on ? HIGH : LOW);
    sendState(on);
  }
}
```

### WS2812 / RGB Light

```cpp
void onCommand(JsonObject payload) {
  setColor(payload["r"], payload["g"], payload["b"]);
  setBrightness(payload["brightness"]);
  sendState();
}
```

### Fan Speed Control

```cpp
void onCommand(JsonObject payload) {
  setFanSpeed(payload["speed"]); // 0–100
  sendState();
}
```

---

## ESPHome vs MQTT vs ET-Bus

### ESPHome

**Strengths**
- YAML-based configuration
- Native Home Assistant API
- OTA updates

**Limitations**
- Firmware-centric
- Less suitable for complex distributed systems

---

### MQTT

**Strengths**
- Huge ecosystem
- Flexible pub/sub model
- Cross-platform support

**Limitations**
- Requires a broker
- Retained message state issues
- Single point of failure

---

### ET-Bus

**Strengths**
- No broker
- No retained state
- Deterministic latency
- True device presence tracking
- Automatic recovery after restarts

**Trade-offs**
- LAN-only by design
- Always-on devices only

---

## Comparison Table

| Feature | ESPHome | MQTT | ET-Bus |
|------|--------|------|-------|
| Broker required | No | Yes | No |
| Local-first | Yes | Optional | Yes |
| Deterministic latency | Medium | Low | High |
| Retained state risk | No | Yes | No |
| Device-owned state | Partial | No | Yes |
| Best for battery devices | Sometimes | Sometimes | No |
| Best for always-on control | Yes | Yes | **Yes (best)** |

---

## Networking Notes

- UDP must be allowed through firewalls
- Multicast must be permitted on the LAN
- VLANs must pass multicast or use unicast mode

ET-Bus is intentionally LAN-first.

---

## Troubleshooting

### Devices appear then disappear

- Heartbeat interval too long
- Home Assistant timeout too short
- Multicast blocked

**Fix**
- Reduce heartbeat interval
- Increase HA timeout
- Verify multicast routing

### Commands sent but nothing happens

- Device does not parse command
- Device does not send state confirmation
- ID or class mismatch

**Fix**
- Log received packets on device
- Confirm state is sent after applying command

---

## Roadmap

- Protocol versioning rules
- Device profile definitions
- WS2812 effect schema
- Encryption configuration UI
- Robust reconnect logic
- Rate limiting
- Stable device registry
- HACS support
- Example ESP32 firmwares

---

## Contributing

Contributions should prioritise:

- Correctness over convenience
- Explicit failure over hidden failure
- Simple, inspectable protocols

---

## License

MIT License

---

## Author

**ElectronicsTech**  
Built for real-world automation — not demos.

