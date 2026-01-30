from __future__ import annotations

import logging
from pathlib import Path

from aiohttp import web

from homeassistant.components.frontend import async_register_built_in_panel, async_remove_panel
from homeassistant.components.http import HomeAssistantView
from homeassistant.core import HomeAssistant

from .const import DOMAIN

_LOGGER = logging.getLogger(__name__)

PANEL_URL_PATH = "et-bus"                 # sidebar panel -> /et-bus
VIEW_URL = f"/{DOMAIN}/etbus.html"        # iframe loads this
PANEL_TITLE = "ET-Bus"
PANEL_ICON = "mdi:lan"

_HTML_CACHE: str | None = None


class EtBusHtmlView(HomeAssistantView):
    """Serve ET-Bus HTML UI (cached) from the integration."""

    url = VIEW_URL
    name = "etbus:html"
    requires_auth = False  # ✅ stops 401 in iframe

    async def get(self, request):
        global _HTML_CACHE

        if _HTML_CACHE is None:
            return web.Response(
                text="ET-Bus UI not loaded (cache empty). Restart Home Assistant.",
                status=500,
                content_type="text/plain",
            )

        return web.Response(
            text=_HTML_CACHE,
            status=200,
            content_type="text/html",
        )


async def async_setup_panel(hass: HomeAssistant) -> None:
    """Register ET-Bus sidebar panel + HTTP view that serves cached HTML."""
    global _HTML_CACHE

    # Load HTML ONCE (executor) to avoid blocking event loop
    if _HTML_CACHE is None:
        file_path = Path(__file__).parent / "www" / "etbus.html"

        def _read_file() -> str:
            return file_path.read_text(encoding="utf-8")

        try:
            _HTML_CACHE = await hass.async_add_executor_job(_read_file)
            _LOGGER.info("ET-Bus UI loaded from %s", file_path)
        except Exception as e:
            _LOGGER.error("ET-Bus UI file read failed: %s", e)
            _HTML_CACHE = (
                "<html><body style='font-family:monospace'>"
                "<h2>ET-Bus UI missing</h2>"
                "<p>Could not read: custom_components/etbus/www/etbus.html</p>"
                "</body></html>"
            )

    # Register view once
    if not hass.data.get(f"{DOMAIN}_view_loaded"):
        hass.http.register_view(EtBusHtmlView)
        hass.data[f"{DOMAIN}_view_loaded"] = True
        _LOGGER.info("ET-Bus HTTP view registered at %s", VIEW_URL)

    # Register sidebar panel
    async_register_built_in_panel(
        hass,
        component_name="iframe",
        sidebar_title=PANEL_TITLE,
        sidebar_icon=PANEL_ICON,
        frontend_url_path=PANEL_URL_PATH,
        config={"url": VIEW_URL},
        require_admin=False,
    )

    _LOGGER.info("ET-Bus panel registered at /%s -> %s", PANEL_URL_PATH, VIEW_URL)


async def async_unload_panel(hass: HomeAssistant) -> None:
    """Remove ET-Bus panel."""
    try:
        async_remove_panel(hass, PANEL_URL_PATH)
    except Exception:
        pass
