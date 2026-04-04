"""ET-Bus Switch Platform - Multi-Switch Support"""
import logging
from typing import Any

from homeassistant.components.switch import SwitchEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN

_LOGGER = logging.getLogger(__name__)


async def async_setup_entry(
    hass: HomeAssistant,
    config_entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Set up ET-Bus switches from a config entry."""
    hub = hass.data[DOMAIN][config_entry.entry_id]
    
    # Track which devices we've already created entities for
    created_devices = set()

    @callback
    def async_discover_switch(dev_id: str, dev_class: str, info: dict):
        """Discover and add new ET-Bus switch(es)."""
        
        if dev_id in created_devices:
            _LOGGER.debug("ET-Bus: device %s already created, skipping", dev_id)
            return

        # Get persisted state for this device
        saved = hub.get_last_reported_state(dev_id)
        saved_payload = {}
        if saved and isinstance(saved, dict):
            saved_payload = saved.get("payload", {})
        
        # Check if this is a multi-switch device
        if "switches" in info and isinstance(info["switches"], list):
            if len(info["switches"]) > 0 and isinstance(info["switches"][0], dict):
                _LOGGER.info(
                    "ET-Bus: discovered multi-switch device %s with %s switches",
                    dev_id, len(info['switches'])
                )

                # Extract saved switch states: {"switches": {"1": true, "2": false, ...}}
                saved_switches = {}
                if isinstance(saved_payload.get("switches"), dict):
                    saved_switches = saved_payload["switches"]
                
                entities = []
                for switch_info in info["switches"]:
                    switch_id = switch_info.get("id")
                    switch_name = switch_info.get("name", "Switch " + str(switch_id))
                    
                    # Restore individual switch state from persisted data
                    initial_on = bool(saved_switches.get(switch_id, False))
                    
                    _LOGGER.info(
                        "ET-Bus: creating switch entity %s_%s (%s) restored_on=%s",
                        dev_id, switch_id, switch_name, initial_on,
                    )
                    
                    entity = ETBusMultiSwitch(
                        hub=hub,
                        dev_id=dev_id,
                        switch_id=switch_id,
                        name=switch_name,
                        dev_class=dev_class,
                        device_info=info,
                        initial_on=initial_on,
                    )
                    entities.append(entity)
                
                async_add_entities(entities)
                created_devices.add(dev_id)
                return
        
        # Single switch device (legacy/simple)
        _LOGGER.info("ET-Bus: discovered single switch %s", dev_id)

        # Restore single switch state
        initial_on = False
        if "on" in saved_payload:
            initial_on = bool(saved_payload["on"])
        elif "state" in saved_payload:
            initial_on = str(saved_payload["state"]).upper() == "ON"
        
        entity = ETBusSingleSwitch(
            hub=hub,
            dev_id=dev_id,
            name=info.get("name", dev_id),
            dev_class=dev_class,
            device_info=info,
            initial_on=initial_on,
        )
        async_add_entities([entity])
        created_devices.add(dev_id)

    @callback
    def async_handle_message(msg: dict):
        """Handle all ET-Bus messages - watch for discovery and state."""
        
        msg_type = msg.get("type", "")
        dev_id = msg.get("id", "")
        dev_class = msg.get("class", "")
        payload = msg.get("payload", {})
        
        if not dev_id or dev_id == "hub":
            return
        
        # Skip if not a switch-related message
        if dev_class and "switch" not in dev_class.lower():
            return
        
        # Check if this is a discovery or state message with multi-switch info
        if dev_id not in created_devices:
            # Look for multi-switch discovery in payload
            if "switches" in payload and isinstance(payload["switches"], list):
                if len(payload["switches"]) > 0:
                    first_switch = payload["switches"][0]
                    if isinstance(first_switch, dict) and "id" in first_switch:
                        _LOGGER.info(
                            "ET-Bus: detected multi-switch discovery in %s message for %s",
                            msg_type, dev_id
                        )
                        
                        device_info = {
                            "name": payload.get("name", dev_id),
                            "model": payload.get("model", "Multi-Switch"),
                            "version": payload.get("version", "1.0"),
                            "switches": payload["switches"]
                        }
                        
                        async_discover_switch(dev_id, dev_class or "switch.multi", device_info)

    # Register the unified message handler
    hub.register_listener(async_handle_message)


class ETBusSingleSwitch(SwitchEntity):
    """Single ET-Bus switch entity (legacy)."""

    _attr_has_entity_name = True
    _attr_should_poll = False

    def __init__(
        self,
        hub,
        dev_id: str,
        name: str,
        dev_class: str,
        device_info: dict,
        initial_on: bool = False,
    ):
        """Initialize single switch."""
        self._hub = hub
        self._dev_id = dev_id
        self._dev_class = dev_class
        self._attr_name = name
        self._attr_unique_id = "etbus_" + dev_id
        self._attr_is_on = initial_on

        # Device info for HA device registry
        self._attr_device_info = {
            "identifiers": {(DOMAIN, dev_id)},
            "name": device_info.get("name", dev_id),
            "manufacturer": "ET-Bus",
            "model": device_info.get("model", "Switch"),
            "sw_version": device_info.get("version", "1.0"),
        }

        if initial_on:
            _LOGGER.info("ET-Bus switch %s: restored on=%s from persisted state", dev_id, initial_on)

    @property
    def available(self) -> bool:
        """Dynamic availability from hub device tracker — same as light.py."""
        return bool(self._hub.devices.get(self._dev_id, {}).get("online", True))

    async def async_added_to_hass(self) -> None:
        """Subscribe to state updates."""
        await super().async_added_to_hass()

        @callback
        def handle_message(msg: dict):
            """Handle ET-Bus message."""
            dev_id = msg.get("id", "")
            if dev_id != self._dev_id:
                return

            payload = msg.get("payload", {})
            old_state = self._attr_is_on
            
            # Update state from device report
            if "on" in payload:
                self._attr_is_on = bool(payload["on"])
            elif "state" in payload:
                self._attr_is_on = str(payload["state"]).upper() == "ON"
            else:
                return

            if old_state != self._attr_is_on:
                _LOGGER.debug(
                    "ET-Bus switch %s: %s -> %s", self._dev_id, old_state, self._attr_is_on
                )

            self.async_write_ha_state()

        self._hub.register_listener(handle_message)

    async def async_turn_on(self, **kwargs: Any) -> None:
        """Turn on switch."""
        _LOGGER.debug("ET-Bus: turning ON switch %s", self._dev_id)
        self._hub.send_command(self._dev_id, self._dev_class, {"on": True})

    async def async_turn_off(self, **kwargs: Any) -> None:
        """Turn off switch."""
        _LOGGER.debug("ET-Bus: turning OFF switch %s", self._dev_id)
        self._hub.send_command(self._dev_id, self._dev_class, {"on": False})


class ETBusMultiSwitch(SwitchEntity):
    """Individual switch in a multi-switch ET-Bus device."""

    _attr_has_entity_name = True
    _attr_should_poll = False

    def __init__(
        self,
        hub,
        dev_id: str,
        switch_id: str,
        name: str,
        dev_class: str,
        device_info: dict,
        initial_on: bool = False,
    ):
        """Initialize multi-switch entity."""
        self._hub = hub
        self._dev_id = dev_id
        self._switch_id = switch_id
        self._dev_class = dev_class
        self._attr_name = name
        self._attr_unique_id = "etbus_" + dev_id + "_" + str(switch_id)
        self._attr_is_on = initial_on

        # Device info - all switches share same device
        self._attr_device_info = {
            "identifiers": {(DOMAIN, dev_id)},
            "name": device_info.get("name", dev_id),
            "manufacturer": "ET-Bus",
            "model": device_info.get("model", "Multi-Switch"),
            "sw_version": device_info.get("version", "1.0"),
        }

        if initial_on:
            _LOGGER.info(
                "ET-Bus switch %s_%s: restored on=%s from persisted state",
                dev_id, switch_id, initial_on,
            )

    @property
    def available(self) -> bool:
        """Dynamic availability from hub device tracker — same as light.py."""
        return bool(self._hub.devices.get(self._dev_id, {}).get("online", True))

    async def async_added_to_hass(self) -> None:
        """Subscribe to state updates."""
        await super().async_added_to_hass()

        @callback
        def handle_message(msg: dict):
            """Handle ET-Bus message."""
            dev_id = msg.get("id", "")
            if dev_id != self._dev_id:
                return

            payload = msg.get("payload", {})

            # Multi-switch state format: {"switches": {"1": true, "2": false}}
            if "switches" in payload:
                switches_state = payload["switches"]
                
                # Handle dict format (actual state)
                if isinstance(switches_state, dict):
                    if self._switch_id in switches_state:
                        old_state = self._attr_is_on
                        self._attr_is_on = bool(switches_state[self._switch_id])

                        if old_state != self._attr_is_on:
                            _LOGGER.debug(
                                "ET-Bus switch %s_%s: %s -> %s",
                                self._dev_id, self._switch_id, old_state, self._attr_is_on
                            )

                        self.async_write_ha_state()

        self._hub.register_listener(handle_message)

    async def async_turn_on(self, **kwargs: Any) -> None:
        """Turn on this switch."""
        _LOGGER.debug("ET-Bus: turning ON switch %s_%s", self._dev_id, self._switch_id)
        self._hub.send_command(
            self._dev_id, self._dev_class,
            {"switch_id": self._switch_id, "on": True},
        )

    async def async_turn_off(self, **kwargs: Any) -> None:
        """Turn off this switch."""
        _LOGGER.debug("ET-Bus: turning OFF switch %s_%s", self._dev_id, self._switch_id)
        self._hub.send_command(
            self._dev_id, self._dev_class,
            {"switch_id": self._switch_id, "on": False},
        )
    def _send_command(self) -> None:
        # Unicast if the hub knows the device IP (commercial-grade for Wi-Fi)
        self._hub.send_command(self._dev_id, self._dev_class, {"on": self._is_on})
