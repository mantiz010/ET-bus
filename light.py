from __future__ import annotations

import time
import logging
from typing import Any

from homeassistant.components.light import LightEntity, ColorMode
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .hub import EtBusHub

_LOGGER = logging.getLogger(__name__)
_RESYNC_COOLDOWN_S = 6.0


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback) -> None:
    hub: EtBusHub = hass.data[DOMAIN][entry.entry_id]
    entities: dict[str, EtBusRgbLight] = {}

    @callback
    def handle_message(msg: dict[str, Any]) -> None:
        if msg.get("v") != 1:
            return

        mtype = msg.get("type")
        dev_id = msg.get("id")
        dev_class = msg.get("class")
        payload = msg.get("payload", {}) or {}

        if not dev_id or dev_class != "light.rgb":
            return

        if mtype in ("discover", "state", "pong"):
            if dev_id not in entities:
                name = payload.get("name", dev_id)
                ent = EtBusRgbLight(hub, dev_id, name)
                entities[dev_id] = ent
                async_add_entities([ent])
                _LOGGER.info("ET-Bus: discovered light.rgb %s", dev_id)

            if mtype == "state":
                entities[dev_id].handle_state(payload)

    @callback
    def handle_status_event(ev) -> None:
        data = ev.data or {}
        dev_id = data.get("id")
        reason = data.get("reason", "")
        if not dev_id:
            return
        ent = entities.get(dev_id)
        if ent:
            ent.async_write_ha_state()
            if reason in ("discover", "pong", "online", "new"):
                ent.maybe_resync_to_device(reason=reason)

    hub.register_listener(handle_message)
    hass.bus.async_listen("etbus_device_status", handle_status_event)


class EtBusRgbLight(LightEntity):
    _attr_should_poll = False
    _attr_supported_color_modes = {ColorMode.RGB}
    _attr_color_mode = ColorMode.RGB
    _attr_entity_registry_enabled_default = True

    def __init__(self, hub: EtBusHub, dev_id: str, name: str):
        self._hub = hub
        self._dev_id = dev_id
        self._attr_name = name

        self._is_on = False
        self._rgb = (255, 255, 255)
        self._brightness = 255
        self._last_resync_ts = 0.0

        self._attr_unique_id = f"etbus_{dev_id}_rgb"
        self._attr_device_info = {
            "identifiers": {(DOMAIN, dev_id)},
            "name": dev_id,
            "manufacturer": "ElectronicsTech",
        }

    @property
    def available(self) -> bool:
        info = self._hub.devices.get(self._dev_id) or {}
        return bool(info.get("online", True))

    @property
    def is_on(self) -> bool:
        return self._is_on

    @property
    def rgb_color(self):
        return self._rgb

    @property
    def brightness(self):
        return self._brightness

    def handle_state(self, payload: dict[str, Any]) -> None:
        if "on" in payload:
            self._is_on = bool(payload["on"])
        if "r" in payload and "g" in payload and "b" in payload:
            self._rgb = (int(payload["r"]), int(payload["g"]), int(payload["b"]))
        if "brightness" in payload:
            self._brightness = int(payload["brightness"])

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

        desired_on = (ha_state.state == "on")
        rgb = ha_state.attributes.get("rgb_color") or self._rgb
        bri = ha_state.attributes.get("brightness")
        if bri is None:
            bri = self._brightness

        self._last_resync_ts = now
        self._is_on = bool(desired_on)
        self._rgb = (int(rgb[0]), int(rgb[1]), int(rgb[2]))
        self._brightness = int(bri)

        _LOGGER.warning("ET-Bus resync light %s after %s", self.entity_id, reason)
        self._send_command()
        self.async_write_ha_state()

    async def async_turn_on(self, **kwargs: Any) -> None:
        if "rgb_color" in kwargs and kwargs["rgb_color"] is not None:
            self._rgb = kwargs["rgb_color"]
        if "brightness" in kwargs and kwargs["brightness"] is not None:
            self._brightness = int(kwargs["brightness"])
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
        self._hub.send_command(
            self._dev_id,
            "light.rgb",
            {
                "on": self._is_on,
                "r": int(self._rgb[0]),
                "g": int(self._rgb[1]),
                "b": int(self._rgb[2]),
                "brightness": int(self._brightness),
            },
        )
