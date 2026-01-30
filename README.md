# ET-Bus (ElectronicsTech Bus)

ET-Bus is a **local-first, real-time device bus** designed for ESP32-based automation systems and Home Assistant.

It is built for **speed, reliability, and deterministic control** on a local LAN — without MQTT brokers, cloud dependencies, or retained message complexity.

> ET-Bus is not a replacement for MQTT or Zigbee.
> It is a complementary protocol for **always-on, mains-powered, low-latency devices**.

---

## Why ET-Bus Exists

Home Assistant users often hit real-world problems with:
- MQTT broker failures stopping all automation
- retained message state desync
- delayed Zigbee or Wi-Fi response times
- devices not recovering cleanly after restarts

ET-Bus was created to solve those problems by shifting to a **device-centric, UDP-based model**.

---

## Key Features

- ⚡ **Ultra-low latency** (UDP on LAN)
- 🔌 **No broker required**
- 🧠 **Devices own their state**
- 🔁 **Automatic resync after reconnect**
- 📡 **Multicast discovery**
- 🔐 **Local-only by default**
- 🛠 **Human-readable JSON (debuggable in Wireshark)**
- 🏠 **Designed specifically for Home Assistant**

---

## What ET-Bus Is (and Is Not)

### ET-Bus is good for:
- Relays and switches
- RGB lighting (WS2812, PWM)
- Fans and motors
- Pumps and valves
- Industrial / hydroponics controllers
- Server rack and homelab control

### ET-Bus is NOT good for:
- Battery-powered sensors
- Sleepy devices
- Cloud-first IoT
- Internet-scale messaging

Use Zigbee / LoRa / BLE for battery sensors.
Use ET-Bus for **fast, reliable control**.

---

## Architecture Overview

- Devices communicate with Home Assistant using **UDP**
- Discovery and health checks use **multicast**
- Commands use **unicast** once device IP is known
- Devices periodically send **pong** heartbeats
- Home Assistant tracks online/offline state

No retained messages.
No broker.
No single point of failure.

---

## Protocol Summary

All messages are JSON over UDP.

Common fields:
```json
{
  "v": 1,
  "type": "state | command | discover | pong | ping",
  "id": "device_id",
  "class": "light.rgb | switch.relay | fan.speed",
  "payload": {}
}
arduino/ETBus/
