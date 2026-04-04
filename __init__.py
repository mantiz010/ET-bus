from __future__ import annotations

import logging
import time

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant

from .const import DOMAIN
from .hub import EtBusHub
from .panel import async_setup_panel, async_unload_panel

_LOGGER = logging.getLogger(__name__)

PLATFORMS: list[str] = ["light", "switch", "fan", "sensor"]


async def async_setup(hass: HomeAssistant, config: dict) -> bool:
    return True


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    hub = EtBusHub(hass, entry)
    await hub.async_start()

    hass.data.setdefault(DOMAIN, {})
    hass.data[DOMAIN][entry.entry_id] = hub

    # Register sidebar panel ONCE
    if not hass.data.get(f"{DOMAIN}_panel_loaded"):
        await async_setup_panel(hass)
        hass.data[f"{DOMAIN}_panel_loaded"] = True

    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)

    # Register Kate AI command service
    async def handle_kate_command(call):
        command = call.data.get("command", "")
        if not command:
            return
        # Ensure hub knows Kate's IP even if multicast didn't reach
        if "kate_ai" not in hub.devices:
            hub.devices["kate_ai"] = {
                "ip": "172.168.1.72",
                "last_seen": time.time(),
                "online": True,
            }
        hub.send_command(
            "kate_ai",
            "hub",
            {"command": command},
            store_last=False,
        )
        _LOGGER.info("ET-Bus: Sent command to Kate: %s", command[:80])

    hass.services.async_register(DOMAIN, "send_kate_command", handle_kate_command)

    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    unload_ok = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)

    hub: EtBusHub = hass.data[DOMAIN].pop(entry.entry_id)
    await hub.async_stop()

    # Remove panel and service if last instance removed
    if not hass.data[DOMAIN]:
        await async_unload_panel(hass)
        hass.data.pop(f"{DOMAIN}_panel_loaded", None)
        hass.services.async_remove(DOMAIN, "send_kate_command")

    return unload_ok

    return unload_ok
