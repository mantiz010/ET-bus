from __future__ import annotations

import logging
from dataclasses import dataclass
from typing import Any

from homeassistant.components.sensor import SensorEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import (
    UnitOfTemperature,
    PERCENTAGE,
    CONCENTRATION_PARTS_PER_MILLION,
)
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .hub import EtBusHub

_LOGGER = logging.getLogger(__name__)

_ENTITIES: dict[str, "EtBusValueSensor"] = {}

_SKIP_PAYLOAD_KEYS = {
    "unit",
    "units",
    "name",
    "fw",
    "model",
    "version",
    "lib",
    "boot",
    "features",
}

_SENSOR_META = {
    "temp": ("Temperature", UnitOfTemperature.CELSIUS, "temperature"),
    "temperature": ("Temperature", UnitOfTemperature.CELSIUS, "temperature"),
    "humidity": ("Humidity", PERCENTAGE, "humidity"),
    "rh": ("Humidity", PERCENTAGE, "humidity"),
    "co2": ("CO2", CONCENTRATION_PARTS_PER_MILLION, "carbon_dioxide"),
    "eco2": ("eCO2", CONCENTRATION_PARTS_PER_MILLION, "carbon_dioxide"),
    "pressure": ("Pressure", "hPa", "pressure"),
    "baro": ("Pressure", "hPa", "pressure"),
    "tvoc": ("TVOC", "ppb", "volatile_organic_compounds_parts"),
    "voc": ("VOC", "ppb", "volatile_organic_compounds_parts"),
    "pm1": ("PM1", "µg/m³", "pm1"),
    "pm1_0": ("PM1", "µg/m³", "pm1"),
    "pm2_5": ("PM2.5", "µg/m³", "pm25"),
    "pm25": ("PM2.5", "µg/m³", "pm25"),
    "pm10": ("PM10", "µg/m³", "pm10"),
    "lux": ("Illuminance", "lx", "illuminance"),
    "light": ("Illuminance", "lx", "illuminance"),
    "battery": ("Battery", PERCENTAGE, "battery"),
    "voltage": ("Voltage", "V", "voltage"),
    "current": ("Current", "A", "current"),
    "power": ("Power", "W", "power"),
    "energy": ("Energy", "kWh", "energy"),
    "rssi": ("RSSI", "dBm", "signal_strength"),
    "noise": ("Noise", "dB", "sound_pressure"),
    "sound": ("Sound", "dB", "sound_pressure"),
    "gas": ("Gas", "kΩ", None),
    "gas_resistance": ("Gas Resistance", "kΩ", None),
    "no2": ("NO2", CONCENTRATION_PARTS_PER_MILLION, "nitrogen_dioxide"),
    "co": ("CO", CONCENTRATION_PARTS_PER_MILLION, "carbon_monoxide"),
}


def _endpoint_from_class(cls: str) -> str:
    return cls.replace(".", "_")


def _entity_key(entry_id: str, dev_id: str, endpoint: str, metric: str) -> str:
    return f"{entry_id}:{dev_id}:{endpoint}:{metric}"


@dataclass
class _Msg:
    entry_id: str
    dev_id: str
    cls: str
    payload: dict[str, Any]


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    hub: EtBusHub = hass.data[DOMAIN][entry.entry_id]

    @callback
    def _on_message(msg: dict[str, Any]) -> None:
        if msg.get("v") != 1:
            return
        if msg.get("type") != "state":
            return

        dev_id = msg.get("id")
        cls = msg.get("class")
        payload = msg.get("payload") or {}

        if not dev_id or not cls:
            return
        if not cls.startswith("sensor."):
            return
        if not isinstance(payload, dict):
            return

        _process_state(async_add_entities, hub, _Msg(entry.entry_id, dev_id, cls, payload))

    @callback
    def _on_status(ev) -> None:
        data = ev.data or {}
        dev_id = data.get("id")
        if not dev_id:
            return
        prefix = f"{entry.entry_id}:{dev_id}:"
        for k, ent in list(_ENTITIES.items()):
            if k.startswith(prefix):
                ent.refresh_availability()
                ent.async_write_ha_state()

    hub.register_listener(_on_message)
    hass.bus.async_listen("etbus_device_status", _on_status)

    _LOGGER.debug("ET-Bus sensor platform ready")


@callback
def _process_state(async_add_entities: AddEntitiesCallback, hub: EtBusHub, m: _Msg) -> None:
    endpoint = _endpoint_from_class(m.cls)

    # single-value payload style
    if "value" in m.payload:
        metric = m.cls.replace("sensor.", "") or "value"
        _get_or_create_and_update(async_add_entities, hub, m, endpoint, metric, m.payload.get("value"), m.payload)
        return

    # multi-metric payload style
    for metric, value in m.payload.items():
        if str(metric).lower() in _SKIP_PAYLOAD_KEYS:
            continue
        if value is None or isinstance(value, (dict, list)):
            continue
        _get_or_create_and_update(async_add_entities, hub, m, endpoint, str(metric), value, m.payload)


def _get_or_create_and_update(
    async_add_entities: AddEntitiesCallback,
    hub: EtBusHub,
    m: _Msg,
    endpoint: str,
    metric: str,
    value: Any,
    payload: dict[str, Any],
) -> None:
    k = _entity_key(m.entry_id, m.dev_id, endpoint, metric)

    ent = _ENTITIES.get(k)
    if ent is None:
        ent = EtBusValueSensor(hub, m.dev_id, m.cls, endpoint, metric)
        _ENTITIES[k] = ent
        async_add_entities([ent])
        _LOGGER.debug("ET-Bus created sensor: %s", k)

    ent.handle_value(value, payload)


class EtBusValueSensor(SensorEntity):
    _attr_should_poll = False
    _attr_has_entity_name = True
    _attr_entity_registry_enabled_default = True

    def __init__(self, hub: EtBusHub, dev_id: str, cls: str, endpoint: str, metric: str):
        self._hub = hub
        self._dev_id = dev_id
        self._cls = cls
        self._endpoint = endpoint
        self._metric = metric
        self._native_value = None

        self._attr_unique_id = f"etbus_{dev_id}_{endpoint}_{metric}"

        meta = _SENSOR_META.get(metric.lower())
        pretty = meta[0] if meta else metric
        self._attr_name = pretty

        self._attr_device_info = {
            "identifiers": {(DOMAIN, dev_id)},
            "name": dev_id,
            "manufacturer": "ElectronicsTech",
        }

        mlow = metric.lower()
        if meta:
            unit, device_class = meta[1], meta[2]
            if unit:
                self._attr_native_unit_of_measurement = unit
            if device_class:
                self._attr_device_class = device_class

        self._attr_state_class = None

        self.refresh_availability()

    @property
    def native_value(self):
        return self._native_value

    def refresh_availability(self) -> None:
        info = self._hub.devices.get(self._dev_id)
        self._attr_available = bool(info.get("online", True)) if info else True

    def handle_value(self, value: Any, payload: dict[str, Any]) -> None:
        self._native_value = value
        self.refresh_availability()
        self._attr_state_class = "measurement" if isinstance(value, (int, float)) and not isinstance(value, bool) else None

        unit = None
        units = payload.get("units")
        if isinstance(units, dict):
            unit = units.get(self._metric)
        unit = unit or payload.get("unit")
        if unit and not getattr(self, "_attr_native_unit_of_measurement", None):
            self._attr_native_unit_of_measurement = unit

        if self.hass is not None:
            self.async_write_ha_state()

