from __future__ import annotations

import asyncio
import base64
import hashlib
import json
import logging
import socket
import time
from typing import Any, Callable

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import EVENT_HOMEASSISTANT_STOP
from homeassistant.core import HomeAssistant
from homeassistant.helpers.storage import Store

from .const import (
    CONF_CRYPTO_ENABLED,
    CONF_PORT,
    CONF_PSK_HEX,
    DEFAULT_HOST_MCAST,
    DEFAULT_PORT,
    ETBUS_KID,
    OFFLINE_TIMEOUT,
    PING_INTERVAL,
)

_LOGGER = logging.getLogger(__name__)

STORAGE_KEY = "etbus_last_commands"
STORAGE_VERSION = 1
STORAGE_KEY_DEVICE_STATE = "etbus_device_states"

try:
    from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
except Exception:  # pragma: no cover
    ChaCha20Poly1305 = None


def _now() -> float:
    return time.time()


def _hex32_to_bytes(psk_hex: str) -> bytes | None:
    if not psk_hex:
        return None
    s = "".join(ch for ch in psk_hex.strip().lower() if ch in "0123456789abcdef")
    if len(s) != 64:
        return None
    try:
        return bytes.fromhex(s)
    except Exception:
        return None


def _b64e(b: bytes) -> str:
    return base64.b64encode(b).decode("ascii")


def _b64d(s: str) -> bytes:
    return base64.b64decode(s.encode("ascii"))


def _u64_le(n: int) -> bytes:
    return int(n).to_bytes(8, "little", signed=False)


class EtBusHub:
    """ET-Bus hub (NO MAC mode): key = sha256(PSK || device_id), AAD=None"""

    def __init__(self, hass: HomeAssistant, entry: ConfigEntry) -> None:
        self.hass = hass
        self.entry = entry

        opts = dict(entry.options)
        self.port: int = int(opts.get(CONF_PORT, DEFAULT_PORT))
        self.crypto_enabled: bool = bool(opts.get(CONF_CRYPTO_ENABLED, False))
        self.psk_hex: str = str(opts.get(CONF_PSK_HEX, "") or "")
        self.master_secret: bytes | None = _hex32_to_bytes(self.psk_hex)

        if self.crypto_enabled and (ChaCha20Poly1305 is None):
            _LOGGER.error("ET-Bus crypto enabled but cryptography is missing")
            self.crypto_enabled = False

        if self.crypto_enabled and not self.master_secret:
            _LOGGER.error("ET-Bus crypto enabled but psk_hex is invalid (needs 64 hex chars)")
            self.crypto_enabled = False

        self.hub_id: str = "hub"

        self.devices: dict[str, dict[str, Any]] = {}
        self._listeners: list[Callable[[dict[str, Any]], None]] = []

        self._sock: socket.socket | None = None
        self._task: asyncio.Task | None = None
        self._ping_task: asyncio.Task | None = None

        # last command per device — persisted to disk for reference
        self._last_command: dict[str, dict[str, Any]] = {}
        self._store = Store(hass, STORAGE_VERSION, STORAGE_KEY)

        # last REPORTED state from each device — persisted so HA entities
        # can restore their state on HA reboot WITHOUT sending commands
        self._last_reported_state: dict[str, dict[str, Any]] = {}
        self._state_store = Store(hass, STORAGE_VERSION, STORAGE_KEY_DEVICE_STATE)

        # per-device tx ctr for commands
        self._tx_ctr: dict[str, int] = {}
        # anti-replay for incoming encrypted STATE
        self._rx_state_last_ctr: dict[str, int] = {}

        self._hub_start_time = int(time.time())

        _LOGGER.info("ET-Bus hub init port=%s crypto=%s startup_time=%s",
                     self.port, self.crypto_enabled, self._hub_start_time)

    def register_listener(self, cb: Callable[[dict[str, Any]], None]) -> None:
        self._listeners.append(cb)

    async def async_start(self) -> None:
        # Load persisted data from disk before anything else
        await self._load_last_commands()
        await self._load_device_states()

        self._open_socket()
        self._task = asyncio.create_task(self._rx_loop())
        self._ping_task = asyncio.create_task(self._ping_loop())
        self.hass.bus.async_listen_once(EVENT_HOMEASSISTANT_STOP, self._on_stop)

        # Send startup ping (devices will re-announce)
        await asyncio.sleep(2)
        self._send_startup_ping()

    async def _on_stop(self, _ev) -> None:
        # Persist before shutting down
        await self._save_last_commands()
        await self._save_device_states()
        await self.async_stop()

    async def async_stop(self) -> None:
        for t in (self._ping_task, self._task):
            if t:
                t.cancel()
        self._ping_task = None
        self._task = None
        if self._sock:
            try:
                self._sock.close()
            except Exception:
                pass
        self._sock = None

    # ── Persistent storage ───────────────────────────────────────────────

    async def _load_last_commands(self) -> None:
        """Load persisted last-command map from disk."""
        try:
            data = await self._store.async_load()
            if isinstance(data, dict):
                self._last_command = data
                _LOGGER.info("✅ ETBUS: Loaded %d persisted device commands", len(data))
            else:
                self._last_command = {}
        except Exception:
            _LOGGER.exception("ET-Bus: failed to load persisted commands")
            self._last_command = {}

    async def _save_last_commands(self) -> None:
        """Persist the last-command map to disk."""
        try:
            await self._store.async_save(self._last_command)
        except Exception:
            _LOGGER.exception("ET-Bus: failed to persist commands")

    def _schedule_save(self) -> None:
        """Fire-and-forget save (non-blocking from sync context)."""
        asyncio.ensure_future(self._save_last_commands())

    async def _load_device_states(self) -> None:
        """Load persisted device-reported states from disk."""
        try:
            data = await self._state_store.async_load()
            if isinstance(data, dict):
                self._last_reported_state = data
                _LOGGER.info("✅ ETBUS: Loaded %d persisted device states", len(data))
            else:
                self._last_reported_state = {}
        except Exception:
            _LOGGER.exception("ET-Bus: failed to load persisted device states")
            self._last_reported_state = {}

    async def _save_device_states(self) -> None:
        """Persist the last-reported-state map to disk."""
        try:
            await self._state_store.async_save(self._last_reported_state)
        except Exception:
            _LOGGER.exception("ET-Bus: failed to persist device states")

    def _schedule_save_states(self) -> None:
        """Fire-and-forget save of device states."""
        asyncio.ensure_future(self._save_device_states())

    def get_last_reported_state(self, dev_id: str) -> dict[str, Any] | None:
        """Get the last reported state for a device (for entity restoration)."""
        return self._last_reported_state.get(dev_id)

    # ── Socket ───────────────────────────────────────────────────────────

    def _open_socket(self) -> None:
        if self._sock:
            return

        mcast_ip = DEFAULT_HOST_MCAST
        port = int(self.port)

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

        try:
            sock.bind(("", port))
        except Exception:
            sock.bind(("0.0.0.0", port))

        mreq = socket.inet_aton(mcast_ip) + socket.inet_aton("0.0.0.0")
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

        sock.setblocking(False)
        self._sock = sock
        _LOGGER.info("ET-Bus UDP bound %s:%s", mcast_ip, port)

    def _derive_key_for_dev(self, dev_id: str) -> bytes | None:
        if not self.crypto_enabled or not self.master_secret:
            return None
        return hashlib.sha256(self.master_secret + dev_id.encode("utf-8")).digest()

    def _nonce_cmd(self, ctr: int) -> bytes:
        return b"\x00\x00\x00\x00" + _u64_le(ctr)

    # ── RX loop ──────────────────────────────────────────────────────────

    async def _rx_loop(self) -> None:
        assert self._sock is not None
        loop = asyncio.get_running_loop()

        while True:
            try:
                data, (src_ip, _src_port) = await loop.sock_recvfrom(self._sock, 8192)
            except asyncio.CancelledError:
                return
            except Exception:
                await asyncio.sleep(0.05)
                continue

            rx_ts = _now()

            try:
                msg = json.loads(data.decode("utf-8", errors="strict"))
            except Exception:
                continue

            v = int(msg.get("v", 0) or 0)
            mtype = str(msg.get("type", "") or "")
            dev_id = str(msg.get("id", "") or "")
            payload = msg.get("payload") or {}

            if v != 1 or not mtype or not dev_id:
                continue

            if dev_id != self.hub_id:
                self._touch_device(dev_id, src_ip, mtype)

            was_encrypted = (self.crypto_enabled and isinstance(payload, dict) and payload.get("_enc") == 1)

            # Decrypt incoming encrypted STATE (device -> hub)
            if was_encrypted:
                plain = self._decrypt_wrapper_state(dev_id=dev_id, wrapper=payload, src_ip=src_ip)
                if plain is None:
                    continue
                msg["payload"] = plain

            # Web panel event
            self.hass.bus.async_fire("etbus_message", {
                "id": dev_id,
                "type": mtype,
                "class": msg.get("class", ""),
                "payload": msg.get("payload", {}),
                "_src_ip": src_ip,
                "_rx_ts": rx_ts,
                "_encrypted": was_encrypted,
            })

            # Persist device-reported state so HA entities can restore
            # their state on HA reboot without sending commands.
            # IMPORTANT: skip discovery-format payloads where "switches"
            # is a list of dicts (e.g. [{"id":"1","name":"Relay 1"},...]).
            # Only persist real state where "switches" is a dict
            # (e.g. {"1": true, "2": false}).
            if mtype == "state" and dev_id != self.hub_id:
                reported = msg.get("payload")
                if isinstance(reported, dict) and reported:
                    # Filter: if "switches" exists and is a list, this is
                    # a discovery payload sent via sendState — don't persist
                    sw = reported.get("switches")
                    if isinstance(sw, list):
                        _LOGGER.debug(
                            "ET-Bus: skipping discovery-format state persistence for %s",
                            dev_id,
                        )
                    else:
                        self._last_reported_state[dev_id] = {
                            "dev_class": str(msg.get("class", "")),
                            "payload": reported,
                            "ts": rx_ts,
                        }
                        self._schedule_save_states()

            for cb in list(self._listeners):
                try:
                    cb(msg)
                except Exception:
                    _LOGGER.exception("ET-Bus listener error")

            if dev_id != self.hub_id:
                self.hass.bus.async_fire("etbus_device_status", {
                    "id": dev_id,
                    "online": True,
                    "reason": mtype,
                    "ip": src_ip
                })

    def _touch_device(self, dev_id: str, ip: str, mtype: str = "") -> None:
        d = self.devices.setdefault(dev_id, {})
        d["ip"] = ip
        d["last_seen"] = _now()
        d["online"] = True

        # NOTE: We do NOT resend commands on HA reboot.
        # The device has its own NVS persistence and will report
        # its current state via pong/discover/state messages.
        # HA entities will update from those state reports.

    def _decrypt_wrapper_state(
        self, *, dev_id: str, wrapper: dict[str, Any], src_ip: str
    ) -> dict[str, Any] | None:
        if not self.crypto_enabled or not self.master_secret or ChaCha20Poly1305 is None:
            return None

        expected_ip = (self.devices.get(dev_id) or {}).get("ip")
        if expected_ip and expected_ip != src_ip:
            _LOGGER.warning("ETBUS DROP rx_state dev=%s from ip=%s (expected %s)", dev_id, src_ip, expected_ip)
            return None

        kid = int(wrapper.get("kid") or 0)
        ctr = int(wrapper.get("ctr") or 0)

        if kid != int(ETBUS_KID) or ctr < 0:
            return None

        last = int(self._rx_state_last_ctr.get(dev_id, 0))

        # Device reboot detection (ctr reset)
        if ctr <= last:
            drop = last - ctr
            # Accept reset only for very low ctr, or huge drop
            if ctr in (0, 1) or ctr < 30 or drop >= 50:
                _LOGGER.warning("✅ ETBUS DEVICE REBOOT: dev=%s ctr=%s last=%s drop=%s", dev_id, ctr, last, drop)
                # Device has NVS persistence — it restores its own state.
                # We just accept the counter reset and let the state report through.
            else:
                _LOGGER.warning("❌ ETBUS REPLAY: dev=%s ctr=%s last=%s drop=%s", dev_id, ctr, last, drop)
                return None

        nonce_b64 = wrapper.get("nonce")
        ct_b64 = wrapper.get("ct")
        tag_b64 = wrapper.get("tag")
        if not (nonce_b64 and ct_b64 and tag_b64):
            return None

        try:
            nonce = _b64d(str(nonce_b64))
            ct = _b64d(str(ct_b64))
            tag = _b64d(str(tag_b64))
        except Exception:
            return None

        if len(nonce) != 12 or len(tag) != 16 or len(ct) == 0:
            return None

        key = self._derive_key_for_dev(dev_id)
        if not key:
            return None

        try:
            aead = ChaCha20Poly1305(key)
            pt = aead.decrypt(nonce, ct + tag, None)
            plain = json.loads(pt.decode("utf-8", errors="strict"))
            if isinstance(plain, dict):
                self._rx_state_last_ctr[dev_id] = ctr
                return plain
        except Exception as e:
            _LOGGER.error("❌ ETBUS DEC FAIL rx_state dev=%s ctr=%s err=%r", dev_id, ctr, e)
            return None

        return None

    def send_command(self, dev_id: str, dev_class: str, payload: dict[str, Any], *, store_last: bool = True) -> None:
        info = self.devices.get(dev_id) or {}
        ip = info.get("ip")
        if not ip:
            _LOGGER.warning("ET-Bus: no IP for %s", dev_id)
            return

        if store_last:
            self._last_command[dev_id] = {
                "dev_class": dev_class,
                "payload": payload.copy() if payload else {}
            }
            # Persist to disk so we survive HA restarts
            self._schedule_save()

        msg: dict[str, Any] = {
            "v": 1,
            "type": "command",
            "id": self.hub_id,
            "class": dev_class,
            "payload": payload or {},
        }

        if self.crypto_enabled:
            wrapper = self._encrypt_command(dev_id=dev_id, plain=payload or {})
            if wrapper is None:
                return
            msg["payload"] = wrapper

        self._udp_send(ip, self.port, msg)

    def _encrypt_command(self, *, dev_id: str, plain: dict[str, Any]) -> dict[str, Any] | None:
        if not self.crypto_enabled or not self.master_secret or ChaCha20Poly1305 is None:
            return None

        key = self._derive_key_for_dev(dev_id)
        if not key:
            return None

        ctr = int(self._tx_ctr.get(dev_id, 0) + 1)
        self._tx_ctr[dev_id] = ctr

        nonce = self._nonce_cmd(ctr)
        pt = json.dumps(plain, separators=(",", ":"), ensure_ascii=False).encode("utf-8")

        try:
            aead = ChaCha20Poly1305(key)
            out = aead.encrypt(nonce, pt, None)
            ct, tag = out[:-16], out[-16:]
        except Exception as e:
            _LOGGER.warning("ETBUS ENC FAIL dev=%s ctr=%s err=%r", dev_id, ctr, e)
            return None

        return {
            "_enc": 1,
            "kid": int(ETBUS_KID),
            "ctr": int(ctr),
            "nonce": _b64e(nonce),
            "ct": _b64e(ct),
            "tag": _b64e(tag),
        }

    async def _ping_loop(self) -> None:
        while True:
            await asyncio.sleep(float(PING_INTERVAL))

            now = _now()
            for dev_id, info in list(self.devices.items()):
                last = float(info.get("last_seen", 0.0) or 0.0)
                online = bool(info.get("online", True))
                if online and last and (now - last) > float(OFFLINE_TIMEOUT):
                    info["online"] = False
                    self.hass.bus.async_fire("etbus_device_status", {
                        "id": dev_id,
                        "online": False,
                        "reason": "offline"
                    })

            self._send_ping_multicast()

    def _send_startup_ping(self) -> None:
        msg = {
            "v": 1,
            "type": "ping",
            "id": self.hub_id,
            "class": "hub",
            "payload": {
                "port": int(self.port),
                "ts": self._hub_start_time,
                "startup": True,
            },
        }
        self._udp_send(DEFAULT_HOST_MCAST, int(self.port), msg, multicast=True)
        _LOGGER.info("✅ ETBUS: Sent startup ping (ts=%s)", self._hub_start_time)

    def _send_ping_multicast(self) -> None:
        msg = {
            "v": 1,
            "type": "ping",
            "id": self.hub_id,
            "class": "hub",
            "payload": {"port": int(self.port), "ts": int(time.time())},
        }
        self._udp_send(DEFAULT_HOST_MCAST, int(self.port), msg, multicast=True)

    def _udp_send(self, ip: str, port: int, msg: dict[str, Any], multicast: bool = False) -> None:
        if not self._sock:
            return
        data = json.dumps(msg, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
        try:
            self._sock.sendto(data, (ip, port))
        except Exception:
            pass
