from __future__ import annotations

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

# ✨ 35 PROFESSIONAL LIGHTING EFFECTS! ✨
DEFAULT_EFFECTS = [
    # ===== SOLID & BASIC (5) =====
    "solid",           # Static color
    "white",           # Pure white
    "warm_white",      # Warm white tone
    "cool_white",      # Cool white tone
    "night_light",     # Dim warm glow
    
    # ===== RAINBOW & COLOR (5) =====
    "rainbow",         # Full spectrum rainbow
    "rainbow_cycle",   # Smooth rainbow transitions
    "color_wipe",      # Color fills LED by LED
    "color_chase",     # Single color chasing
    "color_bounce",    # Color bounces back and forth
    
    # ===== ANIMATED PATTERNS (10) =====
    "cylon",           # KITT scanner effect
    "scanner",         # Larson scanner
    "theater_chase",   # Theater marquee lights
    "twinkle",         # Random twinkling stars
    "sparkle",         # Random sparkles
    "confetti",        # Random colored dots
    "juggle",          # Colored dots weaving
    "pulse",           # Smooth breathing pulse
    "heartbeat",       # Double pulse heartbeat
    "strobe",          # Fast flashing
    
    # ===== RUNNING & CHASING (5) =====
    "running_lights",  # Smooth wave motion
    "comet",           # Comet tail effect
    "meteor",          # Meteor rain
    "chase",           # Multi-color chase
    "fire",            # Realistic fire flicker
    
    # ===== ADVANCED EFFECTS (5) =====
    "aurora",          # Northern lights shimmer
    "plasma",          # Plasma cloud effect
    "pride",           # Pride rainbow colors
    "lava",            # Lava lamp flow
    "ocean",           # Ocean wave effect
    
    # ===== SEASONAL & SPECIAL (5) =====
    "christmas",       # Red/green alternating
    "halloween",       # Orange/purple spooky
    "police",          # Red/blue flashing
    "ambulance",       # Red/white flashing
    "party",           # Multi-color party mode
]


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

                # Restore last known state from hub's persisted device states
                saved = hub.get_last_reported_state(dev_id)
                ent = EtBusRgbLight(hub, dev_id, name, effects, saved)
                entities[dev_id] = ent
                async_add_entities([ent])
                _LOGGER.debug("ET-Bus: discovered RGB light %s with %s effects", dev_id, len(effects))

            # Always update HA entity from device-reported state
            entities[dev_id].handle_state(payload)

    hub.register_listener(handle_message)


class EtBusRgbLight(LightEntity):
    _attr_should_poll = False
    _attr_color_mode = ColorMode.RGB
    _attr_supported_color_modes = {ColorMode.RGB}
    _attr_supported_features = LightEntityFeature.EFFECT

    def __init__(
        self,
        hub: EtBusHub,
        dev_id: str,
        name: str,
        effects: list[str],
        saved_state: dict[str, Any] | None = None,
    ):
        self._hub = hub
        self._dev_id = dev_id
        self._attr_name = name

        # Default state
        self._is_on = False
        self._rgb = (255, 255, 255)
        self._brightness = 255
        self._effect = "solid"
        self._speed = 120

        # Restore from persisted device-reported state (if available)
        # This lets HA show the correct state immediately after reboot
        # WITHOUT sending any commands to the device
        if saved_state and isinstance(saved_state, dict):
            sp = saved_state.get("payload", {})
            if sp:
                _LOGGER.debug("ET-Bus light %s: restoring from persisted state: %s", dev_id, sp)
                self._apply_payload(sp)

        # Use device effects if provided, otherwise use defaults
        self._effect_list = list(effects or DEFAULT_EFFECTS)

        self._attr_unique_id = f"etbus_{dev_id}_rgb"
        self._attr_device_info = {
            "identifiers": {(DOMAIN, dev_id)},
            "name": dev_id,
            "manufacturer": "ElectronicsTech",
            "model": "ET-Bus RGB LED",
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
    # Incoming state from device (device is source of truth)
    # -------------------
    def _apply_payload(self, payload: dict[str, Any]) -> None:
        """Apply state from a device payload without writing HA state."""
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

        if "speed" in payload:
            self._speed = int(payload["speed"])

    def handle_state(self, payload: dict[str, Any]) -> None:
        """Update entity from device-reported state."""
        self._apply_payload(payload)

        # Dynamically add new effects if device reports them
        if "effect" in payload:
            eff = str(payload["effect"])
            if eff not in self._effect_list:
                self._effect_list.append(eff)
                _LOGGER.debug("ET-Bus light %s: learned new effect '%s'", self._dev_id, eff)

        self.async_write_ha_state()

    # -------------------
    # HA → device (only when user explicitly acts)
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
        _LOGGER.debug("ET-Bus light %s: sent command %s", self._dev_id, payload)
