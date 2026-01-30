from __future__ import annotations

import time
import logging
from typing import Any

from homeassistant.components.switch import SwitchEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .hub import EtBusHub

_LOGGER = logging.getLogger(__name__)

_RESYNC_COOLDOWN_S = 6.0


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback) -> None:
    hub: EtBusHub = hass.data[DOMAIN][entry.entry_id]
    entities: dict[tuple[str, str], EtBusSwitch] = {}

    @callback
    def handle_message(msg: dict[str, Any]) -> None:
        if msg.get("v") != 1:
            return

        mtype = msg.get("type")
        dev_id = msg.get("id")
        dev_class = msg.get("class")
        payload = msg.get("payload", {}) or {}

        if not dev_id or dev_class not in ("switch.relay", "switch.pump"):
            return

        endpoint = dev_class.replace(".", "_")
        key = (dev_id, endpoint)

        if mtype in ("discover", "state", "pong"):
            if key not in entities:
                name = payload.get("name", dev_id)
                ent = EtBusSwitch(hub, dev_id, dev_class, endpoint, name)
                entities[key] = ent
                async_add_entities([ent])
                _LOGGER.info("ET-Bus: discovered %s %s", dev_class, dev_id)

            ent = entities[key]

            if mtype == "state":
                ent.handle_state(payload)

    @callback
    def handle_status_event(ev) -> None:
        data = ev.data or {}
        dev_id = data.get("id")
        reason = data.get("reason", "")
        if not dev_id:
            return

        for (did, _), ent in list(entities.items()):
            if did != dev_id:
                continue
            ent.async_write_ha_state()
            if reason in ("discover", "pong", "online", "new"):
                ent.maybe_resync_to_device(reason=reason)

    hub.register_listener(handle_message)
    hass.bus.async_listen("etbus_device_status", handle_status_event)


class EtBusSwitch(SwitchEntity):
    _attr_should_poll = False
    _attr_entity_registry_enabled_default = True

    def __init__(self, hub: EtBusHub, dev_id: str, dev_class: str, endpoint: str, name: str):
        self._hub = hub
        self._dev_id = dev_id
        self._dev_class = dev_class

        self._attr_name = name
        self._is_on = False
        self._extra: dict[str, Any] = {}
        self._last_resync_ts = 0.0

        self._attr_unique_id = f"etbus_{dev_id}_{endpoint}"
        self._attr_device_info = {
            "identifiers": {(DOMAIN, dev_id)},
            "name": dev_id,
            "manufacturer": "ElectronicsTech",
        }

    @property
    def is_on(self) -> bool:
        return self._is_on

    @property
    def available(self) -> bool:
        info = self._hub.devices.get(self._dev_id) or {}
        return bool(info.get("online", True))

    @property
    def extra_state_attributes(self):
        return self._extra

    def handle_state(self, payload: dict[str, Any]) -> None:
        if "on" in payload:
            self._is_on = bool(payload["on"])

        extra = dict(payload)
        extra.pop("on", None)
        self._extra = extra

        if self.hass is not None:
            self.async_write_ha_state()

    def maybe_resync_to_device(self, reason: str) -> None:
        if self.hass is None or not self.available:
            return

        now = time.time()
        if (now - self._last_resync_ts) < _RESYNC_COOLDOWN_S:
            return

        ha_state = self.hass.states.get(self.entity_id)
        if ha_state is None:
            return

        desired = (ha_state.state == "on")

        self._last_resync_ts = now
        self._is_on = desired

        _LOGGER.warning("ET-Bus resync %s -> %s after %s", self.entity_id, "ON" if desired else "OFF", reason)
        self._send_command()
        self.async_write_ha_state()

    async def async_turn_on(self, **kwargs: Any) -> None:
        self._is_on = True
        self._send_command()
        if self.hass is not None:
            self.async_write_ha_state()

    async def async_turn_off(self, **kwargs: Any) -> None:
        self._is_on = False
        self._send_command()
        if self.hass is not None:
            self.async_write_ha_state()

    def _send_command(self) -> None:
        # Unicast if the hub knows the device IP (commercial-grade for Wi-Fi)
        self._hub.send_command(self._dev_id, self._dev_class, {"on": self._is_on})
