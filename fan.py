from __future__ import annotations

import logging
from typing import Any

from homeassistant.components.fan import FanEntity, FanEntityFeature
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .hub import EtBusHub

_LOGGER = logging.getLogger(__name__)


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback) -> None:
    hub: EtBusHub = hass.data[DOMAIN][entry.entry_id]
    entities: dict[tuple[str, str], EtBusFan] = {}

    @callback
    def handle_message(msg: dict[str, Any]) -> None:
        if msg.get("v") != 1:
            return

        mtype = msg.get("type")
        dev_id = msg.get("id")
        dev_class = msg.get("class")
        payload = msg.get("payload", {}) or {}

        if not dev_id or dev_class not in ("fan.speed", "fan.preset"):
            return

        endpoint = dev_class.replace(".", "_")
        key = (dev_id, endpoint)

        if mtype in ("discover", "state", "pong"):
            if key not in entities:
                name = payload.get("name", dev_id)

                # Restore last known state from hub's persisted device states
                saved = hub.get_last_reported_state(dev_id)
                ent = EtBusFan(hub, dev_id, dev_class, endpoint, name, saved)
                entities[key] = ent
                async_add_entities([ent])
                _LOGGER.info("ET-Bus: discovered %s %s", dev_class, dev_id)

            # Always update HA entity from device-reported state
            if mtype == "state":
                entities[key].handle_state(payload)

    hub.register_listener(handle_message)


class EtBusFan(FanEntity):
    _attr_should_poll = False
    _attr_entity_registry_enabled_default = True

    def __init__(
        self,
        hub: EtBusHub,
        dev_id: str,
        dev_class: str,
        endpoint: str,
        name: str,
        saved_state: dict[str, Any] | None = None,
    ):
        self._hub = hub
        self._dev_id = dev_id
        self._dev_class = dev_class
        self._endpoint = endpoint
        self._attr_name = name

        self._is_on = False
        self._percentage = 0
        self._preset: str | None = None

        # Restore from persisted device-reported state (if available)
        # This lets HA show the correct state immediately after reboot
        # WITHOUT sending any commands to the device
        if saved_state and isinstance(saved_state, dict):
            sp = saved_state.get("payload", {})
            if sp:
                _LOGGER.info("ET-Bus fan %s: restoring from persisted state: %s", dev_id, sp)
                self._apply_payload(sp)

        self._attr_unique_id = f"etbus_{dev_id}_{endpoint}"
        self._attr_device_info = {
            "identifiers": {(DOMAIN, dev_id)},
            "name": dev_id,
            "manufacturer": "ElectronicsTech",
        }

        if self._dev_class == "fan.preset":
            self._attr_preset_modes = ["off", "low", "medium", "high"]

    @property
    def available(self) -> bool:
        info = self._hub.devices.get(self._dev_id) or {}
        return bool(info.get("online", True))

    @property
    def supported_features(self) -> FanEntityFeature:
        if self._dev_class == "fan.speed":
            return FanEntityFeature.SET_PERCENTAGE
        return FanEntityFeature.PRESET_MODE

    @property
    def is_on(self) -> bool:
        return self._is_on

    @property
    def percentage(self) -> int | None:
        if self._dev_class != "fan.speed":
            return None
        return self._percentage

    @property
    def preset_mode(self) -> str | None:
        if self._dev_class != "fan.preset":
            return None
        return self._preset

    # -------------------
    # Incoming state from device (device is source of truth)
    # -------------------
    def _apply_payload(self, payload: dict[str, Any]) -> None:
        """Apply state from a device payload without writing HA state."""
        if "on" in payload:
            self._is_on = bool(payload["on"])

        if self._dev_class == "fan.speed" and "speed" in payload:
            self._percentage = int(payload["speed"])

        if self._dev_class == "fan.preset" and "preset" in payload:
            self._preset = str(payload["preset"])

    def handle_state(self, payload: dict[str, Any]) -> None:
        """Update entity from device-reported state."""
        self._apply_payload(payload)

        if self.hass is not None:
            self.async_write_ha_state()

    # -------------------
    # HA → device (only when user explicitly acts)
    # -------------------
    async def async_set_percentage(self, percentage: int) -> None:
        self._percentage = int(percentage)
        self._is_on = self._percentage > 0
        self._send_command()
        if self.hass is not None:
            self.async_write_ha_state()

    async def async_set_preset_mode(self, preset_mode: str) -> None:
        self._preset = preset_mode
        self._is_on = preset_mode != "off"
        self._send_command()
        if self.hass is not None:
            self.async_write_ha_state()

    async def async_turn_on(self, **kwargs: Any) -> None:
        self._is_on = True
        self._send_command()
        if self.hass is not None:
            self.async_write_ha_state()

    async def async_turn_off(self, **kwargs: Any) -> None:
        self._is_on = False
        if self._dev_class == "fan.speed":
            self._percentage = 0
        else:
            self._preset = "off"
        self._send_command()
        if self.hass is not None:
            self.async_write_ha_state()

    def _send_command(self) -> None:
        payload: dict[str, Any] = {"on": self._is_on}

        if self._dev_class == "fan.speed":
            payload["speed"] = int(self._percentage)
        else:
            payload["preset"] = self._preset

        self._hub.send_command(self._dev_id, self._dev_class, payload)
