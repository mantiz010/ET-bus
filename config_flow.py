from __future__ import annotations

import re
import voluptuous as vol

from homeassistant import config_entries
from homeassistant.core import callback

from .const import (
    DOMAIN,
    DEFAULT_PORT,
    CONF_PORT,
    CONF_CRYPTO_ENABLED,
    CONF_PSK_HEX,
)


def _normalize_hex(s: str) -> str:
    return re.sub(r"[^0-9a-fA-F]", "", (s or "")).lower()


def _validate_and_normalize_options(user_input: dict) -> dict:
    out = dict(user_input)

    crypto_on = bool(out.get(CONF_CRYPTO_ENABLED, False))
    psk = _normalize_hex(str(out.get(CONF_PSK_HEX, "") or ""))

    out[CONF_PSK_HEX] = psk

    if crypto_on and len(psk) != 64:
        raise vol.Invalid("psk_hex must be exactly 64 hex chars (32 bytes) when crypto is enabled")

    return out


class EtBusConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

    async def async_step_user(self, user_input=None):
        await self.async_set_unique_id(DOMAIN)
        self._abort_if_unique_id_configured()
        return self.async_create_entry(title="ET-Bus", data={})

    @staticmethod
    @callback
    def async_get_options_flow(config_entry):
        return EtBusOptionsFlowHandler(config_entry)


class EtBusOptionsFlowHandler(config_entries.OptionsFlow):
    def __init__(self, config_entry):
        self._entry = config_entry

    async def async_step_init(self, user_input=None):
        opts = dict(self._entry.options)
        errors: dict[str, str] = {}

        if user_input is not None:
            try:
                fixed = _validate_and_normalize_options(user_input)
                return self.async_create_entry(title="", data=fixed)
            except vol.Invalid:
                errors["base"] = "invalid_psk"

        schema = vol.Schema(
            {
                vol.Required(CONF_PORT, default=opts.get(CONF_PORT, DEFAULT_PORT)): vol.Coerce(int),
                vol.Required(CONF_CRYPTO_ENABLED, default=opts.get(CONF_CRYPTO_ENABLED, False)): bool,
                vol.Optional(CONF_PSK_HEX, default=str(opts.get(CONF_PSK_HEX, ""))): str,
            }
        )
        return self.async_show_form(step_id="init", data_schema=schema, errors=errors)
