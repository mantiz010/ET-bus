from __future__ import annotations

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant

from .const import DOMAIN
from .hub import EtBusHub
from .panel import async_setup_panel, async_unload_panel

PLATFORMS: list[str] = ["light", "switch", "fan", "sensor"]


async def async_setup(hass: HomeAssistant, config: dict) -> bool:
    return True


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    hub = EtBusHub(hass)
    await hub.async_start()

    hass.data.setdefault(DOMAIN, {})
    hass.data[DOMAIN][entry.entry_id] = hub

    # Register sidebar panel ONCE
    if not hass.data.get(f"{DOMAIN}_panel_loaded"):
        await async_setup_panel(hass)
        hass.data[f"{DOMAIN}_panel_loaded"] = True

    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    unload_ok = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)

    hub: EtBusHub = hass.data[DOMAIN].pop(entry.entry_id)
    await hub.async_stop()

    # Remove panel if last instance removed
    if not hass.data[DOMAIN]:
        await async_unload_panel(hass)
        hass.data.pop(f"{DOMAIN}_panel_loaded", None)

    return unload_ok
