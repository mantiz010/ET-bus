from __future__ import annotations

import time
import logging
from typing import Any

from homeassistant.components.light import (
    LightEntity,
    ColorMode,
    LightEntityFeature,
    ATTR_EFFECT,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .hub import EtBusHub

_LOGGER = logging.getLogger(__name__)
_RESYNC_COOLDOWN_S = 6.0

# MUST match Arduino effect names EXACTLY
DEFAULT_EFFECTS = ["solid", "rainbow", "cylon", "confetti", "pulse"]


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
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

        if dev_class != "light.rgb" or not dev_id:
            return

        if mtype in ("discover", "state", "pong"):
            if dev_id not in entities:
                name = payload.get("name", dev_id)
                effects = payload.get("effects") or DEFAULT_EFFECTS
                ent = EtBusRgbLight(hub, dev_id, name, effects)
                entities[dev_id] = ent
                async_add_entities([ent])
                _LOGGER.info("ET-Bus: discovered RGB light %s", dev_id)

            entities[dev_id].handle_state(payload)

    @callback
    def handle_status_event(ev) -> None:
        data = ev.data or {}
        dev_id = data.get("id")
        reason = data.get("reason", "")
        ent = entities.get(dev_id)

        if ent:
            ent.async_write_ha_state()
            if reason in ("discover", "pong", "online", "new"):
                ent.maybe_resync_to_device(reason)

    hub.register_listener(handle_message)
    hass.bus.async_listen("etbus_device_status", handle_status_event)


class EtBusRgbLight(LightEntity):
    _attr_should_poll = False
    _attr_color_mode = ColorMode.RGB
    _attr_supported_color_modes = {ColorMode.RGB}

    # 🔥 THIS IS THE MISSING PIECE
    _attr_supported_features = LightEntityFeature.EFFECT

    def __init__(self, hub: EtBusHub, dev_id: str, name: str, effects: list[str]):
        self._hub = hub
        self._dev_id = dev_id
        self._attr_name = name

        self._is_on = False
        self._rgb = (255, 255, 255)
        self._brightness = 255

        self._effect_list = list(effects or DEFAULT_EFFECTS)
        self._effect = "solid"
        self._speed = 120

        self._last_resync_ts = 0.0

        self._attr_unique_id = f"etbus_{dev_id}_rgb"
        self._attr_device_info = {
            "identifiers": {(DOMAIN, dev_id)},
            "name": dev_id,
            "manufacturer": "ElectronicsTech",
        }

    # -------------------
    # HA properties
    # -------------------
    @property
    def available(self) -> bool:
        return bool(self._hub.devices.get(self._dev_id, {}).get("online", True))

    @property
    def is_on(self) -> bool:
        return self._is_on

    @property
    def rgb_color(self):
        return self._rgb

    @property
    def brightness(self):
        return self._brightness

    @property
    def effect_list(self):
        return self._effect_list

    @property
    def effect(self):
        return self._effect

    @property
    def extra_state_attributes(self):
        return {
            "speed": int(self._speed),
        }

    # -------------------
    # Incoming state
    # -------------------
    def handle_state(self, payload: dict[str, Any]) -> None:
        if "on" in payload:
            self._is_on = bool(payload["on"])

        if {"r", "g", "b"} <= payload.keys():
            self._rgb = (
                int(payload["r"]),
                int(payload["g"]),
                int(payload["b"]),
            )

        if "brightness" in payload:
            self._brightness = int(payload["brightness"])

        if "effect" in payload:
            eff = str(payload["effect"])
            self._effect = eff
            if eff not in self._effect_list:
                self._effect_list.append(eff)

        if "speed" in payload:
            self._speed = int(payload["speed"])

        self.async_write_ha_state()

    # -------------------
    # HA → device
    # -------------------
    async def async_turn_on(self, **kwargs):
        self._is_on = True

        if "rgb_color" in kwargs:
            self._rgb = kwargs["rgb_color"]

        if "brightness" in kwargs:
            self._brightness = int(kwargs["brightness"])

        if ATTR_EFFECT in kwargs:
            self._effect = str(kwargs[ATTR_EFFECT])

        if "speed" in kwargs:
            self._speed = int(kwargs["speed"])

        self._send_command()
        self.async_write_ha_state()

    async def async_turn_off(self, **kwargs):
        self._is_on = False
        self._send_command()
        self.async_write_ha_state()

    def maybe_resync_to_device(self, reason: str):
        now = time.time()
        if (now - self._last_resync_ts) < _RESYNC_COOLDOWN_S:
            return
        self._last_resync_ts = now
        self._send_command()

    # -------------------
    # Send ET-Bus command
    # -------------------
    def _send_command(self):
        payload = {
            "on": self._is_on,
            "r": int(self._rgb[0]),
            "g": int(self._rgb[1]),
            "b": int(self._rgb[2]),
            "brightness": int(self._brightness),
            "effect": self._effect,
            "speed": int(self._speed),
        }
        self._hub.send_command(self._dev_id, "light.rgb", payload)
