from __future__ import annotations

import asyncio
import json
import logging
import socket
import time
from typing import Any, Callable

from homeassistant.core import HomeAssistant

from .const import MULTICAST_GROUP, MULTICAST_PORT, PING_INTERVAL, OFFLINE_TIMEOUT

_LOGGER = logging.getLogger(__name__)

try:
    import psutil
except Exception:  # pragma: no cover
    psutil = None


def _list_ipv4_addrs() -> list[str]:
    """Return a best-effort list of local IPv4 addresses (excluding 127.0.0.1)."""
    if psutil is None:
        return []

    out: list[str] = []
    try:
        for _, infos in psutil.net_if_addrs().items():
            for info in infos:
                if info.family == socket.AF_INET:
                    ip = info.address
                    if ip and ip != "127.0.0.1":
                        out.append(ip)
    except Exception:
        return []

    seen: set[str] = set()
    deduped: list[str] = []
    for ip in out:
        if ip not in seen:
            seen.add(ip)
            deduped.append(ip)
    return deduped


class EtBusHub:
    """ET-Bus UDP hub.

    Design:
      - Multicast is used for discovery/ping only.
      - Commands are unicast once device IP is known.
      - Devices should unicast heartbeat/state to the hub once they learn hub IP.
    """

    def __init__(self, hass: HomeAssistant):
        self.hass = hass
        self._sock: socket.socket | None = None

        # dev_id -> info
        self._devices: dict[str, dict[str, Any]] = {}

        # message listeners (platforms)
        self._listeners: list[Callable[[dict[str, Any]], None]] = []

        self._tasks: list[asyncio.Task] = []

        self._rx_count = 0
        self._last_rx_any = 0.0
        self._last_rx_log = 0.0

        # prevent status spam
        self._last_status_fire: dict[str, float] = {}

    @property
    def devices(self) -> dict[str, dict[str, Any]]:
        # return a copy so platforms don’t mutate it accidentally
        return dict(self._devices)

    def register_listener(self, cb: Callable[[dict[str, Any]], None]) -> None:
        self._listeners.append(cb)

    def device_ready(self, dev_id: str) -> bool:
        info = self._devices.get(dev_id) or {}
        return bool(info.get("online", False)) and bool(info.get("last_addr"))

    # ---------------------------------------------------------------------
    # Socket (build / rebuild)
    # ---------------------------------------------------------------------
    def _build_socket(self) -> socket.socket:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)

        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except Exception:
            pass

        # Listen on all interfaces
        sock.bind(("", MULTICAST_PORT))

        # Join multicast group on 0.0.0.0 and all discovered IPv4 interfaces.
        joined = 0
        ips = _list_ipv4_addrs()

        def _join(if_ip: str) -> None:
            nonlocal joined
            try:
                mreq = socket.inet_aton(MULTICAST_GROUP) + socket.inet_aton(if_ip)
                sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
                joined += 1
            except OSError as e:
                # 98 = Address already in use (can happen on reload)
                if getattr(e, "errno", None) == 98:
                    return
                _LOGGER.warning("ET-Bus multicast join (%s) failed: %s", if_ip, e)
            except Exception as e:
                _LOGGER.warning("ET-Bus multicast join (%s) failed: %s", if_ip, e)

        _join("0.0.0.0")
        for ip in ips:
            _join(ip)

        # Don't loop our own multicast back if the OS supports it
        try:
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)
        except Exception:
            pass

        sock.setblocking(True)

        _LOGGER.info(
            "ET-Bus hub listening UDP *:%s (mcast %s) joins=%d ifaces=%s",
            MULTICAST_PORT,
            MULTICAST_GROUP,
            joined,
            ips,
        )
        return sock

    async def async_start(self) -> None:
        loop = asyncio.get_running_loop()
        self._sock = self._build_socket()
        self._last_rx_any = time.time()

        self._tasks.append(self.hass.loop.create_task(self._receiver(loop)))
        self._tasks.append(self.hass.loop.create_task(self._pinger()))
        self._tasks.append(self.hass.loop.create_task(self._rx_watchdog()))

    async def async_stop(self) -> None:
        for task in self._tasks:
            task.cancel()
        self._tasks.clear()

        if self._sock:
            try:
                self._sock.close()
            except Exception:
                pass
            self._sock = None

    # ---------------------------------------------------------------------
    # Send helpers
    # ---------------------------------------------------------------------
    def send(self, message: dict[str, Any]) -> None:
        """Send to multicast (discovery plane)."""
        self._sendto(message, MULTICAST_GROUP)

    def send_unicast(self, target_ip: str, message: dict[str, Any]) -> None:
        self._sendto(message, target_ip)

    def send_command(self, dev_id: str, dev_class: str, payload: dict[str, Any]) -> None:
        """Send a command, unicast if device IP known."""
        msg = {"v": 1, "type": "command", "id": dev_id, "class": dev_class, "payload": payload}
        info = self._devices.get(dev_id) or {}
        ip = info.get("last_addr")
        if ip:
            self._sendto(msg, ip)
        else:
            # Fallback: multicast (only works if Wi-Fi multicast is behaving)
            self._sendto(msg, MULTICAST_GROUP)

    def _sendto(self, message: dict[str, Any], dst_ip: str) -> None:
        if not self._sock:
            return
        try:
            data = json.dumps(message, separators=(",", ":")).encode("utf-8")
            self._sock.sendto(data, (dst_ip, MULTICAST_PORT))
        except Exception as e:
            _LOGGER.error("ET-Bus send error (%s): %s", dst_ip, e)

    # ---------------------------------------------------------------------
    # Receiver
    # ---------------------------------------------------------------------
    async def _receiver(self, loop) -> None:
        if not self._sock:
            return

        while True:
            try:
                data, addr = await loop.run_in_executor(None, self._sock.recvfrom, 8192)
            except asyncio.CancelledError:
                return
            except OSError as e:
                _LOGGER.error("ET-Bus recv error: %s", e)
                await asyncio.sleep(1)
                continue

            self._last_rx_any = time.time()
            self._rx_count += 1

            try:
                msg = json.loads(data.decode("utf-8"))
            except Exception:
                continue

            now = time.time()
            if now - self._last_rx_log > 10:
                self._last_rx_log = now
                _LOGGER.debug("ET-Bus RX ok: count=%d last_from=%s:%s", self._rx_count, addr[0], addr[1])

            self._update_registry(msg, addr)

            # For dashboard/panel/logging tools
            try:
                msg_with_meta = dict(msg)
                msg_with_meta["_src_ip"] = addr[0]
                msg_with_meta["_rx_ts"] = time.time()
                self.hass.bus.async_fire("etbus_message", msg_with_meta)
            except Exception:
                pass

            for cb in list(self._listeners):
                self.hass.add_job(cb, msg)

    # ---------------------------------------------------------------------
    # RX watchdog:
    # If absolutely nothing arrives, rebuild multicast socket.
    # With devices unicasting to HA after learning hub IP, this should be rare.
    # IMPORTANT: align with OFFLINE_TIMEOUT (don’t rebuild too aggressively)
    # ---------------------------------------------------------------------
    async def _rx_watchdog(self) -> None:
        while True:
            try:
                await asyncio.sleep(5)
            except asyncio.CancelledError:
                return

            # If no RX at all for 90s, recreate socket + rejoin groups.
            # (Was 60s before; too aggressive when pong=30s)
            if (time.time() - self._last_rx_any) < 90:
                continue

            _LOGGER.warning("ET-Bus RX stalled >90s, rebuilding multicast socket...")

            try:
                if self._sock:
                    self._sock.close()
            except Exception:
                pass

            self._sock = self._build_socket()
            self._last_rx_any = time.time()

    # ---------------------------------------------------------------------
    # Online/offline tracking + hub ping
    # ---------------------------------------------------------------------
    def _fire_device_status(self, dev_id: str, reason: str) -> None:
        now = time.time()
        last = float(self._last_status_fire.get(dev_id, 0.0))
        if (now - last) < 0.75:
            return
        self._last_status_fire[dev_id] = now

        info = self._devices.get(dev_id) or {}
        try:
            self.hass.bus.async_fire(
                "etbus_device_status",
                {
                    "id": dev_id,
                    "online": bool(info.get("online", False)),
                    "last_seen": float(info.get("last_seen", 0.0)),
                    "last_addr": info.get("last_addr"),
                    "reason": reason,
                },
            )
        except Exception:
            pass

    async def _pinger(self) -> None:
        while True:
            try:
                await asyncio.sleep(PING_INTERVAL)
            except asyncio.CancelledError:
                return

            # Multicast ping helps devices learn hub IP if they listen for it.
            self.send({"v": 1, "type": "ping", "id": "hub", "class": "hub", "payload": {"ts": int(time.time())}})

            now = time.time()
            for dev_id, info in list(self._devices.items()):
                last_seen = float(info.get("last_seen", 0))
                was_online = bool(info.get("online", False))
                is_online = (now - last_seen) < OFFLINE_TIMEOUT

                if is_online != was_online:
                    info["online"] = is_online
                    state = "online" if is_online else "offline"
                    _LOGGER.warning("ET-Bus device %s is now %s", dev_id, state)
                    self._fire_device_status(dev_id, reason=state)

    # ---------------------------------------------------------------------
    # Registry updates (device IP learning happens here)
    # ---------------------------------------------------------------------
    def _update_registry(self, msg: dict[str, Any], addr) -> None:
        if msg.get("v") != 1:
            return

        dev_id = msg.get("id")
        if not dev_id or dev_id == "hub":
            return

        dev_class = msg.get("class")
        mtype = msg.get("type")
        payload = msg.get("payload", {}) or {}

        now = time.time()
        dev = self._devices.get(dev_id)

        is_new = False
        came_online = False

        if not dev:
            is_new = True
            dev = {
                "id": dev_id,
                "class": dev_class,
                "name": payload.get("name", dev_id),
                "fw": payload.get("fw"),
                "last_addr": addr[0],
                "last_seen": now,
                "online": True,
            }
            self._devices[dev_id] = dev
            _LOGGER.info("ET-Bus new device: %s (%s) from %s", dev_id, dev_class, addr[0])
        else:
            prev_online = bool(dev.get("online", True))
            if dev_class:
                dev["class"] = dev_class
            if "name" in payload:
                dev["name"] = payload.get("name", dev.get("name", dev_id))
            if "fw" in payload:
                dev["fw"] = payload.get("fw")

            dev["last_addr"] = addr[0]
            dev["last_seen"] = now

            if not prev_online:
                dev["online"] = True
                came_online = True

        # Store common health fields when present
        if mtype == "pong" and isinstance(payload, dict):
            if "uptime" in payload:
                dev["uptime"] = payload["uptime"]
            if "rssi" in payload:
                dev["rssi"] = payload["rssi"]

        # ✅ IMPORTANT FIX:
        # Only fire device_status on REAL transitions/new devices.
        # DO NOT fire status events on every pong/discover (that creates spam/thrashing)
        if is_new:
            self._fire_device_status(dev_id, reason="new")
        elif came_online:
            self._fire_device_status(dev_id, reason="online")
        # (offline handled in _pinger via OFFLINE_TIMEOUT)
